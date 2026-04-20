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

Luna is **self-hosted** as of 2026-04-20 for a C-substitute subset:

| Component | State |
|---|---|
| C bootstrap compiler (`bootstrap/luna_bootstrap.c`) | ~5.9 KLOC C99, x86-64 codegen |
| Self-hosted compiler (`src/bootminor/`) | Luna-in-Luna, compiles itself byte-identically |
| Targets | Linux ELF64 + Windows PE64 (bootstrap), Linux ELF64 (bootminor) |
| Bit-identical fixed point | ✓ `luna-mini3 == luna-mini4` (115 889 B ELF64) |
| Self-host test suite | 18 / 18 PASS (fib, FizzBuzz, factorial, structs, recursion) |
| Working runtime: `shine`, `print`, `print_int`, `exit` | ✓ both compilers |
| Arrays, structs, strings, bitwise, `if/while/break/continue` | ✓ both compilers |
| User-defined fns up to N args (SysV + stack spill) | ✓ both compilers |
| VS Code extension | [editors/vscode](editors/vscode) — v0.1.4 |

The language no longer structurally needs the C bootstrap — the shipped
`src/bootminor/luna-mini.elf` binary re-compiles itself from
`src/bootminor/*.luna` to a byte-identical binary. The C step is now
only for first-time setup on a fresh machine.

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

## Self-hosted rebuild

The shipped `src/bootminor/luna-mini.elf` is a 115 889-byte Linux ELF64
binary produced by `bootminor` compiling its own source. To rebuild it
without a C compiler (requires WSL Ubuntu on Windows, or native Linux):

```sh
bash src/bootminor/selfhost_build.sh
```

That script runs the shipped `luna-mini.elf` against the current
`bootminor_prelude.luna + lex.luna + gen.luna + main2.luna` monolith,
checks the result is a fixed point, and reports whether the rebuilt
binary matches the shipped copy.

To verify the full three-stage chain (`bootstrap → luna-mini2 →
luna-mini3 → luna-mini4`, then `cmp`):

```sh
bash src/bootminor/run_tests_m3.sh
# expected: [fixed-point] PASS — luna-mini3 = luna-mini4 byte-identical
# expected: suite 18 PASS, 0 FAIL
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
