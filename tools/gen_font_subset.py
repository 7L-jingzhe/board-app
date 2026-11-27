#!/usr/bin/env python3
"""
Generate the Board App Chinese font subset.

Why a subset:
  A full Noto Sans SC variable font is ~17 MB. The UI only needs a few hundred
  glyphs, so we instantiate weight 400 and keep the characters listed in
  assets/fonts/chars.txt. The resulting TTF is loaded at runtime by LVGL
  TinyTTF (streaming, no whole-file RAM copy).

Usage:
  python3 tools/gen_font_subset.py \
      --input /tmp/NotoSansSC-VF.ttf \
      --chars assets/fonts/chars.txt \
      --output assets/fonts/NotoSansSC-Board.ttf

SPDX-License-Identifier: MIT
Copyright (c) 2026 Board App contributors
"""
import argparse
import os
import sys

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, help="source TTF/OTF (prefer TrueType)")
    parser.add_argument("--chars", required=True, help="UTF-8 file with wanted characters")
    parser.add_argument("--output", required=True, help="output TTF")
    parser.add_argument("--weight", type=float, default=400.0, help="variable font weight")
    args = parser.parse_args()

    if not os.path.isfile(args.input):
        print("input font not found:", args.input, file=sys.stderr)
        return 1
    with open(args.chars, encoding="utf-8") as f:
        text = f.read()
    # Always keep a space and the printable ASCII range for numbers/labels.
    text += "".join(chr(i) for i in range(32, 127))
    text = "".join(sorted(set(text)))

    font = TTFont(args.input)
    if "fvar" in font:
        # TinyTTF/stb_truetype does not handle variable axes; freeze weight 400.
        instantiateVariableFont(font, {"wght": args.weight}, inplace=True)

    options = subset.Options()
    options.layout_features = ["*"]
    options.name_IDs = ["*"]
    options.name_legacy = True
    options.notdef_outline = True
    options.recalc_bounds = True
    options.recalc_timestamp = False
    options.drop_tables += ["DSIG"]

    subsetter = subset.Subsetter(options=options)
    subsetter.populate(text=text)
    subsetter.subset(font)

    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    font.flavor = None
    font.save(args.output)
    print("chars:", len(text), "output:", args.output,
          "size:", os.path.getsize(args.output))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
