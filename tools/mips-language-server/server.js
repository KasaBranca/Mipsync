#!/usr/bin/env node
"use strict";

const {
  validateWithEngine,
  getCompletions,
  getHover,
  getDefinition,
} = require("./lib/analyzer");

const documents = new Map();
const validationTimers = new Map();
let nextServerRequestId = 1;
let settings = {};
let shutdownRequested = false;

function send(message) {
  const json = JSON.stringify(message);
  process.stdout.write(`Content-Length: ${Buffer.byteLength(json, "utf8")}\r\n\r\n${json}`);
}

function respond(id, result) {
  send({ jsonrpc: "2.0", id, result });
}

function respondError(id, code, message) {
  send({ jsonrpc: "2.0", id, error: { code, message } });
}

function notify(method, params) {
  send({ jsonrpc: "2.0", method, params });
}

function scheduleValidation(uri) {
  if (validationTimers.has(uri)) clearTimeout(validationTimers.get(uri));
  validationTimers.set(uri, setTimeout(() => {
    validationTimers.delete(uri);
    const doc = documents.get(uri);
    if (!doc) return;
    const diagnostics = validateWithEngine(doc.text, uri, settings);
    notify("textDocument/publishDiagnostics", { uri, diagnostics });
  }, settings.validationDebounceMs ?? 350));
}

function handleRequest(message) {
  const { id, method, params } = message;
  try {
    switch (method) {
      case "initialize": {
        settings = {
          ...(params?.initializationOptions || {}),
          ...((params?.initializationOptions || {}).mipsync || {}),
        };
        respond(id, {
          capabilities: {
            textDocumentSync: 2,
            completionProvider: { triggerCharacters: ["."] },
            hoverProvider: true,
            definitionProvider: true,
          },
          serverInfo: { name: "mips-language-server", version: "0.1.0" },
        });
        return;
      }
      case "shutdown":
        shutdownRequested = true;
        respond(id, null);
        return;
      case "textDocument/completion": {
        const uri = params?.textDocument?.uri;
        const doc = documents.get(uri);
        respond(id, doc ? { isIncomplete: false, items: getCompletions(doc.text, params.position) } : { isIncomplete: false, items: [] });
        return;
      }
      case "textDocument/hover": {
        const uri = params?.textDocument?.uri;
        const doc = documents.get(uri);
        respond(id, doc ? getHover(doc.text, params.position) : null);
        return;
      }
      case "textDocument/definition": {
        const uri = params?.textDocument?.uri;
        const doc = documents.get(uri);
        respond(id, doc ? getDefinition(doc.text, params.position, uri) : null);
        return;
      }
      default:
        respond(id, null);
        return;
    }
  } catch (error) {
    respondError(id, -32603, error.stack || String(error));
  }
}

function handleNotification(message) {
  const { method, params } = message;
  switch (method) {
    case "exit":
      process.exit(shutdownRequested ? 0 : 1);
      break;
    case "initialized":
      break;
    case "workspace/didChangeConfiguration":
      settings = { ...settings, ...(params?.settings?.mipsync || {}) };
      for (const uri of documents.keys()) scheduleValidation(uri);
      break;
    case "textDocument/didOpen":
      documents.set(params.textDocument.uri, {
        version: params.textDocument.version,
        text: params.textDocument.text,
      });
      scheduleValidation(params.textDocument.uri);
      break;
    case "textDocument/didChange": {
      const uri = params.textDocument.uri;
      const doc = documents.get(uri) || { text: "", version: 0 };
      doc.version = params.textDocument.version;
      doc.text = params.contentChanges?.[0]?.text ?? doc.text;
      documents.set(uri, doc);
      scheduleValidation(uri);
      break;
    }
    case "textDocument/didSave":
      scheduleValidation(params.textDocument.uri);
      break;
    case "textDocument/didClose":
      documents.delete(params.textDocument.uri);
      notify("textDocument/publishDiagnostics", { uri: params.textDocument.uri, diagnostics: [] });
      break;
    default:
      break;
  }
}

let buffer = Buffer.alloc(0);
process.stdin.on("data", (chunk) => {
  buffer = Buffer.concat([buffer, chunk]);
  while (true) {
    const headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) return;
    const header = buffer.slice(0, headerEnd).toString("utf8");
    const match = /Content-Length:\s*(\d+)/i.exec(header);
    if (!match) {
      buffer = buffer.slice(headerEnd + 4);
      continue;
    }
    const length = Number(match[1]);
    const bodyStart = headerEnd + 4;
    const bodyEnd = bodyStart + length;
    if (buffer.length < bodyEnd) return;
    const body = buffer.slice(bodyStart, bodyEnd).toString("utf8");
    buffer = buffer.slice(bodyEnd);
    const message = JSON.parse(body);
    if (Object.prototype.hasOwnProperty.call(message, "id")) handleRequest(message);
    else handleNotification(message);
  }
});

process.stdin.resume();
