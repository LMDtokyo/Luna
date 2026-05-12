# Luna Bot — modular Telegram bot in Luna

A feature-rich Telegram bot built from a dozen small `.luna` files instead
of one monolith. Every command lives in a category-specific module so
new handlers can be dropped in without touching unrelated code.

## Layout

```
examples/luna_bot/
├── main.luna       — entry point: arg parsing, long-poll loop
├── api.luna        — Telegram Bot API plumbing (sendMessage etc.)
├── helpers.luna    — small pure-string utilities (_arg_after, _str_reverse, ...)
├── stats.luna      — file-backed counter store (messages, started_at)
├── help.luna       — static help / about texts
├── basic.luna      — /start /help /about /ping /time /uptime /stats
├── text.luna       — /echo /upper /lower /reverse /rot13 /len
├── crypto.luna     — /sha256 /hmac /uuid /random /dice /coin /randhex
├── encoding.luna   — /b64enc /b64dec /hexenc /hexdec /urlenc /urldec
├── world.luna      — /weather /quote /catfact /btc /eth /ip /myip /joke
├── notes.luna      — /note /notes /clearnotes (per-chat persistent)
└── dispatch.luna   — handle_message: maps text → handler
```

`main.luna` imports every module; `dispatch.luna` references symbols
from all handler modules. Imports are resolved by `bootstrap/luna-boot`'s
include path (the source-file directory plus `src/core` / `src/stdlib`).

## Build

From the repo root:

```sh
bootstrap/luna-boot examples/luna_bot/main.luna -o tg_bot --target linux
```

That produces a single ELF (~60 KB) dynamically linked against libc and
libcurl. Compile time is under a second on a laptop.

## Run

```sh
BOT_TOKEN="123456789:ABCdef..." ./tg_bot
```

You'll see:

```
luna telegram bot online — long-polling /getUpdates
<- [987654321] alice: /ping
<- [987654321] alice: /weather Tokyo
<- [987654321] alice: /sha256 hello
```

Stop with Ctrl-C.

## Commands

| Group | Command | Notes |
|---|---|---|
| Basics | `/start /help /about /ping /time /uptime /stats` | |
| Text   | `/echo /upper /lower /reverse /rot13 /len` | |
| Crypto | `/sha256 <text>` | SHA-256 hex |
|        | `/hmac <key> <msg>` | HMAC-SHA256 hex |
|        | `/uuid` | UUID v4 |
|        | `/random` `/dice` `/coin` | |
|        | `/randhex [N]` | N bytes of `getrandom`, hex-encoded (default 16) |
| Encoding | `/b64enc /b64dec /hexenc /hexdec /urlenc /urldec` | |
| World  | `/weather <city>` | wttr.in format=3 (no auth) |
|        | `/quote` | api.quotable.io random |
|        | `/catfact` | catfact.ninja |
|        | `/btc` `/eth` | coingecko USD price |
|        | `/ip <addr>` | ip-api.com geolocation |
|        | `/myip` | api.ipify.org |
|        | `/joke` | v2.jokeapi.dev (safe-mode) |
| Notes  | `/note <text>` | append to per-chat note file |
|        | `/notes` | list all notes for this chat |
|        | `/clearnotes` | wipe |

Anything else gets echoed back with a `luna echo:` prefix.

## Adding a command

1. Pick the right module (or create a new one) and write `fn cmd_foo(...)`.
2. Wire it up in `dispatch.luna`:
   ```luna
   if str_eq(@text, "/foo")
       return cmd_foo(@token, @chat_id)
   ```
3. If you made a new module, `import` it from `main.luna` after its
   dependencies but before `dispatch.luna`.

## Persistence files

The bot writes to:

- `/tmp/luna_bot_stat_messages` — counter
- `/tmp/luna_bot_stat_started_at` — Unix ts of last start
- `/tmp/luna_bot_notes_<chat_id>` — newline-separated notes

These survive restarts (you'll see uptime keep counting if the bot is
relaunched within the same boot — `started_at` is reset on every
`main()`, so uptime resets but message counts and notes persist).

## What this bot uses from the prelude

| Feature | Source |
|---|---|
| HTTP/HTTPS | `http_get`, `http_post_json` (libcurl-backed) |
| JSON parse | `json_get_int/str`, `json_pick_obj`, `json_find_key`, `json_array_at`, `json_subobj` |
| JSON build | `json_begin`, `json_field_int/str`, `json_end` |
| Crypto | `sha256_hex`, `hmac_sha256_hex` (pure-Luna in prelude) |
| Encoding | `base64_encode/decode`, `hex_encode/decode`, `url_encode/decode` |
| Random | `random_u32`, `random_range`, `rand_bytes`, `random_seed` |
| UUID | `uuid_v4` |
| Time | `iso_timestamp`, `now_unix`, `sleep` |
| Strings | `str_concat`, `str_substr`, `str_starts_with`, `str_to_upper/lower`, `str_trim`, ... |
| File I/O | `read_file`, `write_file` |
| Process | `env_get` (env vars), `arg(i)` (argv) |

The whole stack is built on `bootstrap/prelude.luna` (auto-prepended)
and the `extern "C" fn` libc + libcurl bindings declared inside it.
