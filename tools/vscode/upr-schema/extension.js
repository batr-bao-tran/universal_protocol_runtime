const cp = require("child_process");
const path = require("path");

let vscode = null;
try {
  // The 'vscode' module is only available inside the VS Code extension host.
  vscode = require("vscode");
} catch (error) {
  if (require.main === module) {
    process.stderr.write(
        "This file is a VS Code extension entrypoint and cannot be run directly with Node.\n" +
        "Open 'tools/vscode/upr-schema' in VS Code and press F5 to launch the extension host,\n" +
        "or run 'node server/upr_language_server.js' if you want to start only the language server.\n",
    );
    process.exit(1);
  }
  throw error;
}

class JsonRpcConnection {
  constructor(childProcess, outputChannel) {
    this.childProcess = childProcess;
    this.outputChannel = outputChannel;
    this.buffer = Buffer.alloc(0);
    this.nextId = 1;
    this.pending = new Map();
    this.notificationHandlers = new Map();

    childProcess.stdout.on("data", (chunk) => this.onData(chunk));
    childProcess.stderr.on("data", (chunk) => {
      this.outputChannel.appendLine(String(chunk).trimEnd());
    });
    childProcess.on("exit", (code, signal) => {
      for (const [, pending] of this.pending) {
        pending.reject(new Error(`UPR language server exited (${code ?? "null"}, ${signal ?? "null"})`));
      }
      this.pending.clear();
    });
  }

  onNotification(method, handler) {
    this.notificationHandlers.set(method, handler);
  }

  notify(method, params) {
    this.writeMessage({ jsonrpc: "2.0", method, params });
  }

  request(method, params) {
    const id = this.nextId++;
    const message = { jsonrpc: "2.0", id, method, params };
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this.writeMessage(message);
    });
  }

  writeMessage(message) {
    const payload = Buffer.from(JSON.stringify(message), "utf8");
    const header = Buffer.from(`Content-Length: ${payload.length}\r\n\r\n`, "utf8");
    this.childProcess.stdin.write(Buffer.concat([header, payload]));
  }

  onData(chunk) {
    this.buffer = Buffer.concat([this.buffer, chunk]);
    while (true) {
      const delimiterIndex = this.buffer.indexOf("\r\n\r\n");
      if (delimiterIndex === -1) {
        return;
      }
      const headerText = this.buffer.slice(0, delimiterIndex).toString("utf8");
      const match = headerText.match(/Content-Length:\s*(\d+)/i);
      if (!match) {
        this.buffer = Buffer.alloc(0);
        return;
      }
      const contentLength = Number(match[1]);
      const messageStart = delimiterIndex + 4;
      if (this.buffer.length < messageStart + contentLength) {
        return;
      }
      const payload = this.buffer.slice(messageStart, messageStart + contentLength).toString("utf8");
      this.buffer = this.buffer.slice(messageStart + contentLength);
      this.handleMessage(JSON.parse(payload));
    }
  }

  handleMessage(message) {
    if (Object.prototype.hasOwnProperty.call(message, "id")) {
      const pending = this.pending.get(message.id);
      if (!pending) {
        return;
      }
      this.pending.delete(message.id);
      if (message.error) {
        pending.reject(new Error(message.error.message || "UPR language server request failed."));
        return;
      }
      pending.resolve(message.result);
      return;
    }

    const handler = this.notificationHandlers.get(message.method);
    if (handler) {
      handler(message.params);
    }
  }
}

function toLspPosition(position) {
  return { line: position.line, character: position.character };
}

function toLspRange(range) {
  return {
    start: toLspPosition(range.start),
    end: toLspPosition(range.end),
  };
}

function fromLspPosition(position) {
  return new vscode.Position(position.line, position.character);
}

function fromLspRange(range) {
  return new vscode.Range(fromLspPosition(range.start), fromLspPosition(range.end));
}

function fromLspCompletionItem(item) {
  const kindMap = {
    1: vscode.CompletionItemKind.Text,
    3: vscode.CompletionItemKind.Function,
    5: vscode.CompletionItemKind.Field,
    13: vscode.CompletionItemKind.Enum,
    14: vscode.CompletionItemKind.Keyword,
    21: vscode.CompletionItemKind.Constant,
    22: vscode.CompletionItemKind.Struct,
  };
  const completionItem = new vscode.CompletionItem(
      item.label,
      kindMap[item.kind] ?? vscode.CompletionItemKind.Text,
  );
  completionItem.detail = item.detail;
  completionItem.documentation = item.documentation;
  completionItem.insertText = item.insertText || item.label;
  return completionItem;
}

function fromLspLocation(location) {
  return new vscode.Location(vscode.Uri.parse(location.uri), fromLspRange(location.range));
}

function fromLspHover(hover) {
  if (!hover) {
    return null;
  }
  const contents = Array.isArray(hover.contents) ? hover.contents : [hover.contents];
  const markdown = contents.map((entry) => {
    if (typeof entry === "string") {
      return entry;
    }
    if (entry && typeof entry.value === "string") {
      return entry.kind === "markdown" ? entry.value : `\`\`\`\n${entry.value}\n\`\`\``;
    }
    return "";
  }).filter(Boolean).join("\n\n");
  return new vscode.Hover(new vscode.MarkdownString(markdown), hover.range ? fromLspRange(hover.range) : undefined);
}

function publishDiagnostics(collection, uri, diagnostics) {
  const severityMap = {
    1: vscode.DiagnosticSeverity.Error,
    2: vscode.DiagnosticSeverity.Warning,
    3: vscode.DiagnosticSeverity.Information,
    4: vscode.DiagnosticSeverity.Hint,
  };
  const vscodeDiagnostics = diagnostics.map((diagnostic) => {
    const item = new vscode.Diagnostic(
        fromLspRange(diagnostic.range),
        diagnostic.message,
        severityMap[diagnostic.severity] ?? vscode.DiagnosticSeverity.Error,
    );
    item.source = diagnostic.source || "upr";
    return item;
  });
  collection.set(vscode.Uri.parse(uri), vscodeDiagnostics);
}

function buildSemanticTokens(document) {
  const legend = new vscode.SemanticTokensLegend(["uprMessage", "uprEnum", "uprField"], []);
  const builder = new vscode.SemanticTokensBuilder(legend);
  const enumNames = new Set();
  const lines = document.getText().split(/\r?\n/);

  for (let lineIndex = 0; lineIndex < lines.length; ++lineIndex) {
    const line = lines[lineIndex];
    const enumMatch = line.match(/^(\s*enum\s+)([A-Za-z_][A-Za-z0-9_.-]*)/);
    if (enumMatch) {
      const name = enumMatch[2];
      const start = line.indexOf(name);
      enumNames.add(name);
      builder.push(lineIndex, start, name.length, 1, 0);
    }

    const messageMatch = line.match(/^(\s*message\s+)([A-Za-z_][A-Za-z0-9_.-]*)/);
    if (messageMatch) {
      const name = messageMatch[2];
      const start = line.indexOf(name);
      builder.push(lineIndex, start, name.length, 0, 0);
    }
  }

  const blockStack = [];
  for (let lineIndex = 0; lineIndex < lines.length; ++lineIndex) {
    const line = lines[lineIndex];
    const trimmed = line.trim();
    const opensEnum = /^\s*enum\s+[A-Za-z_][A-Za-z0-9_.-]*/.test(line);
    const opensMessage = /^\s*message\s+[A-Za-z_][A-Za-z0-9_.-]*/.test(line);

    if (opensEnum && line.includes("{")) {
      blockStack.push("enum");
    } else if (opensMessage && line.includes("{")) {
      blockStack.push("message");
    }

    const currentBlock = blockStack.length > 0 ? blockStack[blockStack.length - 1] : null;
    const fieldMatch = line.match(/^(\s*)([A-Za-z_][A-Za-z0-9_.-]*)(\s*:)/);
    if (currentBlock === "message" && fieldMatch) {
      const name = fieldMatch[2];
      const start = line.indexOf(name);
      builder.push(lineIndex, start, name.length, 2, 0);

      const typeMatch = line.match(/:\s*([A-Za-z_][A-Za-z0-9_.-]*)/);
      if (typeMatch && enumNames.has(typeMatch[1])) {
        const typeName = typeMatch[1];
        const typeStart = line.indexOf(typeName, start + name.length);
        if (typeStart >= 0) {
          builder.push(lineIndex, typeStart, typeName.length, 1, 0);
        }
      }
    }

    const enumValueMatch = line.match(/^\s*[0-9]+\s*=\s*([A-Za-z_][A-Za-z0-9_.-]*)\s*(?:,)?\s*$/);
    if (currentBlock === "enum" && enumValueMatch) {
      const name = enumValueMatch[1];
      const start = line.indexOf(name);
      if (start >= 0) {
        builder.push(lineIndex, start, name.length, 2, 0);
      }
    }

    for (const character of line) {
      if (character === "{") {
        if (!opensEnum && !opensMessage) {
          blockStack.push(currentBlock);
        }
      } else if (character === "}") {
        if (blockStack.length > 0) {
          blockStack.pop();
        }
      }
    }

    if ((opensEnum || opensMessage) && !line.includes("{") && trimmed.endsWith("{")) {
      blockStack.push(opensEnum ? "enum" : "message");
    }
  }

  return { legend, tokens: builder.build() };
}

async function activate(context) {
  if (!vscode.workspace.getConfiguration("upr").get("languageServer.enabled", true)) {
    return;
  }

  const outputChannel = vscode.window.createOutputChannel("UPR Schema");
  const diagnosticCollection = vscode.languages.createDiagnosticCollection("upr");
  context.subscriptions.push(outputChannel, diagnosticCollection);

  const semanticLegend = new vscode.SemanticTokensLegend(["uprMessage", "uprEnum", "uprField"], []);

  const serverPath = path.join(context.extensionPath, "server", "upr_language_server.js");
  const child = cp.spawn(process.execPath, [serverPath], {
    stdio: ["pipe", "pipe", "pipe"],
  });
  const connection = new JsonRpcConnection(child, outputChannel);

  connection.onNotification("textDocument/publishDiagnostics", (params) => {
    publishDiagnostics(diagnosticCollection, params.uri, params.diagnostics || []);
  });

  await connection.request("initialize", {
    processId: process.pid,
    rootUri: vscode.workspace.workspaceFolders?.[0]?.uri.toString() || null,
    capabilities: {},
    clientInfo: {
      name: "upr-schema-vscode",
      version: "0.0.1",
    },
  });
  connection.notify("initialized", {});

  const sendOpen = (document) => {
    if (document.languageId !== "upr") {
      return;
    }
    connection.notify("textDocument/didOpen", {
      textDocument: {
        uri: document.uri.toString(),
        languageId: document.languageId,
        version: document.version,
        text: document.getText(),
      },
    });
  };

  const sendChange = (document) => {
    if (document.languageId !== "upr") {
      return;
    }
    connection.notify("textDocument/didChange", {
      textDocument: {
        uri: document.uri.toString(),
        version: document.version,
      },
      contentChanges: [
        {
          text: document.getText(),
        },
      ],
    });
  };

  for (const document of vscode.workspace.textDocuments) {
    sendOpen(document);
  }

  context.subscriptions.push(
      vscode.workspace.onDidOpenTextDocument(sendOpen),
      vscode.workspace.onDidChangeTextDocument((event) => sendChange(event.document)),
      vscode.workspace.onDidCloseTextDocument((document) => {
        if (document.languageId !== "upr") {
          return;
        }
        diagnosticCollection.delete(document.uri);
        connection.notify("textDocument/didClose", {
          textDocument: {
            uri: document.uri.toString(),
          },
        });
      }),
      vscode.languages.registerCompletionItemProvider(
          { language: "upr", scheme: "file" },
          {
            provideCompletionItems: async (document, position) => {
              const result = await connection.request("textDocument/completion", {
                textDocument: { uri: document.uri.toString() },
                position: toLspPosition(position),
              });
              if (!Array.isArray(result)) {
                return [];
              }
              return result.map(fromLspCompletionItem);
            },
          },
          ":", "[", "(", ",", ".",
      ),
      vscode.languages.registerDefinitionProvider(
          { language: "upr", scheme: "file" },
          {
            provideDefinition: async (document, position) => {
              const result = await connection.request("textDocument/definition", {
                textDocument: { uri: document.uri.toString() },
                position: toLspPosition(position),
              });
              if (!result) {
                return null;
              }
              if (Array.isArray(result)) {
                return result.map(fromLspLocation);
              }
              return fromLspLocation(result);
            },
          },
      ),
      vscode.languages.registerHoverProvider(
          { language: "upr", scheme: "file" },
          {
            provideHover: async (document, position) => {
              const result = await connection.request("textDocument/hover", {
                textDocument: { uri: document.uri.toString() },
                position: toLspPosition(position),
              });
              return fromLspHover(result);
            },
          },
      ),
      vscode.languages.registerDocumentSemanticTokensProvider(
          { language: "upr" },
          {
            provideDocumentSemanticTokens: (document) => buildSemanticTokens(document).tokens,
          },
          semanticLegend,
      ),
      new vscode.Disposable(() => {
        connection.notify("shutdown", null);
        child.kill();
      }),
  );
}

function deactivate() {}

module.exports = {
  activate,
  deactivate,
};
