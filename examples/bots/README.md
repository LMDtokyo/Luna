# Luna Bot Examples

Four files, two bots, two eras:

| File | Era | Style |
|---|---|---|
| [telegram_bot.luna](telegram_bot.luna) | cosmic (v4.3) | `beam`, `lumen`, `phase<T>?`, `cosmos` |
| [discord_bot.luna](discord_bot.luna) | cosmic (v4.3) | `cascade`, `nebula.spawn`, `beacon` |
| [telegram_bot_legacy.luna](telegram_bot_legacy.luna) | pre-L4 | `http_*`, `json_*`, byte buffers |
| [discord_bot_legacy.luna](discord_bot_legacy.luna) | pre-L4 | raw `ws_connect`, manual offsets |

Keep both sets around during the transition so the rename map can be audited side-by-side.

## Cosmic style — what changed

Every "before/after" pair demonstrates a concrete gain:

- **Error plumbing collapsed** — `if resp.ok == 0 { ... }` became `?`. The `Dim(Fault)` branch propagates automatically; the `Bright(value)` branch is unwrapped inline.
- **Domain-tagged errors** — `Fault.net("...")`, `Fault.parse("...")`, `Fault.io("...")` instead of integer sentinels (`-1`, `HTTP_ERROR = -1`).
- **JSON reads like data** — `lumen.compose { "chat_id": cid, "text": t }` instead of hand-escaped byte buffers.
- **Unified handle types** — `cascade.Link` wraps the TLS + WebSocket layering that used to be two separate integer handles.
- **Spawn is a language feature** — `nebula.spawn` (or the bare `spawn` keyword) starts a structured fibre; `beacon` lets parent and child share a flag race-free.

## Running

```bash
# Telegram
export TELEGRAM_BOT_TOKEN="123456:AAE..."
luna run examples/bots/telegram_bot.luna

# Discord
export DISCORD_BOT_TOKEN="MTE5...."
luna run examples/bots/discord_bot.luna
```

## What must still land before these run end-to-end

The stdlib rename applies the map in [docs/L4_RENAME_MAP.md](../../docs/L4_RENAME_MAP.md). Until those shims land, these files are *design documents* — they'll lex and parse with the v4.3 compiler, but the imports resolve to modules that are still being renamed.

Concretely pending:

1. **`beam`, `lumen`, `cosmos`, `nebula`, `cascade`, `chrono` aliases** — one-line files that re-export the existing `http`, `json`, `io`, `scheduler`, `net` (ws section), `time` modules under the new names.
2. **Method syntax on `phase<T>` / `seen<T>`** — `.at()`, `.as_int()`, `.as_text()`, `.items()`, `.is_ok()`, `.show()` need lowering to the registered `phase_*` / `seen_*` / `lumen_*` functions.
3. **`lumen.compose { ... }` block literal** — a parser rule for the structured dict literal. Current fallback: `lumen.compose_pairs([...])`.
4. **`arc.seconds(N)` / `arc.millis(N)` / `chrono.slumber(arc)`** — thin wrappers over the existing `time_*` API, mapping an `arc` struct to milliseconds.
5. **`veil` linked into `cascade.link_secure`** — the Rust-free TLS is at [src/stdlib/veil.luna](../../src/stdlib/veil.luna). Cascade's current `wss_connect` talks to the `luna_wss_*` FFI; that call site needs rerouting.
6. **`beacon`** — the atomic-int primitive is already in [src/stdlib/sync.luna](../../src/stdlib/sync.luna) as `AtomicInt`; needs a typed `beacon` alias with `.new(v)`, `.read()`, `.flash(v)`, `.bump(d)`.
7. **`aether.cosmos_peek` → `cosmos.peek`** — one-line shim wrapping [src/core/aether.luna:cosmos_peek](../../src/core/aether.luna) into the new module.

All of the above are additive — they don't break the legacy bots, which keep using the old names via the transition-period aliases documented in the rename map (section on deprecation policy).
