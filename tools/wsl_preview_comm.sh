#!/bin/bash
# 起服务器 + 跑两次主机预览：一次看登录页，一次自动登录看会话页。
set -uo pipefail
SRC=/mnt/d/data/code-files/c/embedded-systems-study/lvgl/lv_port_linux-release-v9.2
DST=/home/jingzhe/lvgl/lv_port_linux-release-v9.2
OUT=/mnt/d/data/code-files/c/embedded-systems-study/lvgl/docs_preview
WORK=/tmp/comm_preview
PORT=20700

pkill -f chat_server 2>/dev/null
sleep 0.2

echo "== build server (x86) + preview =="
cd "$SRC/server"
make clean >/dev/null 2>&1
make -j4 2>&1 | grep -E "error|warning" | head -10
rm -rf "$WORK" && mkdir -p "$WORK"
cp chat_server chat_test_client "$WORK/"

cd "$DST"
rsync -a --delete "$SRC/board_app/" "$DST/board_app/"
rsync -a "$SRC/lv_conf.h" "$SRC/main.c" "$DST/"
cmake -S board_app/tests/preview -B build_preview -DCMAKE_BUILD_TYPE=Debug >/tmp/pcfg.log 2>&1 || {
    tail -20 /tmp/pcfg.log; exit 1; }
cmake --build build_preview -j8 >/tmp/pbuild.log 2>&1 || {
    grep -i " error" /tmp/pbuild.log | head -20; exit 1; }
grep -iE "warning: .*comm" /tmp/pbuild.log | head -10

echo "== 预置账号 alice/bob + 好友关系 =="
cd "$WORK"
./chat_server --port $PORT --data "$WORK/data" --no-color > server.log 2>&1 &
SRV=$!
sleep 0.5
./chat_test_client 127.0.0.1 $PORT --script "/reg alice alice123;/reg bob bob123;/login alice alice123;/add bob;/sleep 400" > /dev/null 2>&1
# 让 bob 上线一会儿，会话页能看到在线状态和一条真实消息
( ./chat_test_client 127.0.0.1 $PORT --udp 31102 --script "/login bob bob123;/sleep 9000" > bob.log 2>&1 ) &
BOB=$!
sleep 0.6
./chat_test_client 127.0.0.1 $PORT --script "/login alice alice123;/msg bob 你好，我是 alice;/sleep 300" > /dev/null 2>&1

echo "== run1: 登录页 =="
cd "$DST"
mkdir -p /tmp/pv_login
BOARD_APP_ROOT="$DST/board_app" PREVIEW_OUT=/tmp/pv_login \
    ./build_preview/board_preview >/tmp/pv1.log 2>&1
cp -f /tmp/pv_login/preview-09-comm-login.png "$OUT/preview-09-comm-login.png"

echo "== run2: 自动登录 -> 聊天 + 围棋（bob 发起邀请） =="
mkdir -p /tmp/pv_chat
# bob 常驻在线，等 alice 登录后发出围棋邀请
( "$WORK/chat_test_client" 127.0.0.1 $PORT --udp 31102 --script "/login bob bob123;/sleep 19000;/wqinvite alice 13;/sleep 6000;/wqmove 3 3;/sleep 12000" \
    > /tmp/pv_bob.log 2>&1 ) &
BOBGAME=$!
BOARD_APP_ROOT="$DST/board_app" \
BOARD_APP_COMM_AUTOLOGIN="127.0.0.1:$PORT,alice,alice123" \
PREVIEW_OUT=/tmp/pv_chat \
    ./build_preview/board_preview 2>&1 | grep -E "\[comm\]|\[game\]|\[nav\] open weiqi" | head -8
for f in preview-09b-comm-chat preview-12-account preview-10-games-with-weiqi preview-11-weiqi-lobby preview-11c-weiqi-size9 preview-11b-weiqi-game; do
  cp -f "/tmp/pv_chat/$f.png" "$OUT/$f.png" 2>/dev/null && echo "  saved $f"
done
kill $BOBGAME 2>/dev/null

echo "== 收尾 =="
wait $BOB 2>/dev/null
kill -INT $SRV 2>/dev/null
sleep 0.3
ls -la "$OUT"/preview-09*.png
