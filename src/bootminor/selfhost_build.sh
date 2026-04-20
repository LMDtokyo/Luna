#!/usr/bin/env bash
# Self-hosted Luna rebuild — no C compiler required.
#
# Uses the pre-built luna-mini.elf shipped in this directory to
# recompile bootminor's own sources into a fresh luna-mini.elf.
#
# Flow (all under WSL Ubuntu, or native Linux when LUNA_NATIVE=1):
#   1. Concatenate prelude + lex + gen + main2 into bootminor.luna
#   2. Run luna-mini.elf bootminor.luna -o luna-mini.elf.new
#   3. Optionally run luna-mini.elf.new bootminor.luna -o luna-mini.elf.next
#      and cmp the two to verify fixed-point.
#   4. On success, move luna-mini.elf.new to luna-mini.elf (overwrite).
#
# Usage (from repo root):
#   bash src/bootminor/selfhost_build.sh        # rebuild + fixed-point check
#   VERIFY_FP=0 bash ... selfhost_build.sh      # skip the second compile

set -u

SRC_DIR="$(dirname "$0")"
OUT_DIR="${OUT_DIR:-c:/tmp/luna_selfhost_build}"
WSL_DISTRO="${WSL_DISTRO:-Ubuntu}"
VERIFY_FP="${VERIFY_FP:-1}"

mkdir -p "$OUT_DIR"

_wsl_path() {
    local p="$1"
    case "$p" in
        [cC]:/*) echo "/mnt/c/${p#?:/}" ;;
        *)       echo "$p" ;;
    esac
}
OUT_DIR_WSL="$(_wsl_path "$OUT_DIR")"
SRC_DIR_WSL="$(_wsl_path "$(realpath "$SRC_DIR" 2>/dev/null || echo "$SRC_DIR")")"

if [ ! -f "$SRC_DIR/luna-mini.elf" ]; then
    echo "luna-mini.elf not found in $SRC_DIR — run run_tests_m3.sh first"
    exit 2
fi

# Copy the pre-built binary and sources into the work dir so WSL sees them.
cp "$SRC_DIR/luna-mini.elf"          "$OUT_DIR/luna-mini.elf"
cat "$SRC_DIR/bootminor_prelude.luna" \
    "$SRC_DIR/lex.luna" \
    "$SRC_DIR/gen.luna" \
    "$SRC_DIR/main2.luna"            > "$OUT_DIR/bootminor.luna"

run_wsl() {
    wsl.exe -d "$WSL_DISTRO" bash -c "$1"
}

if ! run_wsl "cd '$OUT_DIR_WSL' && chmod +x luna-mini.elf && ./luna-mini.elf bootminor.luna -o luna-mini.elf.new" >/dev/null 2>&1; then
    echo "FAIL: luna-mini.elf could not compile bootminor.luna"
    exit 1
fi

if [ "$VERIFY_FP" = "1" ]; then
    if ! run_wsl "cd '$OUT_DIR_WSL' && chmod +x luna-mini.elf.new && ./luna-mini.elf.new bootminor.luna -o luna-mini.elf.next" >/dev/null 2>&1; then
        echo "FAIL: second-stage self-compile"
        exit 1
    fi
    if ! run_wsl "cd '$OUT_DIR_WSL' && cmp luna-mini.elf.new luna-mini.elf.next" >/dev/null 2>&1; then
        sz1=$(run_wsl "cd '$OUT_DIR_WSL' && stat -c %s luna-mini.elf.new")
        sz2=$(run_wsl "cd '$OUT_DIR_WSL' && stat -c %s luna-mini.elf.next")
        echo "FAIL: fixed-point broken — stage1=$sz1 stage2=$sz2"
        exit 1
    fi
    echo "fixed-point OK (stage1 = stage2)"
fi

# Check new binary matches the shipped one (no accidental drift).
if run_wsl "cd '$OUT_DIR_WSL' && cmp luna-mini.elf luna-mini.elf.new" >/dev/null 2>&1; then
    echo "rebuilt luna-mini.elf matches shipped copy — no update needed"
else
    # The source has diverged from the shipped binary.
    sz_old=$(run_wsl "cd '$OUT_DIR_WSL' && stat -c %s luna-mini.elf")
    sz_new=$(run_wsl "cd '$OUT_DIR_WSL' && stat -c %s luna-mini.elf.new")
    echo "source evolved — shipped ($sz_old B) vs new ($sz_new B)"
    echo "copy luna-mini.elf.new into src/bootminor/luna-mini.elf to update the ship."
fi

echo "selfhost_build.sh: OK"
