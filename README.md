<p align="center">
  <img src="https://raw.githubusercontent.com/LMDtokyo/Luna/main/assets/luna-logo.svg" width="200" alt="Luna Logo">
</p>

<h1 align="center">Luna</h1>

<p align="center">
  <strong>The Future of Systems Programming</strong>
</p>

<p align="center">
  <a href="#installation">Installation</a> •
  <a href="#quick-start">Quick Start</a> •
  <a href="#features">Features</a> •
  <a href="#documentation">Docs</a> •
  <a href="#benchmarks">Benchmarks</a>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/version-4.2.0-blue?style=flat-square" alt="Version">
  <img src="https://img.shields.io/badge/self--hosted-100%25-success?style=flat-square" alt="Self-Hosted">
  <img src="https://img.shields.io/badge/license-GPLv3-green?style=flat-square" alt="License">
  <img src="https://github.com/LMDtokyo/Luna/workflows/CI/badge.svg" alt="CI">
</p>

---

## What is Luna?

Luna is a modern systems programming language that combines:

- **Rust-level safety** — Ownership system prevents memory errors at compile time
- **C-level performance** — Zero-cost abstractions, native compilation
- **Python-level productivity** — Clean syntax, powerful type inference

```luna
# Hello World in Luna
fn main()
    shine "Hello, Luna!"

# Concurrent HTTP server in 10 lines
fn server()
    @app = http.create()

    orbit @req in @app.listen(8080)
        phase @req.path
            "/hello" -> http.json({"message": "Hello!"})
            "/users" -> spawn fetch_users(@req)
            _ -> http.text("Not found", 404)
```

## Key Features

### 100% Self-Hosted

Luna v4.2 compiles itself. The entire compiler (45,413 lines) is written in Luna:

```
src/
├── core/           # Compiler core (6 files, ~14K lines)
│   ├── lexer.luna
│   ├── parser.luna
│   ├── types.luna      # Hindley-Milner type system
│   ├── borrow_checker.luna
│   ├── titan_opt.luna  # 8-pass optimizer
│   └── main.luna
├── stdlib/         # Standard library (18 files, ~29K lines)
│   ├── io.luna, net.luna, http.luna, json.luna
│   ├── crypto.luna, sync.luna, collections.luna
│   └── ...
└── lsp/            # Language server (pure Luna)
    └── lsp.luna
```

### Cosmic Syntax

Luna's "cosmic" keywords make code intention explicit:

| Keyword | Purpose | Example |
|---------|---------|---------|
| `shine` | Return/emit value | `shine @result` |
| `eclipse` | Defer until scope exit | `eclipse close(@file)` |
| `orbit` | Iterate with cleanup | `orbit @x in @items` |
| `phase` | Pattern matching | `phase @value` |
| `spawn` | Concurrent task | `spawn process(@data)` |
| `nova` | Async boundary | `nova fetch(@url)` |
| `guard` | Early return | `guard @x > 0` |
| `seal` | Immutable binding | `seal @config` |

### Type System

Full Hindley-Milner inference with algebraic data types:

```luna
type Result<T, E>
    Ok(T)
    Err(E)

fn divide(@a: int, @b: int) -> Result<int, str>
    guard @b != 0 else shine Err("division by zero")
    shine Ok(@a / @b)

# Usage - types are inferred
@result = divide(10, 2)
phase @result
    Ok(@v) -> print("Result: " + @v)
    Err(@e) -> print("Error: " + @e)
```

### Memory Safety

Ownership and borrowing without a garbage collector:

```luna
fn process(@data: &[u8]) -> int      # Immutable borrow
fn modify(@data: &mut [u8])          # Mutable borrow

# Compile-time guarantees:
# - No buffer overflows
# - No use-after-free
# - No data races
# - No null pointers
```

## Installation

### Quick Install (Recommended)

```bash
# Download and run installer
curl -fsSL https://luna-lang.org/install.sh | sh

# Or with cargo
cargo install luna-lang
```

### From Source

```bash
git clone https://github.com/LMDtokyo/Luna.git
cd Luna
cargo build --release

# Install system-wide
./target/release/luna --install-system
```

### Verify Installation

```bash
luna --version
# Luna 4.2.0 (bootstrap: 25 files, 45,413 lines, 100% native)

luna --self-compile
# Self-compile successful: 25/25 files, 45,413 lines
```

## Quick Start

### Hello World

```luna
# hello.luna
fn main()
    shine "Hello, World!"
```

```bash
luna run hello.luna
# Hello, World!
```

### Build Native Binary

```bash
luna build hello.luna -o hello
./hello
```

### HTTP Server

```luna
# server.luna
import http

fn main()
    @app = http.create()

    @app.get("/", fn(@req) ->
        http.html("<h1>Welcome to Luna!</h1>")
    )

    @app.get("/api/users", fn(@req) ->
        @users = db.query("SELECT * FROM users")
        http.json(@users)
    )

    print("Server running on :8080")
    @app.listen(8080)
```

### Concurrency

```luna
import sync

fn main()
    @results: [int] = []
    @mutex = sync.mutex()

    orbit @i in 0..10
        spawn
            @value = expensive_computation(@i)
            sync.lock(@mutex)
            @results.push(@value)
            sync.unlock(@mutex)

    sync.wait_all()
    print("Results: " + @results)
```

## Benchmarks

Performance compared to other languages (lower is better):

| Benchmark | Luna | Rust | C | Go | Python |
|-----------|------|------|---|----|----- |
| Fibonacci(45) | 1.0x | 1.0x | 1.0x | 1.8x | 45x |
| JSON parse 1MB | 1.0x | 0.9x | 0.8x | 1.5x | 12x |
| HTTP RPS | 850K | 900K | 920K | 450K | 15K |
| Binary size | 1.2MB | 1.1MB | 0.8MB | 3.5MB | — |
| Compile time | 3.5s | — | — | — | — |

## VS Code Extension

Install the Luna extension for full IDE support:

```bash
# From VS Code marketplace
ext install luna-team.luna-language

# Or from .vsix
code --install-extension editors/vscode/luna-language-1.7.0.vsix
```

Features:
- Syntax highlighting for cosmic keywords
- Real-time error checking via native LSP
- Code completion with type info
- Go-to-definition, find references
- Format on save

## Project Structure

```
Luna/
├── src/
│   ├── core/           # Compiler (lexer, parser, types, optimizer)
│   ├── stdlib/         # Standard library (31 modules)
│   └── lsp/            # Language server
├── editors/
│   └── vscode/         # VS Code extension v1.7.0
├── LUNA_SPEC.md        # Language specification
├── LICENSE             # GPLv3
└── README.md
```

## Documentation

- [Language Specification](LUNA_SPEC.md)
- [Getting Started Guide](https://luna-lang.org/docs/getting-started)
- [Standard Library Reference](https://luna-lang.org/docs/stdlib)
- [Cosmic Keywords Guide](https://luna-lang.org/docs/cosmic)

## Contributing

We welcome contributions! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

```bash
# Run tests
cargo test

# Run self-compile check
luna --self-compile

# Format code
luna fmt src/
```

## License

Luna is licensed under the [GNU General Public License v3.0](LICENSE).

---

<p align="center">
  <strong>Luna — where safety meets performance</strong>
</p>

<p align="center">
  <a href="https://luna-lang.org">Website</a> •
  <a href="https://github.com/LMDtokyo/Luna/issues">Issues</a> •
  <a href="https://discord.gg/luna-lang">Discord</a>
</p>
