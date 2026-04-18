# Luna C Bootstrap Compiler

`luna_bootstrap.c` is a minimal, single-file C compiler for a tiny subset of
Luna.  Its **only** job is to break the chicken-and-egg problem that every
self-hosted language faces: you cannot build the self-hosted `luna` compiler
without an existing `luna` binary, so on a truly fresh machine somebody has to
write that first binary in a language that is already available.  That
"somebody" is this file.

After the first run, the C bootstrap is never used again — every subsequent
`luna` is built with the previous `luna`.  Accordingly this bootstrap is
deliberately shallow, ugly in places, and not shipped in any release.

## Pipeline

```
luna_bootstrap.c  --cc-->  luna-boot   (C executable)
         luna-boot src/core/main.luna --> luna   (self-hosted ELF64)
                     luna *.luna --> every future luna
```

## Build

```sh
cd bootstrap
make              # -> ./luna-boot
```

The Makefile is a one-liner wrapping `cc -O2 -std=c99 -o luna-boot luna_bootstrap.c`.
No dependencies beyond a C99 compiler and the three standard headers
(`stdio.h`, `stdlib.h`, `string.h`, `stdint.h`) plus POSIX `unistd.h`.

## Use

```sh
./luna-boot ../src/core/main.luna -o ../luna
../luna --version                   # from here on use the self-hosted compiler
```

Flags:

| flag | meaning                                                   |
|------|-----------------------------------------------------------|
| `-o` | output path (default: `a.out`)                            |
| `-v` | verbose: phase timings, token/node counts                 |

## Scope — what this bootstrap supports

Enough to compile `src/core/main.luna` and the modules it transitively imports.

Supported:

- Types: `int` (i64), `str` (byte pointer), `bool`, fixed arrays `[int; N]`,
  raw pointers (`*int`)
- Declarations: `fn`, `const`, module-level `meow`, `struct` with named fields
- Statements: `let`, local `meow`, `return`, `if`/`else`/`eclipse`, `while`,
  `orbit @i in 0..N`, `break`, `continue`, `pass`
- Expressions: integer / string / bool literals, `@ident`, binary
  `+ - * / == != < <= > >= && ||`, unary `not`, field access `@a.b`,
  function calls, postfix `?` (phase propagator: emits
  `cmp byte [rax],0; jne .dim; mov rax,[rax+8]; jmp .done; .dim: epilogue; .done:`)
- `extern "linux_syscall" fn sys_...(...)` — lowers to
  `mov rax, <nr>; syscall` with SysV → Linux syscall register remap
  (R10 replaces RCX for the 4th argument)
- `import X` — look up `X.luna` in `src/core/` then `src/stdlib/`, parse
  recursively, de-duplicate

Explicitly **not** supported by the bootstrap (diagnosed with
`luna-boot: unsupported: <feature> at <file>:<line>` and `exit(1)`):

- Generics (`<T>`)
- Traits, `impl`, dynamic dispatch
- `match` / `phase` pattern matching (use `if/else` chains)
- `actor`, `flow`, `spawn`, `send`, `defer`  → `int3` stub
- Float arithmetic             → `int3` stub
- `nova` / async               → `int3` stub
- `extern "C"` calls           → `int3` stub with TODO marker

These are all fully supported by the **self-hosted** `luna` compiler that
this bootstrap produces — the restriction is only for the 1500-line C
program you are reading now.

## After bootstrap

Once `luna` exists, you can forget this directory.  Re-running
`make` is harmless and idempotent, but the produced `luna-boot` should
never be used to build a production binary — its output is functionally
correct but **not** optimized and does **not** exercise the full language.

## Why a single .c file?

- Auditability: a reader can scroll through the whole compiler in one sitting.
- Portability: any C99 compiler on any POSIX box builds it.
- Trust: no build system, no package manager, no transitive dependencies —
  the attack surface between "clean machine" and "working compiler" is one
  `cc` invocation.
