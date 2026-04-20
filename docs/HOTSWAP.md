# Luna Hot-Swap Protocol v0.1 (MVP)

**Status**: draft, Linux-only, function-level granularity.

Live-edit code in a running Luna process without restart. Compile a
single function, send the new x86-64 bytes over a Unix domain socket,
and Luna's runtime atomically swaps the function-pointer slot in an
in-memory dispatch table. Callers already on the stack keep running
the old code; subsequent calls reach the new code.

Inspired by Erlang hot code upgrade and Smalltalk image patching, but
targeted at a natively-compiled systems language — no VM, no GC.

---

## 1. Binary layout

A Luna binary compiled with `--hotswap` contains a **function dispatch
table** and an accompanying **name index** in its data segment:

```
.data layout (at compile time, written by main2.luna):

offset 0           heap_top (8 bytes)              unchanged
offset 8           hotswap_table_vaddr (8 bytes)   address of
                                                   FnSlot[0]
offset 16          hotswap_count (8 bytes)         N
offset 24          hotswap_name_ptr (8 bytes)      address of name
                                                   pool
offset 32..         reserved                       8 bytes
offset 40..         string literals, const data    as before
...

later:              FnSlot[N] area (N * 16 bytes)
                    each slot = { name_off: u32; pad: u32; code_ptr: u64 }
                    `name_off` is the byte offset into the name pool
                    for the function's NUL-terminated name.

later still:        name pool — NUL-terminated ASCII function names,
                    packed. Only user-defined functions appear here;
                    intrinsics and runtime helpers are not hot-swappable.
```

The binary is still a valid ELF64. The extra table lives inside the
existing PT_LOAD segment.

## 2. Calls emitted in `--hotswap` mode

In default (non-hotswap) compiles, bootminor emits `call rel32` to a
concrete code offset. In hotswap mode, every user-defined function call
is emitted as:

```
call qword [rip + disp32]
```

Where `disp32` points at the `code_ptr` field of the target function's
FnSlot. The initial value of every `code_ptr` points at the function's
in-binary code (same offset a direct call would use). The CPU takes a
single extra indirect-memory read — ~1–2 cycles on modern x86-64.

Builtins (`shine`, `str_*`, `u8_at`, syscalls, SSE helpers) remain
direct — they are compiler intrinsics and are not present in the
dispatch table.

## 3. Runtime control API

Exposed via `bootminor_prelude.luna`:

```luna
# Start a listener on @socket_path (Unix domain socket). Blocks until
# the first connection, then services patch requests in-place on the
# caller thread. Returns when the client disconnects.
fn hot_listen(@socket_path: int) -> int

# Install a single function patch. @name is the NUL-terminated Luna
# function name. @code is a heap-owned blob of x86-64 bytes; the
# runtime mmaps a fresh R+X page, copies the bytes there, and stores
# the new page's address into the corresponding FnSlot.code_ptr.
# Returns 0 on success, negative on error.
fn hot_install_patch(@name: int, @code: int, @code_len: int) -> int

# Lookup helpers.
fn hot_find_slot(@name: int) -> int        # slot index or -1
fn hot_current_ptr(@name: int) -> int      # current code pointer
```

## 4. Wire protocol

Binary, framed. All integers little-endian.

```
message := header payload
header  := magic:u32  op:u8  reserved:u8  name_len:u16  code_len:u32
magic   := 0x4C4E4831   ('LNH1')

INSTALL message (op = 1):
    magic      = 0x4C4E4831
    op         = 1
    reserved   = 0
    name_len   = length of function name in bytes (no NUL)
    code_len   = length of x86-64 code blob in bytes
    name_bytes = <name_len> bytes, raw UTF-8
    code_bytes = <code_len> bytes, raw machine code

Reply (server → client): 4 bytes
    0x00000000  → OK
    0x00000001  → unknown function name
    0x00000002  → mmap failed
    0x00000003  → permission denied / mprotect failed
    0x000000FF  → protocol error
```

## 5. Code-blob constraints (MVP)

The injected code must be **position-independent** and
**self-contained**:

- No calls to other user-defined functions. (Reason: calls through the
  dispatch table use RIP-relative disp32 that was resolved at original
  compile time; re-resolving in a patch blob needs relocations.)
- May call runtime intrinsics (syscalls are plain `0f 05` opcodes).
- May access data-section strings only via RIP-relative LEA baked into
  the blob's own constants.
- Frame size must be compatible with the call site: any local slots
  are private to the function.

Planned post-MVP: relocatable patches with a `{relocs: [name, offset,
kind]*}` block in the wire protocol, processed against the live
FnSlot table.

## 6. Atomicity & safety

- **Function-pointer store is atomic.** On x86-64 a naturally-aligned
  8-byte store is atomic, so a single `mov [rip+disp], rax` flips all
  future callers in a thread-safe way.
- **In-flight calls are NOT interrupted.** Callers currently on the
  stack in the old body finish normally using the now-orphaned old
  page. The runtime keeps old pages alive indefinitely (MVP; future
  versions add epoch-based reclamation after the stack drains).
- **Signal/thread safety**: `hot_install_patch` calls `mmap` +
  `mprotect` and then a single word store. No locks needed for the
  single-writer case. Multi-writer callers should serialise at the
  application level.

## 7. Example session

Terminal 1 (the running service):
```sh
./my-service.elf
# starts, prints "hello v1" every second via greet()
```

Terminal 2 (live edit):
```sh
# Edit greet() in my-service.luna, save.
bash src/bootminor/luna-hot send greet my-service.luna
# → INSTALL message sent, runtime patches in <1 ms.
# Next tick prints "hello v2".
```

No restart. No dropped connections. No lost state.

## 8. CLI tool: `luna-hot`

See `src/bootminor/luna-hot` — a small wrapper that:

1. Compiles a source file into a standalone function blob.
2. Opens the socket exposed by the running service.
3. Sends an `INSTALL` frame per patched function.
4. Reports `OK` / error.

## 9. Next milestones

- **luna-hot watch**: file-system watcher (inotify / FileSystemWatcher)
  that auto-recompiles + auto-sends on save.
- **Relocatable patches**: calls to other user fns via table lookup
  resolved at install time.
- **Struct-layout migration**: hooks to rewrite in-memory data when
  struct fields change.
- **Multi-process fanout**: one compile → N running processes.
- **Safe downgrade**: keep last N versions per function; `luna-hot
  revert` rolls back.
- **Windows PE64 port**: `CreateFileMappingA` + `VirtualProtect`
  equivalents.
