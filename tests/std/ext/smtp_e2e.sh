#!/usr/bin/env bash
# smtp_e2e.sh — start a fake SMTP server on 127.0.0.1:21525, run the
# Luna smtp_send transaction against it, then verify the server captured
# the expected envelope and body.
#
# Uses python3 (stdlib socket only — no aiosmtpd / smtpd dependency).
# If python3 isn't installed, exit 77 (skip).
#
# Usage (from repo root):
#     bash tests/std/ext/smtp_e2e.sh

set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"

if ! command -v python3 >/dev/null 2>&1; then
    echo "smtp_e2e: python3 not installed, skipping" >&2
    exit 77
fi
if ! command -v luna >/dev/null 2>&1; then
    echo "smtp_e2e: luna not on PATH, skipping" >&2
    exit 77
fi

WORK="$(mktemp -d -t luna-smtp-XXXXXX)"
PORT=21525
TRANSCRIPT="$WORK/transcript.txt"
SERVER_PY="$WORK/fake_smtp.py"

cat > "$SERVER_PY" <<'PY'
import socket
import sys

def main(port, out_path):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("127.0.0.1", port))
    s.listen(1)
    # Tell the parent we're listening.
    print("READY", flush=True)
    conn, _ = s.accept()
    rfile = conn.makefile("rb")
    transcript = []

    def send(line):
        conn.sendall((line + "\r\n").encode("ascii"))

    send("220 fake.luna ESMTP ready")
    in_data = False
    while True:
        raw = rfile.readline()
        if not raw:
            break
        line = raw.decode("ascii", "replace").rstrip("\r\n")
        transcript.append(line)
        if in_data:
            if line == ".":
                in_data = False
                send("250 OK message accepted")
            continue
        upper = line.upper()
        if upper.startswith("EHLO") or upper.startswith("HELO"):
            send("250-fake.luna Hello")
            send("250 OK")
        elif upper.startswith("MAIL FROM"):
            send("250 OK sender")
        elif upper.startswith("RCPT TO"):
            send("250 OK recipient")
        elif upper == "DATA":
            send("354 Start mail input; end with <CRLF>.<CRLF>")
            in_data = True
        elif upper == "QUIT":
            send("221 Goodbye")
            break
        else:
            send("502 Command not implemented")
    conn.close()
    s.close()
    with open(out_path, "w") as f:
        f.write("\n".join(transcript) + "\n")

if __name__ == "__main__":
    main(int(sys.argv[1]), sys.argv[2])
PY

# Start the server, wait for its READY signal.
python3 "$SERVER_PY" "$PORT" "$TRANSCRIPT" > "$WORK/server.out" 2>&1 &
SP=$!
trap 'kill -KILL "$SP" 2>/dev/null; rm -rf "$WORK"' EXIT

# Wait for the READY line (up to ~3s).
i=0
while [ $i -lt 30 ]; do
    if grep -q '^READY$' "$WORK/server.out" 2>/dev/null; then
        break
    fi
    sleep 0.1
    i=$((i + 1))
done
if ! grep -q '^READY$' "$WORK/server.out" 2>/dev/null; then
    echo "smtp_e2e: server failed to start"
    cat "$WORK/server.out"
    exit 1
fi

# Build a tiny driver program that calls smtp_send.
DRIVER="$WORK/driver.luna"
cat > "$DRIVER" <<'LUNA'
import smtp

fn main() -> int
    @rc = smtp_send("127.0.0.1", 21525, "alice@luna.test", "bob@luna.test", "hi", "Hello, this is a test body.")
    if @rc == 0
        shine("smtp_send ok")
        return 0
    shine("smtp_send fail")
    return 1
LUNA

export LUNA_PATH="$ROOT/std/runtime:$ROOT/std/core:$ROOT/std/std:$ROOT/std/net:$ROOT/std/ext"

if ! out=$(luna run "$DRIVER" 2>&1); then
    echo "smtp_e2e: luna run failed"
    echo "$out"
    exit 1
fi

# Wait for the server to flush the transcript.
wait "$SP" 2>/dev/null || true

if [ ! -s "$TRANSCRIPT" ]; then
    echo "smtp_e2e: empty transcript"
    echo "--- luna output:"
    echo "$out"
    echo "--- server output:"
    cat "$WORK/server.out"
    exit 1
fi

# Validate the protocol the client spoke.
fail=0
check() {
    if ! grep -q -F "$1" "$TRANSCRIPT"; then
        echo "smtp_e2e: missing line: $1"
        fail=1
    fi
}
check "EHLO luna.client"
check "MAIL FROM:<alice@luna.test>"
check "RCPT TO:<bob@luna.test>"
check "DATA"
check "From: alice@luna.test"
check "To: bob@luna.test"
check "Subject: hi"
check "Hello, this is a test body."
check "QUIT"

if [ "$fail" -ne 0 ]; then
    echo "--- full transcript:"
    cat "$TRANSCRIPT"
    exit 1
fi

echo "smtp_e2e: OK ($(wc -l < "$TRANSCRIPT") lines captured)"
