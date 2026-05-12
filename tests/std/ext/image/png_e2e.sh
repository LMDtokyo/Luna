#!/usr/bin/env bash
# png_e2e.sh — generate PNG fixtures with ImageMagick, then run the
# Luna png_test against them. Verifies that the pure-Luna PNG decoder
# decodes images byte-for-byte equivalent to what a reference encoder
# produced.
#
# Usage (from repo root):
#     bash tests/std/ext/image/png_e2e.sh

set -u

ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
TEST="$ROOT/tests/std/ext/image/png_test.luna"

# ImageMagick 7 ships `magick`; v6 ships `convert`. Either will do.
if command -v magick >/dev/null 2>&1; then
    IM="magick"
elif command -v convert >/dev/null 2>&1; then
    IM="convert"
else
    echo "png_e2e: ImageMagick (magick/convert) not installed, skipping" >&2
    exit 77
fi
if ! command -v luna >/dev/null 2>&1; then
    echo "png_e2e: luna not on PATH, skipping" >&2
    exit 77
fi

FIX="/tmp/luna_png_fixtures"
rm -rf "$FIX"
mkdir -p "$FIX"
trap 'rm -rf "$FIX"' EXIT

# Fixture 1: 1x1 solid red, RGB (no alpha).
# -define png:color-type=2 forces RGB (truecolor, no alpha).
"$IM" -size 1x1 'xc:red' \
      -define png:color-type=2 \
      -define png:bit-depth=8 \
      "$FIX/rgb1.png"

# Fixture 2: 4x4 fully-opaque red, RGBA.
# Force truecolor+alpha so we always get color type 6.
"$IM" -size 4x4 'xc:red' \
      -define png:color-type=6 \
      -define png:bit-depth=8 \
      "$FIX/rgba4.png"

# Fixture 3: large noisy PNG. plasma:fractal produces high-entropy
# pixel data that does NOT compress to a single IDAT chunk under
# libpng's default 8192-byte IDAT cap — exercising the multi-IDAT
# concatenation path in the decoder. 256x256 RGB ≈ 192 KB raw, so the
# encoded payload is comfortably over one chunk's worth.
"$IM" -size 256x256 'plasma:fractal' \
      -define png:color-type=2 \
      -define png:bit-depth=8 \
      "$FIX/big.png"

# Sanity-check that big.png actually has > 1 IDAT chunk (otherwise the
# multi-IDAT case isn't exercised). Don't fail the suite over it — log
# a note so a future maintainer notices if the fixture stops splitting.
idat_count=$(grep -ao IDAT "$FIX/big.png" | wc -l)
if [ "$idat_count" -lt 2 ]; then
    echo "png_e2e: NOTE: big.png only has $idat_count IDAT(s) — multi-IDAT path may not run" >&2
fi

# Wire up the import path so the Luna test resolves `import png` and
# the transitive `import gzip`.
export LUNA_PATH="$ROOT/std/runtime:$ROOT/std/core:$ROOT/std/std:$ROOT/std/net:$ROOT/std/ext:$ROOT/std/ext/compress:$ROOT/std/ext/image"

out=$(luna run "$TEST" 2>&1)
rc=$?

echo "$out"
if [ "$rc" -ne 0 ]; then
    echo "png_e2e: test exited $rc"
    exit 1
fi

summary=$(echo "$out" | grep -E '^=== png: ' | tail -1)
if [[ ! "$summary" =~ ^===\ png:\ ([0-9]+)\ PASS,\ ([0-9]+)\ FAIL\ ===$ ]]; then
    echo "png_e2e: no summary line found"
    exit 1
fi
pass="${BASH_REMATCH[1]}"
fail="${BASH_REMATCH[2]}"
if [ "$fail" != "0" ]; then
    echo "png_e2e: $fail assertion(s) failed"
    exit 1
fi
# Self-contained block alone is 7 assertions; fixtures add another ~15.
if [ "$pass" -lt 18 ]; then
    echo "png_e2e: only $pass assertions — fixtures probably weren't read"
    exit 1
fi

echo "png_e2e: OK ($pass assertions)"
