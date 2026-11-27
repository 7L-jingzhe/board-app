#!/bin/bash
# 验证聊天记录持久化：bob 发消息 -> 记录落盘 -> 重启客户端 -> 历史仍在
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
    grep -q "$sha" /tmp/up.out && echo "  ok   $dst" || echo "  FAIL $dst"
}

echo "== 构建并部署 =="
cd "$DST" || exit 1
rsync -a --delete "$SRC/board_app/" "$DST/board_app/"
cp "$SRC/CMakeLists.txt" "$DST/CMakeLists.txt"
cmake --build build_board_app -j8 2>&1 | grep -E " error|Built target main" | tail -1
ssh_cmd "killall -9 main 2>/dev/null; rm -rf $DATA/comm_history; echo clean"
put "$DST/bin/main" "$APP/main"

echo "== 起服务器 + 客户端(alice 自动登录进聊天室) =="
ssh_cmd "cd $APP && (pgrep -x chat_server >/dev/null || nohup ./chat_server --port 20000 --data $DATA --no-color > /tmp/srv2.log 2>&1 &); sleep 1; (BOARD_APP_COMM_AUTOLOGIN='127.0.0.1:20000,alice,alice123' BOARD_APP_START=comm nohup ./main > /tmp/c1.log 2>&1 &); sleep 6; pgrep -x main && echo RUNNING"

echo "== bob 发两条消息给 alice =="
ssh_cmd "cd $APP && ./chat_test_client 127.0.0.1 20000 --script '/login bob bob123;/msg alice 第一条：你好呀;/sleep 400;/msg alice 第二条：这是历史记录测试;/sleep 800' 2>&1 | grep -E 'msg|ok' | head -4"

echo "== 历史文件 =="
ssh_cmd "ls -la $DATA/comm_history/ 2>/dev/null; echo '--- 内容 ---'; cat $DATA/comm_history/bob.jsonl 2>/dev/null | head -5"

echo "== 重启客户端（模拟下次开机） =="
ssh_cmd "killall -9 main; sleep 1; cd $APP && (BOARD_APP_COMM_AUTOLOGIN='127.0.0.1:20000,alice,alice123' BOARD_APP_START=comm nohup ./main > /tmp/c2.log 2>&1 &); sleep 6; grep -E 'chat history|autologin' /tmp/c2.log | head -3; dd if=/dev/fb0 of=/tmp/fb.raw bs=2400000 count=1 2>/dev/null; gzip -c /tmp/fb.raw | base64 -w0" > /tmp/hist.out
grep -E "chat history|autologin" /tmp/hist.out | head -3
bash "$SRC/board_app/tools/dec_shot.sh" /tmp/hist.out /mnt/d/data/code-files/c/embedded-systems-study/lvgl/docs_preview/board-chat-history.png
