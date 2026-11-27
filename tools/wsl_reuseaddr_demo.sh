#!/bin/bash
# 证明 SO_REUSEADDR 的作用边界：
#   A) 端口上残留 TIME_WAIT 连接时，没有 SO_REUSEADDR 会 bind 失败，有则成功
#   B) 已经有进程在 listen 时，SO_REUSEADDR 依然失败（它不能让两个进程共享端口）
set -uo pipefail
SRC=/mnt/d/data/code-files/c/embedded-systems-study/lvgl/lv_port_linux-release-v9.2
WORK=/tmp/reuse_test
PORT=20800
rm -rf "$WORK"; mkdir -p "$WORK"; cd "$WORK"

CJSON=$SRC/board_app/core/third_party/cjson
COMM=$SRC/board_app/app/comm/core
INC="-I$CJSON -I$COMM/include -I$SRC/server"

echo "== 准备两个版本：正常版 / 去掉 SO_REUSEADDR 的版本 =="
cp "$SRC/server/server.c" srv_reuse.c
sed 's/^    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));/    (void)on; \/* NO REUSEADDR *\//' \
    "$SRC/board_app/app/comm/core/src/comm_net.c" > net_noreuse.c
grep -c "NO REUSEADDR" net_noreuse.c | sed 's/^/  noreuse 版本打了补丁次数: /'
grep -c "SO_REUSEADDR" "$COMM/src/comm_net.c" | sed 's/^/  正常版本 SO_REUSEADDR 出现次数: /'

gcc -std=gnu11 -O1 -w $INC -o srv_reuse srv_reuse.c "$SRC/server/store.c" "$SRC/server/sha256.c" \
    "$COMM/src/comm_proto.c" "$COMM/src/comm_net.c" "$CJSON/cJSON.c" -lpthread -lm || exit 1
gcc -std=gnu11 -O1 -w $INC -o srv_noreuse srv_reuse.c "$SRC/server/store.c" "$SRC/server/sha256.c" \
    "$COMM/src/comm_proto.c" ./net_noreuse.c "$CJSON/cJSON.c" -lpthread -lm || exit 1
echo "  编译完成"

# 造 TIME_WAIT：让服务器接受一个连接并主动关闭
mk_timewait() {
    ./$1 --port $PORT --data "$WORK/d1" --no-color > /dev/null 2>&1 &
    local pid=$!
    sleep 0.5
    (exec 3<>/dev/tcp/127.0.0.1/$PORT; sleep 0.2; exec 3<&-) 2>/dev/null
    sleep 0.4
    kill -9 $pid 2>/dev/null
    wait $pid 2>/dev/null
    sleep 0.2
}

echo
echo "== A) 端口残留 TIME_WAIT 时重启 =="
mk_timewait srv_noreuse
echo -n "  TIME_WAIT 连接数: "
ss -tan 2>/dev/null | grep -c "TIME-WAIT.*:$PORT" || true
echo "  [无 SO_REUSEADDR] 立刻重启:"
timeout 3 ./srv_noreuse --port $PORT --data "$WORK/d1" --no-color 2>&1 | grep -E "监听失败|已启动|已被占用" | head -3 | sed 's/^/    /'
echo "  [有 SO_REUSEADDR] 立刻重启:"
timeout 2 ./srv_reuse --port $PORT --data "$WORK/d1" --no-color 2>&1 | grep -E "监听失败|已启动" | head -2 | sed 's/^/    /'

echo
echo "== B) 已经有一个进程在 listen 时，再起一个 =="
./srv_reuse --port $PORT --data "$WORK/d1" --no-color > /tmp/a.log 2>&1 &
A=$!
sleep 0.6
echo "  第一个进程: $(pgrep -f "srv_reuse --port $PORT" | head -1)"
echo "  [有 SO_REUSEADDR] 再起一个:"
timeout 3 ./srv_reuse --port $PORT --data "$WORK/d1" --no-color 2>&1 | head -4 | sed 's/^/    /'
kill -9 $A 2>/dev/null
wait $A 2>/dev/null

echo
echo "== C) SO_REUSEPORT 才能两进程共享端口（仅演示，本项目不用） =="
cat > /tmp/reuseport.c <<'CEND'
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
int main(int argc, char **argv) {
    int port = atoi(argv[1]);
    int fd = socket(AF_INET, SOCK_STREAM, 0), on = 1;
    struct sockaddr_in a;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    if (argc > 2) setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr.s_addr = INADDR_ANY;
    if (bind(fd, (struct sockaddr *)&a, sizeof(a)) != 0) { perror("bind"); return 1; }
    if (listen(fd, 8) != 0) { perror("listen"); return 1; }
    printf("bind+listen OK (port %d, reuseport=%s)\n", port, argc > 2 ? "on" : "off");
    sleep(2);
    return 0;
}
CEND
gcc -O1 -w -o /tmp/reuseport /tmp/reuseport.c || exit 1
/tmp/reuseport $((PORT + 1)) > /tmp/rp1.log 2>&1 &
P1=$!
sleep 0.4
echo -n "  已有监听时，只用 SO_REUSEADDR: "; /tmp/reuseport $((PORT + 1)) 2>&1 | head -1 | sed 's/^/    /'
kill -9 $P1 2>/dev/null; wait $P1 2>/dev/null
/tmp/reuseport $((PORT + 2)) use > /tmp/rp2.log 2>&1 &
P2=$!
sleep 0.4
echo -n "  已有监听时，加 SO_REUSEPORT:   "; /tmp/reuseport $((PORT + 2)) use 2>&1 | head -1 | sed 's/^/    /'
kill -9 $P2 2>/dev/null; wait $P2 2>/dev/null
echo
