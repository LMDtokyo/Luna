# Luna Quickstart

Get from `git clone` to a working Luna compile/run cycle in under five
minutes. No prior Luna experience assumed.

---

## 1. Requirements

- **Linux x86_64** (native) **or Windows 10/11 with WSL Ubuntu**.
  `luna-mini.elf` is a native Linux ELF64 binary — on Windows it runs
  under WSL.
- A C toolchain (`cc`, `clang`, or `x86_64-w64-mingw32-clang`) — only
  needed for **Path B** below, i.e. first-time builds on machines that
  did not already ship with `src/bootminor/luna-mini.elf`.
- No Rust, Go, Cargo, or LLVM. No package manager.

Check:

```sh
$ uname -m
x86_64
$ cc --version        # only for Path B
gcc (Ubuntu ...) 13.2.0
```

## 2. Get the binary

### Path A — use the shipped `luna-mini.elf`

The repo already ships a self-compiled `src/bootminor/luna-mini.elf`
(169 446 bytes). No C compiler needed.

```sh
$ cd Luna-main
$ chmod +x src/bootminor/luna-mini.elf
$ src/bootminor/luna-mini.elf --help 2>&1 | head -1
usage: luna-mini <input.luna> -o <output.elf>
```

On Windows, run this from a WSL Ubuntu shell (`wsl -d Ubuntu`).

### Path B — build from source

If `luna-mini.elf` is missing or you want to rebuild it end-to-end:

```sh
# 1. Build the C bootstrap (single C99 TU, ~5.9 KLOC, no deps).
$ cc -O2 -std=c99 -o bootstrap/luna-boot bootstrap/luna_bootstrap.c

# 2. Use bootstrap to produce luna-mini.elf by self-compile.
$ bash src/bootminor/selfhost_build.sh
fixed-point OK (stage1 = stage2)
rebuilt luna-mini.elf matches shipped copy — no update needed
selfhost_build.sh: OK
```

`selfhost_build.sh` runs the shipped `luna-mini.elf` against the
concatenated `bootminor_prelude.luna + lex.luna + gen.luna +
main2.luna`, then feeds the result back in to check the binary is a
byte-identical fixed point.

## 3. Hello world

Create `hello.luna`:

```luna
fn main() -> int
    shine("Hello, Luna!")
    return 0
```

Compile and run:

```sh
$ ./src/bootminor/luna-mini.elf hello.luna -o hello.elf
$ chmod +x hello.elf
$ ./hello.elf
Hello, Luna!
$ ls -l hello.elf
-rwxr-xr-x 1 user user ~4200 hello.elf
```

A trivial Luna program compiles to roughly 4 KB of standalone Linux
ELF64. No interpreter, no shared libraries, no runtime startup — just
`_start → main → sys_write → sys_exit`.

## 4. A slightly larger program

Fibonacci by recursion — demonstrates `fn` declaration, typed
parameters, and recursive calls. Taken directly from
`src/bootminor/tests_m2c/02_recursion_fib.luna`:

```luna
fn fib(@n: int) -> int
    if @n < 2
        return @n
    return fib(@n - 1) + fib(@n - 2)

fn main() -> int
    shine_int(fib(10))
    shine_int(fib(20))
    return 0
```

Compile + run:

```sh
$ ./src/bootminor/luna-mini.elf fib.luna -o fib.elf
$ chmod +x fib.elf && ./fib.elf
55
6765
```

Variables prefix with `@`. Functions take typed params and return
types via `-> T`. `shine` prints strings; `shine_int` prints signed
integers followed by a newline.

## 5. Structs and pointers

Bootminor supports structs by value and `*mut T` pointers into them.
Adapted from `tests_types/t_ptr_write.luna`:

```luna
struct Point
    x: int
    y: int

fn main() -> int
    let @pt: Point = Point { x: 7, y: 11 }
    let @px: *mut int = &@pt.x
    *@px = 100
    shine_int(@pt.x)
    shine_int(@pt.y)
    return 0
```

```sh
$ ./src/bootminor/luna-mini.elf point.luna -o point.elf
$ chmod +x point.elf && ./point.elf
100
11
```

`&@pt.x` takes the address of a struct field; `*@px = 100` writes
through the pointer. `let` binds immutably by default, `meow` is
mutable, `seal` is one-shot.

## 6. Raw memory — `u8_at`, `u32_at`, `bswap`

Byte-level access to heap buffers, useful for ELF/PE/network parsing.
Compressed from `tests_types/t_memory.luna` and `t_bits.luna`:

```luna
fn main() -> int
    meow @buf: int = new_str(16)
    u8_set(@buf, 0, 0x7f)
    u8_set(@buf, 1, 0x45)
    u8_set(@buf, 2, 0x4c)
    u8_set(@buf, 3, 0x46)
    u32_set(@buf, 4, 0x0102034f)

    shine_int(u8_at(@buf, 0))
    shine_int(u32_at(@buf, 4))
    shine_int(bswap32(0x11223344))
    shine_int(popcount(0xFF))
    return 0
```

```sh
$ ./src/bootminor/luna-mini.elf mem.luna -o mem.elf
$ chmod +x mem.elf && ./mem.elf
127
16909135
1144201745
8
```

Other primitives in the same family: `u16_at`, `u64_at` and matching
setters, `bswap64`, `clz`, `ctz`, `rotl`, `rotr`, `memset`, `memcpy`,
`sizeof(u8|u16|u32|u64|bool)`.

## 7. File I/O

`read_file(path)` is part of the bootminor prelude — it returns a
heap-allocated string with the file contents, or `0` on error.

```luna
fn main() -> int
    @buf = read_file("hello.luna")
    @n   = str_len(@buf)
    shine("bytes read:")
    shine_int(@n)

    # Print first 16 bytes as decimal codes.
    @i = 0
    while @i < 16
        if @i >= @n
            break
        shine_int(u8_at(@buf, @i))
        @i = @i + 1
    return 0
```

```sh
$ ./src/bootminor/luna-mini.elf readfile.luna -o readfile.elf
$ chmod +x readfile.elf && ./readfile.elf
bytes read:
49
102
110
32
109
97
105
110
40
...
```

`write_file(path, content)` mirrors it on the write side and is also
defined in the prelude.

## 8. Running the test suite

Luna ships an end-to-end self-host test that rebuilds the compiler
through three stages and then runs every M2b + M2c + tests_types test
through the self-compiled binary:

```sh
$ bash src/bootminor/run_tests_m3.sh
[fixed-point] PASS — luna-mini3 = luna-mini4 byte-identical
[mini3 tests_m2b 01_sum_simple] PASS (exit=0)
[mini3 tests_m2b 02_arith_precedence] PASS (exit=0)
...
[mini3 tests_types t_bits] PASS (exit=0)
[mini3 tests_types t_f64] PASS (exit=0)
[mini3 tests_types t_memory] PASS (exit=0)

=== bootminor M3: fixed-point PASS, suite 28 PASS, 0 FAIL ===
```

The three stages:

1. `bootstrap/luna-boot` compiles `bootminor/*.luna` → `luna-mini2`.
2. `luna-mini2` compiles the same sources → `luna-mini3`.
3. `luna-mini3` compiles the same sources → `luna-mini4`.
4. `cmp luna-mini3 luna-mini4` — must match byte-for-byte.

Any divergence (failed fixed point, failed test) is a hard-fail. A
clean run ends with `28 PASS, 0 FAIL`.

## 9. Where to go next

- [`docs/LANGUAGE.md`](LANGUAGE.md) — language reference (grammar,
  types, bindings, operator list).
- [`docs/HOTSWAP.md`](HOTSWAP.md) — the hot-swap protocol v0.1: live
  function replacement over a Unix socket, coming next milestone.
- [`src/bootminor/tests_types/`](../src/bootminor/tests_types) — worked
  examples of every language feature (compound assigns, const-expr,
  casts, SSE `f64_*`, raw memory, pointer writes).
- [`LUNA_SPEC.md`](../LUNA_SPEC.md) — long-form language specification.
- [`examples/stats.luna`](../examples/stats.luna) and
  [`examples/binary_search.luna`](../examples/binary_search.luna) — real
  programs that build under the C bootstrap.

If anything in this guide does not reproduce byte-for-byte, open an
issue with your `uname -a`, the exact command, and the output — Luna
aims for deterministic builds, so diffs are bugs.
