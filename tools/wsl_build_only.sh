#!/bin/bash
# 同步到 WSL 并编译：aarch64 板端（board_app 全量）
set -uo pipefail
SRC=/mnt/d/data/code-files/c/embedded-systems-study/lvgl/lv_port_linux-release-v9.2
DST=/home/jingzhe/lvgl/lv_port_linux-release-v9.2
cd "$DST"
rsync -a --delete "$SRC/board_app/" "$DST/board_app/"
rsync -a "$SRC/main.c" "$SRC/lv_conf.h" "$DST/"
cmake --build build_board_app -j8 2>&1 | grep -E " error|warning: .*(comm|ui_ime|ui_perf)|Built target main" | head -30
ls -la bin/main 2>/dev/null
