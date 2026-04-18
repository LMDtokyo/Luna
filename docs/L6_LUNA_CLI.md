# `luna` — the one-binary toolchain

Phase L6 of Lunar Independence: a single statically-linked `luna` executable replaces `cargo`, `rustc`, `npm`, `make`, and the system linker for a Luna project. Users install one file; everything else flows from it.

## Invariants

- **No external toolchain.** `luna build hello.luna` writes a native binary without invoking `cc`, `link.exe`, `lld`, `ld`, or anything that lives in a system `$PATH`. Forge (L1) emits code; Aether (L2) provides syscalls; the PE/ELF/Mach-O writer bakes the container; an internal linker resolves symbols between translation units.
- **No hidden network.** `luna orbit add <pkg>` is the only command that touches the network, and only when the user runs it.
- **Self-hosting.** `luna --self-compile` rebuilds `luna` from source in under 4 s on a 2024-class laptop.
- **Platform parity.** Linux x86-64, Windows x86-64, and macOS ARM64 are first-class. BSD is best-effort.

## Command grammar

```
luna <command> [args...] [flags...]
```

### Project lifecycle

| command | effect |
|---|---|
| `luna init [name]` | Create `luna.orbit` manifest + `src/main.luna` scaffold. |
| `luna run [file.luna \| --]` | Compile in-memory and execute. `--` reads from stdin. |
| `luna build [file.luna]` | Produce a native binary in `.luna/out/`. Defaults to release. |
| `luna build --debug` | Debug build (inline source references, no opt). |
| `luna build --target=<triple>` | Cross-compile. Triples: `linux-x86_64`, `windows-x86_64`, `macos-arm64`, `wasm32-phase`. |
| `luna check [file.luna]` | Full lex + parse + type + borrow check, no codegen. Fastest failure path for CI. |
| `luna fmt [file.luna \|.]` | Auto-format; `.` recurses the project. |
| `luna fmt --cosmic` | Rewrite legacy names to cosmic equivalents (applies L4_RENAME_MAP). |
| `luna test [pattern]` | Run every `fn test_*` in the project (optionally filtered). |
| `luna phase` | REPL (named for moon-phase, not the type). `:q` quits, `:t expr` shows the inferred type. |

### Packages — the `orbit` subcommand

`luna.orbit` is the manifest; packages live in `.luna/orbits/<name>/<version>/`.

| command | effect |
|---|---|
| `luna orbit add <name>[@<ver>]` | Register a dependency; fetches + verifies signature. |
| `luna orbit remove <name>` | Drop from manifest + prune. |
| `luna orbit list` | Show resolved tree with versions. |
| `luna orbit update` | Bump versions within the manifest's constraint bounds. |
| `luna orbit publish` | Push the current package to the registry (manifest `publish = true` required). |
| `luna orbit verify` | Offline: re-check lockfile hashes against on-disk contents. |

The registry is a content-addressed store. Package ids are `name@version` plus a 32-byte BLAKE3 hash (shown as `sigil<32>`). A lockfile pins every transitive dep to its hash; `luna orbit verify` is reproducible.

### Diagnostics

| command | effect |
|---|---|
| `luna audit --mutables` | List every `meow` global across the project. Helps catch accidental shared mutability. |
| `luna audit --ffi` | List every `extern fn` and the ABI / library it targets. |
| `luna audit --unsafe` | List every `unsafe { ... }` block, the file, and the line. |
| `luna stats` | Line counts per module, optimizer pass frequencies, build time breakdown. |
| `luna explain E0382` | Show the long-form description of an error code (Rust-style E-codes, Luna-branded). |

### Language tooling

| command | effect |
|---|---|
| `luna lsp` | Start the Language Server (stdio JSON-RPC). |
| `luna doc [--open]` | Generate HTML docs from doc comments; `--open` launches the OS browser (via `aether.cosmos_invoke_browser`). |
| `luna bench` | Run every `fn bench_*` with microsecond timing and stability bucketing. |
| `luna disasm [file.luna] [fn]` | Dump the Forge-emitted machine code for one function, annotated with source lines. |
| `luna --self-compile` | Bootstrap test: parse + type-check + codegen the compiler's own sources. |
| `luna --version` | Prints `luna 4.3.0 "Aether"`. |

## Manifest: `luna.orbit`

TOML-ish but parsed by the `lumen` module (JSON-compatible with `=` instead of `:`):

```luna
[project]
name = "my-bot"
version = "0.1.0"
edition = "2026"
license = "GPLv3"
entry = "src/main.luna"

[build]
target = "linux-x86_64"
optimise = "release"

[deps]
tg = { orbit = "telegram-cosmic", version = "^0.3" }
# Git deps are explicit — no hidden redirection:
discord = { orbit = "git:github.com/example/discord-luna.git", rev = "v0.2.0" }

[publish]
registry = "https://orbits.luna-lang.org"
signed_by = "F3A1:...:9DBE"      # Ed25519 key fingerprint
```

## On-disk layout

```
my-bot/
├── luna.orbit            # manifest
├── src/
│   ├── main.luna
│   └── ...
├── examples/             # each file is `luna run`able
├── tests/                # each `fn test_*` is picked up by `luna test`
├── benches/              # each `fn bench_*` is picked up by `luna bench`
└── .luna/
    ├── out/              # compiled artefacts (gitignore)
    ├── orbits/           # fetched dep packages (gitignore)
    ├── cache/            # type/ast cache keyed on source sigil<32>
    └── lock.orbit        # deterministic dep resolution (commit)
```

## Implementation modules (internal)

| file | responsibility |
|---|---|
| `src/core/main.luna` | Top-level driver; argv parsing; subcommand dispatch. |
| `src/core/forge*.luna` | Machine-code emission (see L1). |
| `src/core/aether.luna` | OS interface (see L2). |
| `src/core/titan_opt.luna` | Optimisation. |
| `src/stdlib/luna_pkg.luna` | Package graph, resolver, registry client. To be renamed `luna_orbit.luna`. |
| `src/stdlib/veil.luna` | HTTPS for the registry client (see L3). |

The `luna` binary is produced by running `forge` + internal PE/ELF writer over the bootstrap source set. On a fresh system where no `luna` exists yet, the user downloads one pre-built binary; from that point forward every subsequent `luna` is built by the previous `luna`.

## Cross-check against invariants

| invariant | enforced by |
|---|---|
| No external toolchain | `forge` writes the final object bytes; there is no shell-out to `cc`/`link.exe` in the bootstrap code path. `luna audit --ffi` flags any attempt to introduce one. |
| No hidden network | All `beam.*` HTTPS calls funnel through one module (`src/stdlib/beam_client.luna`); `luna audit --net` enumerates call sites. |
| Self-hosting | CI runs `luna --self-compile` on every commit; budget is a 4 s max wall time. |
| Platform parity | The CI matrix builds the binary on Linux, Windows, and macOS. `luna test` runs on each. |

## Non-goals

- No plugin system. Compiler extensions land in-tree or not at all.
- No language-server-over-WebSocket. Stdio only (simpler, sandboxed).
- No global install command — we do not `sudo`. The binary is relocatable; users drop it anywhere on `$PATH`.

## What's next

L6 is a design document only so far. The milestones to make `luna` the one-binary toolchain are:

1. **Forge ELF/PE writer** — currently missing; needed before `luna build` can emit a runnable native binary.
2. **Orbit resolver** — graph walk and lockfile generation in pure Luna using the existing `luna_pkg.luna` foundations.
3. **`luna fmt --cosmic`** — the easy one; mostly a find/replace driven by `docs/L4_RENAME_MAP.md`.
4. **Cross-compilation harness** — per-target calling conventions in forge (Win64 vs SysV), plus per-target aether ABI (stdcall vs syscall vs libSystem).

Each milestone has a dedicated phase in the roadmap; this document is the shape they're aiming for.
