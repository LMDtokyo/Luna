#!/usr/bin/env bash
# tls_server_e2e.sh — end-to-end coverage for std/ext/tls_server.
#
# Compiles a tiny Luna TLS server that:
#   1. Listens on 127.0.0.1:18444.
#   2. Loads a self-signed cert + key from /tmp.
#   3. Accepts one TCP connection, wraps it with tls_server_accept.
#   4. Reads one chunk (the TLS-decrypted client bytes — typically a
#      handful of HTTP request bytes when curl is the client, or nothing
#      with `openssl s_client -quiet </dev/null`).
#   5. Writes "HELLO\n" back over the TLS channel.
#   6. Cleanly tears down and exits.
#
# Then connects with `openssl s_client` and asserts "HELLO" appears in
# the decrypted output.
#
# Skips with exit 77 if any prerequisite is missing.
#
# Usage:
#     bash tests/std/ext/tls_server_e2e.sh

set -u

LUNA_MINI="${LUNA_MINI:-$HOME/.luna/bin/luna-mini}"
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"

if [ ! -x "$LUNA_MINI" ]; then
    echo "tls_server_e2e: luna-mini not at $LUNA_MINI; skip" >&2
    exit 77
fi
if ! command -v openssl >/dev/null 2>&1; then
    echo "tls_server_e2e: openssl not on PATH; skip" >&2
    exit 77
fi

# libssl.so.3 sanity check — tls_server dlopens it at runtime.
LIBSSL_FOUND=0
for p in \
    /lib/x86_64-linux-gnu/libssl.so.3 \
    /usr/lib/x86_64-linux-gnu/libssl.so.3 \
    /usr/lib64/libssl.so.3 \
    /lib64/libssl.so.3 \
    /usr/lib/libssl.so.3
do
    if [ -f "$p" ]; then LIBSSL_FOUND=1; break; fi
done
if [ "$LIBSSL_FOUND" -ne 1 ]; then
    if ! ldconfig -p 2>/dev/null | grep -q 'libssl.so.3'; then
        if ! ldconfig -p 2>/dev/null | grep -q 'libssl.so.1.1'; then
            echo "tls_server_e2e: libssl not installed; skip" >&2
            exit 77
        fi
    fi
fi

WORK="$(mktemp -d -t luna-tls-server-XXXXXX)"
PORT=18444
CERT="$WORK/cert.pem"
KEY="$WORK/key.pem"

# shellcheck disable=SC2317  # invoked indirectly via `trap ... EXIT`.
cleanup() {
    if [ -n "${SERVER_PID:-}" ]; then
        kill -KILL "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -rf "$WORK"
}
trap cleanup EXIT

# --- 1. Generate self-signed cert/key --------------------------------
if ! openssl req -x509 -newkey rsa:2048 -nodes \
        -keyout "$KEY" -out "$CERT" -days 1 \
        -subj "/CN=localhost" >/dev/null 2>&1; then
    echo "tls_server_e2e: openssl req failed; skip" >&2
    exit 77
fi

# --- 2. Build the Luna driver ----------------------------------------
DRIVER="$WORK/server.luna"
cat > "$DRIVER" <<LUNA
import tls_server
import tcp

fn main() -> int
    @ir = tls_server_init()
    if @ir != 0
        shine("FAIL init")
        shine(tls_server_error())
        return 1

    let @ctx: TlsServerCtx = tls_server_ctx_new("$CERT", "$KEY")
    if tls_server_ctx_ok(@ctx) == 0
        shine("FAIL ctx_new")
        shine(tls_server_error())
        return 1

    @srv = tcp_listen($PORT)
    if @srv < 0
        shine("FAIL listen")
        return 1
    shine("READY")
    @client = tcp_accept(@srv)
    if @client < 0
        shine("FAIL accept")
        return 1

    let @conn: TlsServerConn = tls_server_accept(@ctx, @client)
    if tls_server_conn_ok(@conn) == 0
        shine("FAIL tls accept")
        shine(tls_server_error())
        tcp_close(@client)
        tcp_close(@srv)
        tls_server_ctx_free(@ctx)
        return 1

    # Write immediately — don't read first. The driving client may be
    # silent (e.g. openssl s_client < /dev/null) and a blocking SSL_read
    # would hang the test. The "HELLO" assertion downstream confirms
    # the handshake completed and the TLS write path works.
    @w = tls_server_write(@conn, "HELLO\n")
    if @w <= 0
        shine("FAIL write")
        shine(tls_server_error())

    tls_server_close(@conn)
    tcp_close(@client)
    tcp_close(@srv)
    tls_server_ctx_free(@ctx)
    return 0
LUNA

BIN="$WORK/server.elf"
COMPILE_LOG="$WORK/compile.log"

# Set LUNA_PATH so the resolver finds std/ext/tls_server.luna etc.
# luna-mini is invoked directly here (we need --dynamic), so we have
# to do the import expansion ourselves the way the luna CLI does.
PRELUDE="$HOME/.luna/lib/prelude.luna"
if [ ! -f "$PRELUDE" ]; then
    echo "tls_server_e2e: prelude missing at $PRELUDE; skip" >&2
    exit 77
fi

# Concatenate prelude + every dependency .luna file in topological
# order. tls_server depends on tcp; the test driver depends on both.
# We strip every `import` line — bootminor's parse_import is a stub
# and would otherwise reject the module name as an unresolved symbol.
SRC="$WORK/all.luna"
{
    cat "$PRELUDE"
    cat "$ROOT/std/net/tcp.luna"
    cat "$ROOT/std/ext/tls_server.luna"
    cat "$DRIVER"
} | grep -vE '^[[:space:]]*import[[:space:]]+' > "$SRC"

if ! "$LUNA_MINI" "$SRC" -o "$BIN" --dynamic > "$COMPILE_LOG" 2>&1; then
    echo "tls_server_e2e: compile failed"
    sed 's/^/    /' "$COMPILE_LOG"
    exit 1
fi
chmod +x "$BIN"

# --- 3. Start the server in the background ---------------------------
SERVER_LOG="$WORK/server.log"
"$BIN" > "$SERVER_LOG" 2>&1 &
SERVER_PID=$!

# Wait for "READY" up to ~3 s.
i=0
while [ $i -lt 60 ]; do
    if grep -q '^READY$' "$SERVER_LOG" 2>/dev/null; then break; fi
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "tls_server_e2e: server died before READY"
        echo "--- server log:"
        sed 's/^/    /' "$SERVER_LOG"
        exit 1
    fi
    sleep 0.1
    i=$((i + 1))
done
if ! grep -q '^READY$' "$SERVER_LOG" 2>/dev/null; then
    echo "tls_server_e2e: server never said READY"
    sed 's/^/    /' "$SERVER_LOG"
    exit 1
fi

# --- 4. Drive the handshake with openssl s_client --------------------
# Prefer `openssl s_client </dev/null` because it's the most explicit
# way to prove the bytes came over TLS. The server writes "HELLO\n"
# immediately after the handshake and then sends close_notify; the
# client thus sees data + clean close and exits.
#
# `-quiet` mutes the handshake banner from stdout so our grep is on
# exactly the application data. `-servername localhost` sends SNI
# (some openssl builds fail without it). `-ign_eof` keeps stdin EOF
# from triggering an early TLS shutdown before the server replies.
CLIENT_OUT="$WORK/client.out"
if ! timeout 10 openssl s_client -connect "127.0.0.1:$PORT" \
        -servername localhost -quiet -ign_eof \
        </dev/null > "$CLIENT_OUT" 2>"$WORK/client.err"; then
    rc=$?
    # rc != 0 is acceptable if we still got the bytes — openssl prints
    # the self-signed verify status to stderr and may exit non-zero
    # even with the data fully received. Only treat it as a fatal
    # failure if we got NO data.
    if [ ! -s "$CLIENT_OUT" ]; then
        echo "tls_server_e2e: openssl s_client failed (rc=$rc) and no data"
        echo "--- stderr:"
        sed 's/^/    /' "$WORK/client.err"
        echo "--- server log:"
        sed 's/^/    /' "$SERVER_LOG"
        exit 1
    fi
fi

# --- 5. Wait for the server to exit and check final state ------------
wait "$SERVER_PID" 2>/dev/null || true
SERVER_PID=""

fail=0
pass=0
# shellcheck disable=SC2317  # called below, linter sometimes misses it.
check_grep() {
    local label="$1"
    local pattern="$2"
    local file="$3"
    if grep -qE "$pattern" "$file"; then
        echo "[$label] PASS"
        pass=$((pass + 1))
    else
        echo "[$label] FAIL — no line matching: $pattern"
        echo "--- $file:"
        sed 's/^/    /' "$file"
        fail=$((fail + 1))
    fi
}

# Decoded "HELLO" appeared on the openssl client side -> the TLS
# handshake succeeded AND server side wrote the bytes correctly.
check_grep "handshake + HELLO" '^HELLO$' "$CLIENT_OUT"
# Server reached its READY marker -> ctx_new + listen worked.
check_grep "server READY"      '^READY$' "$SERVER_LOG"
# Server didn't log any FAIL line.
if grep -qE '^FAIL ' "$SERVER_LOG"; then
    echo "[no server FAIL] FAIL — server logged failures:"
    grep -E '^FAIL ' "$SERVER_LOG" | sed 's/^/    /'
    fail=$((fail + 1))
else
    echo "[no server FAIL] PASS"
    pass=$((pass + 1))
fi

echo
echo "=== tls_server_e2e: $pass PASS, $fail FAIL ==="
exit "$fail"
