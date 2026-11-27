#!/bin/bash
# Sync sources and run the x86_64 host unit tests.
set -uo pipefail
SRC=/mnt/d/data/code-files/c/embedded-systems-study/lvgl/lv_port_linux-release-v9.2
DST=/home/jingzhe/lvgl/lv_port_linux-release-v9.2
cd "$DST"
rsync -a --delete "$SRC/board_app/" "$DST/board_app/"
rsync -a "$SRC/lv_conf.h" "$DST/"

cmake -S board_app/tests/host -B build_host_tests -DCMAKE_BUILD_TYPE=Debug >/tmp/hcfg.log 2>&1 || {
    tail -20 /tmp/hcfg.log; exit 1; }
cmake --build build_host_tests -j8 >/tmp/hbuild.log 2>&1 || {
    grep -E " error" /tmp/hbuild.log | head -20; exit 1; }
grep -E "warning:.*(chat|test_chat)" /tmp/hbuild.log | head -10
cd build_host_tests && ctest --output-on-failure 2>&1 | tail -20
