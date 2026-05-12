#!/usr/bin/env bash
# memcached_e2e.sh — spawn a sandbox memcached on 127.0.0.1:21211, run
# the Luna memcached client test suite against it, kill the server.
#
# Usage (from repo root):
#     bash tests/std/ext/db/memcached_e2e.sh
#
# Exits 77 (skip) when memcached or the luna CLI is not installed.

set -u

ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
TEST="$ROOT/tests/std/ext/db/memcached_test.luna"

if ! command -v memcached >/dev/null 2>&1; then
    echo "memcached_e2e: memcached not installed, skipping" >&2
    exit 77
fi
if ! command -v luna >/dev/null 2>&1; then
    echo "memcached_e2e: luna not on PATH, skipping" >&2
    exit 77
fi

PORT=21211
memcached -l 127.0.0.1 -p "$PORT" -U 0 >/dev/null 2>&1 &
MP=$!
trap 'kill -KILL "$MP" 2>/dev/null' EXIT
sleep 0.4

export LUNA_PATH="$ROOT/std/runtime:$ROOT/std/core:$ROOT/std/std:$ROOT/std/net:$ROOT/std/ext:$ROOT/std/ext/db:$ROOT/std/ext/crypto"

out=$(luna run "$TEST" 2>&1)
rc=$?

echo "$out"
if [ "$rc" -ne 0 ]; then
    echo "memcached_e2e: test exit=$rc"
    exit 1
fi

summary=$(echo "$out" | grep -E '^=== memcached: ' | tail -1)
if [[ ! "$summary" =~ ^===\ memcached:\ ([0-9]+)\ PASS,\ ([0-9]+)\ FAIL\ ===$ ]]; then
    echo "memcached_e2e: no summary line"
    exit 1
fi
p="${BASH_REMATCH[1]}"
f="${BASH_REMATCH[2]}"
if [ "$f" != "0" ]; then
    echo "memcached_e2e: $f failures"
    exit 1
fi
if [ "$p" -lt 10 ]; then
    echo "memcached_e2e: only $p assertions — sandbox probably wasn't reached"
    exit 1
fi
echo "memcached_e2e: OK ($p assertions)"
