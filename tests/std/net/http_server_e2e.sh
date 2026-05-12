#!/usr/bin/env bash
# http_server_e2e.sh — Luna HTTP server talks to Luna HTTP client.
#
# Builds and runs the Luna server in the background, then runs the
# Luna client against it, then tears down the server. All pure Luna;
# no curl, no python.
#
# Usage (from repo root):
#     bash tests/std/net/http_server_e2e.sh

set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
SERVER_SRC="$ROOT/tests/std/net/http_server_app.luna"
CLIENT_SRC="$ROOT/tests/std/net/http_server_client.luna"

if ! command -v luna >/dev/null 2>&1; then
    echo "http_server_e2e: luna not on PATH, skipping" >&2
    exit 77
fi

export LUNA_PATH="$ROOT/std/runtime:$ROOT/std/core:$ROOT/std/std:$ROOT/std/net:$ROOT/std/ext"

# Launch the server. `luna run` blocks for the lifetime of the
# program — kill it after the client finishes.
luna run "$SERVER_SRC" >/dev/null 2>&1 &
SERVER_PID=$!
trap 'kill "$SERVER_PID" 2>/dev/null; wait "$SERVER_PID" 2>/dev/null' EXIT

# Build needs a moment, then the server bind+listen — generous wait.
sleep 0.6

out=$(luna run "$CLIENT_SRC" 2>&1)
rc=$?

echo "$out"
if [ "$rc" -ne 0 ]; then
    echo "http_server_e2e: client exit=$rc"
    exit 1
fi
echo "$out" | grep -q '^PASS:' || { echo "http_server_e2e: no PASS line"; exit 1; }
echo "http_server_e2e: OK"
