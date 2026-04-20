#!/usr/bin/env bash
# Smoke-test bootminor: bootstrap compiles bootminor, bootminor
# compiles tiny .luna files, output binaries run and produce the
# expected stdout + exit code.
#
# Usage (from repo root):
#   bash src/bootminor/run_tests.sh
#
# Requires:
#   - luna-boot (bootstrap binary) at ./bootstrap/luna-boot.exe
#   - WSL with a Linux distro (default: Ubuntu) to run the produced
#     ELF64 files when on Windows. On native Linux set LUNA_NATIVE=1.

set -u

LUNA_BOOT="${LUNA_BOOT:-./bootstrap/luna-boot.exe}"
OUT_DIR="${OUT_DIR:-c:/tmp/luna_bootminor}"
SRC_DIR="$(dirname "$0")"
WSL_DISTRO="${WSL_DISTRO:-Ubuntu}"

mkdir -p "$OUT_DIR"

_wsl_path() {
    local p="$1"
    case "$p" in
        [cC]:/*) echo "/mnt/c/${p#?:/}" ;;
        [dD]:/*) echo "/mnt/d/${p#?:/}" ;;
        *)       echo "$p" ;;
    esac
}
OUT_DIR_WSL="$(_wsl_path "$OUT_DIR")"

if [ ! -x "$LUNA_BOOT" ]; then
    echo "luna-boot not found at $LUNA_BOOT" >&2
    exit 2
fi

# Stage 1: bootstrap compiles bootminor into luna-mini (ELF64).
"$LUNA_BOOT" "$SRC_DIR/main.luna" -o "$OUT_DIR/luna-mini" --target linux \
    || { echo "FAIL: bootstrap did not compile bootminor"; exit 1; }

# run_linux STDOUT_VAR EXIT_VAR CMD — runs CMD under WSL (or native
# bash if LUNA_NATIVE=1), captures stdout into STDOUT_VAR and the
# command's exit status into EXIT_VAR. Uses the WSL-return trick:
# bash -c '...' exits with whatever the last command exited with, so
# wrapping wsl.exe ... and reading Bash $? gives us the real code.
run_linux() {
    local _out_var="$1"
    local _rc_var="$2"
    local _cmd="$3"
    local _stdout
    if [ "${LUNA_NATIVE:-0}" = "1" ]; then
        _stdout="$(bash -c "$_cmd")"
    else
        _stdout="$(wsl.exe -d "$WSL_DISTRO" bash -c "$_cmd")"
    fi
    local _rc=$?
    printf -v "$_out_var" "%s" "$_stdout"
    printf -v "$_rc_var" "%d" "$_rc"
}

# Test cases: each triple is SOURCE|EXPECTED_STDOUT|EXPECTED_EXIT.
TESTS=(
    'fn main() -> int\n    shine("one-line-test")\n    return 0|one-line-test|0'
    'fn main() -> int\n    shine("first")\n    shine("second")\n    shine("third")\n    return 0|first\nsecond\nthird|0'
    'fn main() -> int\n    shine("with exit 42")\n    return 42|with exit 42|42'
    'fn main() -> int\n    shine("escape \"nested\"")\n    return 7|escape "nested"|7'
    'fn main() -> int\n    shine("alpha")\n    shine("beta")\n    shine("gamma")\n    shine("delta")\n    return 99|alpha\nbeta\ngamma\ndelta|99'
)

pass=0
fail=0
i=0
for triple in "${TESTS[@]}"; do
    i=$((i + 1))
    IFS='|' read -r src expected_out expected_rc <<< "$triple"
    src_path="$OUT_DIR/case_$i.luna"
    out_path="$OUT_DIR/case_$i.elf"
    printf "%b" "$src" > "$src_path"

    if [ "${LUNA_NATIVE:-0}" = "1" ]; then
        workdir="$OUT_DIR"
    else
        workdir="$OUT_DIR_WSL"
    fi

    # 1) Compile with luna-mini. Capture nothing here — just check we
    #    produced an ELF file.
    run_linux compile_out compile_rc \
        "cd '$workdir' && chmod +x luna-mini && ./luna-mini case_$i.luna -o case_$i.elf"
    if [ "$compile_rc" != "0" ]; then
        echo "[case $i] COMPILE FAIL (rc=$compile_rc)"
        echo "  compile stdout: $compile_out"
        fail=$((fail + 1))
        continue
    fi

    # 2) Run the produced binary and capture its stdout + exit.
    run_linux got_out got_rc "cd '$workdir' && chmod +x case_$i.elf && ./case_$i.elf"

    expected_out_multiline=$(printf "%b" "$expected_out")
    if [ "$got_out" = "$expected_out_multiline" ] && [ "$got_rc" = "$expected_rc" ]; then
        echo "[case $i] PASS (exit=$got_rc)"
        pass=$((pass + 1))
    else
        echo "[case $i] FAIL"
        echo "  expected stdout: $(printf %q "$expected_out_multiline")"
        echo "  got      stdout: $(printf %q "$got_out")"
        echo "  expected exit  : $expected_rc"
        echo "  got      exit  : $got_rc"
        fail=$((fail + 1))
    fi
done

echo
echo "=== bootminor: $pass PASS, $fail FAIL ==="
exit "$fail"
