#!/bin/bash
# 围棋模块板上验证：交叉编译 server/main -> 部署 -> 板上起服务与客户端 -> bob 邀请 alice -> 抓图
set -uo pipefail
SRC=/mnt/d/data/code-files/c/embedded-systems-study/lvgl/lv_port_linux-release-v9.2
DST=/home/jingzhe/lvgl/lv_port_linux-release-v9.2
BOX=192.168.64.216
APP=/root/lvgl/board_apps
DATA=/root/lvgl/board_apps/data

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

echo "== 交叉编译 server（含 game.c + weiqi.c） =="
cd "$SRC/server"
make clean >/dev/null 2>&1
make CROSS=aarch64-linux-gnu- CFLAGS="-std=gnu11 -O2 -static -Wall -Wextra -Wno-unused-parameter" -j4 2>&1 | grep -E "error|warning" | head -10
file chat_server chat_test_client | cut -c1-80

echo "== 交叉编译 board_app =="
cd "$DST"
rsync -a --delete "$SRC/board_app/" "$DST/board_app/"
rsync -a "$SRC/main.c" "$SRC/lv_conf.h" "$DST/"
cmake --build build_board_app -j8 2>&1 | grep -E " error|Built target main" | tail -2

echo "== 部署 =="
ssh_cmd "killall -9 main chat_server 2>/dev/null; sleep 0.5; echo ready"
put "$SRC/server/chat_server" "$APP/chat_server"
put "$SRC/server/chat_test_client" "$APP/chat_test_client"
put "$DST/bin/main" "$APP/main"

echo "== 板上起服务器 =="
ssh_cmd "cd $APP && (nohup ./chat_server --port 20000 --data $DATA --no-color > /tmp/game_srv.log 2>&1 &); sleep 1; grep -E '已启动|围棋' /tmp/game_srv.log | head -3; cat $DATA/friends.txt"

echo "== 板上起围棋客户端（自动登录 alice） =="
ssh_cmd "cd $APP && (BOARD_APP_START=weiqi BOARD_APP_COMM_AUTOLOGIN='127.0.0.1:20000,alice,alice123' nohup ./main > /tmp/game_cli.log 2>&1 &); sleep 5; grep -E 'comm|nav|weiqi' /tmp/game_cli.log | head -6"

echo "== bob 邀请 alice 下 13 路棋，并落子 ==="
ssh_cmd "cd $APP && (nohup ./chat_test_client 127.0.0.1 20000 --script '/login bob bob123;/sleep 800;/wqinvite alice 13;/sleep 3000;/wqmove 3 3;/sleep 3000;/wqmove 9 9;/sleep 20000' > /tmp/game_bob.log 2>&1 &); sleep 8; echo '--- bob ---'; grep -E 'game|board' /tmp/game_bob.log | head -6; echo '--- 服务器 ---'; grep -E '邀请|开局' /tmp/game_srv.log | tail -3"

echo "== 抓图（对局进行中） =="
ssh_cmd "dd if=/dev/fb0 of=/tmp/fb.raw bs=2400000 count=1 2>/dev/null; gzip -c /tmp/fb.raw | base64 -w0" > /tmp/board_weiqi.out
echo "  payload: $(wc -c < /tmp/board_weiqi.out) bytes"
bash "$SRC/board_app/tools/dec_shot.sh" /tmp/board_weiqi.out /mnt/d/data/code-files/c/embedded-systems-study/lvgl/docs_preview/board-weiqi.png
