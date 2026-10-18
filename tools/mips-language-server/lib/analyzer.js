"use strict";

const fs = require("fs");
const os = require("os");
const path = require("path");
const childProcess = require("child_process");
const { KEYWORDS, TYPES, GLOBALS, INSTANCE_MEMBERS, SNIPPETS, ATTRIBUTES } = require("./api");
const { analyze, codeOnly } = require("./language");

function uriToPath(uri) {
  if (!uri || !uri.startsWith("file://")) return uri || "";
  let value = decodeURIComponent(uri.slice("file://".length));
  if (process.platform === "win32" && value.startsWith("/")) value = value.slice(1);
  return value.replace(/\//g, path.sep);
}

function pathToUri(filePath) {
  let resolved = path.resolve(filePath).replace(/\\/g, "/");
  if (!resolved.startsWith("/")) resolved = "/" + resolved;
  return "file://" + encodeURI(resolved);
}

function offsetAt(text, position) {
  let offset = 0, line = 0;
  while (line < position.line && offset < text.length) {
    const next = text.indexOf("\n", offset);
    if (next < 0) return text.length;
    offset = next + 1; ++line;
  }
  const end = text.indexOf("\n", offset);
  const length = (end < 0 ? text.length : end) - offset;
  return offset + Math.min(position.character, length);
}

function positionAt(text, offset) {
  offset = Math.max(0, Math.min(offset, text.length));
  const head = text.slice(0, offset);
  const lines = head.split(/\r?\n/);
  return { line: lines.length - 1, character: lines[lines.length - 1].length };
}

function wordRangeAt(text, position) {
  const offset = offsetAt(text, position);
  let start = offset;
  let end = offset;
  while (start > 0 && /[A-Za-z0-9_]/.test(text[start - 1])) start--;
  while (end < text.length && /[A-Za-z0-9_]/.test(text[end])) end++;
  return {
    word: text.slice(start, end),
    start,
    end,
    range: { start: positionAt(text, start), end: positionAt(text, end) },
  };
}

function findWorkspaceRoot(filePath) {
  let dir = fs.existsSync(filePath) && fs.statSync(filePath).isDirectory()
    ? filePath
    : path.dirname(filePath);
  while (dir && dir !== path.dirname(dir)) {
    if (fs.existsSync(path.join(dir, "project.json")) || fs.existsSync(path.join(dir, ".git")))
      return dir;
    dir = path.dirname(dir);
  }
  return process.cwd();
}

function candidateCliPaths(startFile, settings = {}) {
  const candidates = [];
  if (settings.cliPath) candidates.push(settings.cliPath);
  if (process.env.MIPSYNC_CLI_EXE) candidates.push(process.env.MIPSYNC_CLI_EXE);
  if (settings.enginePath) candidates.push(path.join(path.dirname(settings.enginePath), "mipsync.exe"));
  if (process.env.MIPSYNC_ENGINE_EXE)
    candidates.push(path.join(path.dirname(process.env.MIPSYNC_ENGINE_EXE), "mipsync.exe"));
  const repoRoot = path.resolve(__dirname, "..", "..", "..");
  candidates.push(path.join(repoRoot, "build", "src", "Release", "mipsync.exe"));
  candidates.push(path.join(repoRoot, "build", "src", "mipsync.exe"));
  candidates.push(path.join(repoRoot, "mipsync.exe"));
  if (startFile) {
    const root = findWorkspaceRoot(startFile);
    candidates.push(path.join(root, "mipsync.exe"));
  }
  return [...new Set(candidates)].filter(Boolean);
}

function findCliPath(startFile, settings) {
  return candidateCliPaths(startFile, settings).find((candidate) => {
    try {
      return fs.existsSync(candidate) && fs.statSync(candidate).isFile();
    } catch {
      return false;
    }
  }) || "";
}

function parseCliDiagnostics(output) {
  let response;
  try {
    response = JSON.parse(output);
  } catch {
    return [];
  }
  return (response.diagnostics || []).map((item) => {
    const location = item.location || {};
    const lineNumber = Math.max(0, Number(location.line || 1) - 1);
    const column = Math.max(0, Number(location.column || 1) - 1);
    const severity = item.severity === "warning" ? 2 : item.severity === "info" ? 3 : 1;
    return {
      range: {
        start: { line: lineNumber, character: column },
        end: { line: lineNumber, character: column + 1 },
      },
      severity,
      source: "mips#",
      message: item.message || "Mips# compilation failed.",
      code: item.code || "compiler",
    };
  });
}

function validateWithEngine(text, uri, settings = {}) {
  const originalPath = uriToPath(uri);
  const cliPath = findCliPath(originalPath, settings);
  if (!cliPath) {
    return [{
      range: { start: { line: 0, character: 0 }, end: { line: 0, character: 1 } },
      severity: 3,
      source: "mips#",
      message: "mipsync.exe が見つからないため、コンパイラ診断は無効です。Editorから一度Open in IDEを実行してください。",
      code: "cli-not-found",
    }];
  }

  const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), "mips-lsp-"));
  const safeName = path.basename(originalPath || "script.mips") || "script.mips";
  const tempFile = path.join(tempDir, safeName.endsWith(".mips") ? safeName : `${safeName}.mips`);
  try {
    fs.writeFileSync(tempFile, text, "utf8");
    const result = childProcess.spawnSync(cliPath, ["language", "compile", tempFile, "--json"], {
      cwd: path.dirname(cliPath),
      encoding: "utf8",
      timeout: settings.validationTimeoutMs || 8000,
      windowsHide: true,
    });
    const diagnostics = parseCliDiagnostics(result.stdout || "");
    if (result.error && result.error.code === "ETIMEDOUT") {
      diagnostics.push({
        range: { start: { line: 0, character: 0 }, end: { line: 0, character: 1 } },
        severity: 2,
        source: "mips#",
        message: "Mips# validation timed out.",
        code: "timeout",
      });
    }
    if (result.error && result.error.code !== "ETIMEDOUT") {
      diagnostics.push({
        range: { start: { line: 0, character: 0 }, end: { line: 0, character: 1 } },
        severity: 2,
        source: "mips#",
        message: `Mips# CLI validation failed: ${result.error.message}`,
        code: "cli-error",
      });
    }
    return diagnostics;
  } finally {
    try { fs.rmSync(tempDir, { recursive: true, force: true }); } catch {}
  }
}

function parseSymbols(text) {
  return analyze(text).declarations.map(symbol => ({ ...symbol, range: {
    start: positionAt(text, symbol.start), end: positionAt(text, symbol.start + symbol.length),
  }}));
}

function inferLocalTypes(text) {
  const types = {};
  for (const symbol of analyze(text).declarations)
    if (symbol.type && symbol.kind !== "method" && symbol.kind !== "class" && symbol.kind !== "enum")
      types[symbol.name] = symbol.type.endsWith("[]") ? "Array" : symbol.type;
  const source = codeOnly(text);
  for (const match of source.matchAll(/\bvar\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(?:[A-Za-z_][A-Za-z0-9_]*\s*\.\s*)?GetComponent\s*<\s*([A-Za-z_][A-Za-z0-9_]*)\s*>/g))
    types[match[1]] = match[2];
  for (const match of source.matchAll(/\bvar\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*Scene\s*\.\s*Find(?:ById)?\s*\(/g))
    types[match[1]] = "Entity";
  for (const match of source.matchAll(/\bvar\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(?:new\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*(\(|\[)/g))
    if (match[3] === "[") types[match[1]] = "Array";
    else if (TYPES.includes(match[2])) types[match[1]] = match[2];
  for (const match of source.matchAll(/\bvar\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*\[/g)) types[match[1]] = "Array";
  return types;
}

function completionItem(label, detail, documentation, kind = 6, insertText) {
  return { label, detail, documentation, kind, insertText: insertText || label };
}

function memberCompletions(members) {
  return Object.entries(members || {}).map(([name, detail]) => completionItem(name, detail, undefined, detail.includes("(") ? 3 : 10));
}

function getCompletions(text, position) {
  const offset = offsetAt(text, position);
  const before = text.slice(0, offset);
  if (/(?:^|\n)\s*(?:\[[^\]\r\n]*\]\s*)*\[\s*(?:[A-Za-z]+(?:\([^\r\n]*?\))?\s*,\s*)*[A-Za-z]*$/.test(before)) {
    return Object.entries(ATTRIBUTES).map(([name, info]) => ({
      ...completionItem(name, info.signature, info.documentation, 7, info.snippet), insertTextFormat: 2,
    }));
  }
  const dot = /([A-Za-z_][A-Za-z0-9_]*)\.\s*[A-Za-z0-9_]*$/.exec(before);
  if (dot) {
    const owner = dot[1];
    if (GLOBALS[owner]) return memberCompletions(GLOBALS[owner].members);
    const localType = inferLocalTypes(text)[owner];
    if (localType && INSTANCE_MEMBERS[localType]) return memberCompletions(INSTANCE_MEMBERS[localType]);
    return [];
  }

  const symbols = parseSymbols(text).map((symbol) =>
    completionItem(symbol.name, symbol.type ? `${symbol.kind}: ${symbol.type}` : symbol.kind, undefined, symbol.kind === "method" ? 3 : 6));
  return [
    ...KEYWORDS.map((keyword) => completionItem(keyword, "Mips# keyword", undefined, 14)),
    ...TYPES.map((type) => completionItem(type, "Mips# type", undefined, 7)),
    ...Object.entries(GLOBALS).map(([name, info]) => completionItem(name, info.detail, info.documentation, 6)),
    ...SNIPPETS.map((snippet) => ({
      label: snippet.label,
      kind: 15,
      detail: "Mips# snippet",
      documentation: snippet.documentation,
      insertText: snippet.insertText,
      insertTextFormat: 2,
    })),
    ...symbols,
  ];
}

function getHover(text, position) {
  const { word, range } = wordRangeAt(text, position);
  if (!word) return null;
  const offset = offsetAt(text, position);
  const start = wordRangeAt(text, position).start;
  const prefix = text.slice(Math.max(0, start - 64), start);
  const attribute = ATTRIBUTES[word.replace(/Attribute$/, "")];
  if (attribute && analyze(text).tokens.some(t => t.start === start && t.type === "decorator"))
    return { range, contents: { kind: "markdown", value: `\`\`\`mips\n[${attribute.signature}]\n\`\`\`\n${attribute.documentation}` } };
  const dot = /([A-Za-z_][A-Za-z0-9_]*)\.\s*$/.exec(prefix);
  if (dot) {
    const owner = dot[1];
    const ownerInfo = GLOBALS[owner];
    const localType = inferLocalTypes(text)[owner];
    const members = ownerInfo?.members || INSTANCE_MEMBERS[localType] || {};
    if (members[word]) {
      return {
        range,
        contents: { kind: "markdown", value: `\`\`\`mips\n${members[word]}\n\`\`\`` },
      };
    }
  }
  if (GLOBALS[word]) {
    return {
      range,
      contents: { kind: "markdown", value: `\`\`\`mips\n${GLOBALS[word].detail}\n\`\`\`\n${GLOBALS[word].documentation}` },
    };
  }
  if (TYPES.includes(word)) {
    return {
      range,
      contents: { kind: "markdown", value: `\`\`\`mips\n${word}\n\`\`\`\nMips# type.` },
    };
  }
  const symbol = parseSymbols(text).find((item) => item.name === word);
  if (symbol) {
    return {
      range,
      contents: { kind: "markdown", value: `\`\`\`mips\n${symbol.type ? `${symbol.type} ` : ""}${symbol.name}\n\`\`\`\n${symbol.kind}` },
    };
  }
  return null;
}

function getDefinition(text, position, uri) {
  const { word } = wordRangeAt(text, position);
  if (!word) return null;
  const symbol = parseSymbols(text).find((item) => item.name === word);
  if (!symbol) return null;
  return { uri, range: symbol.range };
}

module.exports = {
  uriToPath,
  pathToUri,
  offsetAt,
  positionAt,
  validateWithEngine,
  getCompletions,
  getHover,
  getDefinition,
  parseSymbols,
  inferLocalTypes,
};
