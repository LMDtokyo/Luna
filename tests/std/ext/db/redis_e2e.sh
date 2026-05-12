#!/usr/bin/env bash
# redis_e2e.sh — spawn a sandbox redis-server on 127.0.0.1:16380, run
# the Luna redis client test suite against it, kill the server.
#
# Usage (from repo root):
#     bash tests/std/ext/db/redis_e2e.sh

set -u

ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
TEST="$ROOT/tests/std/ext/db/redis_test.luna"

if ! command -v redis-server >/dev/null 2>&1; then
    echo "redis_e2e: redis-server not installed, skipping" >&2
    exit 77
fi
if ! command -v luna >/dev/null 2>&1; then
    echo "redis_e2e: luna not on PATH, skipping" >&2
    exit 77
fi

WORK="$(mktemp -d -t luna-redis-XXXXXX)"
PORT=16380
redis-server --port "$PORT" --bind 127.0.0.1 --dir "$WORK" \
    --save "" --appendonly no --daemonize no \
    --logfile "$WORK/redis.log" --pidfile "$WORK/redis.pid" \
    >/dev/null 2>&1 &
RP=$!
trap 'kill -KILL "$RP" 2>/dev/null; rm -rf "$WORK"' EXIT
sleep 0.4

export LUNA_PATH="$ROOT/std/runtime:$ROOT/std/core:$ROOT/std/std:$ROOT/std/net:$ROOT/std/ext:$ROOT/std/ext/db:$ROOT/std/ext/crypto"

out=$(luna run "$TEST" 2>&1)
rc=$?

echo "$out"
if [ "$rc" -ne 0 ]; then
    echo "redis_e2e: test exit=$rc"
    exit 1
fi

# Verify the summary says all passed.
summary=$(echo "$out" | grep -E '^=== redis: ' | tail -1)
if [[ ! "$summary" =~ ^===\ redis:\ ([0-9]+)\ PASS,\ ([0-9]+)\ FAIL\ ===$ ]]; then
    echo "redis_e2e: no summary line"
    exit 1
fi
p="${BASH_REMATCH[1]}"
f="${BASH_REMATCH[2]}"
if [ "$f" != "0" ]; then
    echo "redis_e2e: $f failures"
    exit 1
fi
if [ "$p" -lt 5 ]; then
    echo "redis_e2e: only $p assertions — sandbox probably wasn't reached"
    exit 1
fi
echo "redis_e2e: OK ($p assertions)"
