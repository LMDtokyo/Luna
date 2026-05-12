#!/usr/bin/env bash
# dns_resolve.sh — end-to-end DNS resolution against Google DNS.
#
# Runs the Luna client which sends a UDP query to 8.8.8.8 for
# one.one.one.one and expects the answer to be 1.1.1.1 or 1.0.0.1
# (Cloudflare's stable anycast addresses).
#
# Skipped (exit 77) when there's no internet — we check that with a
# 1-second TCP probe to 8.8.8.8:53 before running the test.

set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
CLIENT_SRC="$ROOT/tests/std/net/dns_resolve_client.luna"

if ! command -v luna >/dev/null 2>&1; then
    echo "dns_resolve: luna not on PATH, skipping" >&2
    exit 77
fi

# Internet reachability probe (TCP/53 is fine — we only need to know
# the path to 8.8.8.8 is up; the actual query goes over UDP).
if ! timeout 1 bash -c '(echo > /dev/tcp/8.8.8.8/53)' 2>/dev/null; then
    echo "dns_resolve: no route to 8.8.8.8:53, skipping" >&2
    exit 77
fi

export LUNA_PATH="$ROOT/std/runtime:$ROOT/std/core:$ROOT/std/std:$ROOT/std/net:$ROOT/std/ext"

out=$(luna run "$CLIENT_SRC" 2>&1)
rc=$?

echo "$out"
if [ "$rc" -ne 0 ]; then
    echo "dns_resolve: client exit=$rc"
    exit 1
fi
echo "$out" | grep -q '^PASS:' || { echo "dns_resolve: no PASS line"; exit 1; }
echo "dns_resolve: OK"
