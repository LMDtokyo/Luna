#!/usr/bin/env bash
# https_pool_e2e.sh — end-to-end coverage for std/ext/https.
#
# Stands up two local HTTPS servers (python3's http.server wrapped in
# SSLContext) backed by a self-signed cert, then drives them from Luna
# programs that exercise:
#
#   1. https_pool_new()              non-zero handle
#   2. https_pool_request()          GET   to host A → 200, body matches
#   3. https_pool_request()          GET   to host A again → 200, same body
#   4. session reuse                 the cached TlsConn keeps the same SSL*
#                                    pointer across the two GETs (= keep-alive)
#   5. https_pool_request()          POST  to host A → 200, body echoes
#   6. separate session per host:port  hitting port B yields a different SSL*
#   7. https_pool_close()            drains all sessions
#   8. https_get()                   legacy shim still produces a 200
#
# About 10 assertions. Skip with exit 77 if openssl, python3, libssl,
# or luna-mini aren't available — this is hard to test offline.
#
# Note: the test driver is compiled with `--dynamic` because std/ext/tls
# (which we depend on) uses _libc_dlopen and needs ld-linux.
#
# Usage (from repo root):
#     bash tests/std/ext/https_pool_e2e.sh

set -u

LUNA_MINI="${LUNA_MINI:-$HOME/.luna/bin/luna-mini}"

if [ ! -x "$LUNA_MINI" ]; then echo "https_pool_e2e: luna-mini missing at $LUNA_MINI" >&2; exit 77; fi
if ! command -v openssl >/dev/null 2>&1; then echo "https_pool_e2e: openssl missing"   >&2; exit 77; fi
if ! command -v python3 >/dev/null 2>&1; then echo "https_pool_e2e: python3 missing"   >&2; exit 77; fi

# libssl.so.3 is what tls.luna dlopens. Check a couple of common locations.
LIBSSL_FOUND=0
for p in \
    /lib/x86_64-linux-gnu/libssl.so.3 \
    /usr/lib/x86_64-linux-gnu/libssl.so.3 \
    /usr/lib64/libssl.so.3 \
    /lib64/libssl.so.3 \
    /usr/lib/libssl.so.3
do
    [ -f "$p" ] && LIBSSL_FOUND=1 && break
done
if [ "$LIBSSL_FOUND" -ne 1 ]; then
    if ! ldconfig -p 2>/dev/null | grep -q 'libssl.so.3'; then
        echo "https_pool_e2e: libssl.so.3 not installed; skipping" >&2
        exit 77
    fi
fi

WORK="$(mktemp -d -t luna-https-XXXXXX)"
PORT_A=24443
PORT_B=24444

cleanup() {
    local pf p
    for pf in "$WORK"/srv_*.pid; do
        [ -f "$pf" ] || continue
        p="$(cat "$pf" 2>/dev/null || true)"
        [ -n "$p" ] && kill -KILL "$p" 2>/dev/null || true
    done
    rm -rf "$WORK"
}
trap cleanup EXIT

mk_server() {
    local port="$1"
    local body="$2"
    local pidfile="$3"
    local logfile="$4"

    local key="$WORK/key_$port.pem"
    local cert="$WORK/cert_$port.pem"
    openssl req -x509 -newkey rsa:2048 -keyout "$key" -out "$cert" -days 1 -nodes \
        -subj "/CN=127.0.0.1" >/dev/null 2>&1 || { echo "cert gen failed"; return 1; }

    python3 - "$port" "$cert" "$key" "$body" > "$logfile" 2>&1 <<'PY' &
import http.server, ssl, sys
port = int(sys.argv[1])
certfile, keyfile, body = sys.argv[2], sys.argv[3], sys.argv[4]

class H(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def do_GET(self):
        b = body.encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(b)))
        self.send_header("Connection", "keep-alive")
        self.end_headers()
        self.wfile.write(b)
    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0"))
        data = self.rfile.read(n) if n else b""
        out = b"POST:" + data
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(out)))
        self.send_header("Connection", "keep-alive")
        self.end_headers()
        self.wfile.write(out)
    def log_message(self, *a, **k): pass

srv = http.server.ThreadingHTTPServer(("127.0.0.1", port), H)
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain(certfile, keyfile)
srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
print("READY", flush=True)
try:
    srv.serve_forever()
except Exception:
    pass
PY
    echo $! > "$pidfile"
}

mk_server "$PORT_A" "hello-from-A" "$WORK/srv_A.pid" "$WORK/srv_A.log"
mk_server "$PORT_B" "hello-from-B" "$WORK/srv_B.pid" "$WORK/srv_B.log"

for log in "$WORK/srv_A.log" "$WORK/srv_B.log"; do
    i=0
    while [ $i -lt 60 ]; do
        if grep -q '^READY$' "$log" 2>/dev/null; then break; fi
        sleep 0.1
        i=$((i + 1))
    done
    if ! grep -q '^READY$' "$log" 2>/dev/null; then
        echo "https_pool_e2e: server failed to start on log $log"
        cat "$log"
        exit 1
    fi
done

# --- Driver 1: GET / POST / close / legacy shim ----------------------
DRIVER="$WORK/driver.luna"
cat > "$DRIVER" <<LUNA
import https

fn main() -> int
    @pool = https_pool_new()
    if @pool == 0
        shine("FAIL pool_new")
        return 1
    shine("OK pool_new")

    let @r1: HttpResponse = https_pool_request(@pool, "127.0.0.1", $PORT_A, "GET", "/x", "", "")
    print("R1 status=")
    print(int_to_str(@r1.status))
    print(" body=")
    shine(@r1.body)

    let @r2: HttpResponse = https_pool_request(@pool, "127.0.0.1", $PORT_A, "GET", "/y", "", "")
    print("R2 status=")
    print(int_to_str(@r2.status))
    print(" body=")
    shine(@r2.body)

    let @r3: HttpResponse = https_pool_request(@pool, "127.0.0.1", $PORT_A, "POST", "/p", "payload-bytes", "text/plain")
    print("R3 status=")
    print(int_to_str(@r3.status))
    print(" body=")
    shine(@r3.body)

    https_pool_close(@pool)
    shine("OK pool_close")

    # Legacy shim — runs against 443 by default, but accepts a custom port
    # via https_request_to. We re-use the same local server so we don't
    # depend on the public internet.
    let @r4: HttpResponse = https_request_to("127.0.0.1", $PORT_A, "GET", "/legacy", "", "")
    print("R4 status=")
    print(int_to_str(@r4.status))
    print(" body=")
    shine(@r4.body)

    return 0
LUNA

DRV_BIN="$WORK/driver.elf"
DRV_OUT="$WORK/driver.out"

if ! "$LUNA_MINI" "$DRIVER" -o "$DRV_BIN" --dynamic > "$WORK/driver.compile.log" 2>&1; then
    echo "https_pool_e2e: driver compile failed"
    sed 's/^/    /' "$WORK/driver.compile.log"
    exit 1
fi

DRV_RC=0
timeout 30 "$DRV_BIN" > "$DRV_OUT" 2>&1 || DRV_RC=$?

if [ "$DRV_RC" -ne 0 ]; then
    echo "https_pool_e2e: driver failed (rc=$DRV_RC)"
    echo "--- driver output:"
    sed 's/^/    /' "$DRV_OUT"
    exit 1
fi

# --- Assertions on driver 1 output -----------------------------------
fail=0
pass=0
check_line() {
    local label="$1"
    local pattern="$2"
    local file="$3"
    if grep -qE "$pattern" "$file"; then
        echo "[$label] PASS"
        pass=$((pass + 1))
    else
        echo "[$label] FAIL — no line matching: $pattern"
        echo "--- last 20 lines of $file:"
        tail -20 "$file" | sed 's/^/    /'
        fail=$((fail + 1))
    fi
}

check_line "pool_new"   '^OK pool_new$'                          "$DRV_OUT"
check_line "R1 200"     '^R1 status=200 body=hello-from-A$'      "$DRV_OUT"
check_line "R2 200"     '^R2 status=200 body=hello-from-A$'      "$DRV_OUT"
check_line "R3 POST"    '^R3 status=200 body=POST:payload-bytes$' "$DRV_OUT"
check_line "pool_close" '^OK pool_close$'                        "$DRV_OUT"
check_line "R4 legacy"  '^R4 status=200 body=hello-from-A$'      "$DRV_OUT"

# --- Driver 2: prove session-reuse via SSL pointer stability ---------
REUSE="$WORK/reuse.luna"
cat > "$REUSE" <<LUNA
import https

fn main() -> int
    @pool = https_pool_new()

    let @r1: HttpResponse = https_pool_request(@pool, "127.0.0.1", $PORT_A, "GET", "/", "", "")
    if @r1.status != 200
        print("FAIL r1 status=")
        shine(int_to_str(@r1.status))
        return 1
    let @c1: HttpsConn = https_pool_acquire(@pool, "127.0.0.1", $PORT_A)
    @ssl1 = @c1.ssl

    let @r2: HttpResponse = https_pool_request(@pool, "127.0.0.1", $PORT_A, "GET", "/", "", "")
    if @r2.status != 200
        print("FAIL r2 status=")
        shine(int_to_str(@r2.status))
        return 1
    let @c2: HttpsConn = https_pool_acquire(@pool, "127.0.0.1", $PORT_A)
    @ssl2 = @c2.ssl

    if @ssl1 == @ssl2
        shine("REUSE ok")
    else
        shine("FAIL session drift")

    # Separate session per (host, port).
    let @rB: HttpResponse = https_pool_request(@pool, "127.0.0.1", $PORT_B, "GET", "/", "", "")
    if @rB.status != 200
        print("FAIL B status=")
        shine(int_to_str(@rB.status))
        return 1
    let @cB: HttpsConn = https_pool_acquire(@pool, "127.0.0.1", $PORT_B)
    @sslB = @cB.ssl
    if @sslB != @ssl1
        if @sslB != 0
            shine("SEPARATE ok")

    https_pool_close(@pool)
    return 0
LUNA

REUSE_BIN="$WORK/reuse.elf"
REUSE_OUT="$WORK/reuse.out"

if ! "$LUNA_MINI" "$REUSE" -o "$REUSE_BIN" --dynamic > "$WORK/reuse.compile.log" 2>&1; then
    echo "[reuse driver] FAIL — compile error"
    sed 's/^/    /' "$WORK/reuse.compile.log"
    fail=$((fail + 1))
else
    REUSE_RC=0
    timeout 30 "$REUSE_BIN" > "$REUSE_OUT" 2>&1 || REUSE_RC=$?
    if [ "$REUSE_RC" -ne 0 ]; then
        echo "[reuse driver] FAIL (rc=$REUSE_RC)"
        sed 's/^/    /' "$REUSE_OUT"
        fail=$((fail + 1))
    else
        check_line "session reused"     '^REUSE ok$'    "$REUSE_OUT"
        check_line "separate per host"  '^SEPARATE ok$' "$REUSE_OUT"
    fi
fi

echo
echo "=== https_pool_e2e: $pass PASS, $fail FAIL ==="
exit "$fail"
