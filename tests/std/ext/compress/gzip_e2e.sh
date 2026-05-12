#!/usr/bin/env bash
# gzip_e2e.sh — generate gzip fixtures with the system `gzip` command,
# then run the Luna gzip_test against them. Verifies that the pure-Luna
# decoder agrees byte-for-byte with the reference encoder.
#
# Usage (from repo root):
#     bash tests/std/ext/compress/gzip_e2e.sh

set -u

ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
TEST="$ROOT/tests/std/ext/compress/gzip_test.luna"

if ! command -v gzip >/dev/null 2>&1; then
    echo "gzip_e2e: gzip not installed, skipping" >&2
    exit 77
fi
if ! command -v luna >/dev/null 2>&1; then
    echo "gzip_e2e: luna not on PATH, skipping" >&2
    exit 77
fi

FIX="/tmp/luna_gzip_fixtures"
rm -rf "$FIX"
mkdir -p "$FIX"
trap 'rm -rf "$FIX"' EXIT

# Fixture 1: short string, default compression.
printf 'hello' | gzip -c > "$FIX/hello.gz"

# Fixture 2: stored block (gzip -0 forces uncompressed).
printf 'stored block hello' | gzip -0 -c > "$FIX/stored.gz"

# Fixture 3: highly repetitive — 1024 bytes of 'A'.
# Build with `printf` + `head -c` so we don't depend on python.
yes A | tr -d '\n' | head -c 1024 | gzip -c > "$FIX/rep.gz"

# Fixture 4: longer English text — likely dynamic-Huffman block.
cat > "$FIX/text.raw" <<'TEXT'
Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod
tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim
veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea
commodo consequat. Duis aute irure dolor in reprehenderit in voluptate
velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint
occaecat cupidatat non proident, sunt in culpa qui officia deserunt
mollit anim id est laborum.

The quick brown fox jumps over the lazy dog. Sphinx of black quartz,
judge my vow. Pack my box with five dozen liquor jugs. How vexingly
quick daft zebras jump!
TEXT
gzip -c < "$FIX/text.raw" > "$FIX/text.gz"

# Fixture 5: ~100 KB multi-block input. Use /usr/share/dict if present
# (real English words give the encoder dynamic-Huffman work to do); fall
# back to a synthesised repeated paragraph otherwise. Either way we go
# well past gzip's default 32 KB block window.
if [ -f /usr/share/dict/words ]; then
    head -c 120000 /usr/share/dict/words > "$FIX/big.raw"
else
    : > "$FIX/big.raw"
    i=0
    while [ "$i" -lt 320 ]; do
        printf 'The quick brown fox jumps over the lazy dog. ' >> "$FIX/big.raw"
        i=$((i + 1))
    done
fi
gzip -c < "$FIX/big.raw" > "$FIX/big.gz"

# Fixture 6: every byte value 0..255. Catches accidental NUL trimming
# / sign-extension in the decoder. Use perl/python/awk in that order so
# this works wherever any one of them is available.
if command -v perl >/dev/null 2>&1; then
    perl -e 'print pack("C*", 0..255)' | gzip -c > "$FIX/allbytes.gz"
elif command -v python3 >/dev/null 2>&1; then
    python3 -c 'import sys; sys.stdout.buffer.write(bytes(range(256)))' \
        | gzip -c > "$FIX/allbytes.gz"
else
    awk 'BEGIN { for (i = 0; i < 256; i++) printf "%c", i }' \
        | gzip -c > "$FIX/allbytes.gz"
fi

# Wire up the import path so the Luna test resolves `import gzip`.
export LUNA_PATH="$ROOT/std/runtime:$ROOT/std/core:$ROOT/std/std:$ROOT/std/net:$ROOT/std/ext:$ROOT/std/ext/compress"

out=$(luna run "$TEST" 2>&1)
rc=$?

echo "$out"
if [ "$rc" -ne 0 ]; then
    echo "gzip_e2e: test exited $rc"
    exit 1
fi

summary=$(echo "$out" | grep -E '^=== gzip: ' | tail -1)
if [[ ! "$summary" =~ ^===\ gzip:\ ([0-9]+)\ PASS,\ ([0-9]+)\ FAIL\ ===$ ]]; then
    echo "gzip_e2e: no summary line found"
    exit 1
fi
pass="${BASH_REMATCH[1]}"
fail="${BASH_REMATCH[2]}"
if [ "$fail" != "0" ]; then
    echo "gzip_e2e: $fail assertion(s) failed"
    exit 1
fi
if [ "$pass" -lt 18 ]; then
    echo "gzip_e2e: only $pass assertions — fixtures probably weren't read"
    exit 1
fi

echo "gzip_e2e: OK ($pass assertions)"
