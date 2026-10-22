#!/usr/bin/env python3
"""Kai-Term.ico -> icons/hicolor/<N>x<N>/apps/kai-term.png. Run by `make icons`.

An .ico is a Windows container; a freedesktop icon theme wants one PNG per size directory, so the
.ico is the source asset and the PNGs are what `make install` copies. Regenerating is optional: the
PNGs are committed, so `make install` needs no Python.

Sizes come from the file, not from a list. Kai-Term.ico holds 16/24/32/48/64/96/128/256 as BMP
frames plus a 512x512 PNG frame whose directory entry claims 256 (the usual HiDPI trick), so the
DECODED size is what names the directory.
"""

import sys
from io import BytesIO
from pathlib import Path

try:
    from PIL import IcoImagePlugin
except ImportError:
    sys.exit("make icons needs python3 + Pillow; the committed icons/ need nothing")

src = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("Kai-Term.ico")
out = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("icons")

with src.open("rb") as handle:
    ico = IcoImagePlugin.IcoFile(BytesIO(handle.read()))

for idx in range(ico.nb_items):
    im = ico.frame(idx).convert("RGBA")
    w, h = im.size
    if w != h:
        sys.exit(f"{src}: frame {idx} is {w}x{h}; an icon-theme size directory must be square")
    dst = out / f"{w}x{w}" / "apps" / "kai-term.png"
    dst.parent.mkdir(parents=True, exist_ok=True)
    im.save(dst, "PNG")
    print(f"{dst}  {w}x{w}")
