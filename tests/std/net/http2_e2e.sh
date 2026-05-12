#!/usr/bin/env bash
# http2_e2e.sh — end-to-end h2c GET against the nghttp2 reference server.
#
# Spawns `nghttpd --no-tls 8765` against a tmp document root, runs
# tests/std/net/http2_e2e_client.luna, and asserts the client exits 0
# with at least 5 PASS lines.
#
# Usage:
#     bash tests/std/net/http2_e2e.sh

set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
PORT="${H2_PORT:-8765}"
CLIENT_SRC="$ROOT/tests/std/net/http2_e2e_client.luna"

# nghttpd lives in the nghttp2-server / nghttp2 package depending on
# the distro. We probe both binary names plus the unit-test shortcut
# of LUNA_NGHTTPD.
NGHTTPD="${LUNA_NGHTTPD:-}"
if [ -z "$NGHTTPD" ]; then
    if command -v nghttpd >/dev/null 2>&1; then
        NGHTTPD="nghttpd"
    fi
fi

if [ -z "$NGHTTPD" ]; then
    echo "http2_e2e: nghttpd not installed (install nghttp2/nghttp2-server), skipping" >&2
    exit 77
fi

if ! command -v luna >/dev/null 2>&1; then
    echo "http2_e2e: luna not on PATH, skipping" >&2
    exit 77
fi

# Build a temp docroot with two probe files.
SERVE_DIR="$(mktemp -d)"
printf 'luna h2 probe\n' > "$SERVE_DIR/probe.txt"
printf 'second\n'         > "$SERVE_DIR/probe2.txt"

cleanup() {
    if [ -n "${SERVER_PID:-}" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -rf "$SERVE_DIR"
}
trap cleanup EXIT

# nghttpd in --no-tls mode accepts h2c with prior-knowledge (which is
# what our client does). -d sets the document root; the last arg is
# the port.
"$NGHTTPD" --no-tls -d "$SERVE_DIR" "$PORT" >/dev/null 2>&1 &
SERVER_PID=$!

# Give nghttpd a moment to bind.
sleep 0.4

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "http2_e2e: nghttpd failed to start (port $PORT in use?)" >&2
    exit 1
fi

export LUNA_PATH="$ROOT/std/runtime:$ROOT/std/core:$ROOT/std/std:$ROOT/std/net:$ROOT/std/ext"

out=$(luna run "$CLIENT_SRC" 2>&1)
rc=$?

echo "$out"

if [ "$rc" -ne 0 ]; then
    echo "http2_e2e: client exit=$rc"
    exit 1
fi

pass_count=$(echo "$out" | grep -c '^PASS:')
if [ "$pass_count" -lt 5 ]; then
    echo "http2_e2e: expected >=5 PASS lines, got $pass_count"
    exit 1
fi

echo "http2_e2e: OK ($pass_count PASS lines)"
