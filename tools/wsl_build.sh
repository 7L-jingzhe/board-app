#!/bin/bash
# Sync board_app sources from the Windows workspace into WSL's ext4 tree and
# build the aarch64 target. Kept as a file (not an inline command) because the
# PowerShell -> wsl.exe layer mangles inline '$VAR' expansions.
set -euo pipefail

SRC=/mnt/d/data/code-files/c/embedded-systems-study/lvgl/lv_port_linux-release-v9.2
DST=/home/jingzhe/lvgl/lv_port_linux-release-v9.2
JOBS=${JOBS:-8}

echo "== sync =="
rsync -a --delete "$SRC/board_app/" "$DST/board_app/"
rsync -a "$SRC/lv_conf.h" "$DST/"

echo "== configure =="
cd "$DST"
if [ ! -f build_board_app/CMakeCache.txt ]; then
    cmake -S . -B build_board_app -DCMAKE_BUILD_TYPE=Release
else
    cmake -S . -B build_board_app -DCMAKE_BUILD_TYPE=Release >/dev/null
fi

echo "== build =="
cmake --build build_board_app -j"$JOBS" 2>&1 | grep -E "error|warning|Error|Built target|^\[100%\]" || true
ls -la "$DST/bin/main"
file "$DST/bin/main"
