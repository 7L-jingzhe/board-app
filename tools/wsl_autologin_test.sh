#!/bin/bash
# 验证「记住上次登录的账号 + 重启自动登录」
#   1) 第一次带 AUTOLOGIN 启动（模拟用户登录）-> 凭据落盘
#   2) 杀掉进程，第二次不带任何环境变量启动 -> 应自动登录 alice
set -uo pipefail
SRC=/mnt/d/data/code-files/c/embedded-systems-study/lvgl/lv_port_linux-release-v9.2
DST=/home/jingzhe/lvgl/lv_port_linux-release-v9.2
BOX=192.168.64.216
APP=/root/lvgl/board_apps
DATA=$APP/data

export DISPLAY=:0 SSH_ASKPASS=/tmp/ap1.sh SSH_ASKPASS_REQUIRE=force
printf '#!/bin/sh\necho 1\n' > /tmp/ap1.sh
chmod +x /tmp/ap1.sh
ssh_cmd() {
    setsid -w ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -o PubkeyAuthentication=no -o PreferredAuthentications=password \
        -o NumberOfPasswordPrompts=1 -T "root@${BOX}" "$1" 2>/dev/null
}
put() {
    local src=$1 dst=$2 sha
    sha=$(sha256sum "$src" | cut -d' ' -f1)
    gzip -9c "$src" | base64 -w0 | ssh_cmd "base64 -d | gzip -dc > '${dst}.new' && mv -f '${dst}.new' '${dst}' && chmod 755 '${dst}' && sha256sum '${dst}'" > /tmp/up.out
    grep -q "$sha" /tmp/up.out && echo "  ok   $dst" || { echo "  FAIL $dst"; cat /tmp/up.out; }
}

echo "== 部署 =="
cd "$DST"
rsync -a --delete "$SRC/board_app/" "$DST/board_app/"
cmake --build build_board_app -j8 2>&1 | grep -E " error|Built target main" | tail -1
put "$DST/bin/main" "$APP/main"

echo "== 清掉旧的记住状态，确保从零开始 =="
ssh_cmd "killall -9 main chat_server 2>/dev/null; sleep 0.5; grep -E 'comm\.' $DATA/settings.conf 2>/dev/null || echo '  (settings 里没有 comm.* 记录)'; sed -i '/^comm\./d' $DATA/settings.conf 2>/dev/null; cd $APP && (nohup ./chat_server --port 20000 --data $DATA --no-color > /tmp/al_srv.log 2>&1 &); sleep 1; grep 已启动 /tmp/al_srv.log"

echo "== 第一次启动：显式登录 alice（模拟用户手动登录） =="
ssh_cmd "cd $APP && (BOARD_APP_COMM_AUTOLOGIN='127.0.0.1:20000,alice,alice123' nohup ./main > /tmp/al1.log 2>&1 &); sleep 6; grep -E 'autologin|session ready' /tmp/al1.log; echo '--- 落盘内容 ---'; grep -E 'comm\.' $DATA/settings.conf; killall -9 main; sleep 0.5"

echo "== 第二次启动：不带任何环境变量（模拟重启） =="
ssh_cmd "cd $APP && (nohup ./main > /tmp/al2.log 2>&1 &); sleep 6; grep -E '发现上次登录|autologin|session ready' /tmp/al2.log; echo '--- 首页账号胶囊 ---'; dd if=/dev/fb0 of=/tmp/fb.raw bs=2400000 count=1 2>/dev/null; gzip -c /tmp/fb.raw | base64 -w0" > /tmp/board_autologin.out
grep -E "发现上次登录|autologin|session ready|comm\." /tmp/board_autologin.out | head -8
bash "$SRC/board_app/tools/dec_shot.sh" /tmp/board_autologin.out /mnt/d/data/code-files/c/embedded-systems-study/lvgl/docs_preview/board-autologin.png
