# Stack traces on panic: design

Roadmap item **1.6** — when a Luna program crashes (panic, segfault,
integer overflow), the user sees nothing useful: the process exits
with a status code and the kernel writes raw hex to stderr. There's
no way to know **which function** failed or **on which line**.

This document is a **design**, not an implementation. Like
[TLS_DESIGN.md](TLS_DESIGN.md), it captures the trade-offs and a phased
plan. Implementation requires a focused session.

---

## What "good" looks like

When a Luna binary crashes, we want this on stderr:

```
panic: division by zero
  at orders.luna:42  fn process_order(@id)
  at handlers/checkout.luna:18  fn handle_checkout(@req)
  at main.luna:7  fn main()
```

Three things to engineer:
1. **PC → source location** mapping (compile-time data).
2. **Stack walking** (runtime).
3. **Panic invocation that prints the trace** (runtime).

---

## Option A: DWARF (the standard)

DWARF is the standard debug format on Linux/macOS. `gdb`, `lldb`, `perf`,
`addr2line` all consume it. Emitting DWARF means we automatically get
debugger support as a free side effect.

### Sections to emit

| Section | Purpose | Size | Minimum complete? |
|---|---|---|---|
| `.debug_line` | PC → file:line lookup table | medium | **REQUIRED** |
| `.debug_info` | Function names, types, lexical scopes | large | required for `bt` |
| `.debug_abbrev` | Abbreviation table that `.debug_info` references | small | required (companion of .debug_info) |
| `.debug_str` | String pool for the above | medium | required |
| `.debug_aranges` | PC range → CU mapping (lookup index) | small | optional but useful |
| `.eh_frame` | Call frame info for stack unwinding | medium | required for runtime unwinding |
| `.debug_loc` | Variable locations | large | skip for MVP |

The minimum-useful set is **`.debug_line` + `.debug_info` + `.debug_abbrev`
+ `.debug_str`**. With these, `addr2line --functions --inlines binary 0x401234`
returns `make_greeting at greeting.luna:5`.

`.eh_frame` is what lets the **runtime** unwind the stack at panic time.
This is the harder piece — see Option B below.

### Implementation in bootminor

Each .luna source file becomes one DWARF "compilation unit" (CU). For each
CU, emit:

1. **`.debug_abbrev` entry** describing the layout of CU DIEs:
   - `DW_TAG_compile_unit` with `DW_AT_name`, `DW_AT_low_pc`, `DW_AT_high_pc`, `DW_AT_stmt_list`.
   - `DW_TAG_subprogram` with `DW_AT_name`, `DW_AT_low_pc`, `DW_AT_high_pc`, `DW_AT_decl_file`, `DW_AT_decl_line`.
2. **`.debug_info` entry** referencing the abbrev above, with actual data.
3. **`.debug_line` program**: opcodes that walk the (file_idx, line, PC)
   tuples. Standard opcodes `DW_LNS_advance_pc`, `DW_LNS_advance_line`,
   `DW_LNS_copy`. RFC: DWARF 5 §6.2.
4. **`.debug_str`** : the actual file/function name strings.

**Effort**: ~600 LoC in [src/bootminor/gen.luna](../src/bootminor/gen.luna).
Tedious binary encoding (ULEB128, SLEB128, abbrev tables) but mechanical.

The pre-existing v3 legacy compiler has a DWARF emitter skeleton at
[legacy/v3-interpreter/forge_dwarf.luna](../legacy/v3-interpreter/forge_dwarf.luna)
(see the project's pending legacy reorganization). It targets the same
spec; review it for table layouts and constants before writing fresh.

---

## Option B: Custom "named addresses" table

Skip DWARF. Emit a custom Luna-specific table at the end of the binary:

```
[
  { pc_lo: 0x401000, pc_hi: 0x40103a, name: "fn make_greeting", file: "greeting.luna", line: 5 },
  { pc_lo: 0x40103a, pc_hi: 0x401087, name: "fn greet_run",     file: "handlers/greet.luna", line: 4 },
  ...
]
```

At process start, the runtime can `mmap` itself, find this table at a
fixed offset (e.g. emitted via a custom section header pointed to by
`__luna_pcinfo` linker symbol or stuffed in the data segment), and use
it for panic traces.

**Pros**:
- Much simpler than DWARF. ~150 LoC of codegen + ~80 LoC of runtime.
- No dependency on `gdb`/`addr2line` — the binary is self-describing.
- The runtime can pretty-print the trace directly, in colors, with
  proper formatting.

**Cons**:
- No `gdb` integration; debugger sees only hex addresses.
- No `perf` annotation; profilers can't symbolize.
- We have to maintain our own format forever.

### Recommendation
**Do Option B first** (1-day implementation), then add Option A later
(week of work) for debugger interop. Option B alone closes the
production hot-path use case ("print a useful trace when something
crashes"); Option A is for developer ergonomics.

---

## Stack walking — the hard part

Whichever option above we choose, at panic time we have to walk the
call stack to know which PCs to look up.

### Sub-option: frame-pointer walking

If bootminor emits `push rbp; mov rbp, rsp` at every function prologue
(it currently **does not** — verify by `objdump -d luna-mini.elf | head`),
then walking is trivial:

```
rbp_0 = current %rbp
while rbp_0 != 0:
  saved_rbp = *(uint64*)(rbp_0)
  return_addr = *(uint64*)(rbp_0 + 8)
  print pc_info(return_addr)
  rbp_0 = saved_rbp
```

**Effort**: ~100 LoC. Codegen change: ensure every function prologue saves
rbp (small bootminor patch). Runtime change: a few dozen lines of inline
assembly via a `panic_with_trace()` intrinsic.

The cost is +3 bytes per function for `push rbp` + +3 for `mov rbp, rsp`,
plus losing %rbp as a general-purpose register (minor).

### Sub-option: `.eh_frame` (DWARF CFI)

Standard Linux uses `.eh_frame` (a Call Frame Information table) for
unwinding without a frame pointer. The compiler emits "every X bytes of
code, the offset of the return address relative to %rsp is Y." A
runtime library (`libgcc_s` or `libunwind`) interprets this.

**Effort**: ~400 LoC codegen + dependency on libgcc_s/libunwind (FFI).

**Recommendation**: Frame-pointer walking for MVP. `.eh_frame` is what
production-grade systems eventually need, but it doubles the work
without doubling the value.

---

## Panic mechanism

Today Luna has no `panic`. Errors propagate via `Result[T, E]` (Tier 5
work) or `int` return codes. For panic to mean anything, we need a
language-level `panic(@msg)` that:

1. Prints `@msg` to stderr.
2. Walks the stack.
3. For each PC, looks it up in the address table.
4. Prints `file:line` + function name for each frame.
5. Aborts via `sys_exit(1)`.

This is the right syscall sequence: do NOT try to recover. Panic means
"unrecoverable, get me a trace and die."

There's already a `_die` pattern in some stdlib modules — they print to
stderr and `return -1`. That's the wrong shape for true unrecoverable
errors (callers can ignore the -1). Panic should be a separate intrinsic.

### Bootminor work
Add `panic(@msg: int) -> int` as a builtin that:
- Pushes a call to a runtime helper `__luna_panic_impl` (linked into
  every binary).
- The helper does the stack walk + table lookup + exit.

**Effort**: ~30 LoC bootminor (the panic intrinsic) + ~150 LoC
prelude/runtime (the helper).

---

## Phased plan

| Phase | Scope | Effort | Unlocks |
|---|---|---|---|
| **P1** | Frame-pointer emission: every function prologue does `push rbp; mov rbp, rsp`. Verify fixed-point self-host still passes. | S (~50 LoC bootminor) | Stack walking foundation |
| **P2** | Custom PC→info table: at end of code section, emit `(pc_lo, pc_hi, file_idx, line, fn_name_idx)` records. String pool for file and function names at end of data section. Two new program globals point to the table and string pool. | M (~150 LoC bootminor) | Static trace data |
| **P3** | `panic(@msg)` builtin + runtime helper: walks rbp, looks up each return-addr in the table, prints `file:line  fn_name`. | M (~150 LoC bootminor + prelude) | **End-user value lands here.** Panic prints a trace. |
| **P4** | Signal handlers for SIGSEGV / SIGFPE: catch fatal signals, call the same trace helper. Requires `sa_restorer` trampoline (also on the roadmap for 1.8). | M (~200 LoC) | SEGFAULT traces |
| **P5** | DWARF emission: `.debug_line` + `.debug_info` + `.debug_abbrev` + `.debug_str`. Coexists with the custom table — both are written. | L (~600 LoC bootminor) | `gdb` / `addr2line` / `perf` |
| **P6** | `.eh_frame` for libunwind-compatible unwinding. Allows other tools (Sentry SDK, async profilers) to walk our stacks. | L (~400 LoC) | Pro-grade integrations |

**Recommended ship sequence**: P1 → P2 → P3 in one focused session
(~half a day total). P4 next session. P5/P6 later when there's clear
demand (e.g. someone asks for gdb support).

---

## Self-host fixed-point risk

Adding frame-pointer prologues changes every function's byte sequence.
The fixed-point check (`run_tests_m3.sh`) WILL break the first time you
flip this on. Plan:

1. Add the frame-pointer emission gated behind a compile flag
   `LUNA_FRAME_POINTERS=1`.
2. Rebuild bootminor with that flag → produces a new `luna-mini2`.
3. Run fixed-point check: `luna-mini2` compiles itself → `luna-mini3`,
   which must be byte-identical to `luna-mini2`. (Both have prologues.)
4. Once that passes, flip the default and update the committed
   `luna-mini.elf`.

The first commit is "luna-mini.elf is bigger by N bytes because every
function has a prologue now." Document why; CI accepts the new size.

---

## What an end user sees today

```
$ luna run app.luna
[crash: program exited with status 139]
```

That's it. Status 139 = 128 + SIGSEGV (11). No location, no function,
no source line.

After P1-P3 lands:

```
$ luna run app.luna
panic: nil dereference
  at db.luna:42  fn get_user(@id)
  at handlers/login.luna:11  fn handle_login(@req)
  at main.luna:3  fn main()
[exit 1]
```

That's the entire point. Everything past P3 is "nicer tools." P3 alone
is the difference between "production-deployable" and "not."

---

## Open questions for the implementation session

1. **Frame pointer cost** — measure: how many bytes bigger does
   `luna-mini.elf` get? Likely ~5-10% (a few KB). Probably acceptable.
2. **rbp clobbering** — bootminor currently uses `rbp` for what? Check
   gen.luna; we may need to spill it. (`grep -n 'rbp\|%rbp' src/bootminor/gen.luna`.)
3. **Inlining** — if bootminor inlines functions later, the PC→info
   table needs to handle "inlined into X at Y". For MVP, no inlining,
   so it's a non-issue.
4. **Function name strings** — emit just the unqualified `fn name`, or
   include the module name (`db.luna#get_user`)? Module-qualified is
   nicer in traces. Bootminor knows the module at compile time.
5. **PC table format** — sorted by pc_lo for binary search at runtime.
   ~24 bytes per entry × ~1000 functions = ~24 KB per binary. Fine.

---

## Until this lands

Today's workaround is **defensive coding plus log_error** at every
boundary that might fail:

```luna
@user = db_get_user(@id)
if @user == 0
    log_error("get_user returned 0 for id", @id)
    return -1
```

This adds 4 lines per call site and the user STILL doesn't know which
call site logged. Cumulative noise; we should ship P3 before any real
production deployment uses Luna for backend work.
