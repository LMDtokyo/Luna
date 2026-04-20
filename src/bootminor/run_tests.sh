#!/usr/bin/env bash
# Smoke-test the minimal self-host (M1): bootstrap compiles bootminor,
# bootminor compiles test .luna files, output binaries run and emit the
# expected strings.
#
# Requires: luna-boot (bootstrap binary), WSL with a Linux distro (we
# shell out to `wsl -d Ubuntu` to run the produced ELF64 files when
# invoked from Windows). If $LUNA_NATIVE=1, skip WSL and assume we're
# already on Linux.
#
# Run from repo root:
#   bash src/bootminor/run_tests.sh

set -u

LUNA_BOOT="${LUNA_BOOT:-./bootstrap/luna-boot.exe}"
OUT_DIR="${OUT_DIR:-c:/tmp/luna_bootminor_m1}"
SRC_DIR="$(dirname "$0")"
WSL_DISTRO="${WSL_DISTRO:-Ubuntu}"

mkdir -p "$OUT_DIR"

# Translate c:/path or C:/path -> /mnt/c/path for WSL, leave /... as-is.
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

# Helper: run a command either natively on Linux or via WSL on Windows.
run_linux() {
    if [ "${LUNA_NATIVE:-0}" = "1" ]; then
        bash -c "$1"
    else
        wsl.exe -d "$WSL_DISTRO" bash -c "$1"
    fi
}

# Test vectors: pairs of (source, expected-line).
TESTS=(
    'fn main() -> int\n    shine("Hello from Luna via bootminor!")\n    return 0|Hello from Luna via bootminor!'
    'fn main() -> int\n    shine("second test from luna-mini 2026")\n    return 0|second test from luna-mini 2026'
    'fn main() -> int\n    shine("unicode-free ascii payload")\n    return 0|unicode-free ascii payload'
)

pass=0
fail=0
i=0
for pair in "${TESTS[@]}"; do
    i=$((i + 1))
    src="${pair%|*}"
    expected="${pair##*|}"
    src_path="$OUT_DIR/case_$i.luna"
    out_path="$OUT_DIR/case_$i.elf"
    printf "%b" "$src" > "$src_path"

    if [ "${LUNA_NATIVE:-0}" = "1" ]; then
        workdir="$OUT_DIR"
    else
        workdir="$OUT_DIR_WSL"
    fi
    got=$(run_linux "cd '$workdir' && chmod +x luna-mini && ./luna-mini case_$i.luna -o case_$i.elf >/dev/null && chmod +x case_$i.elf && ./case_$i.elf")
    if [ "$got" = "$expected" ]; then
        echo "[case $i] PASS  \"$expected\""
        pass=$((pass + 1))
    else
        echo "[case $i] FAIL  expected=\"$expected\"  got=\"$got\""
        fail=$((fail + 1))
    fi
done

echo
echo "=== bootminor M1: $pass PASS, $fail FAIL ==="
exit "$fail"
