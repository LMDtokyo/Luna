#!/usr/bin/env bash
# http_get.sh — end-to-end HTTP GET against python's http.server.
#
# Usage (from repo root):
#     bash tests/std/net/http_get.sh

set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
PORT=18081
CLIENT_SRC="$ROOT/tests/std/net/http_get_client.luna"

if ! command -v python3 >/dev/null 2>&1; then
    echo "http_get: python3 required, skipping" >&2
    exit 77
fi
if ! command -v luna >/dev/null 2>&1; then
    echo "http_get: luna not on PATH, skipping" >&2
    exit 77
fi

# Serving directory with a probe file the client can fetch.
SERVE_DIR=$(mktemp -d)
trap 'rm -rf "$SERVE_DIR"' EXIT
printf 'hello from python http.server' > "$SERVE_DIR/luna_http_probe.txt"

python3 -m http.server "$PORT" --bind 127.0.0.1 --directory "$SERVE_DIR" >/dev/null 2>&1 &
SERVER_PID=$!
trap 'kill "$SERVER_PID" 2>/dev/null; wait "$SERVER_PID" 2>/dev/null; rm -rf "$SERVE_DIR"' EXIT

# Give the server a moment to bind.
sleep 0.4

export LUNA_PATH="$ROOT/std/runtime:$ROOT/std/core:$ROOT/std/std:$ROOT/std/net:$ROOT/std/ext"

out=$(luna run "$CLIENT_SRC" 2>&1)
rc=$?

echo "$out"
if [ "$rc" -ne 0 ]; then
    echo "http_get: client exit=$rc"
    exit 1
fi
echo "$out" | grep -q '^PASS:' || { echo "http_get: no PASS line"; exit 1; }
echo "http_get: OK"
