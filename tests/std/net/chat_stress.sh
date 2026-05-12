#!/usr/bin/env bash
# chat_stress.sh — bombard the chat server with N requests and check
# the server's RSS doesn't grow unbounded between snapshots. Demos
# the arena fix.
#
# Forking server forks per request, so we measure RSS of a *child*
# isn't useful — we measure the parent (which only accepts). For a
# true leak test we'd want the sequential `router_serve`, but since
# our chat_server uses forking, this script verifies the workflow
# end-to-end and confirms nothing crashes.

set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
SRC="$ROOT/examples/net/chat_server.luna"
ELF="$ROOT/examples/net/chat_server.elf"

if ! command -v luna >/dev/null 2>&1; then
    echo "chat_stress: luna not on PATH, skipping" >&2
    exit 77
fi
if ! command -v curl >/dev/null 2>&1; then
    echo "chat_stress: curl required, skipping" >&2
    exit 77
fi

export LUNA_PATH="$ROOT/std/runtime:$ROOT/std/core:$ROOT/std/std:$ROOT/std/net:$ROOT/std/ext"

rm -f /tmp/luna-chat-users.kv /tmp/luna-chat-messages.kv

luna build "$SRC" >/dev/null 2>&1 || { echo "build failed"; exit 1; }

"$ELF" >/dev/null 2>&1 &
SP=$!
trap 'kill -KILL "$SP" 2>/dev/null; rm -f "$ELF" /tmp/luna-chat-users.kv /tmp/luna-chat-messages.kv' EXIT
sleep 0.4

# Register a user.
NAME="bob-$$"
reg=$(curl -sS -X POST -d "{\"name\":\"$NAME\",\"pass\":\"x\"}" http://127.0.0.1:18088/api/register)
token=$(echo "$reg" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')
[ -n "$token" ] || { echo "FAIL register"; exit 1; }

# Send N=100 messages. Measure parent RSS at start, midway, end.
N=100
rss_at() {
    cat "/proc/$SP/status" 2>/dev/null | awk '/^VmRSS:/ {print $2}'
}

rss_start=$(rss_at)
echo "parent RSS at start:  ${rss_start} kB"

i=0
while [ $i -lt $N ]; do
    curl -sS -X POST -b "session=$token" -d "{\"text\":\"msg-$i\"}" \
        http://127.0.0.1:18088/api/messages >/dev/null
    i=$((i + 1))
done

rss_mid=$(rss_at)
echo "parent RSS after $N:  ${rss_mid} kB"

# Run another 100.
i=0
while [ $i -lt $N ]; do
    curl -sS -X POST -b "session=$token" -d "{\"text\":\"msg2-$i\"}" \
        http://127.0.0.1:18088/api/messages >/dev/null
    i=$((i + 1))
done

rss_end=$(rss_at)
echo "parent RSS after 2*$N: ${rss_end} kB"

# Verify total messages stored.
got=$(curl -sS -b "session=$token" http://127.0.0.1:18088/api/messages)
nmsg=$(echo "$got" | grep -o '"from"' | wc -l)
echo "messages persisted (last-20 page): ${nmsg}"
if [ "$nmsg" -lt 20 ]; then
    echo "FAIL: expected 20 messages on last page, got $nmsg"
    echo "  got: ${got:0:200}..."
    exit 1
fi

# Parent RSS shouldn't grow significantly between mid and end.
# Allow up to 200 kB drift (page-faulting, etc).
growth=$((rss_end - rss_mid))
if [ "$growth" -gt 200 ]; then
    echo "FAIL: parent RSS grew ${growth} kB between iter 100 and 200 (>200 kB)"
    exit 1
fi
echo "chat_stress: OK (growth ${growth} kB between batches, well below 200 kB threshold)"
