#!/usr/bin/env bash
# prefork_e2e.sh — fire 5 parallel clients at the pre-fork pool
# server. With 4 workers, the kernel should distribute work across
# them. We don't verify which worker handles which request — just
# that all 5 get their right path back without hanging.

set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
APP_SRC="$ROOT/tests/std/net/prefork_app.luna"
CLI_SRC="$ROOT/tests/std/net/concurrent_client.luna"

if ! command -v luna >/dev/null 2>&1; then
    echo "prefork_e2e: luna not on PATH, skipping" >&2
    exit 77
fi

export LUNA_PATH="$ROOT/std/runtime:$ROOT/std/core:$ROOT/std/std:$ROOT/std/net:$ROOT/std/ext"

APP_ELF="$ROOT/tests/std/net/prefork_app.elf"
CLI_ELF="$ROOT/tests/std/net/concurrent_client.elf"
luna build "$APP_SRC" >/dev/null 2>&1 || { echo "build app failed"; exit 1; }
luna build "$CLI_SRC" >/dev/null 2>&1 || { echo "build client failed"; exit 1; }

# concurrent_client.luna hardcodes port 18084; prefork_app uses 18089.
# Patch the client binary's port at the source level for this test.
PATCHED_CLI_SRC="$ROOT/tests/std/net/prefork_client.luna"
sed 's/18084/18089/g' "$CLI_SRC" > "$PATCHED_CLI_SRC"
luna build "$PATCHED_CLI_SRC" >/dev/null 2>&1
PATCHED_CLI_ELF="$ROOT/tests/std/net/prefork_client.elf"

WORK="$(mktemp -d -t luna-prefork-XXXXXX)"
trap 'pkill -KILL -f prefork_app.elf 2>/dev/null; rm -rf "$WORK" "$APP_ELF" "$CLI_ELF" "$PATCHED_CLI_SRC" "$PATCHED_CLI_ELF"' EXIT

"$APP_ELF" >/dev/null 2>&1 &
SP=$!
sleep 0.4

paths=(one two three four five)
client_pids=()
for p in "${paths[@]}"; do
    ( "$PATCHED_CLI_ELF" "/$p" > "$WORK/$p.out" 2>&1 ) &
    client_pids+=($!)
done
for pid in "${client_pids[@]}"; do
    wait "$pid"
done

fails=0
for p in "${paths[@]}"; do
    if grep -q "^PASS: /$p" "$WORK/$p.out"; then
        :
    else
        echo "FAIL on /$p:"
        sed 's/^/    /' "$WORK/$p.out"
        fails=$((fails + 1))
    fi
done

if [ "$fails" -eq 0 ]; then
    echo "prefork_e2e: OK — 5 parallel clients, all PASSed against 4 workers"
    exit 0
fi
echo "prefork_e2e: $fails of 5 failed"
exit 1
