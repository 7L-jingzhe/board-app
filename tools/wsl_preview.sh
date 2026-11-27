#!/bin/bash
# Sync sources, rebuild the host preview from scratch, run it and report health.
set -uo pipefail
SRC=/mnt/d/data/code-files/c/embedded-systems-study/lvgl/lv_port_linux-release-v9.2
DST=/home/jingzhe/lvgl/lv_port_linux-release-v9.2
OUT=/mnt/d/data/code-files/c/embedded-systems-study/lvgl/docs_preview

echo "== sync =="
rsync -a --delete "$SRC/board_app/" "$DST/board_app/" || exit 1
rsync -a "$SRC/lv_conf.h" "$DST/" || exit 1
echo "preview_main.c has chat hook: $(grep -c 'open(.chat.)' "$DST/board_app/tests/preview/preview_main.c")"

cd "$DST"
rm -rf build_preview
cmake -S board_app/tests/preview -B build_preview -DCMAKE_BUILD_TYPE=Debug >/tmp/cfg.log 2>&1 || {
    tail -20 /tmp/cfg.log; exit 1; }
cmake --build build_preview -j8 >/tmp/build.log 2>&1 || {
    grep -i " error" /tmp/build.log | head -20; exit 1; }
grep -iE "warning: .*(ui_ime|chat_ui|chat_)" /tmp/build.log | head -10

rm -rf /tmp/pv && mkdir -p /tmp/pv
BOARD_APP_ROOT="$DST/board_app" PREVIEW_OUT=/tmp/pv ./build_preview/board_preview >/tmp/pv.log 2>&1
echo "preview exit=$?"
echo "cache-not-allocated: $(grep -c 'cache not allocated' /tmp/pv.log)"
echo "glyph-missing: $(grep -c 'glyph dsc. not found' /tmp/pv.log)"
echo "--- chat / ime lines ---"
grep -E "\[ime\]|\[chat\]|DBG" /tmp/pv.log | head -10

mkdir -p "$OUT"
for n in preview-01-home preview-08-chat preview-08b-chat-pinyin; do
    [ -f "/tmp/pv/$n.png" ] && cp -f "/tmp/pv/$n.png" "$OUT/"
done
cp -f /tmp/pv.log "$OUT/preview.log"
ls -la "$OUT"/preview-08*.png 2>/dev/null
