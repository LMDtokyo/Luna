#!/usr/bin/env bash
# concurrent_e2e.sh — fire 5 parallel clients against a Luna server
# running in fork-per-connection mode. Each client targets a distinct
# path; the server echoes it back. We verify every client got its OWN
# path back (proves requests didn't interleave) and that all five
# PASS.
#
# Stable now that the server forks instead of threading — each child
# has its own COW heap, so bootminor's non-thread-safe bump allocator
# isn't an issue.
#
# Usage:
#     bash tests/std/net/concurrent_e2e.sh

set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
APP_SRC="$ROOT/tests/std/net/concurrent_app.luna"
CLI_SRC="$ROOT/tests/std/net/concurrent_client.luna"

if ! command -v luna >/dev/null 2>&1; then
    echo "concurrent_e2e: luna not on PATH, skipping" >&2
    exit 77
fi

export LUNA_PATH="$ROOT/std/runtime:$ROOT/std/core:$ROOT/std/std:$ROOT/std/net:$ROOT/std/ext"

# Pre-compile both binaries so the parallel launches don't all hit
# the compiler simultaneously.
APP_ELF="$ROOT/tests/std/net/concurrent_app.elf"
CLI_ELF="$ROOT/tests/std/net/concurrent_client.elf"
luna build "$APP_SRC" >/dev/null 2>&1 || { echo "build app failed"; exit 1; }
luna build "$CLI_SRC" >/dev/null 2>&1 || { echo "build client failed"; exit 1; }

WORK="$(mktemp -d -t luna-concurrent-XXXXXX)"
# Note: the server is a multithreaded bootminor binary that doesn't
# react to plain SIGTERM while threads are mid-syscall, so we go
# straight to SIGKILL. `wait "$SP"` would hang for the same reason
# and is intentionally omitted.
trap 'kill -KILL "$SP" 2>/dev/null; rm -rf "$WORK" "$APP_ELF" "$CLI_ELF"' EXIT

"$APP_ELF" >/dev/null 2>&1 &
SP=$!
sleep 0.4

paths=(one two three four five)
client_pids=()
for p in "${paths[@]}"; do
    ( "$CLI_ELF" "/$p" > "$WORK/$p.out" 2>&1 ) &
    client_pids+=($!)
done
# Wait only on the clients — bare `wait` would block on the server too.
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
    echo "concurrent_e2e: OK — 5 parallel clients, all PASSed"
    exit 0
fi
echo "concurrent_e2e: $fails of 5 failed"
exit 1
