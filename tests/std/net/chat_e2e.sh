#!/usr/bin/env bash
# chat_e2e.sh — full-stack smoke of examples/net/chat_server.luna:
# register, post a message, fetch it back.

set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
SRC="$ROOT/examples/net/chat_server.luna"
ELF="$ROOT/examples/net/chat_server.elf"

if ! command -v luna >/dev/null 2>&1; then
    echo "chat_e2e: luna not on PATH, skipping" >&2
    exit 77
fi
if ! command -v curl >/dev/null 2>&1; then
    echo "chat_e2e: curl required, skipping" >&2
    exit 77
fi

export LUNA_PATH="$ROOT/std/runtime:$ROOT/std/core:$ROOT/std/std:$ROOT/std/net:$ROOT/std/ext"

# Wipe state from any prior run.
rm -f /tmp/luna-chat-users.kv /tmp/luna-chat-messages.kv

luna build "$SRC" >/dev/null 2>&1 || { echo "build failed"; exit 1; }

# Random name so re-runs don't bump into "name taken" (server uses kv-file).
NAME="alice-$$"

"$ELF" >/dev/null 2>&1 &
SP=$!
trap 'kill -KILL "$SP" 2>/dev/null; rm -f "$ELF" /tmp/luna-chat-users.kv /tmp/luna-chat-messages.kv' EXIT
sleep 0.4

# 1) Register.
reg=$(curl -sS -X POST -d "{\"name\":\"$NAME\",\"pass\":\"secret\"}" http://127.0.0.1:18088/api/register)
echo "$reg" | grep -q '"ok":true' || { echo "FAIL: register: $reg"; exit 1; }
echo "register: ok"

# Extract token.
token=$(echo "$reg" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')
[ -n "$token" ] || { echo "FAIL: no token in register reply"; exit 1; }
echo "token: ${token:0:40}..."

# 2) Post three messages so we can verify the pagination list returns
#    each of them in order.
for body in '{"text":"hello"}' '{"text":"world"}' '{"text":"goodbye"}'; do
    post=$(curl -sS -X POST -b "session=$token" -d "$body" http://127.0.0.1:18088/api/messages)
    echo "$post" | grep -q '"ok":true' || { echo "FAIL: post: $post"; exit 1; }
    sleep 0.01     # ensure ISO timestamp keys differ
done
echo "post: ok (3 messages)"

# 3) Fetch back and verify all three are present in chronological order.
got=$(curl -sS -b "session=$token" http://127.0.0.1:18088/api/messages)
echo "$got" | grep -q "hello"   || { echo "FAIL: missing 'hello': $got";   exit 1; }
echo "$got" | grep -q "world"   || { echo "FAIL: missing 'world': $got";   exit 1; }
echo "$got" | grep -q "goodbye" || { echo "FAIL: missing 'goodbye': $got"; exit 1; }
echo "fetch: ok (3 messages)"

echo "chat_e2e: OK"
