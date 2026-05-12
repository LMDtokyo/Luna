# Luna Architecture

How the project is layered, what each layer is allowed to depend on, and
why. This is the constitution: every other rule in
[`CONTRIBUTING.md`](CONTRIBUTING.md) and the linter in
[`tools/lint_tiers.sh`](../tools/lint_tiers.sh) flows from this document.

---

## Why tiers

Luna ships a compiler **and** a stdlib. Without a clear hierarchy you
get circular imports, three parallel `json.luna` files, and an `http`
module that secretly forks `curl`. We have already lived with all
three. Tiers fix this by giving every module a fixed altitude.

A module at tier **N** may only depend on modules at tiers **≤ N − 1**.
No upcalls, no sideways exceptions, no "just this once". The linter
enforces it; CI fails if it slips.

The other rule that flows from this: tiers T2–T5 are pure syscalls.
**No shell-out, no FFI to external CLIs, no `curl`/`openssl` shims.**
If something can't be done with `read(2)`/`write(2)`/`socket(2)` in a
T5 module, it doesn't belong in T5. It belongs in T6 (`ext/`) where
FFI is explicit and declared.

---

## The tiers

| Tier | Folder | Role | May depend on | FFI | Shell |
|---|---|---|---|---|---|
| **T0** | `compiler/bootstrap/` | One-shot C bootstrap. Built once, then forgotten. | libc only | — | — |
| **T1** | `compiler/seed/`, `compiler/core/` | The Luna compiler itself. | Any std/* tier | — | — |
| **T2** | `std/runtime/` | Syscalls, atomics, raw memory primitives. | Kernel ABI only | no | no |
| **T3** | `std/core/` | Foundation — `mem`, `str`, `math`, `bits`, `io_raw`, `sys`. | T2 | no | no |
| **T4** | `std/std/` | Ordinary stdlib — `vec`, `map`, `fmt`, `io`, `fs`, `time`, `json`, `regex`, `cli`, `log`. | T3, T2 | no | no |
| **T5** | `std/net/` | Networking + concurrency — `tcp`, `udp`, `http`, `tls`, `url`, `thread`, `sync`, `channel`. | T4 and below | no | no |
| **T6** | `std/ext/` | Heavy or platform-specific — `crypto/`, `compress/`, `db/`, `websocket`, `gui`. | T5 and below | **declared** | no |
| **T7** | `tools/` | The `luna` CLI, `luna-fmt`, `luna-lsp`, `luna-pkg`. | T6 and below | declared | yes |
| **T8** | `examples/` | Demos, bots, sample apps. | Anything | declared | yes |

`compiler/` is special: T1 modules may freely import any `std/*` tier.
The compiler is a *consumer* of stdlib, not a foundation under it.

---

## What each tier can do

### T2 — Runtime

Direct kernel ABI. `syscall.luna` exposes raw `sys_read`, `sys_write`,
`sys_mmap`, `sys_clone`, `sys_futex`, etc. `atomic.luna` is the
`lock`-prefixed primitives. `mem_raw.luna` is `*mut`/`*const`
arithmetic.

T2 modules never call printf, never allocate from a heap, never
`shine` strings. They are the boundary between Luna and the kernel.

### T3 — Core

Builds on T2 to give you usable building blocks: a heap allocator
(`mem`), null-terminated and length-prefixed strings (`str`), SSE math
helpers (`math`), `bswap`/`popcount`/`clz` (`bits`), unbuffered byte
I/O on file descriptors (`io_raw`), process info (`sys`), env vars
(`env`).

Anything that touches data without making a structural decision about
it goes here.

### T4 — Std

The stuff users reach for first. `Vec[T]`, `Map[K,V]`, formatted
printing, buffered I/O, filesystem traversal, time/duration math,
JSON encode/decode, regex, structured logging, argv parser. Pure
Luna, no FFI.

A module here is allowed to depend on anything in T2/T3 but must not
reach sideways into T4 unless the depended-on module is genuinely
fundamental for the depending one (`fmt` may use `str`, `vec` is
self-contained).

### T5 — Net + Concurrent

The line where Luna becomes a useful systems language. TCP/UDP via
`socket(2)`. HTTP/1.1 client+server **written in Luna** on top of
those. URL parsing. Threads via `clone(2)`. Mutex/RwLock via `futex`.
Channels.

**TLS lives here only if implemented in pure Luna.** Otherwise it
sits in T6 with an explicit `# ffi: openssl` declaration.

### T6 — Ext

The escape hatch. Crypto primitives, compression, database drivers,
GUI, anything that's either too heavy to maintain in pure Luna or
genuinely needs a C library. Every T6 module declares its FFI surface
in its header — no hidden `extern "C"`.

T6 is opt-in: nothing in T2–T5 may import from T6. Apps that need
crypto pull it explicitly.

### T7 — Tools

CLI binaries the user invokes: the compiler driver, formatter, LSP,
package manager. May shell out (e.g. `luna build` calling `luna-fmt`).

### T8 — Examples

Apps. Bots, web servers, demos. Free to use everything.

---

## Module header

Every `.luna` source file under `std/`, `compiler/`, `tools/`,
`examples/` declares its tier in the first three comment lines:

```luna
# tier: T4
# deps: str, io_raw
# ffi: none
```

- **`tier:`** — `T0` through `T8`. Must match the file's path (linter
  enforces).
- **`deps:`** — comma-separated list of bare module names this file
  imports (the same names that appear after `import`). Documentation,
  not magic — read by humans during code review.
- **`ffi:`** — `none`, or a comma-separated list of C libraries
  (`libc`, `openssl`, `sqlite3`). `# ffi: none` is **required** at
  T2–T5; non-empty is allowed only at T6+.

A file without a `# tier:` line is treated as legacy and skipped by
the linter. New files must have one.

---

## Folder layout (target)

```
luna/
├── compiler/
│   ├── bootstrap/                  # T0  — C bootstrap (one-time)
│   ├── seed/                       # T1  — minimal self-host (was bootminor)
│   └── core/                       # T1  — production compiler
├── std/
│   ├── runtime/                    # T2  — syscalls, atomics, raw mem
│   ├── core/                       # T3  — mem, str, math, bits, io_raw, sys
│   ├── std/                        # T4  — vec, map, fmt, io, fs, time, json
│   ├── net/                        # T5  — tcp, udp, http, tls, url, thread
│   └── ext/                        # T6  — crypto, compress, db, websocket
├── tools/                          # T7  — luna CLI, fmt, lsp, pkg
├── examples/                       # T8  — apps, demos, bots
├── tests/                          # mirrored tree: tests/std/std/vec_test.luna
├── docs/
└── editors/vscode/
```

Paths inside `std/` are deliberately layered. `std/std/json.luna` is
not a typo — outer `std/` is the stdlib root, inner `std/` is the T4
tier. Same pattern as how `compiler/core/` distinguishes from the
parent `compiler/`.

---

## Migration status

The new layout is being rolled out in phases. Until phase N completes,
modules in tier N–1 still live under their legacy paths and the
linter has no opinion on them.

| Phase | Scope | Status |
|---|---|---|
| 0 | `ARCHITECTURE.md`, `CONTRIBUTING.md`, `lint_tiers.sh`, CI | **in progress** |
| 1 | Populate `std/runtime/` and `std/core/` | pending |
| 2 | Populate `std/std/` (vec, map, json, fmt, io, fs, time, regex, log, cli) | pending |
| 3 | Populate `std/net/` (rewrite `http` in pure Luna, kill curl shim) | pending |
| 4 | Populate `std/ext/` (crypto, compress, db, websocket) | pending |
| 5 | Move compiler — `bootstrap/` → `compiler/bootstrap/`, `src/bootminor/` → `compiler/seed/`, `src/core/` → `compiler/core/` | pending |
| 6 | Reorganise `examples/` by domain (`basics/`, `algo/`, `cli/`, `net/`, `bots/`, `systems/`) | pending |
| 7 | Delete legacy trees (`src/lib/`, `src/stdlib/`, `src/stdlib_new/`), set `LUNA_LINT_REQUIRE_TIER=1` in CI | pending |

When phase 7 lands, the linter ratchets to strict mode: every
`.luna` file under `std/`, `compiler/`, `tools/`, `examples/` must
have a tier header.

---

## Linter

[`tools/lint_tiers.sh`](../tools/lint_tiers.sh) walks every `.luna`
file in the new tree and enforces:

1. **Tier matches path.** `std/std/foo.luna` declaring `# tier: T2`
   is rejected.
2. **No upcalls.** A T3 file may not `import` a T4 module. The linter
   builds a name → tier index from `std/`'s contents and resolves
   bare imports against it.
3. **No shell at T2–T5.** Any occurrence of `shell_run`,
   `shell_capture`, `popen`, `system(` outside a comment is a hard
   error.
4. **No undeclared FFI.** `extern "C"` outside T6+ files is a hard
   error.

Run locally:

```sh
bash tools/lint_tiers.sh
```

CI invokes this on every push; see [`.github/workflows/ci.yml`](../.github/workflows/ci.yml).

---

## FAQ

**Why tiers and not feature folders?**
A compiler+stdlib has true vertical dependencies. `json` cannot exist
without `str`, which cannot exist without `mem`, which cannot exist
without syscalls. Layered is the right shape; feature folders are
right when modules are siblings, which they aren't here.

**Can I add a new tier?**
Maybe in five years. For now, eight tiers is more than enough — most
projects survive on three.

**`std/std/`? Really?**
Yes. The outer `std/` is the stdlib root (parallel to `compiler/`,
`tools/`, `examples/`). The inner `std/` is the T4 tier. Renaming
either to avoid the repetition makes the structure read worse, not
better.

**What about a module that legitimately needs both T4 and T6 features?**
You probably want to split it. The T4 portion is the data structure
or format; the T6 portion is the platform-specific glue. Keep them in
separate files.
