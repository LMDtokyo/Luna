#!/usr/bin/env bash
# rate_limit_e2e.sh — spawn a sandbox redis-server on 127.0.0.1:16380,
# run the Luna rate-limit test suite against it, then kill the server.
#
# Usage (from repo root):
#     bash tests/std/ext/rate_limit_e2e.sh

set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
TEST="$ROOT/tests/std/ext/rate_limit_test.luna"

if ! command -v redis-server >/dev/null 2>&1; then
    echo "rate_limit_e2e: redis-server not installed, skipping" >&2
    exit 77
fi
if ! command -v luna >/dev/null 2>&1; then
    echo "rate_limit_e2e: luna not on PATH, skipping" >&2
    exit 77
fi

WORK="$(mktemp -d -t luna-ratelimit-XXXXXX)"
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
    echo "rate_limit_e2e: test exit=$rc"
    exit 1
fi

summary=$(echo "$out" | grep -E '^=== rate_limit: ' | tail -1)
if [[ ! "$summary" =~ ^===\ rate_limit:\ ([0-9]+)\ PASS,\ ([0-9]+)\ FAIL\ ===$ ]]; then
    echo "rate_limit_e2e: no summary line"
    exit 1
fi
p="${BASH_REMATCH[1]}"
f="${BASH_REMATCH[2]}"
if [ "$f" != "0" ]; then
    echo "rate_limit_e2e: $f failures"
    exit 1
fi
if [ "$p" -lt 8 ]; then
    echo "rate_limit_e2e: only $p assertions — sandbox probably wasn't reached"
    exit 1
fi
echo "rate_limit_e2e: OK ($p assertions)"
