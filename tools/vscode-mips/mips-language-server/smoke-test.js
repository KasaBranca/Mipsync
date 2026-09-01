#!/usr/bin/env node
"use strict";

const childProcess = require("child_process");

function send(processHandle, message) {
  const json = JSON.stringify(message);
  processHandle.stdin.write(`Content-Length: ${Buffer.byteLength(json, "utf8")}\r\n\r\n${json}`);
}

function readMessages(processHandle, onMessage) {
  let buffer = Buffer.alloc(0);
  processHandle.stdout.on("data", (chunk) => {
    buffer = Buffer.concat([buffer, chunk]);
    while (true) {
      const headerEnd = buffer.indexOf("\r\n\r\n");
      if (headerEnd < 0) return;
      const header = buffer.slice(0, headerEnd).toString("utf8");
      const match = /Content-Length:\s*(\d+)/i.exec(header);
      if (!match) throw new Error(`Invalid LSP header: ${header}`);
      const length = Number(match[1]);
      const bodyStart = headerEnd + 4;
      const bodyEnd = bodyStart + length;
      if (buffer.length < bodyEnd) return;
      const message = JSON.parse(buffer.slice(bodyStart, bodyEnd).toString("utf8"));
      buffer = buffer.slice(bodyEnd);
      onMessage(message);
    }
  });
}

async function main() {
  const enginePath = process.argv[2] || "D:/Nostalty/build/src/MipsyncEngine.exe";
  const server = childProcess.spawn(process.execPath, ["tools/mips-language-server/server.js"], {
    cwd: process.cwd(),
    stdio: ["pipe", "pipe", "pipe"],
    windowsHide: true,
  });

  const results = {
    initialized: false,
    inputCompletions: false,
    diagnostics: false,
  };

  readMessages(server, (message) => {
    if (message.id === 1) results.initialized = true;
    if (message.id === 2) {
      const labels = (message.result?.items || []).map((item) => item.label);
      results.inputCompletions = labels.includes("GetKey") && labels.includes("GetAxis");
    }
    if (message.method === "textDocument/publishDiagnostics") {
      results.diagnostics = (message.params?.diagnostics || []).some((diag) => diag.message.includes("expected expression"));
    }
    if (Object.values(results).every(Boolean)) {
      console.log("Mips# LSP smoke test OK");
      server.kill();
      process.exit(0);
    }
  });

  server.stderr.on("data", (chunk) => process.stderr.write(chunk));

  const text = "class Foo : MipsBehaviour { public float x = ; void Update() { Input. } }";
  send(server, {
    jsonrpc: "2.0",
    id: 1,
    method: "initialize",
    params: {
      initializationOptions: {
        enginePath,
        validationDebounceMs: 10,
      },
    },
  });
  setTimeout(() => {
    send(server, {
      jsonrpc: "2.0",
      method: "textDocument/didOpen",
      params: {
        textDocument: {
          uri: "file:///D:/Nostalty/tests/mips/Foo.mips",
          languageId: "mips",
          version: 1,
          text,
        },
      },
    });
    send(server, {
      jsonrpc: "2.0",
      id: 2,
      method: "textDocument/completion",
      params: {
        textDocument: { uri: "file:///D:/Nostalty/tests/mips/Foo.mips" },
        position: { line: 0, character: text.indexOf("Input.") + "Input.".length },
      },
    });
  }, 100);

  setTimeout(() => {
    console.error(`Mips# LSP smoke test failed: ${JSON.stringify(results)}`);
    server.kill();
    process.exit(1);
  }, 6000);
}

main().catch((error) => {
  console.error(error.stack || String(error));
  process.exit(1);
});
