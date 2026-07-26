# Zap LSP 0.1.0

`zap-lsp` provides diagnostics, completion, definition, hover, signature help,
UTF-16 positions, document synchronization, and request cancellation over
standard LSP stdio transport.

## Build the server

From the repository root:

```bash
meson setup build -Dinclude_lsp=true
meson compile -C build zap-lsp
```

The binary is written to `build/zap-lsp`.

## Configure a workspace

The server reads an optional `zaplsp.json` from the workspace. Paths may be
absolute or relative to `zapRoot`:

```json
{
  "zapRoot": "/opt/zap",
  "corePath": "core",
  "stdlibPath": "std"
}
```

When `zapRoot` is omitted, relative paths are resolved from the directory that
contains `zaplsp.json`. The server uses the installed Zap `core` and `std`; it
does not require a private copy for each editor.

## Build and install the VS Code extension

Build `zap-lsp` first, then run:

```bash
cd src/lsp/vscode/zap
npm install
npm run package
```

Install the generated `.vsix` with **Extensions → … → Install from VSIX…**.
The package bundles the server binary, but not `zapc`, `core`, or `std`.

If a workspace has no `zaplsp.json`, the extension offers to create one from a
detected Zap installation. Use `zap-lsp.path` only to override the bundled
server, and `zap-lsp.zapcPath` when `zapc` is not available in the workspace or
`PATH`.
