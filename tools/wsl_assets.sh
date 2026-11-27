#!/bin/bash
# Rebuild the IME dictionary + CJK font subset from the committed chars.txt and
# copy the results BACK to the D: workspace (otherwise the next rsync restores
# the stale 864-glyph font and the IME shows empty boxes).
set -euo pipefail
SRC=/mnt/d/data/code-files/c/embedded-systems-study/lvgl/lv_port_linux-release-v9.2
DST=/home/jingzhe/lvgl/lv_port_linux-release-v9.2
FONT=/tmp/NotoSansSC-VF.ttf
ZIP=/tmp/Unihan.zip

cd "$DST"

echo "== restore pristine LVGL sources =="
if [ -f /tmp/lv_tiny_ttf.c.orig ]; then
    cp -f /tmp/lv_tiny_ttf.c.orig lvgl/src/libs/tiny_ttf/lv_tiny_ttf.c
    echo "lv_tiny_ttf.c restored"
fi

echo "== dictionary =="
if [ ! -f "$ZIP" ]; then
    curl -fsSL --max-time 180 -o "$ZIP" \
        https://www.unicode.org/Public/UCD/latest/ucd/Unihan.zip
fi
python3 board_app/tools/gen_pinyin_dict.py --unihan "$ZIP" \
    --output board_app/assets/ime/pinyin_dict.txt

echo "== chars.txt =="
python3 board_app/tools/gen_chars.py board_app

echo "== font subset =="
if [ ! -f "$FONT" ]; then
    curl -fsSL --max-time 240 -o "$FONT" \
        https://raw.githubusercontent.com/notofonts/noto-cjk/main/Sans/Variable/TTF/Subset/NotoSansSC-VF.ttf
fi
python3 board_app/tools/gen_font_subset.py --input "$FONT" \
    --chars board_app/assets/fonts/chars.txt \
    --output board_app/assets/fonts/NotoSansSC-Board.ttf

echo "== coverage =="
python3 board_app/tools/check_font_coverage.py \
    board_app/assets/fonts/NotoSansSC-Board.ttf \
    board_app/assets/ime/pinyin_dict.txt

A="$SRC/board_app/assets/ime"
B="$SRC/board_app/assets/fonts"
mkdir -p "$A" "$B"
cp -f board_app/assets/ime/pinyin_dict.txt "$A/"
cp -f board_app/assets/fonts/chars.txt board_app/assets/fonts/NotoSansSC-Board.ttf "$B/"
echo "copied dictionary + font back to D:"
ls -la "$B"
