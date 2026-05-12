#!/usr/bin/env bash
# http_host_get.sh — end-to-end DNS + TCP + HTTP exercise.
#
# Resolves example.com via std/net/dns, opens a TCP connection,
# performs an HTTP/1.1 GET, expects 200 OK with a non-trivial body.
# All pure Luna; no curl, no FFI.
#
# Skipped (exit 77) when the network isn't reachable.

set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
CLIENT_SRC="$ROOT/tests/std/net/http_host_get_client.luna"

if ! command -v luna >/dev/null 2>&1; then
    echo "http_host_get: luna not on PATH, skipping" >&2
    exit 77
fi

# Need 8.8.8.8 for DNS and example.com:80 for HTTP. One probe is
# sufficient evidence the test can run.
if ! timeout 1 bash -c '(echo > /dev/tcp/8.8.8.8/53)' 2>/dev/null; then
    echo "http_host_get: no route to 8.8.8.8:53, skipping" >&2
    exit 77
fi

export LUNA_PATH="$ROOT/std/runtime:$ROOT/std/core:$ROOT/std/std:$ROOT/std/net:$ROOT/std/ext"

out=$(luna run "$CLIENT_SRC" 2>&1)
rc=$?

echo "$out"
if [ "$rc" -ne 0 ]; then
    echo "http_host_get: client exit=$rc"
    exit 1
fi
echo "$out" | grep -q '^PASS:' || { echo "http_host_get: no PASS line"; exit 1; }
echo "http_host_get: OK"
