#!/usr/bin/env python3
"""Verify that every hanzi the IME can produce has a glyph in the font subset.

If this reports missing glyphs the candidate bar shows empty boxes on the board:
run tools/gen_chars.py then tools/gen_font_subset.py again.

Usage:
  python3 tools/check_font_coverage.py <font.ttf> <pinyin_dict.txt>

SPDX-License-Identifier: MIT
Copyright (c) 2026 Board App contributors
"""
import sys

from fontTools.ttLib import TTFont


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    font_path, dict_path = sys.argv[1], sys.argv[2]
    cmap = TTFont(font_path).getBestCmap()

    chars = ""
    with open(dict_path, encoding="utf-8") as f:
        for line in f:
            if line.startswith("#") or "\t" not in line:
                continue
            chars += line.split("\t", 1)[1].strip()
    chars = "".join(dict.fromkeys(chars))

    missing = sorted({c for c in chars if ord(c) not in cmap})
    print("dictionary characters:", len(chars))
    print("missing glyphs:", len(missing))
    if missing:
        print("sample:", "".join(missing[:60]))
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
