# The Luna Language — Practical Tutorial (bootminor)

> A tour of every feature the current Luna compiler (`src/bootminor/luna-mini.elf`)
> actually supports today. Audience: systems programmers with a C or Rust background.
> Written against commit `84aec44` (M2e). If it's in `LUNA_SPEC.md` but not here,
> it's not implemented yet — see **Current limitations**.

---

## 1. Introduction

Luna is a self-hosted systems language. The `bootminor` compiler shipped at
`src/bootminor/luna-mini.elf` is written in Luna, runs on 64-bit Linux, and emits
static ELF64 binaries that talk directly to the kernel via `syscall`. There is no
VM, no runtime library, no garbage collector, no link step. Every program is a
single PT_LOAD segment: 64-byte ELF header, 56-byte program header, code, data,
and a 64 MiB zero-filled bump heap tacked on via `memsz > filesz`.

The compiler is one token-stream pass that drives a direct x86-64 code emitter
(`src/bootminor/gen.luna`). There is no separate AST; `parse_primary` literally
writes machine bytes into a `StrBuf` as it walks the source. Locals live on the
stack, allocations bump `heap_top` (the first 8 bytes of the data section),
and every runtime value — pointer, int, bool, float bit-pattern — fits in one
64-bit register.

Luna today is small, honest, and debuggable end-to-end: you can read the entire
pipeline (lexer, parser/codegen, driver) in about 4,000 lines of Luna. This
document is the companion to that code.

---

## 2. Hello, Luna

A minimal program:

```luna
fn main() -> int
    shine("Hello, Luna")
    return 0
```

Indentation is significant (off-side rule, tracked by the lexer at
`lex.luna:216-282`). Four-space steps are conventional; tabs count as 4.
`shine` is the one built-in that writes a line to stdout with a trailing
newline — see `gen.luna:2694-2735`.

**Building on Linux** (native):

```
./src/bootminor/luna-mini.elf hello.luna -o hello
./hello
```

**Building on Windows** (through WSL Ubuntu — the shipped compiler is an
ELF64 binary; it does not run natively on Windows):

```
bash src/bootminor/selfhost_build.sh           # rebuilds the compiler
wsl.exe -d Ubuntu ./src/bootminor/luna-mini.elf hello.luna -o hello
wsl.exe -d Ubuntu ./hello
```

`selfhost_build.sh` concatenates `bootminor_prelude.luna`, `lex.luna`,
`gen.luna`, and `main2.luna` into a single monolith (`bootminor.luna`),
compiles it with the current `luna-mini.elf`, then compiles the result *again*
and byte-compares — a full fixed-point check. If both stages match, the
language, runtime, and emitter are all self-consistent.

Compiler invocation is minimal:

```
luna-mini.elf <source.luna> [-o <output>]
```

Argument parsing happens in `main2.luna:87-94`; anything after `-o` is the
output path, default `a.out`.

---

## 3. Primitive types

Every runtime value is 8 bytes. Type names exist only at compile time: they
drive `as`-cast emission (`_emit_cast` in `gen.luna:1152-1198`), `sizeof`,
and struct-field offset lookup. The compiler never emits tagged values, never
inserts implicit coercions, and never checks that two values of compatible
types were actually assigned.

| Category        | Accepted type names                         |
|-----------------|---------------------------------------------|
| Integer (fixed) | `i8 i16 i32 i64 u8 u16 u32 u64`             |
| Integer (word)  | `int isize usize`                           |
| Boolean         | `bool`                                      |
| Float           | `f64` (also accepted: `f32`, treated as 4B for `sizeof`) |
| String          | `str`                                       |
| Pointers        | `*T`, `*mut T`, `*const T`                  |
| References      | `&T`, `&mut T`                              |
| Slice / array   | `[T]`, `[T; N]`                             |

The type parser (`_parse_type`, `gen.luna:2419-2476`) also swallows
`Ident<...>` generic parameter lists and `Ident?` nullable suffixes — they are
parsed and discarded so source written for a richer future syntax can still
compile.

```luna
fn sum(@a: i64, @b: i64) -> i64
    return @a + @b

fn main() -> int
    let @n: u32    = 0xCAFEBABE
    let @p: *mut u8 = new_str(8) as *mut u8
    let @arr: [u8; 8] = [65, 66, 67, 68, 69, 70, 71, 72]
    let @ok: bool  = 1 as bool
    shine_int(@n as i32)
    shine_int(@arr[3])
    shine_int(@ok)
    return 0
```

**Where types do something:**

- `as T` triggers a real mask/extend emission (`and rax, 0xff`, `movsx`,
  etc.) — see `_emit_cast` in `gen.luna:1152-1198`.
- `let @x: StructName = ...` records `StructName` against the local so
  `@x.field` can resolve a field offset (`gen.luna:1412-1433`).
- `sizeof(T)` folds to an immediate based on the type's name
  (`gen.luna:2240-2272`).

**Where they don't:** everything else. There is no type checker. Expression
arithmetic is always `int` (signed 64-bit). A `bool` is indistinguishable
from a `u64` at runtime.

---

## 4. Literals

### Integers

The lexer accepts decimal, hex, binary, and octal with optional `_`
separators (`lex.luna:299-371`):

```luna
let @dec: int = 1_000_000
let @hex: int = 0xDEAD_BEEF
let @bin: int = 0b1010_1111
let @oct: int = 0o755
```

There is no negative-literal token — `-5` is unary minus on `5`, handled
by `parse_unary` (`gen.luna:1200-1204`).

### Floats

Parsed in `lex.luna:378-424` and emitted to SSE by `parse_primary`
(`gen.luna:1292-1393`). Three forms:

```luna
let @pi:    f64 = 3.14159
let @kilo:  f64 = 1e3
let @milli: f64 = 1.5e-6
```

A literal like `3.14` is lowered to: load mantissa as int → `cvtsi2sd xmm0`
→ chain `mulsd` / `divsd` by 10.0 until `total_exp = exp - fraction_digits`
is exhausted. Precision is acceptable for most uses but don't expect
round-trip fidelity of pathological decimal strings — the compiler does
no Dragon4-style rounding.

### Strings

Double-quoted, with the escapes `\n`, `\t`, `\r`, `\0`, `\\`, `\"`
(decoded in `lex.luna:166-198`). Every string literal is heap-allocated
in the data section with an 8-byte little-endian length prefix sitting at
`ptr - 8`:

```luna
shine("Hello, Luna")       # strlen = 11 sits at &"H..." - 8
```

This layout lets `str_len` be a one-instruction intrinsic
(`mov rax, [rax - 8]` — `gen.luna:1624-1634`).

### Array literals

```luna
let @nums: [int; 5] = [10, 20, 30, 40, 50]
let @zeros: [u8; 64] = [0; 64]
```

Both forms inline a bump-allocator call (`_inline_new_arr`,
`gen.luna:914-967`) and then fill the slots. The "repeat" form emits a
tight loop in machine code rather than unrolling.

---

## 5. Bindings

Luna has three binding forms and one module-level constant (`lex.luna:63-74`;
parser in `gen.luna:2478-2533`). The `@` prefix marks *runtime values*;
it is part of the identifier.

| Form      | Writable? | Declares a type? | Notes                                 |
|-----------|-----------|------------------|---------------------------------------|
| `let`     | yes       | optional         | Default local binding                 |
| `meow`    | yes       | optional         | Identical to `let` today; intended for mutable locals |
| `seal`    | **no** — compiler errors on reassignment (`gen.luna:2654-2670`) | optional | One-shot immutable    |
| `const`   | no        | yes (required)   | Compile-time integer constant         |

```luna
fn main() -> int
    let  @a: int = 10
    meow @b: int = 20
    seal @c: int = 30
    shine_int(@a + @b + @c)

    @b = @b * 2                     # OK — meow is mutable
    # @c = 99                       # ERROR: cannot reassign sealed local: @c

    let @bare = 7                   # Type omitted → untyped slot
    shine_int(@bare)
    return 0
```

The example above is a near-copy of `src/bootminor/tests_types/t_binding.luna`
and passes the shipped test runner.

### Bare `@name = expr`

The compiler allows implicit first-use binding for `meow`-like semantics —
any `@name = expr` that doesn't name an existing local creates one
(`gen.luna:2665-2672`). This is how most of `bootminor`'s own sources are
written. `let` / `meow` / `seal` are needed only when you want a type
annotation or to seal the slot.

### `const`

```luna
const PAGE_SIZE:   int = 1 << 12
const PAGE_MASK:   int = PAGE_SIZE - 1
const SIGN_FLAG:   int = 1 << 63
const MASK:        int = 0xFFFF0000 | 0x0000FFFF

fn main() -> int
    shine_int(PAGE_SIZE)
    let @p: int = 0x1000 + PAGE_MASK
    shine_int(@p)
    return 0
```

Evaluated by the tiny const interpreter at `gen.luna:3068-3162`. Operator
support matches the full integer expression grammar: `+ - * / %`, `&  | ^`,
`<< >>`, `~`, unary `-`, parentheses, and references to earlier consts.
There are no string or float consts today.

---

## 6. Operators

Expression parsing is precedence-climbing (`parse_*`,
`gen.luna:984-1149`). Lowest to highest:

| Prec | Operator(s)                  | Associativity | Emitter                                     |
|------|------------------------------|---------------|---------------------------------------------|
| 1    | `\|` (bitwise or)             | left          | `or rax, rcx`                               |
| 2    | `^` (bitwise xor)            | left          | `xor rax, rcx`                              |
| 3    | `&` (bitwise and)            | left          | `and rax, rcx`                              |
| 4    | `==  !=  <  <=  >  >=`       | left          | `cmp` + `setcc` + `movzx`                   |
| 5    | `<<  >>`                     | left          | `shl/shr rax, cl`                           |
| 6    | `+  -`                       | left          | `add/sub`                                   |
| 7    | `*  /  %`                    | left          | `imul` / `cqo+idiv`                         |
| 8    | unary `-  ~  *  &`           | right         | `neg` / `not` / `mov [rax]` / `lea`         |
| 9    | postfix `as T`, `[i]`, `.f`, `(...)` call | left | `_emit_cast`, array load, field load, call   |

### Compound assignments

```luna
@x += 5     @x -= 3     @x *= 4     @x /= 2     @x %= 7
@b &= 0x0F  @b |= 0xA0  @b ^= 0xFF
@s <<= 10   @s >>= 3
```

Tokens `TK_PLUSEQ`..`TK_SHREQ` at `lex.luna:55-64`; assignment lowering in
`gen.luna:2649-2664`. Compound assignments respect `seal`.

### `as T` cast

```luna
meow @big: int = 0x123456789abcdef0
shine_int(@big as u8)      # 0xf0
shine_int(@big as u32)     # 0x9abcdef0
shine_int(0 - 1 as i8)     # -1 (movsx sign-extends)
shine_int(42 as bool)      # 1
```

Examples directly from `tests_types/t_cast.luna`. Cast emission table at
`gen.luna:1152-1198`:

- `u8`: `and rax, 0xff`
- `u16`: `and rax, 0xffff`
- `u32`: `mov eax, eax` (zero-extends)
- `i8`: `movsx rax, al`
- `i16`: `movsx rax, ax`
- `i32`: `movsxd rax, eax`
- `bool`: `cmp rax, 0; setne al; movzx rax, al`
- `i64`, `u64`, `int`, `usize`, pointer, struct name: no-op.

`as` chains:

```luna
let @n: int = (0 - 1) as u8 as i8   # 255 reinterpreted as signed -1
shine_int(@n)
```

### Address-of and dereference

```luna
meow @v: int = 10
seal @p: *mut int = &@v
*@p = 99                       # pointer write
shine_int(@v)                  # 99
shine_int(*@p)                 # 99
```

- `&@name` emits `lea rax, [rbp + local_off]` — a pointer to the stack slot
  (`gen.luna:1244-1249`).
- `&@obj.field` loads the struct base then adds `field_off` (`gen.luna:1219-1242`).
- `*expr` emits `mov rax, [rax]` (`gen.luna:1250-1257`).
- `*expr = val` emits `mov [rax], rcx` (`gen.luna:2554-2567`).

Pointers are just `int`-sized integers — you can add, cast, and compare them
freely.

---

## 7. Control flow

### If / elif / else

```luna
fn classify(@n: int) -> int
    if @n < 0
        shine("negative")
    elif @n == 0
        shine("zero")
    elif @n < 10
        shine("small")
    else
        shine("big")
    return 0
```

`parse_if` lives at `gen.luna:2747-2790`. Each branch emits a
conditional `jz` over its body and an unconditional `jmp` past the chain;
all chain-exit jumps are patched when the final `else` / trailing `elif`
closes.

### While

```luna
fn main() -> int
    meow @i: int = 1
    while @i <= 15
        if @i % 15 == 0
            shine("FizzBuzz")
        elif @i % 3 == 0
            shine("Fizz")
        elif @i % 5 == 0
            shine("Buzz")
        else
            shine_int(@i)
        @i = @i + 1
    return 0
```

Direct lift from `tests_m2b/05_fizzbuzz.luna`. Parser: `parse_while`,
`gen.luna:2793-2818`.

### Break / continue

```luna
fn first_even(@arr: [int; 10]) -> int
    meow @i: int = 0
    while @i < 10
        if @arr[@i] % 2 == 0
            break
        @i += 1
    return @arr[@i]
```

The compiler maintains a stack of `continue` targets and a stack of
per-loop break-patch vectors (`gen.luna:52-54, 2793-2818`). Break outside
a loop is a compile error.

### Return

`return <expr>` inside a user function emits the fn epilogue
(`mov rsp, rbp ; pop rbp ; ret`). Inside `main`, `return` becomes a
`sys_exit` syscall (`gen.luna:2511-2522`). Falling off the end of
`main` exits with 0; falling off a normal fn returns 0 in `rax`.

---

## 8. Functions

```luna
fn fib(@n: int) -> int
    if @n < 2
        return @n
    return fib(@n - 1) + fib(@n - 2)

fn main() -> int
    shine_int(fib(20))
    return 0
```

Parsing at `gen.luna:2836-2940`. Notes:

- **Parameter prefix**: params use `@name` like locals. Types are optional
  but required if you want struct field access inside the body.
- **Return type**: `-> T` is parsed and discarded — return-type checking is
  future work.
- **Arity**: up to the first 6 arguments go through SysV integer registers
  (`rdi rsi rdx rcx r8 r9`). Args 7+ are passed on the stack and loaded at
  function entry (`gen.luna:2903-2920`). Call-site lowering at
  `gen.luna:2307-2390` builds a stack argument frame, dispatches to regs
  for the first six, and slides the rest.
- **Forward references**: calls to not-yet-parsed functions emit a
  `call rel32` with a zero placeholder; a second pass patches them
  (`gen.luna:3237-3251`). Mutual recursion works out of the box.
- **Void calls**: an expression statement `do_thing()` discards `rax`
  (`gen.luna:2684-2687`).

### Six-argument example (passes the shipped test)

```luna
fn sum6(@a: int, @b: int, @c: int, @d: int, @e: int, @f: int) -> int
    return @a + @b + @c + @d + @e + @f

fn main() -> int
    shine_int(sum6(1, 2, 3, 4, 5, 6))
    return 0
```

From `tests_m2c/04_6_params.luna`.

---

## 9. Structs

```luna
struct Point
    x: int
    y: int

fn len2(@p: Point) -> int
    return @p.x * @p.x + @p.y * @p.y

fn main() -> int
    let @a: Point = Point { x: 3, y: 4 }
    shine_int(len2(@a))              # 25
    @a.x = 30
    shine_int(@a.x + @a.y)           # 34
    return 0
```

Parsing at `gen.luna:2979-3009`. Runtime shape: every field occupies 8
bytes; a struct is just a heap allocation of `n_fields * 8` bytes
returned by the inline `new_arr` sequence (`gen.luna:914-967`, called
from `parse_primary` at `gen.luna:1464-1485`).

**Rules that matter in practice:**

- Field-init order in a struct literal **must** match declaration order.
  The parser pushes values left to right and stores them into fixed slots
  — it does not rearrange.
- Field access (`@obj.f`) and field write (`@obj.f = v`) only resolve an
  offset if the local's **type annotation** is known at the point of use.
  Without `let @p: Point = ...`, `@p.x` compiles to an offset of 0 —
  silently. This is the single biggest landmine for new users.
- `sizeof(StructName)` returns `n_fields * 8` (`gen.luna:2267-2272`).

### Address-of a field

```luna
let @pt: Point = Point { x: 7, y: 11 }
let @px: *mut int = &@pt.x
let @py: *mut int = &@pt.y
*@px = 100
*@py = 200
shine_int(@pt.x)              # 100
shine_int(@pt.y)              # 200
```

From `tests_types/t_ptr_write.luna`.

---

## 10. Arrays

```luna
let @xs: [int; 5] = [10, 20, 30, 40, 50]
let @zs: [u8; 64] = [0; 64]

shine_int(@xs[2])             # 30
@xs[0] = 99
shine_int(@xs[0])             # 99
```

- Indexed read: `@arr[i]` emits `mov rax, [rax + rcx*8]`
  (`gen.luna:1399-1411`).
- Indexed write: the statement form is at `gen.luna:2582-2602`.
- Length: the first 8 bytes of an array allocation hold N; read with
  `str_len(@arr)` (they share the length-prefix convention).

Arrays are just allocated memory: the element width as emitted is always 8
regardless of the declared `T`. If you need packed bytes, allocate with
`new_str(n)` and use `u8_at` / `u8_set`.

---

## 11. Strings

String layout is fixed at the runtime level:

```
  [ 8-byte little-endian length ][ N data bytes ][ optional NUL ]
                                ^
                         pointer you get
```

This is why `str_len(s)` is one instruction — it reads the 8 bytes at
`ptr - 8`. Every string in the data section also gets a trailing NUL
(`gen.luna:756-771`) so you can hand it straight to `sys_open` et al.

### Built-in string intrinsics

These are **inlined** by the parser; they are not function calls:

| Intrinsic                     | gen.luna            | Behavior                                       |
|-------------------------------|---------------------|------------------------------------------------|
| `str_len(s)`                  | 1624-1634           | Load `[s - 8]`                                 |
| `str_byte(s, i)`              | 1636-1652           | `movzx rax, byte [s + i]`                      |
| `str_set_byte(s, i, v)`       | 2274-2306           | `mov byte [s + i], v_low_byte` → 0             |
| `new_str(n)`                  | 1655-1706           | Bump alloc, write N as length prefix, return `ptr` |

### Prelude helpers (`bootminor_prelude.luna`)

These are real Luna functions — you pay a call for each invocation:

```luna
str_eq(a, b)         -> int    # byte-compare, returns 0 or 1
str_concat(a, b)     -> int    # fresh allocation
str_substr(s, i, n)  -> int    # fresh allocation
int_to_str(n)        -> int    # decimal conversion (signed)
```

### Mutating a string

```luna
fn main() -> int
    meow @buf: int = new_str(5)
    str_set_byte(@buf, 0, 72)        # 'H'
    str_set_byte(@buf, 1, 101)       # 'e'
    str_set_byte(@buf, 2, 108)       # 'l'
    str_set_byte(@buf, 3, 108)       # 'l'
    str_set_byte(@buf, 4, 111)       # 'o'
    shine(@buf)                      # Hello
    return 0
```

Strings are mutable — they are just byte buffers with a length prefix.

---

## 12. Memory primitives

The hacker toolkit: fixed-width loads and stores with explicit byte
offsets. Intended for parsing ELF headers, wire protocols, compressed
formats — anything where you need to read a specific field at a specific
offset in a flat buffer.

| Intrinsic               | gen.luna     | Semantics                                          |
|-------------------------|--------------|----------------------------------------------------|
| `u8_at(ptr, off)`       | 1970-1986    | `movzx rax, byte [ptr + off]`                      |
| `u16_at(ptr, off)`      | 1988-2004    | `movzx rax, word [ptr + off]`                      |
| `u32_at(ptr, off)`      | 2006-2020    | `mov eax, [ptr + off]` (zero-extends)              |
| `u64_get(buf, idx)`     | 1923-1938    | `mov rax, [buf + idx*8]`                           |
| `u8_set(ptr, off, v)`   | 2022-2046    | `mov byte [ptr + off], v_low`                      |
| `u16_set(ptr, off, v)`  | 2048-2072    | `mov word [ptr + off], v_word`                     |
| `u32_set(ptr, off, v)`  | 2074-2097    | `mov dword [ptr + off], v_dword`                   |
| `u64_set(buf, idx, v)`  | 1940-1967    | `mov [buf + idx*8], v`                             |
| `memcpy(dst, src, n)`   | 2191-2213    | `rep movsb`, returns 0                             |
| `memset(ptr, b, n)`     | 2215-2237    | `rep stosb`, returns 0                             |
| `sizeof(T)`             | 2240-2272    | Compile-time constant                              |

Canonical ELF-parser fragment:

```luna
# Identify an ELF64 file.
meow @buf: int = new_str(32)
u8_set(@buf, 0, 0x7f)
u8_set(@buf, 1, 0x45)      # 'E'
u8_set(@buf, 2, 0x4c)      # 'L'
u8_set(@buf, 3, 0x46)      # 'F'
u32_set(@buf, 4, 0x0102034f)
u16_set(@buf, 8, 0xBEEF)

shine_int(u8_at(@buf, 0))   # 127
shine_int(u32_at(@buf, 4))  # 16909647
shine_int(u16_at(@buf, 8))  # 48879
```

From `tests_types/t_memory.luna`.

Note: `u64_get` / `u64_set` are *word*-indexed (multiply by 8 under the
hood); `u8_at` / `u32_at` are *byte*-indexed. Mixing them is the most
common source of wrong offsets.

---

## 13. Bit tricks

Hardware-accelerated operations that compile to a single x86 instruction.
All take `int`, all return `int`.

| Intrinsic          | gen.luna     | Emits          |
|--------------------|--------------|----------------|
| `bswap32(x)`       | 2110-2117    | `bswap eax`    |
| `bswap64(x)`       | 2100-2108    | `bswap rax`    |
| `popcount(x)`      | 2119-2130    | `popcnt rax, rax` |
| `clz(x)`           | 2132-2143    | `lzcnt rax, rax`  |
| `ctz(x)`           | 2145-2156    | `tzcnt rax, rax`  |
| `rotl(x, n)`       | 2158-2172    | `rol rax, cl`  |
| `rotr(x, n)`       | 2174-2188    | `ror rax, cl`  |

```luna
fn main() -> int
    shine_int(bswap64(0x1122334455667788))   # 9833440827789222417
    shine_int(bswap32(0x11223344))           # 1144201745
    shine_int(popcount(0xFF))                # 8
    shine_int(clz(1))                        # 63
    shine_int(ctz(0x80))                     # 7
    shine_int(rotl(1, 8))                    # 256
    shine_int(rotr(0x100, 8))                # 1
    return 0
```

(Straight from `tests_types/t_bits.luna`.) Use these for hash functions,
big-endian wire protocols, bit-packed format readers — SHA-256 / CRC32 /
leb128 become very natural.

---

## 14. Floating point

Luna uses bare SSE2. Every `f64` value is carried in `rax` as the raw IEEE
bit pattern and round-tripped through `xmm0`/`xmm1` for math.
Intrinsics at `gen.luna:1767-1921`.

### Construction

- `f64_lit(bits)` — you supply the raw u64 pattern. No-op at runtime:
  `f64_lit(0x400921FB54442D18)` just leaves that value in `rax`.
- `f64_from_int(n)` — `cvtsi2sd xmm0, rax`.
- Literal `3.14`, `1e3`, `1.5e-6` — lowered to a `cvtsi2sd` + repeated
  `mulsd`/`divsd` chain (see §4).

### Conversion

- `f64_to_int(x)` — `cvttsd2si` (truncation toward zero).

### Arithmetic

| Intrinsic       | SSE opcode  |
|-----------------|-------------|
| `f64_add(a,b)`  | `addsd`     |
| `f64_sub(a,b)`  | `subsd`     |
| `f64_mul(a,b)`  | `mulsd`     |
| `f64_div(a,b)`  | `divsd`     |
| `f64_sqrt(x)`   | `sqrtsd`    |

### Comparison

| Intrinsic      | x86 result        |
|----------------|-------------------|
| `f64_lt(a,b)`  | `ucomisd` + `setb`  (1 if a<b)   |
| `f64_gt(a,b)`  | `ucomisd` + `seta`  (1 if a>b)   |
| `f64_eq(a,b)`  | `ucomisd` + `sete`  (1 if a==b)  |

There are **no** `f64_le` / `f64_ge` / `f64_ne` — emit them yourself
from the three primitives.

### Example

```luna
fn main() -> int
    @pi = 3.14159
    @r  = 5.0
    @area = f64_mul(@pi, f64_mul(@r, @r))
    shine_int(f64_to_int(@area))                         # 78

    shine_int(f64_to_int(f64_sqrt(f64_from_int(10000)))) # 100
    shine_int(f64_lt(f64_from_int(3), f64_from_int(4)))  # 1
    return 0
```

Adapted from `tests_types/t_f64.luna` and `t_float_lit.luna`.

### Caveats

- Float ops do NOT use `+`/`-`/`*`/`/`. Those operators always use the
  integer path. You must call `f64_add(a, b)`.
- There is no `f32` at runtime — `sizeof(f32)` returns 4 but there are no
  instructions to load, store, or operate on a `f32` value.
- NaN handling is whatever `ucomisd` does; be careful with ordered vs
  unordered comparisons.

---

## 15. Linux syscalls

Syscall intrinsics take 0 to 6 integer args and return the raw `rax` value.
Linux ABI uses `rdi rsi rdx r10 r8 r9` (note `r10`, not `rcx`); the
compiler lays args into those registers in `_emit_syscall_intrinsic`
(`gen.luna:784-821`).

| Intrinsic        | Syscall #  | Args                                     |
|------------------|-----------:|------------------------------------------|
| `sys_read`       | 0          | `(fd, buf, count)`                       |
| `sys_write`      | 1          | `(fd, buf, count)`                       |
| `sys_open`       | 2          | `(path, flags, mode)`                    |
| `sys_close`      | 3          | `(fd)`                                   |
| `sys_lseek`      | 8          | `(fd, offset, whence)`                   |
| `sys_mmap`       | 9          | `(addr, len, prot, flags, fd, offset)`   |
| `sys_munmap`     | 11         | `(addr, len)`                            |
| `sys_brk`        | 12         | `(addr)`                                 |
| `sys_getpid`     | 39         | `()`                                     |
| `sys_exit`       | 60         | `(code)`                                 |
| `sys_creat`      | 85         | `(path, mode)`                           |
| `sys_getrandom`  | 318        | `(buf, buflen, flags)`                   |

Minimal write-a-file example:

```luna
fn main() -> int
    @fd = sys_creat("/tmp/luna-greet.txt", 420)     # 0o644
    if @fd < 0
        shine("open failed")
        return 1
    seal @msg: int = "Hello from Luna\n"
    sys_write(@fd, @msg, str_len(@msg))
    sys_close(@fd)
    return 0
```

The prelude functions `read_file`, `write_file`, `arg`, `print` are all
thin wrappers around these primitives (see
`bootminor_prelude.luna:170-226`).

---

## 16. Const expressions

```luna
const KB:         int = 1024
const MB:         int = KB * 1024
const PAGE_SIZE:  int = 1 << 12
const PAGE_MASK:  int = PAGE_SIZE - 1
const COLOR_MASK: int = 0xFFFF0000 | 0x0000FFFF
const SIGN_FLAG:  int = 1 << 63
const ABS_MASK:   int = ~SIGN_FLAG
```

The const evaluator (`gen.luna:3068-3162`) supports exactly what the
runtime integer grammar does:

- Literals (decimal, `0x`, `0b`, `0o`, underscores).
- References to earlier `const` names.
- Binary operators: `+ - * / %`, `<< >>`, `& ^ |`, in the same precedence
  as runtime.
- Unary operators: `-`, `~`, parentheses.

There is no string-literal const, no float const, no `sizeof(T)` in const
position, and no user-function call in a const. The type annotation after
the name is parsed but ignored.

At a use site, a `const`-reference is folded to an immediate:
`_const_lookup` returns the value; `parse_primary` emits either
`mov eax, imm32` or `mov rax, imm64` depending on range
(`gen.luna:1600-1613`).

---

## 17. Module-level declarations

`parse_program` (`gen.luna:3201-3220`) accepts exactly five top-level forms:

```luna
import kernel                             # (no-op today — see below)

extern "C" fn libc_puts(@s: *const u8) -> int  # declaration only, no body

const MAX_FD: int = 65535

struct Point
    x: int
    y: int

fn main() -> int
    return 0
```

### `import`

Parsed, name recorded for documentation, otherwise **ignored**
(`parse_import`, `gen.luna:3048-3054`). The self-host build works by
concatenating source files into a single monolith (see `selfhost_build.sh`).
A real module system is on the roadmap but not implemented.

### `extern "C" fn`

Parsed and **skipped** (`parse_extern_decl`, `gen.luna:3015-3044`). There
is no linker. Calling an externally-declared function will try to emit a
forward `call` and fail at patch time with `undefined fn: <name>`. Use it
to document your intent — the only thing the compiler will "link" against
today is the built-in syscall and math intrinsic set.

### Order

Declarations can appear in any order. Function-to-function forward refs
are resolved in a second pass; constants and structs are used only by
lexically later code so they must appear before their first use. (This
matters for struct names — `let @p: Point = ...` before `struct Point`
fails.)

---

## 18. Prelude

The prelude (`src/bootminor/bootminor_prelude.luna`, 226 lines) is
ordinary Luna that happens to be `cat`'d in front of every self-compile.
Nothing is magic; everything is readable.

### `Vec`

```luna
struct Vec
    data: int
    len:  int
    cap:  int
```

Growing 8-byte-slot dynamic array. Used constantly in `gen.luna` for
tokens, struct-field lists, patch records, etc.

| Fn                        | Returns                                    |
|---------------------------|--------------------------------------------|
| `vec_new()`               | Fresh `Vec`, cap 8                         |
| `vec_push(@v, @x)`        | 0 (grows if `len == cap`)                  |
| `vec_get(@v, @i)`         | Slot value                                 |
| `vec_set(@v, @i, @x)`     | 0                                          |
| `vec_pop(@v)`             | Popped value (0 if empty)                  |
| `vec_len(@v)`             | Length                                     |

### `StrBuf`

```luna
struct StrBuf
    buf: int
    len: int
    cap: int
```

Appending byte buffer with ownership transfer via `strbuf_done`.

| Fn                          | Returns                                                    |
|-----------------------------|------------------------------------------------------------|
| `strbuf_new()`              | Fresh `StrBuf`, cap 64                                     |
| `strbuf_add_byte(@b, @x)`   | 0                                                          |
| `strbuf_add_str(@b, @s)`    | 0 (appends the length-prefixed `@s`)                       |
| `strbuf_done(@b)`           | Fresh length-prefixed string with exactly `len` bytes      |

### I/O

| Fn                  | Behavior                                             |
|---------------------|------------------------------------------------------|
| `shine(@s)`         | Write `@s + "\n"` to fd 1 (built-in; `gen.luna:2694`) |
| `shine_int(@n)`     | Decimal + "\n" via the inline `_print_int` runtime  |
| `print(@s)`         | Write `@s` to fd 1, no newline                       |
| `print_int(@n)`     | Decimal, no newline                                  |

### File I/O

```luna
read_file(@path)      -> int      # size-on-disk slurp; returns str or 0 on error
write_file(@p, @s)    -> int      # O_CREAT, mode 0o644; returns bytes written or -1
arg(@i)               -> int      # /proc/self/cmdline split on NUL; "" past end
```

Typical pipeline (copied from `main2.luna`):

```luna
@src_path = arg(1)
@src = read_file(@src_path)
if @src == 0
    shine(str_concat("err: cannot read ", @src_path))
    return 1
# ... compile ...
write_file(@out_path, @buf)
```

---

## 19. Cookbook

### ELF header parser

Read your own binary from `/proc/self/exe`, print the magic bytes, class
byte, endianness, machine, and entry point.

```luna
fn main() -> int
    seal @path: int = "/proc/self/exe"
    @fd = sys_open(@path, 0, 0)
    if @fd < 0
        shine("open failed")
        return 1
    @buf = new_str(64)
    @n = sys_read(@fd, @buf, 64)
    sys_close(@fd)
    if @n < 64
        shine("short read")
        return 1

    # e_ident[0..4]: 0x7F 'E' 'L' 'F'
    shine_int(u8_at(@buf, 0))      # 127
    shine_int(u8_at(@buf, 1))      # 69  ('E')
    shine_int(u8_at(@buf, 2))      # 76  ('L')
    shine_int(u8_at(@buf, 3))      # 70  ('F')
    # e_ident[EI_CLASS] = 2 (ELF64), [EI_DATA] = 1 (little-endian)
    shine_int(u8_at(@buf, 4))      # 2
    shine_int(u8_at(@buf, 5))      # 1
    # e_type @ 16 (u16),  e_machine @ 18 (u16 — 62 = x86_64)
    shine_int(u16_at(@buf, 16))
    shine_int(u16_at(@buf, 18))
    # e_entry @ 24 (u64). We fetch low 32 bits via u32_at; add high word.
    @lo = u32_at(@buf, 24)
    @hi = u32_at(@buf, 28)
    shine_int(@lo)
    shine_int(@hi)
    return 0
```

You now have a working parser for roughly every binary format: `sys_open`,
`sys_read`, then `u*_at` at the byte offsets from the spec.

### SHA-256 of an input buffer

The algorithm ports directly from any C reference. Keep 64 `u32` K-constants
in an array, run 64 rounds, emit eight `u32`s. Skeleton:

```luna
const K_LEN: int = 64

fn sha256_k(@i: int) -> int
    # First 32 bits of the fractional part of the cube roots of the first 64 primes.
    seal @table: [int; 64] = [
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
        0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
        0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
        0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
        0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    ]
    return @table[@i]

fn rotr32(@x: int, @n: int) -> int
    return ((@x >> @n) | (@x << (32 - @n))) & 0xFFFFFFFF

fn ch(@x: int, @y: int, @z: int) -> int
    return (@x & @y) ^ ((~@x) & @z) & 0xFFFFFFFF

fn maj(@x: int, @y: int, @z: int) -> int
    return (@x & @y) ^ (@x & @z) ^ (@y & @z) & 0xFFFFFFFF

fn big_sig0(@x: int) -> int
    return rotr32(@x, 2) ^ rotr32(@x, 13) ^ rotr32(@x, 22)

fn big_sig1(@x: int) -> int
    return rotr32(@x, 6) ^ rotr32(@x, 11) ^ rotr32(@x, 25)
```

The full round loop, message-schedule extension, padding, and output
emission follow the same shape: read 64-byte blocks with `u32_at`, byte-swap
to big-endian with `bswap32`, run the standard 64-round core, write
final digest into an output buffer. All of it fits within the bootminor
subset: you only need `int` arithmetic, `&` `|` `^` `~` `>> <<`,
`bswap32`, `rotl`/`rotr`, and arrays.

Longer working implementations live in `bootstrap/prelude.luna` — port the
body as-is and replace `>>>` (if present) with explicit `>>` plus a mask to
32 bits.

### Linked list via heap pointers

```luna
struct Node
    val:  int
    next: int      # 0 or a Node pointer (treat `int` as a nullable heap ref)

fn cons(@v: int, @tl: int) -> int
    let @n: Node = Node { val: @v, next: @tl }
    return @n as int        # no-op, but documents intent

fn sum_list(@head: int) -> int
    meow @p: int = @head
    meow @acc: int = 0
    while @p != 0
        # Interpret the raw pointer as a Node.
        @acc = @acc + u64_get(@p, 0)       # field #0 = val
        @p   = u64_get(@p, 1)              # field #1 = next
    return @acc

fn main() -> int
    @l = cons(1, cons(2, cons(3, cons(4, 0))))
    shine_int(sum_list(@l))               # 10
    return 0
```

Field access through a typed local (`let @x: Node = ...`) also works and
is cleaner; the `u64_get` form is shown to emphasise that a struct is
just indexed memory.

### Naive HTTP GET (raw bytes)

No `connect` intrinsic ships today, but `sys_mmap` + `brk` + `sys_open`
of `/dev/tcp/<host>/<port>` in a bash subprocess-style workflow is
cheating; a real socket requires two syscalls not yet wired through
(`socket`=41, `connect`=42). You can splice them in via inline assembly
is a near-future feature — for now, the pattern is: wrap the external
call with `extern "C" fn` so it parses, and use the compiler's
"undefined fn" message as a TODO marker.

Skeleton that **does** work using an already-open fd (e.g. fed via
`socat TCP4:host:80 EXEC:'./your-luna-prog'`):

```luna
fn main() -> int
    seal @req: int = "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n"
    sys_write(1, @req, str_len(@req))        # fd 1 = the socket in this setup

    @buf = new_str(65536)
    @n = sys_read(0, @buf, 65536)            # fd 0 = the socket inbound
    if @n > 0
        sys_write(2, @buf, @n)               # echo to stderr
    return 0
```

### Compute π via Leibniz series

```luna
fn main() -> int
    @sum  = 0.0
    @sign = 1.0
    @i    = 0
    while @i < 1000000
        @denom = f64_from_int(2 * @i + 1)
        @term  = f64_div(@sign, @denom)
        @sum   = f64_add(@sum, @term)
        @sign  = f64_sub(0.0, @sign)          # flip sign: 0 - x
        @i     = @i + 1
    @pi = f64_mul(@sum, f64_from_int(4))
    # Multiply by 1e6 so shine_int gets the first 6 digits.
    @scaled = f64_mul(@pi, f64_from_int(1000000))
    shine_int(f64_to_int(@scaled))           # ~3141591
    return 0
```

### sqrt lookup table

```luna
fn main() -> int
    meow @table: [int; 16] = [0; 16]
    @i = 0
    while @i < 16
        @table[@i] = f64_to_int(f64_sqrt(f64_from_int(@i * @i)))
        @i = @i + 1
    @j = 0
    while @j < 16
        shine_int(@table[@j])                 # 0,1,2,3,4,...
        @j = @j + 1
    return 0
```

---

## 20. Current limitations

Honest inventory of what bootminor does *not* support. Each item is a
milestone on the roadmap, linked where one exists.

| Feature | Why not yet | Target phase |
|---------|-------------|--------------|
| Algebraic data types (`type Foo \| Bar`) | No tag discriminator in codegen | M3 ADT |
| Pattern matching (`phase`/`match`) | Needs ADT first | M3 ADT |
| String matching / regex | Library, blocked on ADT | M5 |
| Closures / fn pointers as values | No first-class fn layout | M4 Closures |
| Generics / templates | Parser swallows `<T,U>`, no instantiation | M4 Generics |
| Traits / `impl` | Not parsed | M4 Traits |
| Borrow checker | `&mut` is parsed but unchecked | M5 Safety |
| Multi-file modules | `import` is a no-op; use concatenation today | M5 Modules |
| Windows PE64 / macOS Mach-O | ELF64-only backend | M6 Backends |
| ARM64 / RISC-V | x86-64 instruction emitters are hand-rolled | M6 Backends |
| Real type checker | Types are hints, not constraints | M7 Typeck |
| String interpolation | Use `str_concat` | — |
| Float `+ - * /` operators | Must use `f64_add` etc. | M5 |
| `f64_le`, `f64_ge`, `f64_ne` | Compose from `lt`/`gt`/`eq` | — |
| `break` with a label | Only the innermost loop | — |
| `for` loop | Use `while` | — |
| Defer / RAII | Nothing drops automatically | M5 |
| Concurrency (`spawn`, `actor`) | No scheduler / no runtime | post-1.0 |
| Async (`nova`) | No event loop | post-1.0 |

Anything listed in `LUNA_SPEC.md` under §2.2, §2.3, §2.4, §3.2, §3.3, §4,
§5, §6 — treat as aspirational. The spec describes where Luna is headed;
this document describes where it is today.

---

## 21. See also

- `README.md` — repo overview and build status.
- `docs/HOTSWAP.md` — how the self-host fixed-point check catches
  compiler regressions across commits.
- `src/bootminor/tests_types/` — one focused `.luna` file per language
  feature, each paired with a `.expect` output.
- `src/bootminor/tests_m2b/` — classic programs (FizzBuzz, Fib, nested
  loops).
- `src/bootminor/tests_m2c/` — function-call edge cases (recursion,
  6-arg calls, mutual recursion).
- `src/bootminor/bootminor_prelude.luna` — every helper you can assume.
- `src/bootminor/gen.luna` — the parser and codegen. When in doubt about
  semantics, read this file. Every intrinsic listed above has its
  emission logic in there; the line references throughout this document
  point you to the exact block.
- `LUNA_SPEC.md` — the aspirational v4.2 spec. Use as direction, not as
  a feature checklist.

---

## Appendix A — Quick reference card

### Tokens (what the lexer yields)

`lex.luna` maps source bytes to token kinds `TK_*`. You can ignore these
unless you're hacking on the compiler, but the list is a handy summary of
the surface syntax:

```
TK_INT TK_FLOAT TK_STR TK_IDENT TK_ATIDENT
TK_FN TK_IF TK_ELIF TK_ELSE TK_WHILE TK_RETURN
TK_KW_LET TK_KW_MEOW TK_KW_SEAL TK_KW_CONST
TK_KW_STRUCT TK_KW_EXTERN TK_KW_IMPORT
TK_KW_BREAK TK_KW_CONTINUE TK_KW_AS
TK_PLUS TK_MINUS TK_STAR TK_SLASH TK_PERCENT
TK_AMP TK_PIPE TK_CARET TK_TILDE TK_SHL TK_SHR
TK_ASSIGN TK_EQ TK_NEQ TK_LT TK_LE TK_GT TK_GE
TK_PLUSEQ TK_MINUSEQ TK_STAREQ TK_SLASHEQ TK_PERCENTEQ
TK_AMPEQ TK_PIPEEQ TK_CARETEQ TK_SHLEQ TK_SHREQ
TK_LPAREN TK_RPAREN TK_COMMA TK_COLON TK_ARROW
TK_LBRACK TK_RBRACK TK_SEMI TK_LBRACE TK_RBRACE TK_DOT
TK_NEWLINE TK_INDENT TK_DEDENT TK_EOF
```

### Reserved words

```
fn if elif else while return
let meow seal const struct extern import as
break continue
```

There is no `true`/`false`, no `null`, no `nil`, no `self`. Use `1` and
`0` as bools; use `0` as a null pointer.

### Built-in intrinsic names (always in scope)

```
# I/O
shine  shine_int  print  print_int

# Strings
str_len  str_byte  str_set_byte  new_str
str_eq   str_concat  str_substr  int_to_str   (prelude)

# Memory
u8_at u16_at u32_at u64_get
u8_set u16_set u32_set u64_set
memcpy memset sizeof

# Bits
bswap32 bswap64 popcount clz ctz rotl rotr

# Float (SSE)
f64_lit f64_from_int f64_to_int
f64_add f64_sub f64_mul f64_div f64_sqrt
f64_lt f64_gt f64_eq

# Linux syscalls
sys_read sys_write sys_open sys_close sys_lseek
sys_mmap sys_munmap sys_brk sys_exit sys_creat
sys_getpid sys_getrandom

# File / args (prelude)
read_file write_file arg

# Vec / StrBuf (prelude)
vec_new vec_push vec_get vec_set vec_pop vec_len
strbuf_new strbuf_add_byte strbuf_add_str strbuf_done
```

Shadowing these by defining a user fn of the same name is undefined
behavior — the parser matches built-in names first.

### Operator precedence (lowest to highest)

```
|              bitwise or
^              bitwise xor
&              bitwise and
== != < <= > >=    comparison
<<  >>         shifts
+  -           add, subtract
*  /  %        mul, div, mod
-  ~  *  &     unary neg / bit-not / deref / addr-of  (right-assoc)
as T           cast (postfix)
[i]  .field  (args)   index / field / call   (postfix)
```

No logical operators (`&&`, `||`, `!`) — but `&`, `|`, and comparison
against 0 cover the common cases. A typical short-circuit pattern:

```luna
if @n > 0
    if @n < 100
        shine("in range")
```

---

## Appendix B — Gotchas and patterns

A curated list of the sharp edges I hit and how to work around them. Read
this before your first program frustrates you.

### Typed bindings are required for field access

```luna
# WRONG — silently compiles to offset 0.
@p = Point { x: 3, y: 4 }
shine_int(@p.y)                # prints 3 (reads field #0)

# RIGHT
let @p: Point = Point { x: 3, y: 4 }
shine_int(@p.y)                # prints 4
```

The rule: anywhere you use `.field`, the owner must have a known struct
type. Parameter annotations count: `fn f(@p: Point) -> int` lets the body
dot-access freely.

### Struct field order matters

```luna
struct Color
    r: int
    g: int
    b: int

# WRONG — compiler stores pushed values into r,g,b in order. You said
# b first, so what lands in r is 255.
let @c: Color = Color { b: 255, g: 0, r: 128 }

# RIGHT — match declaration order.
let @c: Color = Color { r: 128, g: 0, b: 255 }
```

### `shine` adds a newline; `print` does not

```luna
shine("Hello")          # prints "Hello\n"
print("Hello")          # prints "Hello"  (no newline)
```

Mirror for integers: `shine_int` vs `print_int`.

### Division is signed integer

```luna
shine_int(7 / 2)         # 3
shine_int(0 - 7 / 2)     # -3  (truncation toward zero)
shine_int(7 % 2)         # 1
shine_int(0 - 7 % 2)     # -1  (sign follows dividend)
```

Use `f64_div` for real division.

### Comparison results are 0 or 1, not bool

```luna
meow @flag: int = (5 < 10)
shine_int(@flag)             # 1
```

You can store them anywhere an `int` fits.

### No implicit boolean context

```luna
# WRONG:
if @p
    ...

# RIGHT:
if @p != 0
    ...
```

Every condition is an integer expression compared to zero
(`test rax, rax`; `jz` if zero).

### Strings are null-terminated AND length-prefixed

You get both. The length prefix makes `str_len` O(1); the trailing NUL
makes the string directly usable with `sys_open` (which expects a
C-string path).

**But:** strings returned from `new_str(n)` are *NOT* zero-filled. Use
`memset(@buf, 0, n)` right after if you need zeroing.

### `0 - x` vs unary `-x`

Both work and emit `neg rax`, but in constant-eval contexts only the
binary form is safe if x is a large positive literal — unary minus on
`9223372036854775808` will overflow during parsing. Prefer `0 - LIT` for
large magnitudes.

### Heap never shrinks

Every allocation — `new_str`, `new_arr`, struct literal, `Vec` grow,
`StrBuf` grow — bumps `heap_top` and never rolls it back. Programs that
allocate in a hot loop will exhaust the 64 MB `memsz` heap. For long-lived
processes you must manage memory manually via `sys_mmap`.

### Comments are `#`, not `//`

The lexer only recognises `#` to end of line. `// foo` will tokenise as
two `TK_SLASH` and an `IDENT`, which is a syntax error in every position
it can appear.

### Indentation is space-counted, tabs count as 4

Be consistent inside a single file. The lexer tracks columns
(`lex.luna:242-282`) and will generate unbalanced `TK_INDENT`/`TK_DEDENT`
pairs if you mix tabs and spaces inconsistently, producing a parse error
far from the real cause.

### `import` is a comment

```luna
import stdlib.collections           # does nothing
```

Until modules land, every source must either be in a single file or
pre-concatenated.

### `extern "C"` cannot actually link

The compiler has no linker. Treat `extern "C" fn foo(...) -> T` as a TODO
comment. Calls to `foo` will fail at compile time with
`bootminor: undefined fn: foo`.

---

## Appendix C — ABI quick facts

If you want to read the machine code bootminor emits (for example,
`objdump -d a.out`), here's what you'll see:

- **Calling convention**: SysV AMD64. Args 0..5 in
  `rdi rsi rdx rcx r8 r9`. Return in `rax`.
- **Syscall convention**: Linux. Args 0..5 in
  `rdi rsi rdx r10 r8 r9`. Syscall number in `rax`. Return in `rax`.
- **Frame**: `push rbp; mov rbp, rsp; sub rsp, N` prologue; `mov rsp, rbp;
  pop rbp; ret` epilogue. N is patched to 16-byte alignment
  (`gen.luna:2934-2939`).
- **Locals**: each local occupies 8 bytes at `[rbp - k*8]` for k=1,2,...
  in first-assignment order.
- **String LEA**: `lea rax, [rip + disp32]` with `disp32` patched by the
  driver once the code/data section layout is final (`main2.luna:132-158`).
- **Heap LEA**: same pattern, pointing at the `heap_top` 8-byte cell at
  the very start of the data section. Every bump allocation reads, adds,
  writes-back this cell.
- **Data section layout**: `[heap_top_u64][str1_len_u64][str1_bytes][NUL]
  [str2_len_u64]...`. Strings pre-known at compile time live in
  `[data_off..)`; dynamic allocations live past `file_sz`.

---

## Appendix D — A complete, nontrivial program

Here's a standalone program you can compile and run end-to-end. It reads
up to 64 KB of stdin, computes a simple hash (FNV-1a 64-bit), and prints
the result in hex. It exercises strings, bit tricks, syscalls, and
output formatting — most of what this document covers.

```luna
const FNV_OFFSET: int = 0xcbf29ce484222325
const FNV_PRIME:  int = 0x100000001b3

fn fnv1a(@buf: int, @n: int) -> int
    meow @h: int = FNV_OFFSET
    meow @i: int = 0
    while @i < @n
        @h = @h ^ u8_at(@buf, @i)
        @h = @h * FNV_PRIME
        @i = @i + 1
    return @h

fn nibble_to_hex(@n: int) -> int
    if @n < 10
        return 48 + @n                 # '0'..'9'
    return 87 + @n                     # 'a'..'f' (97 - 10)

fn print_hex64(@x: int) -> int
    meow @out: int = new_str(16)
    meow @i: int = 15
    while @i >= 0
        str_set_byte(@out, @i, nibble_to_hex(@x & 15))
        @x = @x >> 4
        @i = @i - 1
    shine(@out)
    return 0

fn main() -> int
    @buf = new_str(65536)
    @n = sys_read(0, @buf, 65536)
    if @n <= 0
        shine("empty input")
        return 1
    @h = fnv1a(@buf, @n)
    print_hex64(@h)
    return 0
```

Compile and run under WSL:

```
./luna-mini.elf fnv.luna -o fnv
echo -n "hello" | ./fnv
# prints: a430d84680aabd0b
```

That's roughly the full end-to-end experience: write Luna, compile to ELF,
run it, no linker, no runtime, no surprises.
