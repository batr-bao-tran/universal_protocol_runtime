"use strict";

const fs = require("fs");
const path = require("path");
const { fileURLToPath, pathToFileURL } = require("url");

const CompletionItemKind = {
  Text: 1,
  Keyword: 14,
  Field: 5,
  Struct: 22,
  Enum: 13,
  Constant: 21,
  Function: 3,
};

const DiagnosticSeverity = {
  Error: 1,
  Warning: 2,
  Information: 3,
};

const builtinTypes = new Set([
  "uint8", "uint16", "uint32", "uint64",
  "uint8_be", "uint16_be", "uint32_be", "uint64_be",
  "int8", "int16", "int32", "int64",
  "int8_be", "int16_be", "int32_be", "int64_be",
  "float32", "float64", "float32_be", "float64_be",
  "bytes", "string", "ascii", "utf8", "enum",
]);

const checksumAlgorithms = ["xor8", "sum16", "crc16_ccitt", "crc32", "crc32c"];
const builtinChecksumAnchors = ["frame_start", "frame_end", "before_self", "after_self"];
const topLevelKeywords = ["protocol", "import", "enum", "struct", "message"];
const scalarTypeCompletions = [
  "uint8", "uint16", "uint32", "uint64",
  "int8", "int16", "int32", "int64",
  "float32", "float64",
  "bytes", "string", "ascii", "utf8", "enum<uint8>",
];

class Transport {
  constructor() {
    this.buffer = Buffer.alloc(0);
    process.stdin.on("data", (chunk) => this.onData(chunk));
  }

  onData(chunk) {
    this.buffer = Buffer.concat([this.buffer, chunk]);
    while (true) {
      const separator = this.buffer.indexOf("\r\n\r\n");
      if (separator === -1) {
        return;
      }
      const header = this.buffer.slice(0, separator).toString("utf8");
      const match = header.match(/Content-Length:\s*(\d+)/i);
      if (!match) {
        this.buffer = Buffer.alloc(0);
        return;
      }
      const contentLength = Number(match[1]);
      const bodyStart = separator + 4;
      if (this.buffer.length < bodyStart + contentLength) {
        return;
      }
      const payload = this.buffer.slice(bodyStart, bodyStart + contentLength).toString("utf8");
      this.buffer = this.buffer.slice(bodyStart + contentLength);
      handleMessage(JSON.parse(payload));
    }
  }
}

function writeMessage(message) {
  const payload = Buffer.from(JSON.stringify(message), "utf8");
  process.stdout.write(`Content-Length: ${payload.length}\r\n\r\n`);
  process.stdout.write(payload);
}

function reply(id, result) {
  writeMessage({ jsonrpc: "2.0", id, result });
}

function replyError(id, code, message) {
  writeMessage({
    jsonrpc: "2.0",
    id,
    error: { code, message },
  });
}

function notify(method, params) {
  writeMessage({ jsonrpc: "2.0", method, params });
}

function makeRange(start, end) {
  return {
    start: { line: start.line, character: start.character },
    end: { line: end.line, character: end.character },
  };
}

function makePoint(line, character, offset) {
  return { line, character, offset };
}

function cloneRange(range) {
  return {
    start: { line: range.start.line, character: range.start.character },
    end: { line: range.end.line, character: range.end.character },
  };
}

function comparePosition(a, b) {
  if (a.line !== b.line) {
    return a.line - b.line;
  }
  return a.character - b.character;
}

function positionInRange(position, range) {
  return comparePosition(position, range.start) >= 0 && comparePosition(position, range.end) <= 0;
}

class Lexer {
  constructor(text) {
    this.text = text;
    this.index = 0;
    this.line = 0;
    this.character = 0;
  }

  tokenize() {
    const tokens = [];
    while (true) {
      this.skipTrivia();
      if (this.eof()) {
        tokens.push(this.makeToken("eof", "", this.currentPoint(), this.currentPoint()));
        return tokens;
      }
      const ch = this.peek();
      if (this.isIdentifierStart(ch)) {
        tokens.push(this.readIdentifier());
        continue;
      }
      if (this.isDigit(ch)) {
        tokens.push(this.readNumber());
        continue;
      }
      if (ch === "\"" || ch === "'") {
        tokens.push(this.readString());
        continue;
      }
      if ("{}[]():,@=<>".includes(ch)) {
        tokens.push(this.readPunctuation());
        continue;
      }
      throw this.error("Unexpected character.");
    }
  }

  skipTrivia() {
    while (!this.eof()) {
      const ch = this.peek();
      if (/\s/.test(ch)) {
        this.advance();
        continue;
      }
      if (ch === "#") {
        while (!this.eof() && this.peek() !== "\n") {
          this.advance();
        }
        continue;
      }
      if (ch === "/" && this.peek(1) === "/") {
        this.advance();
        this.advance();
        while (!this.eof() && this.peek() !== "\n") {
          this.advance();
        }
        continue;
      }
      break;
    }
  }

  readIdentifier() {
    const start = this.currentPoint();
    let value = "";
    while (!this.eof() && this.isIdentifierPart(this.peek())) {
      value += this.advance();
    }
    return this.makeToken("identifier", value, start, this.currentPoint());
  }

  readNumber() {
    const start = this.currentPoint();
    let value = "";
    if (this.peek() === "0" && (this.peek(1) === "x" || this.peek(1) === "X")) {
      value += this.advance();
      value += this.advance();
      while (!this.eof() && /[0-9a-fA-F]/.test(this.peek())) {
        value += this.advance();
      }
    } else {
      while (!this.eof() && this.isDigit(this.peek())) {
        value += this.advance();
      }
    }
    return this.makeToken("number", value, start, this.currentPoint());
  }

  readString() {
    const start = this.currentPoint();
    const quote = this.advance();
    let value = "";
    while (!this.eof()) {
      const ch = this.advance();
      if (ch === quote) {
        return this.makeToken("string", value, start, this.currentPoint());
      }
      if (ch === "\\") {
        if (this.eof()) {
          throw this.error("Unterminated escape sequence.");
        }
        const escaped = this.advance();
        value += escaped;
        continue;
      }
      value += ch;
    }
    throw this.error("Unterminated string literal.");
  }

  readPunctuation() {
    const start = this.currentPoint();
    const value = this.advance();
    return this.makeToken("punctuation", value, start, this.currentPoint());
  }

  makeToken(kind, value, start, end) {
    return { kind, value, start, end, range: makeRange(start, end) };
  }

  currentPoint() {
    return makePoint(this.line, this.character, this.index);
  }

  eof() {
    return this.index >= this.text.length;
  }

  peek(lookahead = 0) {
    return this.text[this.index + lookahead] || "";
  }

  advance() {
    const ch = this.text[this.index++];
    if (ch === "\n") {
      this.line += 1;
      this.character = 0;
    } else {
      this.character += 1;
    }
    return ch;
  }

  isIdentifierStart(ch) {
    return /[A-Za-z_$]/.test(ch);
  }

  isIdentifierPart(ch) {
    return /[A-Za-z0-9_.-]/.test(ch);
  }

  isDigit(ch) {
    return /[0-9]/.test(ch);
  }

  error(message) {
    return new ParseError(message, makeRange(this.currentPoint(), this.currentPoint()));
  }
}

class ParseError extends Error {
  constructor(message, range) {
    super(message);
    this.range = range;
  }
}

class Parser {
  constructor(text, tokens) {
    this.text = text;
    this.tokens = tokens;
    this.index = 0;
  }

  parse() {
    const document = {
      protocol: null,
      imports: [],
      enums: [],
      structs: [],
      messages: [],
      diagnostics: [],
    };

    while (!this.is("eof")) {
      if (this.matchIdentifier("protocol")) {
        const name = this.expectName("Expected protocol name.");
        document.protocol = {
          name: name.value,
          nameRange: name.range,
        };
        continue;
      }
      if (this.matchIdentifier("import")) {
        const path = this.expectName("Expected import path.");
        document.imports.push({
          path: path.value,
          pathRange: path.range,
        });
        continue;
      }
      if (this.matchIdentifier("enum")) {
        document.enums.push(this.parseEnumDecl());
        continue;
      }
      if (this.matchIdentifier("struct")) {
        document.structs.push(this.parseLayout("struct"));
        continue;
      }
      if (this.matchIdentifier("message")) {
        document.messages.push(this.parseLayout("message"));
        continue;
      }
      throw new ParseError("Expected 'protocol', 'import', 'enum', 'struct', or 'message'.", this.peek().range);
    }
    return document;
  }

  parseEnumDecl() {
    const name = this.expectName("Expected enum name.");
    this.expectPunctuation(":", "Expected ':' after enum name.");
    const underlying = this.expectIdentifier("Expected enum underlying type.");
    this.expectPunctuation("{", "Expected '{' after enum declaration.");
    const values = this.parseEnumValues();
    return {
      kind: "enum",
      name: name.value,
      nameRange: name.range,
      underlying: underlying.value,
      underlyingRange: underlying.range,
      values,
    };
  }

  parseLayout(kind) {
    const name = this.expectName(`Expected ${kind} name.`);
    const layout = {
      kind,
      name: name.value,
      nameRange: name.range,
      fields: [],
      fieldMap: new Map(),
    };
    if (kind === "message" && this.matchIdentifier("allow_trailing_bytes")) {
      layout.allowTrailingBytes = true;
    }
    this.expectPunctuation("{", `Expected '{' after ${kind} name.`);
    while (!this.matchPunctuation("}")) {
      if (kind === "message" && this.matchIdentifier("allow_trailing_bytes")) {
        layout.allowTrailingBytes = true;
        this.matchPunctuation(",");
        continue;
      }
      const field = this.parseField(layout);
      layout.fields.push(field);
      this.matchPunctuation(",");
    }
    return layout;
  }

  parseField(layout) {
    const name = this.expectName("Expected field name.");
    this.expectPunctuation(":", "Expected ':' after field name.");
    const typeToken = this.expectToken((token) => token.kind === "identifier", "Expected field type.");
    const field = {
      name: name.value,
      nameRange: name.range,
      typeToken: typeToken.value,
      typeRange: typeToken.range,
      typeKind: builtinTypes.has(typeToken.value) ? "builtin" : "named",
      inlineEnum: null,
      sizeRef: null,
      sizeRefRange: null,
      checksum: null,
      bitfieldsRange: null,
      layout,
    };

    if (["bytes", "string", "ascii", "utf8"].includes(typeToken.value) && this.matchPunctuation("[")) {
      const sizeToken = this.expectToken(
          (token) => token.kind === "number" || token.kind === "identifier" || token.kind === "string",
          "Expected field size or field reference.",
      );
      if (sizeToken.kind !== "number") {
        field.sizeRef = sizeToken.value;
        field.sizeRefRange = sizeToken.range;
      }
      this.expectPunctuation("]", "Expected ']' after field size.");
    } else if (typeToken.value === "enum" && this.matchPunctuation("<")) {
      field.typeKind = "inlineEnum";
      const underlying = this.expectIdentifier("Expected enum underlying type.");
      field.inlineEnum = {
        underlying: underlying.value,
        underlyingRange: underlying.range,
        values: [],
      };
      this.expectPunctuation(">", "Expected '>' after enum underlying type.");
    }

    if (this.matchPunctuation("=")) {
      field.expectToken = this.expectToken((token) => token.kind === "number", "Expected numeric value.");
    }
    if (this.isIdentifierAhead("checksum") && this.isPunctuationAhead(1, "(")) {
      this.index += 1;
      field.checksum = this.parseChecksum();
    }
    if (this.matchPunctuation("{")) {
      if (field.typeKind === "inlineEnum") {
        field.inlineEnum.values = this.parseEnumValues();
      } else {
        field.bitfieldsRange = this.skipBraceBody();
      }
    }

    layout.fieldMap.set(field.name, field);
    return field;
  }

  parseChecksum() {
    const checksum = {
      algorithm: null,
      algorithmRange: null,
      from: null,
      fromRange: null,
      to: null,
      toRange: null,
    };
    this.expectPunctuation("(", "Expected '(' after checksum.");
    const algorithm = this.expectIdentifier("Expected checksum algorithm.");
    checksum.algorithm = algorithm.value;
    checksum.algorithmRange = algorithm.range;
    if (this.matchPunctuation(",")) {
      const from = this.expectName("Expected checksum anchor.");
      checksum.from = from.value;
      checksum.fromRange = from.range;
      if (this.matchPunctuation(",")) {
        const to = this.expectName("Expected checksum anchor.");
        checksum.to = to.value;
        checksum.toRange = to.range;
      }
    }
    this.expectPunctuation(")", "Expected ')' after checksum.");
    return checksum;
  }

  parseEnumValues() {
    const values = [];
    while (!this.matchPunctuation("}")) {
      const numeric = this.expectToken((token) => token.kind === "number", "Expected enum value.");
      this.expectPunctuation("=", "Expected '=' in enum declaration.");
      const label = this.expectName("Expected enum label.");
      values.push({
        value: numeric.value,
        valueRange: numeric.range,
        label: label.value,
        labelRange: label.range,
      });
      this.matchPunctuation(",");
    }
    return values;
  }

  skipBraceBody() {
    const start = cloneRange(this.previous().range).start;
    let depth = 1;
    while (!this.is("eof")) {
      const token = this.advance();
      if (token.kind === "punctuation" && token.value === "{") {
        depth += 1;
      } else if (token.kind === "punctuation" && token.value === "}") {
        depth -= 1;
        if (depth === 0) {
          return makeRange(start, token.end);
        }
      }
    }
    throw new ParseError("Unterminated '{' block.", makeRange(start, start));
  }

  expectName(message) {
    return this.expectToken((token) => token.kind === "identifier" || token.kind === "string", message);
  }

  expectIdentifier(message) {
    return this.expectToken((token) => token.kind === "identifier", message);
  }

  expectPunctuation(value, message) {
    const token = this.expectToken((token) => token.kind === "punctuation" && token.value === value, message);
    return token;
  }

  expectToken(predicate, message) {
    const token = this.peek();
    if (!predicate(token)) {
      throw new ParseError(message, token.range);
    }
    this.index += 1;
    return token;
  }

  matchIdentifier(value) {
    if (this.is("identifier", value)) {
      this.index += 1;
      return true;
    }
    return false;
  }

  matchPunctuation(value) {
    if (this.is("punctuation", value)) {
      this.index += 1;
      return true;
    }
    return false;
  }

  isIdentifierAhead(value) {
    return this.is("identifier", value);
  }

  isPunctuationAhead(offset, value) {
    const token = this.tokens[this.index + offset];
    return token && token.kind === "punctuation" && token.value === value;
  }

  is(kind, value = null) {
    const token = this.peek();
    if (token.kind !== kind) {
      return false;
    }
    return value === null ? true : token.value === value;
  }

  peek() {
    return this.tokens[this.index];
  }

  previous() {
    return this.tokens[this.index - 1];
  }

  advance() {
    const token = this.tokens[this.index];
    this.index += 1;
    return token;
  }
}

function analyze(text) {
  try {
    const tokens = new Lexer(text).tokenize();
    const document = new Parser(text, tokens).parse();
    document.diagnostics = [];
    return document;
  } catch (error) {
    if (error instanceof ParseError) {
      return {
        protocol: null,
        imports: [],
        enums: [],
        structs: [],
        messages: [],
        diagnostics: [{
          range: error.range,
          severity: DiagnosticSeverity.Error,
          message: error.message,
          source: "upr",
        }],
      };
    }
    return {
      protocol: null,
      imports: [],
      enums: [],
      structs: [],
      messages: [],
      diagnostics: [{
        range: makeRange(makePoint(0, 0, 0), makePoint(0, 1, 1)),
        severity: DiagnosticSeverity.Error,
        message: String(error.message || error),
        source: "upr",
      }],
    };
  }
}

function validateDocument(document, symbols = null) {
  const diagnostics = [];
  const topLevelSymbols = new Map();
  const recordTopLevel = (kind, item) => {
    const existing = topLevelSymbols.get(item.name);
    if (existing) {
      diagnostics.push(diagnostic(item.nameRange, `Duplicate ${kind} name '${item.name}'.`));
      return;
    }
    topLevelSymbols.set(item.name, { kind, item });
  };

  if (!document.protocol && document.messages.length > 0) {
    diagnostics.push(diagnostic(makeRange(makePoint(0, 0, 0), makePoint(0, 1, 1)), "Missing protocol declaration."));
  }

  for (const enumDecl of document.enums) {
    recordTopLevel("enum", enumDecl);
  }
  for (const structDecl of document.structs) {
    recordTopLevel("struct", structDecl);
  }
  for (const messageDecl of document.messages) {
    recordTopLevel("message", messageDecl);
  }

  const enumMap = symbols?.enums || new Map(document.enums.map((item) => [item.name, { kind: "enum", item }]));
  const structMap = symbols?.structs || new Map(document.structs.map((item) => [item.name, { kind: "struct", item }]));

  for (const layout of [...document.structs, ...document.messages]) {
    const seenFields = new Map();
    for (const field of layout.fields) {
      if (seenFields.has(field.name)) {
        diagnostics.push(diagnostic(field.nameRange, `Duplicate field name '${field.name}'.`));
      } else {
        seenFields.set(field.name, field);
      }
      if (field.typeKind === "named" && !enumMap.has(field.typeToken) && !structMap.has(field.typeToken)) {
        diagnostics.push(diagnostic(field.typeRange, `Unknown type '${field.typeToken}'.`));
      }
      if (field.sizeRef && !hasPriorField(layout, field, field.sizeRef)) {
        diagnostics.push(diagnostic(field.sizeRefRange, `Unknown prior field '${field.sizeRef}'.`));
      }
      if (field.checksum) {
        if (!checksumAlgorithms.includes(field.checksum.algorithm)) {
          diagnostics.push(diagnostic(field.checksum.algorithmRange, `Unknown checksum algorithm '${field.checksum.algorithm}'.`));
        }
        if (field.checksum.fromRange) {
          validateAnchor(layout, field.checksum.from, field.checksum.fromRange, diagnostics);
        }
        if (field.checksum.toRange) {
          validateAnchor(layout, field.checksum.to, field.checksum.toRange, diagnostics);
        }
      }
    }
  }

  return diagnostics;
}

function hasPriorField(layout, field, name) {
  for (const candidate of layout.fields) {
    if (candidate === field) {
      return false;
    }
    if (candidate.name === name) {
      return true;
    }
  }
  return false;
}

function validateAnchor(layout, anchor, range, diagnostics) {
  if (builtinChecksumAnchors.includes(anchor)) {
    return;
  }
  const parts = anchor.split(".");
  if (parts.length === 2 && (parts[1] === "start" || parts[1] === "end")) {
    const field = layout.fieldMap.get(parts[0]);
    if (!field) {
      diagnostics.push(diagnostic(range, `Unknown checksum anchor field '${parts[0]}'.`));
    }
    return;
  }
  diagnostics.push(diagnostic(range, `Unsupported checksum anchor '${anchor}'.`));
}

function diagnostic(range, message) {
  return {
    range,
    severity: DiagnosticSeverity.Error,
    message,
    source: "upr",
  };
}

function isFileUri(uri) {
  return typeof uri === "string" && uri.startsWith("file://");
}

function uriToFsPath(uri) {
  if (!isFileUri(uri)) {
    return null;
  }
  try {
    return fileURLToPath(uri);
  } catch {
    return null;
  }
}

function fsPathToUri(fsPath) {
  return pathToFileURL(fsPath).toString();
}

function findWorkspaceRoot(startPath) {
  let current = path.dirname(startPath);
  while (true) {
    if (fs.existsSync(path.join(current, "MODULE.bazel")) ||
        fs.existsSync(path.join(current, "WORKSPACE.bazel")) ||
        fs.existsSync(path.join(current, "WORKSPACE"))) {
      return current;
    }
    const parent = path.dirname(current);
    if (parent === current) {
      return null;
    }
    current = parent;
  }
}

function isRelativeImportPath(importPath) {
  return importPath.startsWith("./") || importPath.startsWith("../");
}

function resolveImportUri(importerUri, importPath) {
  const importerPath = uriToFsPath(importerUri);
  if (!importerPath) {
    return null;
  }

  let candidatePath = null;
  if (path.isAbsolute(importPath)) {
    candidatePath = importPath;
  } else if (isRelativeImportPath(importPath)) {
    candidatePath = path.resolve(path.dirname(importerPath), importPath);
  } else {
    const workspaceRoot = findWorkspaceRoot(importerPath);
    if (!workspaceRoot) {
      return null;
    }
    candidatePath = path.resolve(workspaceRoot, importPath);
  }

  try {
    return fsPathToUri(fs.realpathSync.native(candidatePath));
  } catch {
    return null;
  }
}

function loadAnalysisForUri(uri) {
  const openDocumentState = documents.get(uri);
  if (openDocumentState) {
    return openDocumentState.analysis;
  }
  const fsPath = uriToFsPath(uri);
  if (!fsPath) {
    return null;
  }
  try {
    return analyze(fs.readFileSync(fsPath, "utf8"));
  } catch {
    return null;
  }
}

function indexSymbols(document, uri, symbolIndex) {
  for (const enumDecl of document.enums) {
    if (!symbolIndex.enums.has(enumDecl.name)) {
      symbolIndex.enums.set(enumDecl.name, { kind: "enum", item: enumDecl, uri });
    }
  }
  for (const structDecl of document.structs) {
    if (!symbolIndex.structs.has(structDecl.name)) {
      symbolIndex.structs.set(structDecl.name, { kind: "struct", item: structDecl, uri });
    }
  }
}

function resolveDocumentContext(uri) {
  const rootDocument = documents.get(uri);
  if (!rootDocument) {
    return null;
  }

  const symbolIndex = {
    enums: new Map(),
    structs: new Map(),
  };
  const importDiagnostics = [];
  const visited = new Set();

  const visit = (documentUri, importer = null, importDecl = null) => {
    if (visited.has(documentUri)) {
      return;
    }
    visited.add(documentUri);

    const analysis = loadAnalysisForUri(documentUri);
    if (!analysis) {
      if (importer && importDecl) {
        importDiagnostics.push(diagnostic(importDecl.pathRange, `Unable to resolve import '${importDecl.path}'.`));
      }
      return;
    }

    if (documentUri === uri) {
      indexSymbols(analysis, documentUri, symbolIndex);
    }

    for (const nestedImport of analysis.imports) {
      const importedUri = resolveImportUri(documentUri, nestedImport.path);
      if (!importedUri) {
        if (documentUri === uri) {
          importDiagnostics.push(diagnostic(nestedImport.pathRange, `Unable to resolve import '${nestedImport.path}'.`));
        }
        continue;
      }

      const importedAnalysis = loadAnalysisForUri(importedUri);
      if (!importedAnalysis) {
        if (documentUri === uri) {
          importDiagnostics.push(diagnostic(nestedImport.pathRange, `Unable to read import '${nestedImport.path}'.`));
        }
        continue;
      }

      if (importedAnalysis.diagnostics.length > 0) {
        if (documentUri === uri) {
          importDiagnostics.push(diagnostic(nestedImport.pathRange, `Imported schema '${nestedImport.path}' has parse errors.`));
        }
        continue;
      }

      indexSymbols(importedAnalysis, importedUri, symbolIndex);
      visit(importedUri, documentUri, nestedImport);
    }
  };

  visit(uri);

  return {
    uri,
    document: rootDocument,
    symbols: symbolIndex,
    diagnostics: rootDocument.analysis.diagnostics.length > 0
      ? rootDocument.analysis.diagnostics
      : [...validateDocument(rootDocument.analysis, symbolIndex), ...importDiagnostics],
  };
}

function getLinePrefix(text, position) {
  const lines = text.split(/\r?\n/);
  const line = lines[position.line] || "";
  return line.slice(0, position.character);
}

function findLayoutByPosition(document, position) {
  for (const layout of [...document.structs, ...document.messages]) {
    for (const field of layout.fields) {
      if (positionInRange(position, field.nameRange) || positionInRange(position, field.typeRange) ||
          (field.sizeRefRange && positionInRange(position, field.sizeRefRange)) ||
          (field.checksum?.algorithmRange && positionInRange(position, field.checksum.algorithmRange)) ||
          (field.checksum?.fromRange && positionInRange(position, field.checksum.fromRange)) ||
          (field.checksum?.toRange && positionInRange(position, field.checksum.toRange))) {
        return { layout, field };
      }
    }
  }
  return null;
}

function findSymbolAt(document, position, symbols = null) {
  const context = findLayoutByPosition(document, position);
  if (!context) {
    for (const enumDecl of document.enums) {
      if (positionInRange(position, enumDecl.nameRange)) {
        return { kind: "enum", item: enumDecl, uri: null };
      }
    }
    for (const structDecl of document.structs) {
      if (positionInRange(position, structDecl.nameRange)) {
        return { kind: "struct", item: structDecl, uri: null };
      }
    }
    return null;
  }

  const { layout, field } = context;
  if (positionInRange(position, field.typeRange) && field.typeKind === "named") {
    const enumEntry = symbols?.enums?.get(field.typeToken);
    if (enumEntry) {
      return enumEntry;
    }
    const structEntry = symbols?.structs?.get(field.typeToken);
    if (structEntry) {
      return structEntry;
    }
  }
  if (field.sizeRefRange && positionInRange(position, field.sizeRefRange)) {
    return { kind: "field", item: layout.fieldMap.get(field.sizeRef), uri: null };
  }
  if (field.checksum?.fromRange && positionInRange(position, field.checksum.fromRange)) {
    const target = anchorField(layout, field.checksum.from);
    return target ? { kind: "field", item: target, uri: null } : null;
  }
  if (field.checksum?.toRange && positionInRange(position, field.checksum.toRange)) {
    const target = anchorField(layout, field.checksum.to);
    return target ? { kind: "field", item: target, uri: null } : null;
  }
  return null;
}

function anchorField(layout, anchor) {
  const parts = anchor.split(".");
  if (parts.length === 2 && (parts[1] === "start" || parts[1] === "end")) {
    return layout.fieldMap.get(parts[0]) || null;
  }
  return null;
}

function makeLocation(uri, range) {
  return { uri, range };
}

function makeCompletion(label, kind, detail, documentation = undefined) {
  return { label, kind, detail, documentation };
}

function completionItems(document, position, text, symbols) {
  const prefix = getLinePrefix(text, position);
  const context = findLayoutByPosition(document, position);

  const checksumMatch = prefix.match(/checksum\s*\(\s*([^)]*)$/);
  if (checksumMatch && context) {
    const commaCount = checksumMatch[1].split(",").length - 1;
    if (commaCount <= 0) {
      return checksumAlgorithms.map((algorithm) =>
        makeCompletion(algorithm, CompletionItemKind.Function, "Checksum algorithm"));
    }
    return checksumAnchorCompletions(context.layout);
  }

  const sizeMatch = prefix.match(/\[\s*([A-Za-z0-9_.-]*)$/);
  if (sizeMatch && context) {
    return priorFieldCompletions(context.layout, context.field);
  }

  const typeMatch = prefix.match(/:\s*([A-Za-z0-9_.-]*)$/);
  if (typeMatch) {
    const enumItems = [...(symbols?.enums?.values() || [])]
        .map((entry) => entry.item.name)
        .sort()
        .map((name) => makeCompletion(name, CompletionItemKind.Enum, "Named enum"));
    const structItems = [...(symbols?.structs?.values() || [])]
        .map((entry) => entry.item.name)
        .sort()
        .map((name) => makeCompletion(name, CompletionItemKind.Struct, "Struct type"));
    return [
      ...scalarTypeCompletions.map((item) => makeCompletion(item, CompletionItemKind.Keyword, "Builtin type")),
      ...enumItems,
      ...structItems,
    ];
  }

  return topLevelKeywords.map((keyword) => makeCompletion(keyword, CompletionItemKind.Keyword, "UPR keyword"));
}

function priorFieldCompletions(layout, field) {
  const items = [];
  for (const candidate of layout.fields) {
    if (candidate === field) {
      break;
    }
    items.push(makeCompletion(candidate.name, CompletionItemKind.Field, "Prior field"));
  }
  return items;
}

function checksumAnchorCompletions(layout) {
  const items = builtinChecksumAnchors.map((anchor) =>
    makeCompletion(anchor, CompletionItemKind.Constant, "Builtin checksum anchor"));
  for (const field of layout.fields) {
    items.push(makeCompletion(`${field.name}.start`, CompletionItemKind.Field, "Field start anchor"));
    items.push(makeCompletion(`${field.name}.end`, CompletionItemKind.Field, "Field end anchor"));
  }
  return items;
}

function hoverFor(document, position, symbols = null) {
  const symbol = findSymbolAt(document, position, symbols);
  if (symbol?.kind === "enum") {
    return {
      contents: [{
        kind: "markdown",
        value: `**enum ${symbol.item.name}**\n\n${symbol.item.values.map((item) => `- \`${item.value}\` = ${item.label}`).join("\n")}`,
      }],
      range: symbol.item.nameRange,
    };
  }
  if (symbol?.kind === "struct") {
    return {
      contents: [{
        kind: "markdown",
        value: `**struct ${symbol.item.name}**\n\nFields: ${symbol.item.fields.length}`,
      }],
      range: symbol.item.nameRange,
    };
  }
  if (symbol?.kind === "field") {
    return {
      contents: [{
        kind: "markdown",
        value: `**field ${symbol.item.name}**\n\nType: \`${symbol.item.typeToken}\``,
      }],
      range: symbol.item.nameRange,
    };
  }
  const context = findLayoutByPosition(document, position);
  if (context?.field?.checksum?.algorithmRange && positionInRange(position, context.field.checksum.algorithmRange)) {
    return {
      contents: [{
        kind: "markdown",
        value: `**checksum ${context.field.checksum.algorithm}**`,
      }],
      range: context.field.checksum.algorithmRange,
    };
  }
  return null;
}

const documents = new Map();

function openDocument(uri, text) {
  documents.set(uri, {
    uri,
    text,
    analysis: analyze(text),
  });
  publishAllDiagnostics();
}

function publishDiagnostics(uri) {
  const document = resolveDocumentContext(uri);
  notify("textDocument/publishDiagnostics", {
    uri,
    diagnostics: document ? document.diagnostics : [],
  });
}

function publishAllDiagnostics() {
  for (const uri of documents.keys()) {
    publishDiagnostics(uri);
  }
}

function handleMessage(message) {
  try {
    if (message.method === "initialize") {
      reply(message.id, {
        capabilities: {
          textDocumentSync: 1,
          definitionProvider: true,
          hoverProvider: true,
          completionProvider: {
            triggerCharacters: [":", "[", "(", ",", "."],
          },
        },
      });
      return;
    }
    if (message.method === "initialized") {
      return;
    }
    if (message.method === "shutdown") {
      reply(message.id, null);
      return;
    }
    if (message.method === "exit") {
      process.exit(0);
    }
    if (message.method === "textDocument/didOpen") {
      openDocument(message.params.textDocument.uri, message.params.textDocument.text);
      return;
    }
    if (message.method === "textDocument/didChange") {
      openDocument(message.params.textDocument.uri, message.params.contentChanges[0].text);
      return;
    }
    if (message.method === "textDocument/didClose") {
      documents.delete(message.params.textDocument.uri);
      notify("textDocument/publishDiagnostics", {
        uri: message.params.textDocument.uri,
        diagnostics: [],
      });
      publishAllDiagnostics();
      return;
    }
    if (message.method === "textDocument/completion") {
      const document = resolveDocumentContext(message.params.textDocument.uri);
      if (!document) {
        reply(message.id, []);
        return;
      }
      reply(message.id, completionItems(document.document.analysis, message.params.position, document.document.text, document.symbols));
      return;
    }
    if (message.method === "textDocument/definition") {
      const document = resolveDocumentContext(message.params.textDocument.uri);
      if (!document) {
        reply(message.id, null);
        return;
      }
      const symbol = findSymbolAt(document.document.analysis, message.params.position, document.symbols);
      if (!symbol) {
        reply(message.id, null);
        return;
      }
      reply(message.id, makeLocation(symbol.uri || document.uri, symbol.item.nameRange));
      return;
    }
    if (message.method === "textDocument/hover") {
      const document = resolveDocumentContext(message.params.textDocument.uri);
      reply(message.id, document ? hoverFor(document.document.analysis, message.params.position, document.symbols) : null);
      return;
    }
    if (Object.prototype.hasOwnProperty.call(message, "id")) {
      reply(message.id, null);
    }
  } catch (error) {
    if (Object.prototype.hasOwnProperty.call(message, "id")) {
      replyError(message.id, -32603, error.message || String(error));
    }
  }
}

new Transport();
