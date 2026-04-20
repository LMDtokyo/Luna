#!/usr/bin/env bash
# Smoke-runner for src/stdlib_new/ modules.
# Compiles each <module>.luna + <module>_test.luna via luna-boot and runs
# the produced native binary. Prints FAIL-exit-code or stdout for each.
#
# Run from repo root:
#   bash src/stdlib_new/run_tests.sh
#
# Override the compiler / output dir:
#   LUNA_BOOT=./bootstrap/luna-boot.exe OUT_DIR=/tmp/out bash ...

set -u

LUNA_BOOT="${LUNA_BOOT:-./bootstrap/luna-boot.exe}"
OUT_DIR="${OUT_DIR:-/tmp/luna_stdlib_new}"
SRC_DIR="$(dirname "$0")"

mkdir -p "$OUT_DIR"

if [ ! -x "$LUNA_BOOT" ]; then
    echo "luna-boot not found at $LUNA_BOOT — build it first (make -C bootstrap)" >&2
    exit 2
fi

# Modules listed in dependency order. websocket_test depends on base64
# (for ws_accept_key), so base64 is linked ahead of websocket_test.
MODULES=(base64 csv sha512 chacha20 cli logger)

fail=0
pass=0

for m in "${MODULES[@]}"; do
    full="$OUT_DIR/${m}_full.luna"
    exe="$OUT_DIR/${m}.exe"
    cat "$SRC_DIR/$m.luna" "$SRC_DIR/${m}_test.luna" > "$full"
    if ! "$LUNA_BOOT" "$full" -o "$exe" 2>"$OUT_DIR/$m.compile.err"; then
        echo "[$m] COMPILE FAIL"
        cat "$OUT_DIR/$m.compile.err"
        fail=$((fail + 1))
        continue
    fi
    if ! "$exe" > "$OUT_DIR/$m.stdout" 2>"$OUT_DIR/$m.stderr"; then
        echo "[$m] RUN FAIL (exit=$?)"
        cat "$OUT_DIR/$m.stderr"
        fail=$((fail + 1))
        continue
    fi
    n_fail=$(grep -c FAIL "$OUT_DIR/$m.stdout" || true)
    n_pass=$(grep -c PASS "$OUT_DIR/$m.stdout" || true)
    if [ "$n_fail" -gt 0 ]; then
        echo "[$m] $n_pass PASS / $n_fail FAIL"
        cat "$OUT_DIR/$m.stdout"
        fail=$((fail + 1))
    else
        echo "[$m] $n_pass PASS"
        pass=$((pass + 1))
    fi
done

# websocket needs base64 linked in as well
ws_full="$OUT_DIR/ws_full.luna"
ws_exe="$OUT_DIR/ws.exe"
cat "$SRC_DIR/base64.luna" "$SRC_DIR/websocket.luna" "$SRC_DIR/websocket_test.luna" > "$ws_full"
if "$LUNA_BOOT" "$ws_full" -o "$ws_exe" 2>"$OUT_DIR/ws.compile.err" \
   && "$ws_exe" > "$OUT_DIR/ws.stdout" 2>"$OUT_DIR/ws.stderr"; then
    n_fail=$(grep -c FAIL "$OUT_DIR/ws.stdout" || true)
    n_pass=$(grep -c PASS "$OUT_DIR/ws.stdout" || true)
    if [ "$n_fail" -gt 0 ]; then
        echo "[websocket] $n_pass PASS / $n_fail FAIL"
        cat "$OUT_DIR/ws.stdout"
        fail=$((fail + 1))
    else
        echo "[websocket] $n_pass PASS"
        pass=$((pass + 1))
    fi
else
    echo "[websocket] COMPILE or RUN FAIL"
    cat "$OUT_DIR/ws.compile.err" "$OUT_DIR/ws.stderr" 2>/dev/null
    fail=$((fail + 1))
fi

echo
echo "=== $pass modules PASS, $fail FAIL ==="
exit "$fail"
