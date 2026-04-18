# Bot examples

Two end-to-end examples of writing bots in Luna.

| File | Platform | What it does |
|---|---|---|
| [telegram_bot.luna](telegram_bot.luna) | Telegram | Long-polls `getUpdates`, replies to `/start`, `/echo <text>`, echoes any plain message. |
| [discord_bot.luna](discord_bot.luna) | Discord | Connects to Gateway v10, sends `IDENTIFY`, runs a heartbeat fibre, responds to `!ping` and `!echo <text>`. |

## Running

Set the credential as an environment variable, then run:

```sh
export TELEGRAM_BOT_TOKEN="123456:AAE..."
luna run examples/bots/telegram_bot.luna

export DISCORD_BOT_TOKEN="MTE5..."
luna run examples/bots/discord_bot.luna
```

## Dependencies

Both examples use the Luna standard library:

- `beam` — HTTP client
- `cascade` — WebSocket (Discord only)
- `lumen` — JSON parsing and composition
- `chrono` — time and sleep
- `cosmos` — environment variables
- `nebula` — concurrency / spawn (Discord only)

The `?` operator propagates `phase<T>` errors up the call stack.
