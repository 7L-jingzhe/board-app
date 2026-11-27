#!/bin/bash
# 交叉编译服务器 + 测试客户端，部署到开发板，板上起服务器并自动登录截图。
set -uo pipefail
SRC=/mnt/d/data/code-files/c/embedded-systems-study/lvgl/lv_port_linux-release-v9.2
DST=/home/jingzhe/lvgl/lv_port_linux-release-v9.2
BOX=192.168.64.216
APP=/root/lvgl/board_apps
DATA=/root/lvgl/chat_data

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

echo "== 交叉编译 server / test_client (aarch64 static) =="
cd "$SRC/server"
make clean >/dev/null 2>&1
make CROSS=aarch64-linux-gnu- CFLAGS="-std=gnu11 -O2 -static -Wall -Wextra -Wno-unused-parameter" -j4 2>&1 | grep -E "error|warning" | head -10
file chat_server chat_test_client
ls -la chat_server chat_test_client

echo "== 交叉编译 board_app =="
cd "$DST"
rsync -a --delete "$SRC/board_app/" "$DST/board_app/"
rsync -a "$SRC/main.c" "$SRC/lv_conf.h" "$DST/"
cmake --build build_board_app -j8 2>&1 | grep -E " error|Built target main" | tail -2
file bin/main

echo "== 部署 =="
ssh_cmd "killall -9 main chat_server 2>/dev/null; mkdir -p $DATA; echo ready"
put "$SRC/server/chat_server" "$APP/chat_server"
put "$SRC/server/chat_test_client" "$APP/chat_test_client"
put "$DST/bin/main" "$APP/main"
ssh_cmd "ls -la $APP/chat_server $APP/chat_test_client $APP/main"

echo "== 板上起服务器 + 建账号 =="
ssh_cmd "cd $APP && (./chat_server --port 20000 --data $DATA --no-color > /tmp/srv.log 2>&1 &) ; sleep 1; ./chat_test_client 127.0.0.1 20000 --script '/reg alice alice123;/reg bob bob123;/login alice alice123;/add bob;/logout'; echo '--- 账号 ---'; cat $DATA/accounts.txt"

echo "== 板上跑客户端（自动登录）并抓图 =="
ssh_cmd "cd $APP && (BOARD_APP_START=comm BOARD_APP_COMM_AUTOLOGIN='127.0.0.1:20000,alice,alice123' nohup ./main > /tmp/cli.log 2>&1 &); sleep 6; pgrep -l main; echo '--- client log ---'; grep -E 'comm|nav' /tmp/cli.log | head -8; dd if=/dev/fb0 of=/tmp/fb.raw bs=2400000 count=1 2>/dev/null; gzip -c /tmp/fb.raw | base64 -w0" > /tmp/board_comm.out
grep -E "account|alice|bob|comm|nav|pgrep|^[0-9]+ main" /tmp/board_comm.out | head -12
echo "  payload bytes: $(wc -c < /tmp/board_comm.out)"
