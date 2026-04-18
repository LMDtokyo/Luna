# Changelog

All notable changes to the Luna extension for Visual Studio Code.

## 0.1.0

Initial public release.

- Syntax highlighting (TextMate grammar) for Luna keywords, types,
  operators and literals.
- File icon theme — raven logo for `.luna` files.
- Language configuration: line comments, brackets, off-side folding.
- Snippets for common constructs (`fn`, `struct`, `if`, `orbit`,
  `eclipse`, `guard`, `impl`, `spawn`).
- Commands: run / build / check / format the current file via the Luna
  toolchain, when installed.
- Language server client — talks to `luna lsp` over stdio when the
  binary is available, silently disabled otherwise.
