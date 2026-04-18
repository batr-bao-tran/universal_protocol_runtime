# UPR Schema VS Code Support

This folder contains the VS Code support package for `.upr` files.

It provides:

- syntax highlighting
- comment support
- bracket pairing
- basic authoring snippets
- a built-in language server for diagnostics, go-to-definition, hover, and completion

## Activate

For normal use, install the extension and VS Code will enable it automatically.

One-time install flow:

1. Package the extension from `tools/vscode/upr-schema`: run `npx @vscode/vsce package`
2. Install the resulting `.vsix` in VS Code
3. Reopen `universal_protocol_runtime`

If you want to run only the language server process for debugging, use:

```bash
node server/upr_language_server.js
```

## What The Language Server Understands

- named enums
- named structs
- field references used in variable-length fields
- checksum algorithms and checksum anchors
