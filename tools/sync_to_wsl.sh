#!/usr/bin/env bash
# Sync board_app from the Windows workspace into WSL's ext4 tree.
#
# Build only on ext4: compiling LVGL over /mnt/d (9p) is several times slower.
#
# Usage:
#   tools/sync_to_wsl.sh [src-project] [dst-project] [--with-top]
#
# Defaults are the paths used during development.
set -euo pipefail

SRC="${1:-/mnt/d/data/code-files/c/embedded-systems-study/lvgl/lv_port_linux-release-v9.2}"
DST="${2:-/home/jingzhe/lvgl/lv_port_linux-release-v9.2}"
WITH_TOP="${3:-}"

if [ ! -d "$SRC/board_app" ]; then
    echo "source board_app not found: $SRC/board_app" >&2
    exit 1
fi
if [ ! -d "$DST" ]; then
    echo "destination project not found: $DST" >&2
    exit 1
fi

echo "sync $SRC/board_app -> $DST/board_app"
rsync -a --delete "$SRC/board_app/" "$DST/board_app/"

if [ "$WITH_TOP" = "--with-top" ]; then
    echo "sync top-level CMakeLists.txt/main.c/lv_conf.h and LVGL CMake tweak"
    cp -a "$DST/CMakeLists.txt" "$DST/CMakeLists.txt.bak.$(date +%Y%m%d_%H%M%S)" 2>/dev/null || true
    cp -a "$DST/main.c" "$DST/main.c.bak.$(date +%Y%m%d_%H%M%S)" 2>/dev/null || true
    cp -a "$DST/lv_conf.h" "$DST/lv_conf.h.bak.$(date +%Y%m%d_%H%M%S)" 2>/dev/null || true
    rsync -a "$SRC/CMakeLists.txt" "$SRC/main.c" "$SRC/lv_conf.h" "$DST/"
    rsync -a "$SRC/lvgl/CMakeLists.txt" "$DST/lvgl/CMakeLists.txt"
fi

echo "done"
