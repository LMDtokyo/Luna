#!/usr/bin/env bash
# tcp_echo.sh — end-to-end TCP echo round-trip.
#
# Starts a Python echo server on 127.0.0.1:18080, runs the compiled
# Luna echo client, checks it printed PASS, kills the server.
#
# Usage (from repo root):
#     bash tests/std/net/tcp_echo.sh

set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
PORT=18080
CLIENT_SRC="$ROOT/tests/std/net/tcp_echo_client.luna"

if ! command -v python3 >/dev/null 2>&1; then
    echo "tcp_echo: python3 required for the echo server, skipping" >&2
    exit 77   # standard skip exit code
fi

if ! command -v luna >/dev/null 2>&1; then
    echo "tcp_echo: luna not on PATH, skipping" >&2
    exit 77
fi

# Echo server: accept one connection, echo first read, exit.
python3 -c "
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('127.0.0.1', $PORT))
s.listen(1)
c, _ = s.accept()
data = c.recv(4096)
c.send(data)
c.close()
s.close()
" &
SERVER_PID=$!

# Give the kernel a moment to bind+listen. A polling readiness check
# via /dev/tcp would consume the single accept slot, so we just wait.
sleep 0.3

# Configure module search so `import tcp` resolves in-checkout.
export LUNA_PATH="$ROOT/std/runtime:$ROOT/std/core:$ROOT/std/std:$ROOT/std/net:$ROOT/std/ext"

out=$(luna run "$CLIENT_SRC" 2>&1)
rc=$?

kill "$SERVER_PID" 2>/dev/null
wait "$SERVER_PID" 2>/dev/null

echo "$out"
if [ "$rc" -ne 0 ]; then
    echo "tcp_echo: client exit=$rc"
    exit 1
fi
echo "$out" | grep -q '^PASS:' || { echo "tcp_echo: no PASS line"; exit 1; }
echo "tcp_echo: OK"
