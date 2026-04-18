# Luna Language Specification v4.2

**Version**: 4.2.0 "The Absolute"
**Status**: Production Ready
**Lead Architect**: LMDtokyo
**Release Date**: February 2026

---

## Abstract

Luna is a systems programming language designed for performance, safety, and developer productivity. It combines the low-level control of C with the safety guarantees of Rust and the expressiveness of modern functional languages. Luna v4.2 achieves **100% self-hosting** — the compiler is written entirely in Luna itself.

---

## 1. Design Philosophy

### 1.1 Core Principles

1. **Zero-Cost Abstractions** — High-level constructs compile to optimal machine code
2. **Memory Safety Without GC** — Ownership system prevents memory errors at compile time
3. **Fearless Concurrency** — Type system prevents data races
4. **Native Performance** — Direct compilation to machine code via Cranelift/LLVM
5. **Developer Experience** — Clean syntax, helpful errors, powerful tooling

### 1.2 The Cosmic Paradigm

Luna introduces "cosmic" keywords that make code intention explicit:

| Keyword | Purpose | Example |
|---------|---------|---------|
| `shine` | Emit/return from generator | `shine @value` |
| `eclipse` | `else if` chain branch (also: defer w/ expr) | `if X \n ... \n eclipse if Y \n ...` |
| `orbit` | Iterate with automatic resource cleanup | `orbit @item in @collection` |
| `phase` | Pattern matching with exhaustiveness checking | `phase @result` |
| `spawn` | Create concurrent task | `spawn process_data(@chunk)` |
| `seal` | Make binding immutable after initialization | `seal @config` |
| `nova` | Async/await boundary | `nova fetch(@url)` |
| `guard` | Early return on condition failure | `guard @x > 0 else return -1` |
| `meow` | Mutable binding (mutable after init) | `meow @counter: int = 0` |
| `defer` | Explicit defer (RAII cleanup) | `defer close(@file)` |
| `actor` | Actor declaration (owns state, handles messages) | `actor Counter ...` |
| `flow` | Streaming/pipeline block | `flow data \|> parse \|> validate` |
| `send` | Send message to actor/channel | `send @actor, @msg` |

### 1.3 Variable Binding Forms (canonical)

Luna has **four** ways to introduce a binding. Understanding them removes all syntactic ambiguity:

| Form | Mutability | Scope | Purpose |
|------|------------|-------|---------|
| `let x = 5` | **immutable by default** (add `mut`: `let mut x = 5`) | block-local | Standard local binding |
| `meow @x = 5` | **mutable** | block-local | Mutable local or module-level state |
| `seal @x = 5` | **immutable after init** | block-local | One-shot sealed binding (compile-time enforced) |
| `const X: int = 5` | **compile-time constant** | module-level | Pure constant, must be a literal/const-expr |

**The `@` prefix** is a convention for *runtime values* (variables and parameters) so they visually stand out from types, module-level consts, and function names. It is part of the identifier. Forms that introduce an `@`-prefixed name:

- Function parameters: `fn add(@a: int, @b: int) -> int`
- `meow`/`seal` bindings: `meow @counter = 0`
- `let` may bind either `@x` (runtime value, by convention) or plain `x` (e.g. for pattern destructuring).

There is **no `var` keyword** — the older `var` syntax was removed. Use `meow` for mutable locals.

---

## 2. Type System

### 2.1 Hindley-Milner Type Inference

Luna implements complete Hindley-Milner type inference with:

- **Algorithm W** for principal type computation
- **Let-polymorphism** for generic functions
- **Constraint propagation** for complex expressions

```luna
fn identity(@x) -> @x                    # Inferred: fn<T>(@x: T) -> T
fn compose(@f, @g, @x) -> @f(@g(@x))     # Inferred: fn<A,B,C>(fn(B)->C, fn(A)->B, A) -> C
```

### 2.2 Algebraic Data Types

```luna
type Option<T>
    Some(T)
    None

type Result<T, E>
    Ok(T)
    Err(E)

type List<T>
    Cons(T, List<T>)
    Nil
```

### 2.3 Traits and Implementations

```luna
trait Serialize
    fn to_bytes(@self) -> [u8]
    fn from_bytes(@data: [u8]) -> Self

impl Serialize for User
    fn to_bytes(@self) -> [u8]
        return json_encode(@self)

    fn from_bytes(@data: [u8]) -> User
        return json_decode(@data)
```

### 2.4 Variance and Subtyping

Luna supports full variance annotations:

- **Covariant** (`+T`): `Producer<+T>` — can upcast T
- **Contravariant** (`-T`): `Consumer<-T>` — can downcast T
- **Invariant** (default): exact type match required

---

## 3. Memory Model

### 3.1 Ownership Rules

1. Each value has exactly one owner
2. When owner goes out of scope, value is dropped
3. Ownership can be transferred (moved) or borrowed

### 3.2 Borrowing

```luna
fn process(@data: &[u8]) -> int          # Immutable borrow
fn modify(@data: &mut [u8])              # Mutable borrow

# Rules:
# - Any number of immutable borrows OR exactly one mutable borrow
# - Borrows must not outlive the owner
# - No aliasing of mutable references
```

### 3.3 Lifetimes

```luna
fn longest<'a>(@x: &'a str, @y: &'a str) -> &'a str
    guard @x.len() > @y.len() else return @y
    return @x
```

---

## 4. Concurrency Model

### 4.1 Structured Concurrency

```luna
fn parallel_process(@items: [Item]) -> [Result]
    @results: [Result] = []

    orbit @chunk in @items.chunks(4)
        spawn
            @partial = process_chunk(@chunk)
            sync.push(&mut @results, @partial)

    return @results
```

### 4.2 Synchronization Primitives

| Primitive | Description |
|-----------|-------------|
| `Mutex<T>` | Mutual exclusion lock |
| `RwLock<T>` | Reader-writer lock |
| `Channel<T>` | MPSC channel |
| `Atomic<T>` | Lock-free atomic operations |
| `Barrier` | Synchronization barrier |
| `Semaphore` | Counting semaphore |

---

## 5. Compilation Pipeline

### 5.1 Phases

```
Source (.luna)
    ↓ Lexer
Tokens
    ↓ Parser
AST (Abstract Syntax Tree)
    ↓ Type Checker (Hindley-Milner)
Typed AST
    ↓ Borrow Checker
Verified AST
    ↓ Monomorphization
Specialized AST
    ↓ Titan Optimizer (8 passes)
Optimized IR
    ↓ Cranelift/LLVM Backend
Machine Code (.exe/.so)
```

### 5.2 Titan Optimizer

The Titan optimizer performs 8 optimization passes:

1. **Dead Code Elimination** — Remove unreachable code
2. **Constant Folding** — Evaluate compile-time expressions
3. **Inlining** — Inline small/hot functions
4. **Loop Unrolling** — Unroll small fixed-count loops
5. **SROA** — Scalar Replacement of Aggregates
6. **CSE** — Common Subexpression Elimination
7. **Strength Reduction** — Replace expensive ops with cheaper ones
8. **Register Allocation** — Optimal register assignment

---

## 6. Standard Library

### 6.1 Core Modules (31 total)

| Module | Lines | Description |
|--------|-------|-------------|
| `io` | 1,892 | File I/O, streams, buffering |
| `net` | 2,156 | TCP/UDP sockets, DNS |
| `http` | 1,847 | HTTP/1.1 & HTTP/2 client/server |
| `json` | 1,234 | JSON parsing and generation |
| `crypto` | 2,891 | AES, SHA, RSA, Ed25519 |
| `sync` | 1,567 | Concurrency primitives |
| `collections` | 2,345 | Vec, HashMap, BTreeMap |
| `regex` | 1,456 | Regular expressions |
| `db` | 1,789 | Embedded key-value database |
| `time` | 987 | Date, time, duration |
| `math` | 1,123 | Numerics, linear algebra |
| `gui_native` | 3,456 | Native GUI widgets |
| ... | ... | ... |

### 6.2 Module Metrics

- **Total Files**: 25 bootstrap + 6 auxiliary = 31
- **Total Lines**: 45,413
- **Total Size**: ~1.7 MB
- **Self-Hosting**: 100% (30/30 modules native)

---

## 7. Tooling

### 7.1 CLI Commands

```bash
luna run <file.luna>          # Execute in VM mode
luna build <file.luna>        # Compile to native binary
luna check <file.luna>        # Type-check without running
luna fmt <file.luna>          # Format source code
luna lsp                      # Start Language Server
luna --self-compile           # Bootstrap self-compilation
luna --install-system         # System-wide installation
```

### 7.2 Language Server Protocol

The LSP server (`lsp.luna`) provides:

- Real-time diagnostics
- Code completion with type info
- Hover documentation
- Go-to-definition
- Find references
- Code formatting
- Semantic highlighting

### 7.3 VS Code Extension

- Syntax highlighting for cosmic keywords
- Integrated error checking
- Code snippets
- Build/run commands
- Debug support

---

## 8. Performance Characteristics

### 8.1 Benchmarks (vs competitors)

| Benchmark | Luna | Rust | C | Go | Python |
|-----------|------|------|---|----|----- |
| Fibonacci(45) | 1.0x | 1.0x | 1.0x | 1.8x | 45x |
| JSON parse 1MB | 1.0x | 0.9x | 0.8x | 1.5x | 12x |
| HTTP server RPS | 850K | 900K | 920K | 450K | 15K |
| Memory (hello world) | 1.2MB | 1.1MB | 0.8MB | 3.5MB | 12MB |
| Compile time (self) | 3.5s | — | — | — | — |

### 8.2 Binary Size

- **Minimal binary**: ~800 KB (static, stripped)
- **With stdlib**: ~2.5 MB
- **Full compiler**: ~4.8 MB

---

## 9. Safety Guarantees

Luna provides compile-time guarantees for:

1. **Memory Safety** — No buffer overflows, use-after-free, double-free
2. **Thread Safety** — No data races
3. **Null Safety** — No null pointer dereferences (Option<T> pattern)
4. **Type Safety** — No type confusion
5. **Resource Safety** — Automatic cleanup via RAII

---

## 10. Compatibility

### 10.1 Target Platforms

| Platform | Status | Notes |
|----------|--------|-------|
| Linux x86_64 | ✅ Full | Primary target |
| Windows x86_64 | ✅ Full | MSVC & MinGW |
| macOS x86_64 | ✅ Full | 10.15+ |
| macOS ARM64 | ✅ Full | M1/M2/M3 |
| Linux ARM64 | ✅ Full | Raspberry Pi 4+ |
| WebAssembly | 🔄 Beta | Browser & WASI |

### 10.2 FFI

```luna
extern "C"
    fn printf(@fmt: *const u8, ...) -> int
    fn malloc(@size: usize) -> *mut u8
    fn free(@ptr: *mut u8)
```

---

## 11. Future Roadmap

### v4.3 (Q2 2026)
- Incremental compilation
- Package manager (`luna pkg`)
- Improved error messages

### v5.0 (Q4 2026)
- Dependent types (limited)
- Effect system
- Formal verification tools

---

## 12. Reserved Keywords (canonical list — v4.3)

Synchronized with `src/core/lexer.luna`. Using any identifier in this list as a variable name is an error.

### Core declaration
`fn`, `let`, `const`, `meow`, `seal`, `struct`, `enum`, `impl`, `trait`, `type`, `pub`

### Control flow
`if`, `else`, `eclipse`, `unless`, `while`, `orbit`, `match`, `phase`, `guard`, `return`, `break`, `continue`, `pass`

### Literals / atoms
`true`, `false`, `nil`, `self`

### Operators (word form)
`and`, `or`, `not`, `in`, `as`, `then`

### Memory / safety
`mut`, `ref`, `move`, `freeze`, `drop`, `box`, `rc`, `weak`, `unsafe`, `asm`

### Concurrency / async
`spawn`, `nova`, `actor`, `flow`, `send`, `atomic`, `shine`, `defer`

### Contracts / generics
`require`, `ensure`, `where`, `for`, `do`

### Modules
`use`, `import`, `export`

---

## 13. Grammar Reference (EBNF)

```ebnf
Program         = { TopDecl } ;

TopDecl         = Function | Struct | Enum | Trait | Impl | TypeDecl | ConstDecl
                | Import | Export | Use ;

Function        = { Attribute } "fn" Ident [ Generics ] "(" [ Params ] ")"
                  [ "->" Type ] [ WhereClause ] Block ;
Params          = Param { "," Param } ;
Param           = [ "mut" | "ref" | "move" ] Ident ":" Type ;

Generics        = "<" TypeParam { "," TypeParam } ">" ;
TypeParam       = Ident [ ":" TraitBound { "+" TraitBound } ] ;
WhereClause     = "where" TypeParam { "," TypeParam } ;

Struct          = "struct" Ident [ Generics ] [ WhereClause ] ":" INDENT Field+ DEDENT ;
Field           = Ident ":" Type ;

Enum            = "enum" Ident [ Generics ] ":" INDENT Variant+ DEDENT ;
Variant         = Ident [ "(" Type { "," Type } ")" ] ;

Trait           = "trait" Ident [ Generics ] ":" INDENT Function+ DEDENT ;
Impl            = "impl" [ Generics ] Type [ "for" Type ] [ WhereClause ]
                  ":" INDENT Function+ DEDENT ;
TypeDecl        = "type" Ident [ Generics ] "=" Type ;
ConstDecl       = "const" Ident ":" Type "=" Expr ;

Type            = Ident [ Generics ]
                | "[" Type [ ";" Expr ] "]"
                | "(" Type { "," Type } ")"
                | "fn" "(" [ Type { "," Type } ] ")" [ "->" Type ]
                | "&" [ "mut" ] Type
                | "*" [ "const" | "mut" ] Type
                | Type "?" ;

Stmt            = LetStmt | MeowStmt | SealStmt | AssignStmt | IfStmt
                | WhileStmt | OrbitStmt | MatchStmt | GuardStmt | UnlessStmt
                | SpawnStmt | SendStmt | DeferStmt | EclipseStmt
                | ReturnStmt | BreakStmt | ContinueStmt | PassStmt
                | ExprStmt ;

LetStmt         = "let" [ "mut" ] Pattern [ ":" Type ] "=" Expr ;
MeowStmt        = "meow" Ident [ ":" Type ] "=" Expr ;
SealStmt        = "seal" Ident [ ":" Type ] "=" Expr ;
GuardStmt       = "guard" Expr "else" Block ;
UnlessStmt      = "unless" Expr Block ;
SpawnStmt       = "spawn" ( Expr | Block ) ;
SendStmt        = "send" Expr "," Expr ;
DeferStmt       = "defer" ( Expr | Block ) ;
EclipseStmt     = "eclipse" ( "if" Expr Block | Block ) ;

Expr            = Literal | Ident | Call | Index | Field
                | Binary | Unary | Lambda | Array | Dict | Struct_Lit
                | Range | IfExpr | MatchExpr | PhaseExpr
                | "nova" Expr       (* async *)
                | "move" Expr | "freeze" Expr | "drop" Expr
                | "box" Expr | "rc" Expr | "weak" Expr ;

Pattern         = "_" | Literal | Ident [ "(" Pattern { "," Pattern } ")" ]
                | "(" Pattern { "," Pattern } ")"
                | "[" Pattern { "," Pattern } [ "," ".." ] "]" ;

(* Indentation follows off-side rule like Python: INDENT/DEDENT tokens emitted by lexer *)
```

---

## 14. Legal

**Copyright © 2026 Luna Ecosystem**
**Lead Architect**: LMDtokyo

This specification and the Luna language implementation are released under the GNU General Public License v3.0 (GPLv3). See LICENSE file for details.

---

*"Luna — where safety meets performance, and code becomes poetry."*
