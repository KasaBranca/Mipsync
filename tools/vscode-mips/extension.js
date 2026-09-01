"use strict";

const path = require("path");
const childProcess = require("child_process");
const vscode = require("vscode");

const mipsLanguageSelector = [{ language: "mipssharp" }];

function isMipsDocument(document) {
  return document.languageId === "mipssharp";
}

function isMipsFile(document) {
  return document?.uri?.scheme === "file" && document.fileName.toLowerCase().endsWith(".mips");
}

async function forceMipsSharpLanguage(document) {
  if (!isMipsFile(document) || document.languageId === "mipssharp") return document;
  try {
    return await vscode.languages.setTextDocumentLanguage(document, "mipssharp");
  } catch (error) {
    console.error(`[Mips#] Failed to switch language mode: ${error}`);
    return document;
  }
}

class MiniLspClient {
  constructor(context) {
    this.context = context;
    this.nextId = 1;
    this.pending = new Map();
    this.buffer = Buffer.alloc(0);
    this.diagnostics = vscode.languages.createDiagnosticCollection("mips#");
    this.start();
  }

  start() {
    const serverPath = this.resolveServerPath();
    this.process = childProcess.spawn(process.execPath, [serverPath], {
      cwd: path.resolve(this.context.extensionPath, "..", ".."),
      stdio: ["pipe", "pipe", "pipe"],
      windowsHide: true,
    });
    this.process.stdout.on("data", (chunk) => this.onData(chunk));
    this.process.stderr.on("data", (chunk) => console.error(`[Mips# LSP] ${chunk}`));
    this.process.on("exit", () => {
      for (const { reject } of this.pending.values()) reject(new Error("Mips# language server exited"));
      this.pending.clear();
    });
    this.request("initialize", {
      processId: process.pid,
      rootUri: vscode.workspace.workspaceFolders?.[0]?.uri.toString(),
      capabilities: {},
      initializationOptions: this.readSettings(),
    }).then(() => this.notify("initialized", {}));
  }

  resolveServerPath() {
    const candidates = [
      path.resolve(this.context.extensionPath, "mips-language-server", "server.js"),
      path.resolve(this.context.extensionPath, "..", "mips-language-server", "server.js"),
      path.resolve(this.context.extensionPath, "..", "..", "mips-language-server", "server.js"),
    ];
    for (const candidate of candidates) {
      try {
        if (require("fs").existsSync(candidate)) return candidate;
      } catch {
        // Try the next layout.
      }
    }
    return candidates[0];
  }

  readSettings() {
    const config = vscode.workspace.getConfiguration("mipsync");
    return {
      cliPath: config.get("cliPath") || "",
      enginePath: config.get("enginePath") || "",
      validationDebounceMs: config.get("validationDebounceMs") || 350,
      validationTimeoutMs: config.get("validationTimeoutMs") || 8000,
    };
  }

  dispose() {
    this.diagnostics.dispose();
    if (this.process) {
      this.request("shutdown", {}).finally(() => {
        this.notify("exit", {});
        this.process.kill();
      });
    }
  }

  send(message) {
    const json = JSON.stringify(message);
    this.process.stdin.write(`Content-Length: ${Buffer.byteLength(json, "utf8")}\r\n\r\n${json}`);
  }

  request(method, params) {
    const id = this.nextId++;
    this.send({ jsonrpc: "2.0", id, method, params });
    return new Promise((resolve, reject) => this.pending.set(id, { resolve, reject }));
  }

  notify(method, params) {
    this.send({ jsonrpc: "2.0", method, params });
  }

  onData(chunk) {
    this.buffer = Buffer.concat([this.buffer, chunk]);
    while (true) {
      const headerEnd = this.buffer.indexOf("\r\n\r\n");
      if (headerEnd < 0) return;
      const header = this.buffer.slice(0, headerEnd).toString("utf8");
      const match = /Content-Length:\s*(\d+)/i.exec(header);
      if (!match) {
        this.buffer = this.buffer.slice(headerEnd + 4);
        continue;
      }
      const length = Number(match[1]);
      const bodyStart = headerEnd + 4;
      const bodyEnd = bodyStart + length;
      if (this.buffer.length < bodyEnd) return;
      const body = this.buffer.slice(bodyStart, bodyEnd).toString("utf8");
      this.buffer = this.buffer.slice(bodyEnd);
      this.handleMessage(JSON.parse(body));
    }
  }

  handleMessage(message) {
    if (message.method === "textDocument/publishDiagnostics") {
      const uri = vscode.Uri.parse(message.params.uri);
      const diagnostics = (message.params.diagnostics || []).map((diag) => {
        const range = new vscode.Range(
          diag.range.start.line,
          diag.range.start.character,
          diag.range.end.line,
          diag.range.end.character,
        );
        const severityMap = {
          1: vscode.DiagnosticSeverity.Error,
          2: vscode.DiagnosticSeverity.Warning,
          3: vscode.DiagnosticSeverity.Information,
          4: vscode.DiagnosticSeverity.Hint,
        };
        const item = new vscode.Diagnostic(range, diag.message, severityMap[diag.severity] ?? vscode.DiagnosticSeverity.Error);
        item.source = diag.source || "mips#";
        item.code = diag.code;
        return item;
      });
      this.diagnostics.set(uri, diagnostics);
      return;
    }
    if (Object.prototype.hasOwnProperty.call(message, "id")) {
      const pending = this.pending.get(message.id);
      if (!pending) return;
      this.pending.delete(message.id);
      if (message.error) pending.reject(new Error(message.error.message));
      else pending.resolve(message.result);
    }
  }

  didOpen(document) {
    if (!isMipsDocument(document)) return;
    this.notify("textDocument/didOpen", {
      textDocument: {
        uri: document.uri.toString(),
        languageId: document.languageId,
        version: document.version,
        text: document.getText(),
      },
    });
  }

  didChange(document) {
    if (!isMipsDocument(document)) return;
    this.notify("textDocument/didChange", {
      textDocument: { uri: document.uri.toString(), version: document.version },
      contentChanges: [{ text: document.getText() }],
    });
  }

  didSave(document) {
    if (!isMipsDocument(document)) return;
    this.notify("textDocument/didSave", { textDocument: { uri: document.uri.toString() } });
  }

  didClose(document) {
    if (!isMipsDocument(document)) return;
    this.notify("textDocument/didClose", { textDocument: { uri: document.uri.toString() } });
  }
}

function toVsCodeCompletion(item) {
  const result = new vscode.CompletionItem(item.label, item.kind || vscode.CompletionItemKind.Field);
  result.detail = item.detail;
  result.documentation = item.documentation;
  result.insertText = item.insertTextFormat === 2 ? new vscode.SnippetString(item.insertText) : item.insertText;
  return result;
}

const semanticTokenTypes = [
  "class",
  "enum",
  "function",
  "method",
  "property",
  "variable",
  "type",
];
const semanticTokenModifiers = ["declaration", "static"];
const semanticTokenLegend = new vscode.SemanticTokensLegend(semanticTokenTypes, semanticTokenModifiers);

const builtinTypeNames = [
  "MipsBehaviour",
  "IEnumerator",
  "WaitForSeconds",
  "AudioClip",
  "Entity",
  "Transform",
  "Vector3",
  "Animator",
  "AudioSource",
  "Collider",
  "Rigidbody",
  "bool",
  "int",
  "float",
  "string",
  "void",
];
const staticClassNames = [
  "Time",
  "Input",
  "Log",
  "Debug",
  "Mathf",
  "Physics",
  "Scene",
  "Application",
  "Save",
];
const mipsTypeRegex = "(?:bool|int|float|string|void|IEnumerator|Vector3|AudioClip|Entity|Transform|Animator|AudioSource|Collider|Rigidbody|[A-Za-z_][A-Za-z0-9_]*(?:\\[\\])?)";

function makeCodeOnlyText(text) {
  const chars = Array.from(text);
  let i = 0;
  while (i < chars.length) {
    if (chars[i] === "/" && chars[i + 1] === "/") {
      while (i < chars.length && chars[i] !== "\n") chars[i++] = " ";
      continue;
    }
    if (chars[i] === "/" && chars[i + 1] === "*") {
      chars[i++] = " ";
      chars[i++] = " ";
      while (i < chars.length) {
        if (chars[i] === "*" && chars[i + 1] === "/") {
          chars[i++] = " ";
          chars[i++] = " ";
          break;
        }
        if (chars[i] !== "\n") chars[i] = " ";
        i++;
      }
      continue;
    }
    if (chars[i] === "\"") {
      chars[i++] = " ";
      while (i < chars.length) {
        if (chars[i] === "\\") {
          chars[i++] = " ";
          if (i < chars.length && chars[i] !== "\n") chars[i++] = " ";
          continue;
        }
        const done = chars[i] === "\"";
        if (chars[i] !== "\n") chars[i] = " ";
        i++;
        if (done) break;
      }
      continue;
    }
    i++;
  }
  return chars.join("");
}

function addSemanticRegex(tokens, text, regex, groupIndex, type, modifiers = [], priority = 0) {
  for (const match of text.matchAll(regex)) {
    const value = match[groupIndex];
    if (!value) continue;
    const offsetInMatch = match[0].indexOf(value);
    if (offsetInMatch < 0) continue;
    tokens.push({
      start: match.index + offsetInMatch,
      length: value.length,
      type,
      modifiers,
      priority,
    });
  }
}

function buildMipsSemanticTokens(document) {
  const source = makeCodeOnlyText(document.getText());
  const tokens = [];

  addSemanticRegex(tokens, source, /\b(class)\s+([A-Za-z_][A-Za-z0-9_]*)/g, 2, "class", ["declaration"], 100);
  addSemanticRegex(tokens, source, /\b(enum)\s+([A-Za-z_][A-Za-z0-9_]*)/g, 2, "enum", ["declaration"], 100);
  addSemanticRegex(tokens, source, new RegExp(`\\b(?:public\\s+|private\\s+)?${mipsTypeRegex}\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*(?=\\()`, "g"), 1, "function", ["declaration"], 90);
  addSemanticRegex(tokens, source, new RegExp(`\\b(public|private)\\s+${mipsTypeRegex}\\s+([A-Za-z_][A-Za-z0-9_]*)\\b(?!\\s*\\()`, "g"), 2, "property", ["declaration"], 80);
  addSemanticRegex(tokens, source, new RegExp(`\\b(?:var|bool|int|float|string|Vector3|AudioClip|Entity|Transform|Animator|AudioSource|Collider|Rigidbody)\\s+([A-Za-z_][A-Za-z0-9_]*)\\b(?!\\s*\\()`, "g"), 1, "variable", ["declaration"], 70);
  addSemanticRegex(tokens, source, new RegExp(`\\b(${builtinTypeNames.join("|")})\\b`, "g"), 1, "type", [], 60);
  addSemanticRegex(tokens, source, new RegExp(`\\b(${staticClassNames.join("|")})\\b`, "g"), 1, "class", ["static"], 60);
  addSemanticRegex(tokens, source, /\.(\s*)([A-Za-z_][A-Za-z0-9_]*)\s*(?=\()/g, 2, "method", [], 50);
  addSemanticRegex(tokens, source, /\b(?!if\b|for\b|while\b|switch\b|return\b|new\b|class\b|enum\b)([A-Za-z_][A-Za-z0-9_]*)\s*(?=\()/g, 1, "function", [], 40);
  addSemanticRegex(tokens, source, /\.(\s*)([A-Za-z_][A-Za-z0-9_]*)\b/g, 2, "property", [], 30);

  tokens.sort((a, b) => a.start - b.start || b.priority - a.priority || b.length - a.length);

  const filtered = [];
  let lastLine = -1;
  let lastEnd = -1;
  for (const token of tokens) {
    const start = document.positionAt(token.start);
    const end = document.positionAt(token.start + token.length);
    if (start.line !== end.line) continue;
    if (start.line === lastLine && start.character < lastEnd) continue;
    filtered.push({ ...token, startPosition: start, endPosition: end });
    lastLine = start.line;
    lastEnd = end.character;
  }

  const builder = new vscode.SemanticTokensBuilder(semanticTokenLegend);
  for (const token of filtered) {
    builder.push(new vscode.Range(token.startPosition, token.endPosition), token.type, token.modifiers);
  }
  return builder.build();
}

function activate(context) {
  console.log("[Mips#] Mipsync Mips# extension activated");
  const client = new MiniLspClient(context);
  context.subscriptions.push(client);

  for (const doc of vscode.workspace.textDocuments) {
    forceMipsSharpLanguage(doc).then((updated) => client.didOpen(updated));
  }
  context.subscriptions.push(vscode.workspace.onDidOpenTextDocument((doc) => {
    forceMipsSharpLanguage(doc).then((updated) => client.didOpen(updated));
  }));
  context.subscriptions.push(vscode.workspace.onDidChangeTextDocument((event) => client.didChange(event.document)));
  context.subscriptions.push(vscode.workspace.onDidSaveTextDocument((doc) => client.didSave(doc)));
  context.subscriptions.push(vscode.workspace.onDidCloseTextDocument((doc) => client.didClose(doc)));
  context.subscriptions.push(vscode.workspace.onDidChangeConfiguration((event) => {
    if (event.affectsConfiguration("mipsync")) {
      client.notify("workspace/didChangeConfiguration", { settings: { mipsync: client.readSettings() } });
    }
  }));

  context.subscriptions.push(vscode.languages.registerDocumentSemanticTokensProvider(
    mipsLanguageSelector,
    { provideDocumentSemanticTokens: buildMipsSemanticTokens },
    semanticTokenLegend,
  ));

  context.subscriptions.push(vscode.languages.registerCompletionItemProvider(mipsLanguageSelector, {
    async provideCompletionItems(document, position) {
      const result = await client.request("textDocument/completion", {
        textDocument: { uri: document.uri.toString() },
        position: { line: position.line, character: position.character },
      });
      return new vscode.CompletionList((result.items || []).map(toVsCodeCompletion), Boolean(result.isIncomplete));
    },
  }, "."));

  context.subscriptions.push(vscode.languages.registerHoverProvider(mipsLanguageSelector, {
    async provideHover(document, position) {
      const result = await client.request("textDocument/hover", {
        textDocument: { uri: document.uri.toString() },
        position: { line: position.line, character: position.character },
      });
      if (!result) return null;
      const contents = result.contents?.kind === "markdown"
        ? new vscode.MarkdownString(result.contents.value)
        : result.contents;
      const range = result.range
        ? new vscode.Range(result.range.start.line, result.range.start.character, result.range.end.line, result.range.end.character)
        : undefined;
      return new vscode.Hover(contents, range);
    },
  }));

  context.subscriptions.push(vscode.languages.registerDefinitionProvider(mipsLanguageSelector, {
    async provideDefinition(document, position) {
      const result = await client.request("textDocument/definition", {
        textDocument: { uri: document.uri.toString() },
        position: { line: position.line, character: position.character },
      });
      if (!result) return null;
      return new vscode.Location(
        vscode.Uri.parse(result.uri),
        new vscode.Range(result.range.start.line, result.range.start.character, result.range.end.line, result.range.end.character),
      );
    },
  }));
}

function deactivate() {}

module.exports = {
  activate,
  deactivate,
};
