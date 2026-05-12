# Luna Project Layout

How to organize a Luna project — the convention every `luna new` and `luna
build` understands. This is the equivalent of FSD for the frontend or Go's
`cmd/`/`internal/`/`pkg/` for the backend, tuned to Luna's tier model.

If you read [`ARCHITECTURE.md`](ARCHITECTURE.md), you already know the idea:
the stdlib is layered (T0–T8), modules at tier N only import from tier ≤N−1.
Your project mirrors that, just at a smaller scale.

---

## Three sizes

Pick the smallest layout that fits. Adding structure later is one `git mv`
per file; living with structure you don't need is friction every day.

### 1. Single file

For scripts, demos, one-shot tools.

```
hello.luna
```

Run with `luna run hello.luna`. No manifest, no folders, no ceremony.
When the file pushes past ~300 lines or you start copy-pasting a helper into
a second file, graduate to **flat**.

### 2. Flat project

For small apps, bots, CLIs that fit in 5–15 modules.

```
my-app/
├── main.luna            # fn main()
├── luna.toml            # manifest
├── api.luna             # any other module, flat
├── db.luna
└── README.md
```

`luna new my-app` produces this. Imports stay flat: `import db` finds
`db.luna` next to `main.luna`. No subdirs, no rules to memorize.

When you find yourself adding a third or fourth "kind" of file (data
types vs. HTTP vs. parsing vs. CLI handlers), and the directory listing
stops fitting on a screen, graduate to **layered**.

### 3. Layered project

For serious apps — multi-route servers, bots with many commands, CLIs
with multiple subcommands.

```
my-app/
├── main.luna            # composition root: wires layers, calls fn main()
├── luna.toml
├── core/                # pure domain: types, business rules, NO I/O
│   ├── user.luna
│   └── order.luna
├── lib/                 # utilities: combines core + stdlib
│   ├── validate.luna
│   └── format.luna
├── io/                  # the world: HTTP, DB, files, processes
│   ├── db.luna
│   └── http_api.luna
├── handlers/            # composition leaves: one file per route/command
│   ├── login.luna
│   ├── logout.luna
│   └── orders.luna
├── tests/               # mirrored tree
│   ├── core/user_test.luna
│   ├── lib/validate_test.luna
│   └── io/db_test.luna
├── assets/              # html templates, SQL migrations, static files
├── scripts/             # one-off bash: deploy.sh, seed.sh
└── README.md
```

`luna new my-app --layered` produces an empty version of this tree with
one placeholder file in each layer.

### 4. Library project

For reusable code published via `luna pkg`.

```
my-lib/
├── my_lib.luna          # public face: re-exports + the documented surface
├── luna.toml
├── internal/            # private modules, not part of the public API
│   ├── parser.luna
│   └── encoder.luna
├── tests/
│   ├── my_lib_test.luna
│   └── internal/parser_test.luna
└── README.md
```

`luna new my-lib --lib` produces this. The convention: **one entry file
named after the project**, anything else is internal until the library
explicitly re-exports it.

---

## The four layers explained

Every layered project has the same four layers plus a composition root.
Each layer can import from any lower layer — never the reverse.

| Layer | Folder | What lives there | May import |
|---|---|---|---|
| **App** | `main.luna` | Composition root. Builds the dependency graph, calls `fn main()`. Has the only entry point. | anything |
| **Handlers** | `handlers/` | One file per HTTP route / CLI subcommand / message handler. Thin glue between the outside request and your `io`/`lib`/`core`. | io, lib, core, stdlib |
| **IO** | `io/` | The world: HTTP clients, DB drivers, file readers, process runners, socket servers. Anything that touches outside your process. | lib, core, stdlib T4–T6 |
| **Lib** | `lib/` | Utilities that combine `core` types with stdlib helpers: validators, formatters, parsers without I/O. | core, stdlib T3–T5 |
| **Core** | `core/` | Pure domain. Your business types (`User`, `Order`, `Quote`), the rules they obey. No file reads, no network, no logging. | stdlib T3 only |

### Why the direction matters

The rule **"never import upward"** means:

- `core/user.luna` can never `import db` — your `User` type doesn't depend
  on Postgres existing. Tomorrow you swap Postgres for SQLite, `core` does
  not change.
- `lib/validate.luna` can never `import http_api` — validation has nothing
  to do with HTTP.
- `io/db.luna` can never `import login` — the DB driver doesn't know what
  uses it.

Imports flow downward. Composition flows downward. That's it.

When you're tempted to break the rule, **the file is in the wrong layer.**
A "validator that needs the DB" is actually a handler, not a lib. Move it.

---

## Import resolution

`import foo` finds `foo.luna` via a search path:

1. The directory of the file doing the import.
2. Every direct subdirectory of `luna.toml`'s directory that contains a
   `.luna` file. **This is what makes `import user` find `core/user.luna`
   without you setting `LUNA_PATH`.**
3. `$LUNA_PATH` (colon-separated, for custom search dirs).
4. `~/.luna/lib/std/` — shipped stdlib.
5. `~/.luna/lib/` — prelude and bare modules.

Because step 2 is flat, **module names must be unique across all layers
of one project**. Two files both named `user.luna` (one in `core/`, one in
`io/`) is an error. Use distinct names — `user.luna` and `user_repo.luna`,
or `user.luna` and `user_api.luna`.

Subdirectories named `tests/`, `assets/`, `scripts/`, `docs/`, `examples/`,
`.luna/`, `target/`, `build/`, `dist/`, `node_modules/` are **never** added
to the search path. Anything else is.

---

## Naming

- **Files and modules: lowercase `snake_case`.** `user_repo.luna`, not
  `UserRepo.luna` or `user-repo.luna`. The file name (without `.luna`) is
  the import name: `import user_repo`.
- **One concept per file.** `user.luna` defines the `User` type and the
  functions that operate on it. If two unrelated types end up in the same
  file, split.
- **Tests are `*_test.luna`** under `tests/`, mirroring the source layout.
  `core/user.luna` is tested by `tests/core/user_test.luna`.
- **No `mod` or `package` declaration** inside files. The file name is the
  module name. There is no separate namespace.
- **No leading underscore for "private".** Luna has no visibility modifier
  yet; treat functions documented in module-level comments as the public
  surface and the rest as internal-by-convention.

---

## Where everything else goes

| Need | Folder |
|---|---|
| Database migrations | `assets/migrations/` |
| HTML / template files | `assets/templates/` |
| Static images, JSON seeds | `assets/` |
| Default config files (`.toml`, `.yaml`) | `configs/` or repo root |
| Deploy / seed / one-off scripts | `scripts/` |
| Sample runs, demos | `examples/` |
| Generated binaries | repo root (gitignored: `*.elf`) |
| Vendored packages | `.luna/pkgs/` (created by `luna pkg add`) |
| Secrets, credentials | **never in repo.** Read from env or a secret manager. `.env` goes in `.gitignore`. |

---

## Tests

- **Separate `tests/` tree, mirroring source.** Tests don't ship in the
  binary; the compiler today bundles every `.luna` it sees, so colocating
  `_test.luna` next to source files would either inflate `main.elf` or
  require build-time filtering. Mirroring keeps it simple.
- One test file per source file: `core/user.luna` ↔ `tests/core/user_test.luna`.
- Tests use the `test` stdlib module:
  `assert_eq_int`, `assert_eq_str`, `test_summary`. See
  [`std/std/test.luna`](../std/std/test.luna).
- A run-tests script at the repo root (`run_tests.sh` or `tests/run_all.sh`)
  walks `tests/` and runs each `*_test.luna`.

---

## What `luna new` scaffolds

```sh
luna new my-app                # flat project (current default)
luna new my-app --layered      # layered: core/lib/io/handlers/tests
luna new my-app --lib          # library: <name>.luna + internal/ + tests/
```

Pick `--layered` when you already know the project will outgrow flat —
typically anything with multiple routes, multiple commands, or multiple
external services.

Migrate flat → layered later by `git mv`-ing modules into the right folder.
Imports keep working because resolution is by file name, not by path.

---

## Comparison

### vs. Go (`cmd/`, `internal/`, `pkg/`)

- `main.luna` ≈ `cmd/<binary>/main.go` — the binary entry point.
- `core/` ≈ `internal/` — private to this project, never published.
- `lib/` ≈ `pkg/` — reusable building blocks.
- `io/` is the layer Go usually scatters across `internal/storage/`,
  `internal/api/`, `internal/queue/`. Luna pulls them all into one layer
  because the boundary that matters is "does this touch the world", not
  "which external thing."

### vs. FSD (frontend)

- Four layers instead of seven. FSD's `app/pages/widgets/features/entities/shared`
  is right for UI work where the page is a first-class unit. Backend / CLI
  / systems work doesn't have pages, so the layering collapses.
- FSD's "shared" layer maps to `core/` + `lib/` here, split because in
  Luna the line between "pure data" and "data + I/O-free utility" is
  meaningful: `core/` is type-only, `lib/` does small computations.

### vs. Rust (Cargo workspaces)

- No equivalent of `Cargo.toml` workspaces yet — `luna.toml` is one
  package. Multi-package workspaces are on the roadmap, not the default.
- `examples/` and `tests/` are top-level here, same as Rust.

---

## Migrating an existing flat project

1. Decide which file is `core` (pure types), which is `lib` (utility),
   which is `io` (touches the outside), which is `handler` (composition).
   Read each file's imports — if it imports `http`/`tcp`/`sqlite`/`process`,
   it's `io`. If it only imports stdlib basics (`str`, `vec`, `time`,
   `json`), it's `core` or `lib`.
2. `mkdir core lib io handlers tests`
3. `git mv api.luna handlers/`, `git mv db.luna io/`, etc.
4. `luna run` — should still pass, because subdir auto-discovery handles
   imports.
5. Audit imports: any `core` file that imports an `io` module is wrong.
   Move the file to the right layer or split it.
6. Add `tests/<layer>/<name>_test.luna` to lock in the layout.

The whole migration is mechanical and reversible.

---

## FAQ

**Why three (or four) layers and not five or seven?**
Empirically, backend/CLI projects have three concerns: pure logic,
side-effecting drivers, and the glue. `handlers/` is a fourth because
"glue lives in main.luna" stops scaling at ~5 routes/commands. Five
layers is the next natural breakpoint, but if you're there you probably
need a workspace, not more layers in one package.

**My helper feels half-core, half-lib. Where does it go?**
If it touches data of a single domain type only (`User`-formatting) →
`core/`. If it stitches multiple types together → `lib/`. If you can't
tell, default to `lib/`.

**Can I nest layers? `core/auth/` and `core/billing/`?**
Today, no — the import resolver only adds direct subdirs to the search
path. Practically, until your project has 50+ files in `core/`, flat is
fine. Beyond that, talk to us — nested layers will need a small change
to the wrapper.

**Where do I put the dependency graph documentation?**
A short section in `README.md` describing each layer's purpose and one
example import. That's enough. Don't generate Graphviz unless it earns
its keep.
