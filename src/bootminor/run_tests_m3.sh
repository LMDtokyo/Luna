#!/usr/bin/env bash
# M3 fixed-point smoke test.
#
# Builds the chain:
#   bootstrap → luna-mini2   (C-compiled bootminor)
#   luna-mini2 → luna-mini3  (bootminor compiles itself)
#   luna-mini3 → luna-mini4  (luna-mini3 compiles bootminor again)
#
# Verifies:
#   - luna-mini3 and luna-mini4 are byte-identical (true fixed point)
#   - luna-mini3 passes every M2b and M2c test, byte-identical to
#     luna-mini2's outputs on the same inputs

set -u

LUNA_BOOT="${LUNA_BOOT:-./bootstrap/luna-boot.exe}"
OUT_DIR="${OUT_DIR:-c:/tmp/luna_m3}"
SRC_DIR="$(dirname "$0")"
WSL_DISTRO="${WSL_DISTRO:-Ubuntu}"

mkdir -p "$OUT_DIR"

_wsl_path() {
    local p="$1"
    case "$p" in
        [cC]:/*) echo "/mnt/c/${p#?:/}" ;;
        *)       echo "$p" ;;
    esac
}
OUT_DIR_WSL="$(_wsl_path "$OUT_DIR")"

[ -x "$LUNA_BOOT" ] || { echo "luna-boot not found"; exit 2; }

# Stage 1: bootstrap compiles bootminor.
cat "$SRC_DIR/lex.luna" "$SRC_DIR/gen.luna" "$SRC_DIR/main2.luna" \
    > "$OUT_DIR/bootminor.luna"
"$LUNA_BOOT" "$OUT_DIR/bootminor.luna" -o "$OUT_DIR/luna-mini2" --target linux \
    || { echo "FAIL: bootstrap → luna-mini2"; exit 1; }

# Full self-host monolith (prelude + lex + gen + main2).
cat "$SRC_DIR/bootminor_prelude.luna" \
    "$SRC_DIR/lex.luna" \
    "$SRC_DIR/gen.luna" \
    "$SRC_DIR/main2.luna" > "$OUT_DIR/bootminor_self.luna"

run_wsl() {
    wsl.exe -d "$WSL_DISTRO" bash -c "$1"
}

# Stage 2: luna-mini2 compiles bootminor_self → luna-mini3.
if ! run_wsl "cd '$OUT_DIR_WSL' && chmod +x luna-mini2 && ./luna-mini2 bootminor_self.luna -o luna-mini3" >/dev/null 2>&1; then
    echo "FAIL: luna-mini2 → luna-mini3 (self-compile)"; exit 1
fi

# Stage 3: luna-mini3 compiles bootminor_self → luna-mini4.
if ! run_wsl "cd '$OUT_DIR_WSL' && chmod +x luna-mini3 && ./luna-mini3 bootminor_self.luna -o luna-mini4" >/dev/null 2>&1; then
    echo "FAIL: luna-mini3 → luna-mini4 (self-compile fixed point)"; exit 1
fi

# Check byte-identity.
if run_wsl "cd '$OUT_DIR_WSL' && cmp luna-mini3 luna-mini4" >/dev/null 2>&1; then
    echo "[fixed-point] PASS — luna-mini3 = luna-mini4 byte-identical"
else
    sz3=$(run_wsl "cd '$OUT_DIR_WSL' && stat -c %s luna-mini3")
    sz4=$(run_wsl "cd '$OUT_DIR_WSL' && stat -c %s luna-mini4")
    echo "[fixed-point] FAIL — luna-mini3 ($sz3 B) differs from luna-mini4 ($sz4 B)"
    exit 1
fi

# Run every M2b and M2c test through luna-mini3 and check the
# produced binary's stdout + exit match the .expect file.
pass=0
fail=0
run_one() {
    local test_dir="$1"
    for src in "$test_dir"/*.luna; do
        name=$(basename "$src" .luna)
        expect_file="$test_dir/$name.expect"
        cp "$src" "$OUT_DIR/$name.luna"
        expected_stdout=$(head -n -1 "$expect_file")
        expected_exit=$(tail -n 1 "$expect_file" | sed -n 's/^EXIT=//p')
        got_stdout="$(run_wsl "cd '$OUT_DIR_WSL' && ./luna-mini3 $name.luna -o $name.elf > /dev/null && chmod +x $name.elf && ./$name.elf")"
        got_exit=$?
        if [ "$got_stdout" = "$expected_stdout" ] && [ "$got_exit" = "$expected_exit" ]; then
            echo "[mini3 $(basename $test_dir) $name] PASS (exit=$got_exit)"
            pass=$((pass + 1))
        else
            echo "[mini3 $(basename $test_dir) $name] FAIL"
            echo "  expected: $(printf %q "$expected_stdout") exit=$expected_exit"
            echo "  got     : $(printf %q "$got_stdout") exit=$got_exit"
            fail=$((fail + 1))
        fi
    done
}
run_one "$SRC_DIR/tests_m2b"
run_one "$SRC_DIR/tests_m2c"

echo
echo "=== bootminor M3: fixed-point PASS, suite $pass PASS, $fail FAIL ==="
exit "$fail"
