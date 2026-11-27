#!/bin/bash
# Build + deploy, then run on the board with the perf probe enabled and print
# the perf log lines. usage: wsl_perf.sh <start-id> <seconds>
set -uo pipefail
SRC=/mnt/d/data/code-files/c/embedded-systems-study/lvgl/lv_port_linux-release-v9.2
DST=/home/jingzhe/lvgl/lv_port_linux-release-v9.2
BOX=192.168.64.216
DIR=/root/lvgl/board_apps
START="${1:-home}"
WAIT="${2:-12}"

cd "$DST"
export DISPLAY=:0 SSH_ASKPASS=/tmp/ap1.sh SSH_ASKPASS_REQUIRE=force
printf '#!/bin/sh\necho 1\n' > /tmp/ap1.sh
chmod +x /tmp/ap1.sh

echo "== sync + build =="
rsync -a --delete "$SRC/board_app/" "$DST/board_app/"
rsync -a "$SRC/lv_conf.h" "$SRC/main.c" "$DST/"
cmake --build build_board_app -j8 2>&1 | grep -E " error|Built target main" | tail -3
ls -la bin/main

echo "== upload main =="
sha=$(sha256sum bin/main | cut -d' ' -f1)
gzip -9c bin/main | base64 -w0 | timeout 300 setsid -w ssh \
    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    -o PubkeyAuthentication=no -o PreferredAuthentications=password \
    -o NumberOfPasswordPrompts=1 -T "root@${BOX}" \
    "base64 -d | gzip -dc > ${DIR}/main.new && mv -f ${DIR}/main.new ${DIR}/main && chmod 755 ${DIR}/main && sha256sum ${DIR}/main" \
    2>/dev/null > /tmp/up.out
grep -q "$sha" /tmp/up.out && echo "upload OK" || { echo "upload FAIL"; cat /tmp/up.out; exit 1; }

echo "== run ${START} ${WAIT}s with perf =="
timeout $((WAIT + 90)) setsid -w ssh \
    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    -o PubkeyAuthentication=no -o PreferredAuthentications=password \
    -o NumberOfPasswordPrompts=1 -T "root@${BOX}" \
    "killall -9 main 2>/dev/null; sleep 1; cd ${DIR} && (BOARD_APP_PERF=1 BOARD_APP_START=${START} nohup ./main > /tmp/perf.log 2>&1 &); sleep ${WAIT}; pgrep -l main; echo '--- perf ---'; grep '\[perf\]' /tmp/perf.log; echo '--- glyph errors ---'; grep -c 'cache not allocated' /tmp/perf.log; echo '--- log tail ---'; grep -v 'cache not allocated' /tmp/perf.log | tail -6" \
    2>/dev/null
