<p align="center">
  <img src="https://raw.githubusercontent.com/LMDtokyo/Luna/main/editors/vscode/images/banner.jpg" width="420" alt="Luna programming language">
</p>

<h1 align="center">Luna</h1>

<p align="center">
  A cosmic-themed systems programming language with an indent-based
  syntax, a standalone C bootstrap compiler, and no dependency on Rust,
  Cargo, or LLVM.
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPLv3-blue?style=flat-square" alt="License"></a>
  <img src="https://img.shields.io/badge/bootstrap-C99-orange?style=flat-square" alt="Bootstrap">
  <img src="https://img.shields.io/badge/status-early-yellow?style=flat-square" alt="Status">
</p>

---

## Status

Luna is under active development. The current milestone is **self-hosting
via a C99 bootstrap compiler**:

| Component | State |
|---|---|
| Bootstrap compiler (`bootstrap/luna_bootstrap.c`) | ~3.7 KLOC C99, x86-64 codegen |
| Targets | Linux ELF64 + native Windows PE64, one source → two binaries |
| Core modules compiled through bootstrap | 19 / 19 (both targets) |
| Working runtime: `shine`, `print`, `print_int`, `exit` | ✓ both targets |
| Arrays: `[a, b, c]` / `[v; N]`, index read/write | ✓ both targets |
| Structs: declaration, literals, field read/write | ✓ both targets |
| Self-hosted compiler (compile Luna with Luna) | in progress |
| VS Code extension | [editors/vscode](editors/vscode) — v0.1.4 |

Real programs that build and run on both platforms today:

- [examples/stats.luna](examples/stats.luna) — struct-based count / sum / min / max / mean
- [examples/binary_search.luna](examples/binary_search.luna) — classic bsearch over a sorted array

Pure-Luna stdlib modules that compile through the bootstrap ([src/stdlib_new/](src/stdlib_new)):

- `base64` — RFC 4648 encode/decode
- `csv` — RFC 4180 parser + encoder
- `sha512` — FIPS 180-4 SHA-512 + RFC 4231 HMAC-SHA-512
- `chacha20` — RFC 7539 stream cipher
- `cli` — GNU-style argument parser (`--flag`, `--k=v`, `-xvf`, `--`)
- `logger` — DEBUG/INFO/WARN/ERROR with ISO timestamps
- `websocket` — RFC 6455 frame codec + handshake (uses `base64`)

Run the full test suite:

```sh
make -C bootstrap test-stdlib
```

## Cosmic syntax

Luna uses a deliberately distinct keyword set so programs look like
Luna, not C or Rust dressed up:

| Keyword | Role |
|---|---|
| `shine` | Print to stdout |
| `orbit` | `for` loop, e.g. `orbit @i in 0..10` |
| `eclipse` | `else` clause / match arm introducer |
| `phase` | Pattern match |
| `nova` | Break / early termination |
| `guard` | Early-return guard |
| `seal` | Immutable binding (like `let` in Rust) |
| `meow` | Mutable / top-level binding |
| `spawn` | Concurrent task |
| `defer` | Schedule cleanup |
| `actor` / `flow` | Message-passing primitives |

Variables are `@`-prefixed (`@count`, `@buffer`); comments start with `#`;
blocks are off-side (indentation-based) with no `{}` in source.

## Building from source

The bootstrap compiler is a single C99 translation unit with no
external dependencies. Any modern C compiler works.

**Linux / macOS:**

```sh
cc -O2 -std=c99 -o bootstrap/luna-boot bootstrap/luna_bootstrap.c
```

**Windows (MinGW-w64 / LLVM MinGW):**

```sh
x86_64-w64-mingw32-clang -O2 -std=c99 -o bootstrap/luna-boot.exe bootstrap/luna_bootstrap.c
```

## Hello, Luna

`bootstrap/hello.luna`:

```luna
fn main()
    shine("Hello from Luna!")
```

Compile and run:

```sh
./bootstrap/luna-boot bootstrap/hello.luna
./a.out
# Hello from Luna!
```

## Iteration and control flow

```luna
fn main()
    orbit @i in 0..5
        if @i % 2 == 0
            shine(@i.show() + " even")
        else
            shine(@i.show() + " odd")
```

## Repository layout

```
Luna/
├── bootstrap/          C bootstrap compiler + demos
│   ├── luna_bootstrap.c
│   ├── hello.luna, fizzbuzz.luna, smoketest.luna, ...
│   └── make_icon.py    (build-time utility)
├── src/
│   ├── core/           Compiler core (lexer, parser, types,
│   │                   forge, lower, luna_build, ...)
│   └── stdlib/         Standard library (crypto, veil, http,
│                       net, sync, x509_roots, runtime_core, ...)
├── editors/
│   └── vscode/         VS Code extension (highlighting,
│                       icons, snippets, LSP client)
├── assets/             Logos and icons
├── docs/               Language notes
├── LUNA_SPEC.md        Language reference
└── LICENSE             GPL-3.0-only
```

## VS Code extension

```sh
code --install-extension editors/vscode/luna-language-0.1.3.vsix
```

Provides syntax highlighting, the raven file icon, snippets, and an
optional LSP client that activates only when a `luna` toolchain is on
`PATH`.

## License

GPL-3.0-only. See [LICENSE](LICENSE).
