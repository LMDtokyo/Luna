#!/usr/bin/env python3
# Generate luna.ico (multi-size Windows icon) from the raven PNG.
# Sizes follow Windows file-icon conventions.

from PIL import Image
from pathlib import Path
import sys

SRC = Path(__file__).resolve().parent.parent / "assets" / "luna-raven.png"
OUT = Path(__file__).resolve().parent.parent / "assets" / "luna.ico"

SIZES = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]

def main() -> int:
    if not SRC.exists():
        print(f"missing source: {SRC}", file=sys.stderr)
        return 1
    img = Image.open(SRC).convert("RGBA")
    resized = []
    for w, h in SIZES:
        r = img.resize((w, h), Image.LANCZOS)
        resized.append(r)
    resized[-1].save(OUT, format="ICO", sizes=SIZES)
    print(f"wrote {OUT} ({len(SIZES)} sizes)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
