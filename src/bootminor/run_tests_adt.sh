#!/usr/bin/env bash
# ADT / pattern matching smoke suite.
set -u
LUNA_BOOT="${LUNA_BOOT:-./bootstrap/luna-boot.exe}"
OUT_DIR="${OUT_DIR:-c:/tmp/luna_bootminor_adt}"
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
full="$OUT_DIR/bootminor.luna"
cat "$SRC_DIR/lex.luna" "$SRC_DIR/gen.luna" "$SRC_DIR/main2.luna" > "$full"
"$LUNA_BOOT" "$full" -o "$OUT_DIR/luna-mini" --target linux \
    || { echo "FAIL: bootstrap did not compile bootminor ADT-build"; exit 1; }
cp "$SRC_DIR/tests_adt/"*.luna "$OUT_DIR/"
cp "$SRC_DIR/tests_adt/"*.expect "$OUT_DIR/"
run_linux() {
    local _out_var="$1" _rc_var="$2" _cmd="$3" _stdout
    if [ "${LUNA_NATIVE:-0}" = "1" ]; then
        _stdout="$(bash -c "$_cmd")"
    else
        _stdout="$(wsl.exe -d "$WSL_DISTRO" bash -c "$_cmd")"
    fi
    local _rc=$?
    printf -v "$_out_var" "%s" "$_stdout"
    printf -v "$_rc_var" "%d" "$_rc"
}
pass=0; fail=0
for src in "$SRC_DIR/tests_adt/"*.luna; do
    name=$(basename "$src" .luna)
    expect_file="$SRC_DIR/tests_adt/$name.expect"
    expected_stdout=$(head -n -1 "$expect_file")
    expected_exit=$(tail -n 1 "$expect_file" | sed -n 's/^EXIT=//p')
    workdir="$OUT_DIR_WSL"
    [ "${LUNA_NATIVE:-0}" = "1" ] && workdir="$OUT_DIR"
    run_linux got_out got_rc \
        "cd '$workdir' && chmod +x luna-mini && ./luna-mini $name.luna -o $name.elf > /dev/null 2>&1 && chmod +x $name.elf && ./$name.elf"
    if [ "$got_out" = "$expected_stdout" ] && [ "$got_rc" = "$expected_exit" ]; then
        echo "[$name] PASS (exit=$got_rc)"
        pass=$((pass + 1))
    else
        echo "[$name] FAIL"
        echo "  expected: $(printf %q "$expected_stdout") exit=$expected_exit"
        echo "  got     : $(printf %q "$got_out") exit=$got_rc"
        fail=$((fail + 1))
    fi
done
echo
echo "=== bootminor ADT: $pass PASS, $fail FAIL ==="
exit "$fail"
