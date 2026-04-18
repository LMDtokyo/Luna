<p align="center">
  <img src="https://raw.githubusercontent.com/LMDtokyo/Luna/main/editors/vscode/images/banner.jpg" width="420" alt="Luna programming language">
</p>

# Luna for Visual Studio Code

Syntax highlighting, file icons, snippets and an optional language server
for the [Luna](https://github.com/LMDtokyo/Luna) programming language.

## Features

- **Syntax highlighting** for Luna keywords, types, operators and literals
  (hex `0x`, binary `0b`, octal `0o`, char, raw strings).
- **File icons** — every `.luna` file gets the project logo in the editor
  tab and, when the Luna icon theme is enabled, in the Explorer.
- **Language configuration** — line comments (`#`), bracket matching,
  auto-closing pairs, off-side folding.
- **Snippets** for common constructs: `fn`, `struct`, `if`, `orbit`,
  `eclipse`, `guard`, `impl`, `spawn`.
- **Commands** for running, building and checking the current file —
  enabled when the `luna` toolchain is installed and on `PATH`.
- **Language server client** — if a `luna` binary is available and
  supports `luna lsp`, the extension talks to it over stdio for
  diagnostics, completion, hover and go-to-definition.

## Requirements

- Visual Studio Code 1.74 or newer.
- Optional: the Luna toolchain on `PATH`, needed only for the Run /
  Build / Check commands and the language server. Plain syntax
  highlighting and icons work without it.

## Installation

Install from the Marketplace, or from a `.vsix`:

```sh
code --install-extension luna-language-0.1.3.vsix
```

To see the Luna logo on `.luna` files in the Explorer:
**Command Palette → Preferences: File Icon Theme → Luna Icons**.

## Commands

| Command | Default keybinding | Notes |
|---|---|---|
| Luna: Run Current File | `Ctrl+Shift+R` | `luna <file>` in a terminal |
| Luna: Build Current File | `Ctrl+Shift+B` | `luna build <file>` |
| Luna: Check Current File | `Ctrl+Shift+C` | `luna check <file>` |
| Luna: Format Current File | — | Uses LSP formatting if available |
| Luna: Restart Language Server | — | |
| Luna: Show Output | — | |

## Settings

| Setting | Default | Description |
|---|---|---|
| `luna.path` | `"luna"` | Path to the Luna executable |
| `luna.lsp.enabled` | `true` | Start the language server on activation |
| `luna.lsp.trace` | `false` | Log LSP traffic to the Output panel |
| `luna.formatOnSave` | `true` | Format on save when LSP is running |
| `luna.checkOnSave` | `true` | Type-check on save when LSP is running |

When no `luna` binary is found, the extension still provides
highlighting, icons and snippets — it just skips starting the server
silently.

## Example

```luna
fn main()
    @greeting = "Hello, Luna!"
    shine(@greeting)

    orbit @i in 1..4
        shine("iteration: " + @i.show())
```

## Contributing

- Issues: <https://github.com/LMDtokyo/Luna/issues>
- Source: <https://github.com/LMDtokyo/Luna/tree/main/editors/vscode>

## License

GPL-3.0-only. See [LICENSE](https://github.com/LMDtokyo/Luna/blob/main/LICENSE).
