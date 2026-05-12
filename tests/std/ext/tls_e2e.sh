#!/usr/bin/env bash
# tls_e2e.sh — end-to-end coverage for std/ext/tls.
#
# Starts a self-signed openssl s_server on 127.0.0.1:18443, then drives
# it from a Luna program compiled with `luna-mini --dynamic`. The
# server uses `-rev` so it echoes each received line back reversed —
# perfect for proving the bytes survive the TLS round-trip.
#
# Exit codes:
#   0 — all assertions pass.
#   1 — at least one assertion failed.
#  77 — environment is missing a dependency (skip).
#
# Run from anywhere:
#     bash tests/std/ext/tls_e2e.sh

set -u

ROOT="$(cd "$(dirname "$0")/../.." && cd .. && pwd)"

LUNA_HOME="${LUNA_HOME:-$HOME/.luna}"
LUNA_MINI="${LUNA_COMPILER:-$LUNA_HOME/bin/luna-mini}"
PRELUDE="${LUNA_PRELUDE:-$LUNA_HOME/lib/prelude.luna}"

# --- Pre-flight ------------------------------------------------------
if [ ! -x "$LUNA_MINI" ]; then
    echo "tls_e2e: luna-mini not at $LUNA_MINI (run install.sh first)" >&2
    exit 77
fi
if [ ! -f "$PRELUDE" ]; then
    echo "tls_e2e: prelude not at $PRELUDE (run install.sh first)" >&2
    exit 77
fi
if ! command -v openssl >/dev/null 2>&1; then
    echo "tls_e2e: openssl missing (need s_server)" >&2
    exit 77
fi

# Require a libssl.so.3 / libssl.so on the system — without it tls_init
# can't dlopen anything and every later assertion would skew.
if ! ldconfig -p 2>/dev/null | grep -qE '\blibssl\.so(\.3)?\b'; then
    if ! [ -e /usr/lib/x86_64-linux-gnu/libssl.so.3 ] \
       && ! [ -e /usr/lib64/libssl.so.3 ] \
       && ! [ -e /usr/lib/libssl.so.3 ] \
       && ! [ -e /usr/lib/x86_64-linux-gnu/libssl.so ]; then
        echo "tls_e2e: libssl.so.3 / libssl.so not present" >&2
        exit 77
    fi
fi

WORK="$(mktemp -d -t luna-tls-XXXXXX)"
PORT=18443
SRV_LOG="$WORK/server.log"
SRV_PID_FILE="$WORK/server.pid"
DRV_OUT="$WORK/driver.out"

cleanup() {
    if [ -f "$SRV_PID_FILE" ]; then
        local p
        p="$(cat "$SRV_PID_FILE" 2>/dev/null || true)"
        if [ -n "$p" ]; then
            kill -KILL "$p" 2>/dev/null || true
        fi
    fi
    # Best-effort sweep for any leftover s_server on our port.
    pkill -KILL -f "openssl s_server.*-accept $PORT" 2>/dev/null || true
    rm -rf "$WORK"
}
trap cleanup EXIT

# --- Generate a self-signed cert + key -------------------------------
CERT="$WORK/cert.pem"
KEY="$WORK/key.pem"
openssl req -x509 -newkey rsa:2048 -keyout "$KEY" -out "$CERT" -days 1 -nodes \
    -subj "/CN=127.0.0.1" >/dev/null 2>&1 || {
    echo "tls_e2e: cert generation failed" >&2
    exit 1
}

# --- Start openssl s_server in -rev mode -----------------------------
# -rev reverses every received line and writes it back; gives us a
# deterministic echo without writing a custom server.
( openssl s_server -accept "$PORT" -cert "$CERT" -key "$KEY" \
        -rev -quiet -naccept 4 \
        > "$SRV_LOG" 2>&1 ) &
SRV_PID=$!
echo "$SRV_PID" > "$SRV_PID_FILE"

# Wait for the server to bind. s_server doesn't log "ready", so we
# probe the port directly with `openssl s_client -connect` until it
# succeeds (or we time out).
i=0
ready=0
while [ $i -lt 40 ]; do
    if echo "Q" | openssl s_client -connect "127.0.0.1:$PORT" -quiet \
            < /dev/null > /dev/null 2>&1; then
        ready=1
        break
    fi
    if ! kill -0 "$SRV_PID" 2>/dev/null; then
        echo "tls_e2e: s_server died early" >&2
        sed 's/^/    /' "$SRV_LOG"
        exit 1
    fi
    sleep 0.1
    i=$((i + 1))
done
if [ "$ready" -ne 1 ]; then
    echo "tls_e2e: s_server did not become ready" >&2
    sed 's/^/    /' "$SRV_LOG"
    exit 1
fi

# The -rev server is configured with -naccept 4. We consumed one
# above for the readiness probe; up to three remain for the Luna
# client (it only needs one, but openssl's connect path occasionally
# retries on transient handshake hiccups).

# --- Build the driver: prelude + dependencies + tls.luna + test ------
# We can't go through `luna run` here because the CLI wrapper doesn't
# expose --dynamic. Concatenate the source tree by hand instead.
DRIVER_SRC="$WORK/driver.luna"
{
    cat "$PRELUDE"
    cat "$ROOT/std/net/tcp.luna"
    cat "$ROOT/std/net/dns.luna"
    cat "$ROOT/std/ext/tls.luna"
    cat "$ROOT/tests/std/ext/tls_test.luna"
} | grep -vE '^[[:space:]]*import[[:space:]]+' > "$DRIVER_SRC"

DRIVER_ELF="$WORK/driver.elf"
if ! "$LUNA_MINI" "$DRIVER_SRC" -o "$DRIVER_ELF" --dynamic > "$WORK/compile.log" 2>&1; then
    echo "tls_e2e: luna-mini --dynamic failed to compile" >&2
    sed 's/^/    /' "$WORK/compile.log"
    exit 1
fi
chmod +x "$DRIVER_ELF"

# --- Run the driver ---------------------------------------------------
DRV_RC=0
timeout 20 "$DRIVER_ELF" > "$DRV_OUT" 2>&1 || DRV_RC=$?
if [ "$DRV_RC" -ne 0 ]; then
    echo "tls_e2e: driver exited rc=$DRV_RC" >&2
    echo "--- driver output:"
    sed 's/^/    /' "$DRV_OUT"
    echo "--- server log:"
    sed 's/^/    /' "$SRV_LOG"
    exit 1
fi

# --- Assertions -------------------------------------------------------
pass=0
fail=0
check_line() {
    local label="$1"
    local pattern="$2"
    if grep -qE "$pattern" "$DRV_OUT"; then
        echo "[$label] PASS"
        pass=$((pass + 1))
    else
        echo "[$label] FAIL — no line matching: $pattern"
        fail=$((fail + 1))
    fi
}

check_line "tls_init"        '^INIT ok$'
check_line "tls_connect"     '^CONN ok$'
check_line "tls_write"       '^WRITE n=5$'
check_line "tls_read len>0"  '^READ len=[1-9][0-9]*$'
# The server is `openssl s_server -rev`, so "PING\n" comes back with
# the reversed payload "GNIP\n" somewhere in the response. Bytes
# 0x47 0x4e 0x49 0x50 0x0a in hex. We allow leading welcome bytes
# (some openssl versions send a banner even with -quiet) and trailing
# data by matching the hex as a substring.
check_line "tls_read echo"   '^READ_BYTES .*474e49500a'
check_line "tls_close"       '^CLOSE ok$'
check_line "double_close"    '^DOUBLE_CLOSE ok$'
check_line "closed_port"     '^CLOSED_PORT ok$'

echo
echo "=== tls_e2e: $pass PASS, $fail FAIL ==="
if [ "$fail" -gt 0 ]; then
    echo "--- full driver output:"
    sed 's/^/    /' "$DRV_OUT"
    echo "--- server log:"
    sed 's/^/    /' "$SRV_LOG"
fi
exit "$fail"
