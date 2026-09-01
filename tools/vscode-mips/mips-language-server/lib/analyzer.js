"use strict";

const fs = require("fs");
const os = require("os");
const path = require("path");
const childProcess = require("child_process");
const { KEYWORDS, TYPES, GLOBALS, INSTANCE_MEMBERS, SNIPPETS } = require("./api");

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
  const lines = text.split(/\r?\n/);
  let offset = 0;
  for (let i = 0; i < position.line && i < lines.length; i++) offset += lines[i].length + 1;
  return offset + Math.min(position.character, lines[position.line]?.length ?? 0);
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
  const symbols = [];
  const patterns = [
    { kind: "class", re: /\bclass\s+([A-Za-z_][A-Za-z0-9_]*)/g },
    { kind: "enum", re: /\benum\s+([A-Za-z_][A-Za-z0-9_]*)/g },
    { kind: "field", re: /\bpublic\s+([A-Za-z_][A-Za-z0-9_]*(?:\[\])?)\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:=|;|\{)/g },
    { kind: "method", re: /\b(?:public\s+)?([A-Za-z_][A-Za-z0-9_]*(?:\[\])?|void)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(/g },
    { kind: "local", re: /\b(?:var|bool|int|float|string|Vector3|Animator|AudioSource|Transform|Entity|AudioClip)\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:=|;|\))/g },
  ];
  for (const pattern of patterns) {
    for (const match of text.matchAll(pattern.re)) {
      const name = match[pattern.kind === "field" || pattern.kind === "method" ? 2 : 1];
      const type = pattern.kind === "field" || pattern.kind === "method" ? match[1] : "";
      const start = match.index + match[0].indexOf(name);
      symbols.push({ name, type, kind: pattern.kind, start, range: { start: positionAt(text, start), end: positionAt(text, start + name.length) } });
    }
  }
  return symbols;
}

function inferLocalTypes(text) {
  const types = {};
  for (const match of text.matchAll(/\b(Transform|Vector3|Animator|AudioSource|Entity|AudioClip|bool|int|float|string|[A-Za-z_][A-Za-z0-9_]*\[\])\s+([A-Za-z_][A-Za-z0-9_]*)/g)) {
    types[match[2]] = match[1].endsWith("[]") ? "Array" : match[1];
  }
  for (const match of text.matchAll(/\bvar\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*GetComponent\s*<\s*([A-Za-z_][A-Za-z0-9_]*)\s*>/g)) {
    types[match[1]] = match[2];
  }
  for (const match of text.matchAll(/\bvar\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*Vector3\s*\(/g)) {
    types[match[1]] = "Vector3";
  }
  for (const match of text.matchAll(/\bvar\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*\[/g)) {
    types[match[1]] = "Array";
  }
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
  const prefix = text.slice(Math.max(0, offset - word.length - 64), offset - word.length);
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
};
