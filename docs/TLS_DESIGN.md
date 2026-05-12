# TLS in-process: design

Roadmap item **1.3** — replace the `openssl s_client` subprocess in
[std/ext/https.luna](../std/ext/https.luna) with in-process TLS. This is
the single hardest gap between Luna and production-grade backend work.

This document is a **design**, not an implementation. It captures the
state of the codegen, the two paths forward, the trade-offs, and a
phased plan for execution. Read it before opening a session for the
actual TLS work.

---

## Why this is hard

Bootminor today produces **fully static ELF** binaries. From `readelf -lW
luna-mini.elf`:

```
Type:     EXEC (Executable)
Program headers: 1
  LOAD  0x000000  0x400000  RWE  0x4039b5a
Section headers: absent
```

One load segment. No `PT_INTERP`. No `PT_DYNAMIC`. No section header
table. This is intentional — the bootstrap compiler is minimalist and
emits the smallest possible ELF that the kernel will run.

The flip side: **bootminor's `extern "C" fn sys_xxx(...)` is a fake.**
Look at [src/bootminor/gen.luna:911](../src/bootminor/gen.luna#L911)
(`_emit_syscall_intrinsic`). When you write `extern "C" fn sys_socket(...)`,
the compiler doesn't emit a real shared-library call — it emits a Linux
`syscall` instruction with the corresponding number. There is no linker,
no PLT, no GOT, no `DT_NEEDED`. The prelude's `extern "C"` declarations
are syntactic sugar for raw kernel calls.

So **calling `SSL_new` or `sqlite3_open` from Luna today is impossible.**
There's no runtime mechanism for it. Closing this gap is what 1.2 (SQLite
FFI) and 1.3 (TLS) both need.

---

## What we need to add to bootminor

A real dynamic-linked ELF, big enough to invoke a libssl call:

| Component | Bytes | Where it points |
|---|---|---|
| `PT_INTERP` program header + body | ~30 | `/lib64/ld-linux-x86-64.so.2` |
| `PT_DYNAMIC` segment | ~200 | Holds `DT_NEEDED`, `DT_STRTAB`, `DT_SYMTAB`, `DT_HASH`/`DT_GNU_HASH`, `DT_RELA`/`DT_JMPREL`, `DT_PLTGOT`, `DT_PLTRELSZ`, `DT_PLTREL`, `DT_NULL` |
| `.dynstr` (string table) | variable | `"libssl.so.3\0libcrypto.so.3\0libc.so.6\0SSL_new\0SSL_connect\0...\0"` |
| `.dynsym` (symbol table) | 24 × N | One entry per imported function, type `STT_FUNC`, binding `STB_GLOBAL`, section `SHN_UNDEF` |
| `.gnu.hash` or `.hash` | ~32+ | Hash table over .dynsym (ld-linux needs one) |
| `.rela.plt` (PLT relocations) | 24 × N | `R_X86_64_JUMP_SLOT`, one per imported function |
| `.plt` (procedure linkage table) | 16 + 16 × N | Trampoline stubs |
| `.got.plt` (PLT GOT) | 24 + 8 × N | First three slots reserved (link map, resolver), then one per func |

A minimal "hello via libc puts" dynamic ELF is around 4-8 KB of structure
plus the actual code. For libssl + libsqlite3 with maybe 30 imported
symbols total, we're at ~10-15 KB of dynamic metadata.

Bootminor currently emits a flat code segment with the data section
appended. The new emission needs:

1. **Two program headers** instead of one: a `PT_LOAD` for code (R+X)
   and another `PT_LOAD` for the data+got (R+W), so libc/ld-linux's
   relro/got writes don't trip the W^X check.
2. **`PT_INTERP`** pointing to the dynamic linker.
3. **`PT_DYNAMIC`** pointing to the `.dynamic` table.
4. A **section header table** (so `readelf` / `gdb` work), with the
   above sections referenced.
5. **PLT call codegen**: `call sym@PLT` resolves to `call <reladdr>`
   where the PLT stub does the GOT indirection. The codegen needs a
   new `_emit_call_plt(@g: Gen, @sym_idx: int)` to emit
   `call rel32` with a relocation pinning rel32 to the PLT slot offset.

**Estimated work in bootminor**: ~600–800 lines added to gen.luna.

---

## Two paths

### Path A: dlopen via libdl

Once bootminor can produce a dynamic ELF that pulls in `libc.so.6`
(needed anyway for `__libc_start_main` if we use a normal entry point,
or skipped if we keep the bare `_start`), we can also `DT_NEEDED libdl.so.2`
and call `dlopen`/`dlsym` to load libssl at runtime.

```luna
# std/ext/dl.luna (new T6 module)
extern "C" fn dlopen(@path: int, @flags: int) -> int
extern "C" fn dlsym(@handle: int, @sym: int) -> int

# Then:
@libssl = dlopen("libssl.so.3", 2)   # RTLD_NOW
@SSL_new = dlsym(@libssl, "SSL_new")
# Indirect call through @SSL_new...
```

**Pros**:
- Doesn't pin libssl version at compile time.
- One mechanism (`dl`) handles libssl, libsqlite3, libpq, anything else.
- The graceful-degradation pattern works: if libssl isn't installed,
  skip with a clear error.

**Cons**:
- We still need bootminor to support **indirect calls** through a
  function pointer (`call qword [rax]`). Today bootminor only emits
  direct `call rel32`. Adding indirect call is small (~30 lines).
- Type safety is gone — each `dlsym` returns `int` and the caller
  must know the signature. Common pattern in C; Luna would replicate it.

### Path B: direct linking to libssl

Skip dlopen entirely; have the bootminor-produced ELF carry
`DT_NEEDED libssl.so.3` and call `SSL_new@PLT` directly.

**Pros**:
- Simpler code at the call site: `let @ssl: int = SSL_new(@ctx)`.
- The dynamic linker fails to start the binary if libssl is missing —
  clear error at process load, not at dlopen-time.

**Cons**:
- Pins us to a specific libssl version (`libssl.so.3` won't load on a
  box that only has `libssl.so.1.1`). dlopen lets us probe.
- Every linked `.so` adds a couple kB to every Luna binary, even
  hello-world.

### Recommendation

**Do Path A (dlopen).** The flexibility wins. The bootminor work is the
same effort either way — once we can emit dynamic ELF, dlopen is just
two extra symbols (`dlopen`, `dlsym`) and indirect-call codegen.

---

## Path C (very long-term): pure-Luna TLS

Port [BearSSL](https://bearssl.org) or a minimal TLS 1.3-only stack to
Luna. No FFI, no shared library, no runtime dependency.

**Pros**:
- Truly self-contained binaries.
- Cross-compiles to Windows / macOS trivially (no libssl on Windows).
- Audit surface is in our hands.

**Cons**:
- BearSSL is ~30 KLoC of C. A port — even a stripped TLS-1.3-only one —
  is ~10 KLoC of Luna. **Months of work**, not days.
- Crypto primitives (AES, ChaCha20, P-256, X25519, RSA, SHA-2) are
  another ~3-5 KLoC.
- Constant-time correctness is hard to get right in any new language.
  Side-channel guarantees are easy to break.

**Recommendation**: park it. Revisit when Path A is in production and
we've earned the right to engineer a self-hosted TLS stack from a
position of strength.

---

## Phased plan

| Phase | Scope | Effort | Unlocks |
|---|---|---|---|
| **P1** | Bootminor emits two LOAD segments + PT_INTERP + dynamic table with one `DT_NEEDED libc.so.6` and one libc symbol (`puts`). Hello-world dynamic ELF compiles. | M (~400 LoC bootminor) | Foundation |
| **P2** | `extern "C" fn` syntax for **user-imported functions** (not just syscalls). PLT codegen. Multiple `DT_NEEDED`. | M (~200 LoC bootminor) | Foundation |
| **P3** | Indirect-call codegen + `dlopen`/`dlsym` declared in a new `std/ext/dl.luna`. Works via `DT_NEEDED libdl.so.2`. | S (~80 LoC bootminor + 50 LoC std) | dlopen works |
| **P4** | `std/ext/db/sqlite.luna` rewritten to use dlopen(libsqlite3) instead of subprocess. Replaces 1.1. | M (~400 LoC) | SQLite FFI (Roadmap 1.2) |
| **P5** | `std/ext/tls.luna` — dlopen(libssl) wrapping `SSL_CTX_new` / `SSL_new` / `SSL_connect` / `SSL_read` / `SSL_write`. Outbound HTTPS via in-process TLS. | L (~500 LoC) | Outbound TLS done |
| **P6** | `std/ext/https.luna` deletes the openssl-subprocess path, calls into tls.luna. | S (~50 LoC) | https.luna no shim |
| **P7** | Inbound HTTPS: `tls_accept(@fd, @cert, @key)`. http_server can serve HTTPS. | M (~300 LoC) | TLS server (Roadmap 1.3 done) |

**Total**: roughly 6-8 weeks of solo work; can be compressed to ~3-4 with
parallel agents on the test surface.

---

## Self-host fixed-point risk

Bootminor compiles itself byte-identically (`run_tests_m3.sh` enforces
this). Adding dynamic ELF emission means **every change has to keep
the fixed-point invariant**. The recipe:

1. Implement the new codegen for **emit-time choice**: a flag selects
   static (today's path) vs dynamic (new path).
2. luna-mini.elf itself stays compiled with the static path. The fixed-point
   check still passes.
3. User code opting into dlopen passes `--target dynamic` (or has any
   `extern "C" fn` with a `# from: libfoo` annotation that triggers the
   dynamic path).
4. Once everything is comfortable, **gradually** flip the default and
   tighten the fixed-point check to cover dynamic.

The phase-gate is critical: do not flip the default until P5 lands and
the dynamic path has burned in via at least two stdlib modules
(sqlite + tls).

---

## Compatibility with `--target windows` (M14)

Windows PE64 has its own dynamic-linking story (Import Address Table —
IAT, `kernel32.dll`/`ws2_32.dll`/`schannel.dll`). The dlopen analogue
on Windows is `LoadLibraryA` + `GetProcAddress`. The good news: the
codegen extension is **target-conditional**. Both ELF and PE add the
same kind of pieces (interp/IAT, dynamic table/Import directory, GOT/IAT,
PLT/IAT trampolines).

Schedule TLS for Linux first; once dlopen works on Linux, the Windows
port is mostly a different table format with the same conceptual moves.

---

## What to read in this codebase before starting

1. [src/bootminor/gen.luna:823-948](../src/bootminor/gen.luna#L823) —
   `_em_syscall` + `_emit_syscall_intrinsic`. The current "fake FFI"
   path. New code lives alongside.
2. [src/bootminor/gen.luna:4250-4315](../src/bootminor/gen.luna) — the
   final ELF header / program header emission. The dynamic table goes
   here.
3. [src/bootminor/luna-mini.elf](../src/bootminor/luna-mini.elf) — open
   in `readelf -aW` to see the absolute-minimum baseline.
4. Any small dynamic ELF on the host: `readelf -aW /bin/true` is a good
   reference for what a fully-loaded dynamic ELF looks like.

---

## Open questions for the implementation session

1. **`__libc_start_main` or bare `_start`?** A bare `_start` skips libc
   initialization. dlopen needs `__libc_start_main`, so we have to take
   the libc init hit (~few hundred μs at process start) once we go
   dynamic.
2. **ELF section header table — emit or skip?** ld-linux works without
   it, but `gdb` and `nm` want one. Recommend emitting a minimal SHT
   (it's almost free once we have the section names).
3. **Per-binary `# ffi:` declaration aggregation.** A module declares
   `# ffi: libssl, libcrypto`; the linker pass collects them into one
   `DT_NEEDED` list at link time. We need a place to centralize this.
4. **dlopen flags semantics.** Default `RTLD_NOW | RTLD_GLOBAL`? Or
   `RTLD_LAZY`? `RTLD_NOW` is safer (errors at dlopen time, not at first
   call).
5. **glibc vs musl.** Pin to glibc for now (`/lib64/ld-linux-x86-64.so.2`).
   musl support (`/lib/ld-musl-x86_64.so.1`) is a one-line interp change.

---

## Until this lands

The current shim ([std/ext/https.luna](../std/ext/https.luna)) is
**fine for non-hot-path use**: cron-style outbound requests, occasional
API calls, scripting. The shim's cost is dominated by `fork+exec
openssl` — about 10-50 ms per request, plus the inability to keep
connections alive between requests.

For anything serving real traffic, the workaround is **terminate TLS
at nginx/caddy in front of Luna**, then Luna serves HTTP/1.1 over
plaintext on a localhost socket. This is the current Luna prod recipe
and stays the recipe until P7 lands.
