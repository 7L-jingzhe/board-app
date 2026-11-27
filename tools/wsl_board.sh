#!/bin/bash
# Build + deploy main, then run on the board for N seconds and pull a screenshot.
# usage: wsl_board.sh [start-id] [wait-seconds]
set -uo pipefail
SRC=/mnt/d/data/code-files/c/embedded-systems-study/lvgl/lv_port_linux-release-v9.2
DST=/home/jingzhe/lvgl/lv_port_linux-release-v9.2
BOX=192.168.64.216
DIR=/root/lvgl/board_apps
START="${1:-chat}"
WAIT="${2:-45}"
SHOT=/mnt/d/data/code-files/c/embedded-systems-study/lvgl/docs_preview/board-${START}.raw
LOG=/mnt/d/data/code-files/c/embedded-systems-study/lvgl/docs_preview/board-${START}.log

cd "$DST"
export DISPLAY=:0 SSH_ASKPASS=/tmp/ap1.sh SSH_ASKPASS_REQUIRE=force
printf '#!/bin/sh\necho 1\n' > /tmp/ap1.sh
chmod +x /tmp/ap1.sh

echo "== sync + build =="
rsync -a --delete "$SRC/board_app/" "$DST/board_app/"
rsync -a "$SRC/lv_conf.h" "$DST/"
cmake --build build_board_app -j8 2>&1 | grep -E " error|Built target main" | tail -2
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

echo "== run ${START} for ${WAIT}s =="
timeout $((WAIT + 60)) setsid -w ssh \
    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    -o PubkeyAuthentication=no -o PreferredAuthentications=password \
    -o NumberOfPasswordPrompts=1 -T "root@${BOX}" \
    "killall -9 main 2>/dev/null; sleep 1; cd ${DIR} && (BOARD_APP_START=${START} nohup ./main > /tmp/board_app.log 2>&1 &); sleep ${WAIT}; pgrep -l main; cat /tmp/board_app.log" \
    2>/dev/null > "$LOG"
cat "$LOG"

echo "== screenshot =="
timeout 120 setsid -w ssh \
    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    -o PubkeyAuthentication=no -o PreferredAuthentications=password \
    -o NumberOfPasswordPrompts=1 -T "root@${BOX}" \
    "dd if=/dev/fb0 of=/tmp/fb.raw bs=2400000 count=1 2>/dev/null; gzip -c /tmp/fb.raw | base64 -w0" \
    2>/dev/null > /tmp/fb.b64
echo "payload bytes: $(wc -c < /tmp/fb.b64)"
python3 - "$SHOT" <<'PY'
import base64, gzip, sys
raw = open('/tmp/fb.b64','rb').read()
lines = [l for l in raw.split(b'\n') if len(l) > 1000]
if not lines:
    print("no screenshot payload")
    raise SystemExit(0)
out = gzip.decompress(base64.b64decode(lines[-1]))
open(sys.argv[1], 'wb').write(out)
print("framebuffer bytes:", len(out))
PY
