# Contributing to Luna

Practical guide for adding code. The architectural model lives in
[`ARCHITECTURE.md`](ARCHITECTURE.md) — read that first if you haven't.
This file answers the everyday question: **where does my code go?**

---

## Where does my new module go?

Walk this decision tree top to bottom. Stop at the first match.

1. **Is this a syscall wrapper, atomic primitive, or raw memory op?**
   → `std/runtime/` (T2)
2. **Does it depend only on T2 — strings, allocator, math, bits, raw fd I/O, env vars?**
   → `std/core/` (T3)
3. **Is it a self-contained data structure or a pure parser/formatter (vec, map, fmt, json, regex, time, fs)?**
   → `std/std/` (T4)
4. **Does it touch the network or do concurrency (tcp, http, tls, url, threads, channels)?**
   → `std/net/` (T5)
5. **Is it heavy, optional, or does it need a C library (crypto, db, compression, gui, websocket)?**
   → `std/ext/` (T6)
6. **Is it a CLI binary the user invokes (`luna`, `luna-fmt`, `luna-lsp`)?**
   → `tools/` (T7)
7. **Is it an app or demo?**
   → `examples/<domain>/` (T8)
8. **Is it part of the compiler itself?**
   → `compiler/seed/` if it needs to live in the bootminor self-host;
     `compiler/core/` if it's the production compiler.

If two answers seem to fit, pick the lower tier. A module placed too
high creates a hidden dependency; placed too low it's harmless.

---

## Module header

Top of every `.luna` source file:

```luna
# tier: T4
# deps: std/core/str, std/core/io_raw, std/runtime/syscall
# ffi: none
```

Three rules:
- `tier:` must match the file's folder. `std/std/foo.luna` → `T4`.
- `deps:` lists the modules you `import`, by their bare name. Keep it
  in sync — out-of-date `deps:` is a code-review smell, not a build
  break.
- `ffi:` is `none` everywhere except T6+. If you write `extern "C"`,
  declare it (`# ffi: openssl, libc`).

Short example — a T3 module:

```luna
# tier: T3
# deps: std/runtime/syscall, std/runtime/mem_raw
# ffi: none

import syscall
import mem_raw

fn alloc(@n: int) -> *mut u8
    # ...
```

---

## Imports

Imports are bare module names (`import json`), not paths. The
compiler resolves them against a search path that includes the new
tier tree.

The tier rule: **a module at tier N may import only modules at tiers
≤ N**.

| Source tier | May import |
|---|---|
| T2 | nothing (kernel ABI only) |
| T3 | T2 |
| T4 | T3, T2 |
| T5 | T4, T3, T2 |
| T6 | T5, T4, T3, T2 |
| T7 (tools) | anything in `std/` |
| T8 (examples) | anything |
| T1 (compiler) | anything in `std/` |

Linter command:

```sh
bash tools/lint_tiers.sh
```

It builds a module → tier index from `std/`'s file paths, then
flags any import that violates the rule above.

---

## Forbidden patterns

These will fail CI.

### Shell-out at T2–T5

```luna
# tier: T5
shell_run("curl https://example.com")     # ❌ T5 must use sockets
```

T5 has `tcp`, `udp`, `http`, `tls`. Use them. If you find yourself
wanting `shell_run` in a T2–T5 module, the feature belongs in T6
(opt-in FFI) or T7 (CLI tool that may legitimately invoke other
programs).

### Undeclared FFI

```luna
# tier: T4
# ffi: none
extern "C" fn SSL_read(...)               # ❌ extern at T4
```

T4 has no business calling C. Move the module to T6 and declare
`# ffi: openssl`.

### Upcalls

```luna
# tier: T3
import http                               # ❌ T3 imports T5
```

Either the module is mis-tiered (probably actually T5+) or the
import is wrong. Never the third option.

### Cross-tier name collision

You cannot have `std/std/json.luna` and `std/ext/json.luna`
simultaneously. Bare imports must resolve to one tier. The linter
catches this.

---

## Tests

Tests live in [`tests/`](../tests/), in a tree that mirrors the
source. A module at `std/std/json.luna` is tested by
`tests/std/std/json_test.luna`.

```
std/std/json.luna
tests/std/std/json_test.luna
tests/std/std/json_test.expect
```

The `.expect` file holds expected stdout (legacy-compatible with
`bootminor/run_tests_*.sh`). Use [`std/std/test`](../std/std/) helpers
(`assert_eq_int`, `assert_eq_str`, `test_summary`) for non-trivial
assertions.

Cross-module integration tests — anything that exercises more than
one module — go under `tests/integration/`.

Run:

```sh
bash tests/run_all.sh                    # everything
bash tests/run_all.sh std/std            # one tier
bash tests/run_all.sh std/std/json       # one module
```

---

## Examples

`examples/` is grouped by domain, not by feature size:

| Folder | What goes here |
|---|---|
| `examples/basics/` | hello, fizzbuzz, fib, pi — language tour |
| `examples/algo/` | binary_search, sha256_demo, linked_list |
| `examples/cli/` | wc, stats, token_tool — Unix-style CLIs |
| `examples/net/` | http_server, https_server, url_shortener |
| `examples/bots/` | tg_bot, luna_bot — chat/messenger bots |
| `examples/systems/` | elf_parser, hot_dump, hotswap_demo |

A new example goes in the closest existing folder. Don't create a
new top-level folder for a single demo.

---

## Language gotchas

This section catalogs known limitations of the **bootminor** compiler
(`src/bootminor/luna-mini.elf`). The aspirational full compiler in
`compiler/core/` lifts most of them.

### 1. Struct return — annotate the receiver

Bootminor resolves struct field offsets from the **declared type of
the receiver variable**, not from the function's return signature.
Without an explicit annotation the receiver is treated as plain `int`
and every `.field` access silently reads offset 0.

```luna
fn make() -> HttpResponse
    ...

# WRONG — @r.status works (offset 0) but @r.body crashes.
@r = make()

# RIGHT — bootminor knows @r is 24 bytes with three fields.
let @r: HttpResponse = make()
```

This applies anywhere you receive a struct from a function call:
`Vec`, `StrBuf`, `HttpResponse`, your own structs. Inside the
function the field offsets are already known because the parameter
or `let` binding has the type. Only the *callsite* needs the fix.

### 2. `let @x: Type = call(...)` outside its declaring function

If function `f` is declared `-> T`, you can write `let @x: T = f(...)`
inside `f` or any function that also returns `T`. From a function
that returns something else, bootminor's parser rejects the form.

Workaround — declare the typed binding via `new_str(N)` first, then
reassign:

```luna
fn _httpsrv_handle_one(@cli: int, @handler: int) -> int
    let @req: HttpRequest = new_str(40)        # seed the type
    ...
    @req = http_parse_request(@buf, @n)        # plain reassignment
```

The reassignment preserves the original type binding, so `@req.field`
keeps working.

### 3. Function-pointer call as a statement

Bootminor parses `@handler(args)` as the start of an assignment
(`@handler = ...`). On its own line that fails with `expected '='`.
Capture the return value into any sink to turn it back into an
expression context:

```luna
@_ = @handler(@req, @cli)
```

Same workaround works for any indirect call. `return @handler(...)`
also parses fine because it's already an expression position.

### 4. Multi-line function-call arguments

Bootminor's lexer requires all arguments of a call to live on the
same line.

```luna
# WRONG — second line is not recognised as a continuation.
@r = assert_eq_str("label",
                   some_call(x), "expected")

# RIGHT
@r = assert_eq_str("label", some_call(x), "expected")
```

### 5. Deeply nested blocks (5+ levels)

`fn > while > if > else > if` is the deepest nesting bootminor parses
reliably. Beyond that the indent tracker mis-counts. Factor the inner
body into a helper function with shallower scope.

### 6. Escaped quotes inside string literals

Bootminor's string lexer mishandles `\"` in some contexts (notably
when the surrounding code includes braces). If you need JSON-shaped
literals in tests, prefer alternative payloads or build the string at
runtime via `str_concat`.

---

---

## Memory management — `arena_mark` / `arena_reset`

Bootminor's heap is a bump allocator: every `new_str` advances a
pointer and nothing is ever freed automatically. A long-running
process that allocates inside a loop will eventually OOM.

The fix is **arenas**: snapshot the bump pointer before a unit of
work, do the work, restore it afterwards.

```luna
fn handle_request(@cli: int) -> int
    @mark = arena_mark()                # save heap_top
    @buf = new_str(8192)
    @req = http_parse_request(@buf, ...)
    @_ = @handler(@req, @cli)
    arena_reset(@mark)                  # release everything above mark
    return 0
```

After `arena_reset`, every pointer into the [mark .. previous_top)
range is **dangling** — never keep them. Long-lived state (router
table, db connection, config) must be allocated *before* the mark.

The HTTP server's request loop does this automatically — handlers
don't need to call `arena_mark` themselves unless they spawn a
sub-arena for an inner loop (large CSV import, etc).

Forking servers (`router_serve_forking`) get the same effect for
free: child exits → kernel reclaims everything. Arenas are belt and
braces in that case; the real win is sequential `http_serve` and
long-lived bots.

---

## Quick checklist before you push

- [ ] File has `# tier: TN` header matching its folder.
- [ ] `deps:` lists every module you import.
- [ ] `ffi:` is `none` (T2–T5) or declared (T6+).
- [ ] `bash tools/lint_tiers.sh` passes locally.
- [ ] Test exists at the mirrored `tests/` path and passes.
- [ ] No `shell_run`, no `extern "C"`, unless the tier permits.

If any box is unchecked, fix before opening a PR. Reviewers will
bounce it otherwise.
