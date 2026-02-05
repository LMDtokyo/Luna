<p align="center">
  <img src="icons/luna.png" alt="Luna Logo" width="128" height="128">
</p>

<h1 align="center">Luna Language for VS Code</h1>

<p align="center">
  <strong>v1.7.0 — Pure Luna Engine</strong>
</p>

<p align="center">
  <a href="https://marketplace.visualstudio.com/items?itemName=luna-team.luna-language">
    <img src="https://img.shields.io/visual-studio-marketplace/v/luna-team.luna-language?style=flat-square&color=blue" alt="Version">
  </a>
  <a href="https://marketplace.visualstudio.com/items?itemName=luna-team.luna-language">
    <img src="https://img.shields.io/visual-studio-marketplace/d/luna-team.luna-language?style=flat-square&color=green" alt="Downloads">
  </a>
  <a href="https://github.com/luna-lang/luna/blob/main/LICENSE">
    <img src="https://img.shields.io/badge/license-MIT-orange?style=flat-square" alt="License">
  </a>
  <a href="https://github.com/luna-lang/luna">
    <img src="https://img.shields.io/github/stars/luna-lang/luna?style=flat-square&color=yellow" alt="Stars">
  </a>
</p>

<p align="center">
  <a href="#features">Features</a> •
  <a href="#installation">Installation</a> •
  <a href="#quick-start">Quick Start</a> •
  <a href="#commands">Commands</a> •
  <a href="#settings">Settings</a>
</p>

---

## 🚀 What's New in v1.7.0

### Pure Luna Engine

The Language Server is now **100% implemented in Luna** (`lsp.luna`) — no Rust FFI dependencies!

- ✅ Native JSON-RPC 2.0 over stdio
- ✅ Borrow checker diagnostics from `borrow_checker.luna`
- ✅ IntelliSense for all 31 stdlib modules
- ✅ Cosmic keywords: `shine`, `eclipse`, `orbit`, `phase`, `spawn`, `seal`, `nova`

---

## Features

### 🌙 Pure Luna LSP

Full Language Server Protocol integration powered by native Luna:

| Feature | Description |
|---------|-------------|
| **IntelliSense** | Smart completion for 31 stdlib modules |
| **Hover Information** | Type info and documentation |
| **Go to Definition** | Jump to symbol definitions |
| **Find References** | Find all usages of a symbol |
| **Real-time Diagnostics** | Borrow checker errors in Problems panel |
| **Code Formatting** | LSP-powered formatting |

### ✨ Syntax Highlighting

Beautiful semantic highlighting for Luna's cosmic syntax:

- **Cosmic Keywords**: `shine`, `eclipse`, `orbit`, `phase`, `spawn`, `seal`, `nova`, `guard`, `meow`
- **Variables**: `@prefix` variable highlighting
- **Types**: All primitives (`int`, `float`, `str`, `bool`, `u8`-`u64`, `f32`, `f64`)
- **Operators**: `|>` pipe, `?.` safe nav, `??` null coalescing, `->` arrow
- **Literals**: Hex (`0x`), binary (`0b`), octal (`0o`), raw strings (`r"..."`)

### 📦 Snippets

Built-in templates for Luna patterns:

- `fn` — Function declaration
- `orbit` — For loop
- `eclipse` — Pattern matching
- `struct` — Struct definition
- `spawn` — Async task
- `guard` — Guard statement
- `impl` — Implementation block

---

## Installation

### Requirements

- **Luna v4.2** or newer installed
- Run `luna --install-system` for automatic PATH setup
- VS Code 1.74.0 or newer

### From VS Code Marketplace

1. Open VS Code
2. Go to Extensions (`Ctrl+Shift+X` / `Cmd+Shift+X`)
3. Search for "Luna Language"
4. Click **Install**

### From VSIX File

```bash
code --install-extension luna-language-1.7.0.vsix
```

### From Source

```bash
cd Luna_Core/vscode-extension
npm install
npm run compile
npm run package
code --install-extension luna-language-1.7.0.vsix
```

---

## Quick Start

### Hello World

Create `hello.luna`:

```luna
# Your first Luna program
@greeting = "Hello, Luna!"
shine(@greeting)

orbit @i in 1..4
    shine("Iteration: " + str(@i))
```

Run with `Ctrl+Shift+R` (or `Cmd+Shift+R` on macOS).

### Example: HTTP Server

```luna
import http
import json

fn handle_request(@req: Request) -> Response
    eclipse @req.path
        phase "/" then
            http.response_json({message: "Hello from Luna v4.2!"})
        phase "/users" then
            @users = db.scan_prefix("user:")
            http.response_json(@users)
        phase _ then
            http.response_text("Not Found", 404)

fn main()
    http.serve(8080, handle_request)
    shine("Server running at http://localhost:8080")
```

### Example: Concurrent Processing

```luna
import sync

fn main()
    @tx, @rx = sync.channel<int>()

    # Producer
    spawn fn() ->
        orbit @i in 1..101
            sync.send(@tx, @i)
        sync.close(@tx)

    # Consumer
    @mut @sum = 0
    orbit @n in @rx
        @sum += @n

    shine("Sum: " + str(@sum))  # 5050
```

---

## Commands

| Command | Shortcut | Description |
|---------|----------|-------------|
| **Luna: Run Current File** | `Ctrl+Shift+R` | Execute with JIT |
| **Luna: Build Current File** | `Ctrl+Shift+B` | Compile to native binary |
| **Luna: Check Current File** | `Ctrl+Shift+C` | Type check only |
| **Luna: Restart Language Server** | — | Restart LSP |
| **Luna: Show Output Channel** | — | View logs |
| **Luna: Format Current File** | — | Format code |

---

## Settings

| Setting | Default | Description |
|---------|---------|-------------|
| `luna.path` | `"luna"` | Path to Luna executable |
| `luna.lsp.enabled` | `true` | Enable Language Server |
| `luna.lsp.trace` | `false` | Enable LSP message tracing |
| `luna.formatOnSave` | `true` | Auto-format on save |
| `luna.checkOnSave` | `true` | Type check on save |

### Custom Luna Path

```json
{
    "luna.path": "C:/Users/you/.luna/bin/luna.exe"
}
```

Or on macOS/Linux:

```json
{
    "luna.path": "/home/you/.luna/bin/luna"
}
```

---

## Cosmic Keywords Reference

| Keyword | Meaning | Example |
|---------|---------|---------|
| `shine` | Print to stdout | `shine("Hello!")` |
| `orbit` | For loop iterator | `orbit @i in 1..10` |
| `eclipse` | Pattern match (switch) | `eclipse @value` |
| `phase` | Match arm (case) | `phase 42 then "answer"` |
| `nova` | Panic/assert | `nova("Error!")` |
| `guard` | Early return guard | `guard @x isnt none else return` |
| `seal` | Finalize value | `seal @config` |
| `spawn` | Create async task | `spawn fn() -> ...` |
| `meow` | Static global array | `meow @buffer: [u8; 1024]` |

---

## 31 Stdlib Modules

All modules are 100% pure Luna:

| Category | Modules |
|----------|---------|
| **Core** | `core`, `io`, `string`, `math`, `time`, `fs` |
| **Data** | `json`, `regex`, `base64`, `uuid` |
| **Collections** | `vec`, `hashmap`, `hashset`, `btree`, `heap` |
| **Crypto** | `crypto`, `hash` |
| **Network** | `http`, `net`, `websocket`, `url` |
| **Concurrency** | `sync`, `thread`, `atomic` |
| **Database** | `db`, `sql` |
| **System** | `env`, `process`, `os` |
| **Formatting** | `fmt`, `log` |

---

## Troubleshooting

### LSP Not Starting

1. Verify Luna is installed: `luna --version`
2. Check Output panel: **View > Output > Luna**
3. Restart LSP: **Cmd/Ctrl+Shift+P** > "Luna: Restart Language Server"
4. Run `luna --install-system` to fix PATH

### No IntelliSense

1. Ensure `luna.lsp.enabled` is `true`
2. Check file has `.luna` extension
3. Restart VS Code

### Performance Issues

Enable trace mode for debugging:

```json
{
    "luna.lsp.trace": true
}
```

Check Output panel for slow operations.

---

## Architecture

```
VS Code Extension (TypeScript)
         │
         │ JSON-RPC 2.0 (stdio)
         ▼
   luna lsp (Pure Luna)
         │
         ├── lsp.luna (protocol handling)
         ├── lexer.luna → parser.luna
         ├── types.luna → borrow_checker.luna
         └── stdlib docs (31 modules)
```

---

## Contributing

- [Report Issues](https://github.com/luna-lang/luna/issues)
- [View Source](https://github.com/luna-lang/luna/tree/main/vscode-extension)
- [Documentation](https://luna-lang.dev/docs)

---

## License

MIT License - see [LICENSE](https://github.com/luna-lang/luna/blob/main/LICENSE)

---

<p align="center">
  <strong>Luna v4.2 "The Absolute"</strong> — 100% Self-Hosted<br>
  Made with 🌙 by the <a href="https://github.com/luna-lang">Luna Team</a>
</p>
