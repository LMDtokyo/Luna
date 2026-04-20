#!/usr/bin/env bash
# Smoke-test bootminor M2b: the real lex+parse+codegen compiler.
# Compiles each tests_m2b/*.luna via luna-mini, runs the output, and
# checks stdout + exit code against the sibling .expect file.
#
# .expect format: all stdout lines, followed by a final `EXIT=N` line
# (N is the expected exit code).

set -u

LUNA_BOOT="${LUNA_BOOT:-./bootstrap/luna-boot.exe}"
OUT_DIR="${OUT_DIR:-c:/tmp/luna_bootminor_m2b}"
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

# Build luna-mini by concatenating lex.luna + gen.luna + main2.luna and
# compiling the concatenation with the bootstrap.
full="$OUT_DIR/bootminor_m2b.luna"
cat "$SRC_DIR/lex.luna" "$SRC_DIR/gen.luna" "$SRC_DIR/main2.luna" > "$full"
"$LUNA_BOOT" "$full" -o "$OUT_DIR/luna-mini" --target linux \
    || { echo "FAIL: bootstrap did not compile bootminor M2b"; exit 1; }

# Copy tests into OUT_DIR so WSL sees them.
cp "$SRC_DIR/tests_m2b/"*.luna "$OUT_DIR/"
cp "$SRC_DIR/tests_m2b/"*.expect "$OUT_DIR/"

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

pass=0
fail=0
for src in "$SRC_DIR/tests_m2b/"*.luna; do
    name=$(basename "$src" .luna)
    expect_file="$SRC_DIR/tests_m2b/$name.expect"

    expected_stdout=$(head -n -1 "$expect_file")
    expected_exit=$(tail -n 1 "$expect_file" | sed -n 's/^EXIT=//p')

    if [ "${LUNA_NATIVE:-0}" = "1" ]; then
        workdir="$OUT_DIR"
    else
        workdir="$OUT_DIR_WSL"
    fi

    run_linux got_out got_rc \
        "cd '$workdir' && chmod +x luna-mini && ./luna-mini $name.luna -o $name.elf > /dev/null && chmod +x $name.elf && ./$name.elf"

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
echo "=== bootminor M2b: $pass PASS, $fail FAIL ==="
exit "$fail"
