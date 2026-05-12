<p align="center">
  <img src="https://raw.githubusercontent.com/LMDtokyo/Luna/main/editors/vscode/images/banner.jpg" width="420" alt="Luna programming language">
</p>

<h1 align="center">Luna</h1>

<p align="center">
  A self-hosted systems language for math, memory, and hacking —
  no GC, no VM.
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPLv3-blue?style=flat-square" alt="License"></a>
  <img src="https://img.shields.io/badge/bootstrap-C99-orange?style=flat-square" alt="Bootstrap">
  <img src="https://img.shields.io/badge/self--host-fixed%20point-brightgreen?style=flat-square" alt="Self-host">
</p>

---

## Status

Luna is **self-hosted** as of 2026-04-20 for a C-substitute subset. The
shipped `src/bootminor/luna-mini.elf` recompiles itself from its own
`.luna` source to a byte-identical binary.

| Component | State |
|---|---|
| C bootstrap compiler (`bootstrap/luna_bootstrap.c`) | 5.9 KLOC C99, x86-64 → both ELF64 + PE64 (first-time setup only) |
| Self-hosted compiler (`src/bootminor/luna-mini.elf`) | 233 KB, Luna compiles itself byte-identically |
| Targets | Linux ELF64 from bootminor; Linux ELF64 + Windows PE64 from C bootstrap |
| Fixed point | ✓ `luna-mini3 == luna-mini4` |
| Compiler tests | 53 / 53 (m2b + m2c + types + adt + generics + threads + closures) |
| Stdlib tests | 123 / 123 across 6 modules (strings, env, map, cli, json, io) |
| Module system | ✓ `import foo` resolves transitively; dedup, search path |
| Coming next | hot-swap protocol (see [`docs/HOTSWAP.md`](docs/HOTSWAP.md)) |
| VS Code extension | [editors/vscode](editors/vscode) — v0.1.4 |

## Why Luna

- **Self-host in under 1 second.** `luna-mini.elf` re-emits its own
  233 KB binary from source — no C compiler, no make, no linker.
- **Native ELF binaries from 4 KB up.** No runtime, no VM, no GC. A
  `hello.luna` compiles to a standalone executable you can `strace` or
  `objdump`. A multi-module Telegram bot fits in 49 KB.
- **Module system + stdlib.** `import strings`, `import json`,
  `import http` — 9 pure-Luna modules ship with `install.sh`, all
  compile via bootminor (no C dependency at runtime).
- **Hot-swap protocol.** Function-level live patching over a Unix
  socket — see [`docs/HOTSWAP.md`](docs/HOTSWAP.md).

## Feature highlights

- Cosmic keywords — `shine` prints, `seal`/`meow`/`let` bind, `orbit`
  loops, `phase` matches.
- Raw memory — `u8_at`, `u16_at`, `u32_at`, `u64_at`, matching
  `*_set` writers, plus `bswap32`/`bswap64`, `popcount`, `clz`, `ctz`,
  `rotl`, `rotr`.
- SSE-backed `f64_*` — `f64_add`, `f64_mul`, `f64_div`, `f64_sqrt`,
  `f64_lt`, `f64_eq`, `f64_from_int`, `f64_to_int`, IEEE-754 bit-lit.
- Typed pointers — `*mut T`, `*const T`, address-of (`&@x`,
  `&@obj.field`) and deref (`*@p = 42`) round-trip through the
  codegen.
- Compound assigns — `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`,
  `<<=`, `>>=`.
- Compile-time constants — `const PAGE_SIZE: int = 1 << 12`, folded in
  `main2.luna` before emission.
- Bitwise operators plus `break`/`continue`, user-defined fns to any
  arity (SysV + stack spill), structs by value and by pointer.
- Hot-swap protocol coming soon — see [`docs/HOTSWAP.md`](docs/HOTSWAP.md).

## Quick start

```luna
# hello.luna
fn main() -> int
    shine("Hello, Luna!")
    return 0
```

```sh
bash src/bootminor/selfhost_build.sh          # rebuild luna-mini.elf
./src/bootminor/luna-mini.elf hello.luna -o hello.elf
chmod +x hello.elf && ./hello.elf
# Hello, Luna!
```

Full walkthrough, including file I/O and the full test suite, lives in
[`docs/QUICKSTART.md`](docs/QUICKSTART.md).

## Self-hosted rebuild

The shipped `src/bootminor/luna-mini.elf` is a Linux ELF64 binary
produced by `bootminor` compiling its own source. To rebuild it
without a C compiler (requires WSL Ubuntu on Windows, or native
Linux):

```sh
bash src/bootminor/selfhost_build.sh
```

That script concatenates `bootminor_prelude.luna + lex.luna +
gen.luna + main2.luna`, runs the shipped `luna-mini.elf` on the
result, verifies the rebuilt binary is a fixed point, and reports
whether it matches the shipped copy.

To verify the full three-stage chain (`bootstrap → luna-mini2 →
luna-mini3 → luna-mini4`, then `cmp`):

```sh
bash src/bootminor/run_tests_m3.sh
# expected: [fixed-point] PASS — luna-mini3 = luna-mini4 byte-identical
# expected: suite 38 PASS, 0 FAIL
```

On native Linux (no WSL), set `LUNA_NATIVE=1`:

```sh
LUNA_NATIVE=1 bash src/bootminor/run_tests_m3.sh
```

## Bootminor stdlib (`src/lib/std/`)

Pure-Luna modules that compile via `luna-mini.elf` (no C bootstrap).
`install.sh` ships them to `~/.luna/lib/std/`, where the `luna` CLI
auto-resolves `import foo` against them.

| Module | Lines | Functions |
|---|---|---|
| [`strings`](src/lib/std/strings.luna) | 200 | `parse_int`, `str_to_upper/lower`, `str_split/join`, `str_trim`, `str_replace`, `str_starts_with`, `str_index_of`, `str_contains`, … |
| [`env`](src/lib/std/env.luna) | 65 | `env_get`, `env_lookup` (reads `/proc/self/environ`) |
| [`map`](src/lib/std/map.luna) | 95 | `map_new`, `map_get`, `map_set`, `map_has`, `map_remove`, `map_keys/values` |
| [`cli`](src/lib/std/cli.luna) | 130 | GNU-style argv parser: `--key=val`, `--flag`, `-xyz`, `--` sentinel |
| [`sys`](src/lib/std/sys.luna) | 50 | `args_count`, `args_user`, `program_name`, `exit` |
| [`process`](src/lib/std/process.luna) | 110 | `shell_run`, `shell_capture` — fork + execve + wait4 |
| [`json`](src/lib/std/json.luna) | 320 | in-place scanner: `json_obj_str/int/bool/pos`, `json_array_positions`, `json_escape` |
| [`http`](src/lib/std/http.luna) | 65 | `http_get`, `http_post_json` — TLS via curl shim |
| [`io`](src/lib/std/io.luna) | 130 | `file_exists`, `file_size`, `read_lines`, `append_file`, `basename/dirname`, `path_join`, `path_extension/stem` |
| [`test`](src/lib/std/test.luna) | 60 | `assert_eq_int/str`, `assert_true/false`, `test_summary` |

Run all stdlib tests:

```sh
bash src/lib/std/run_tests.sh
# expected: 6 modules, 123 PASS, 0 FAIL
```

### Larger stdlib (compiled via C bootstrap)

These modules in `src/stdlib_new/` use language features bootminor
doesn't ship yet (`extern "C"`-only helpers, etc.) — they go through
`bootstrap/luna-boot`:

- `base64`, `csv`, `sha512`, `chacha20`, `logger`, `websocket`

Run them:

```sh
make -C bootstrap test-stdlib
```

## Real programs in Luna

Multi-file Luna programs that compile via bootminor:

- [`examples/wc/`](examples/wc/) — GNU-compatible word/line/byte counter (148 lines, 32 KB ELF)
- [`examples/tg_bot/`](examples/tg_bot/) — Telegram bot (145 lines, 49 KB ELF) — uses `std/json`, `std/http`, `std/process`

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

## Building the C bootstrap

The bootstrap compiler is a single C99 translation unit with no
external dependencies. Any modern C compiler works. It is only needed
for first-time setup on a machine without a prebuilt `luna-mini.elf`.

**Linux / macOS:**

```sh
cc -O2 -std=c99 -o bootstrap/luna-boot bootstrap/luna_bootstrap.c
```

**Windows (MinGW-w64 / LLVM MinGW):**

```sh
x86_64-w64-mingw32-clang -O2 -std=c99 -o bootstrap/luna-boot.exe bootstrap/luna_bootstrap.c
```

## Documentation

- [`docs/QUICKSTART.md`](docs/QUICKSTART.md) — get from `git clone` to
  a running binary in under five minutes.
- [`docs/LANGUAGE.md`](docs/LANGUAGE.md) — language reference (full
  grammar + type system).
- [`docs/HOTSWAP.md`](docs/HOTSWAP.md) — hot-swap protocol v0.1 spec.
- [`LUNA_SPEC.md`](LUNA_SPEC.md) — long-form language specification.

## Repository layout

```
Luna/
├── bootstrap/          C bootstrap compiler + demos
│   ├── luna_bootstrap.c
│   └── hello.luna, fizzbuzz.luna, smoketest.luna, ...
├── src/
│   ├── bootminor/      Self-hosted compiler (Luna in Luna)
│   │   ├── luna-mini.elf           (shipped binary, 169 KB)
│   │   ├── bootminor_prelude.luna
│   │   ├── lex.luna, gen.luna, main2.luna
│   │   └── tests_m2b/, tests_m2c/, tests_types/
│   ├── core/           Bootstrap compiler core
│   └── stdlib_new/     Pure-Luna stdlib (base64, csv, sha512, ...)
├── editors/
│   └── vscode/         VS Code extension
├── docs/               QUICKSTART, LANGUAGE, HOTSWAP notes
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
