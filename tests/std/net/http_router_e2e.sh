#!/usr/bin/env bash
# http_router_e2e.sh — exercise std/net/http_router via 4 round-trips
# from a Luna client to a Luna server. All pure Luna.
#
# Usage (from repo root):
#     bash tests/std/net/http_router_e2e.sh

set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
SERVER_SRC="$ROOT/tests/std/net/http_router_app.luna"
CLIENT_SRC="$ROOT/tests/std/net/http_router_client.luna"

if ! command -v luna >/dev/null 2>&1; then
    echo "http_router_e2e: luna not on PATH, skipping" >&2
    exit 77
fi

export LUNA_PATH="$ROOT/std/runtime:$ROOT/std/core:$ROOT/std/std:$ROOT/std/net:$ROOT/std/ext"

luna run "$SERVER_SRC" >/dev/null 2>&1 &
SERVER_PID=$!
trap 'kill "$SERVER_PID" 2>/dev/null; wait "$SERVER_PID" 2>/dev/null' EXIT

# Compile + bind takes a moment.
sleep 0.6

out=$(luna run "$CLIENT_SRC" 2>&1)
rc=$?

echo "$out"
if [ "$rc" -ne 0 ]; then
    echo "http_router_e2e: client exit=$rc"
    exit 1
fi
echo "$out" | grep -q '^PASS:' || { echo "http_router_e2e: no PASS line"; exit 1; }
echo "http_router_e2e: OK"
