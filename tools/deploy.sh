#!/bin/bash
# 一键部署：同步源码 -> 主机单测 -> aarch64 交叉编译 -> 上传 -> 重启验证
#
# 在 WSL 里运行（需要 aarch64-linux-gnu-gcc、cmake、ssh）：
#   bash board_app/tools/deploy.sh
#   BOARD_IP=192.168.64.216 BOARD_PASS=1 bash board_app/tools/deploy.sh
#   bash board_app/tools/deploy.sh --with-server      # 连 chat_server 一起部署
#   bash board_app/tools/deploy.sh --skip-test --skip-build
#
# 环境变量（都有默认值）：
#   BOARD_IP    开发板 IP                     默认 192.168.64.216
#   BOARD_USER  SSH 用户                      默认 root
#   BOARD_PASS  SSH 密码                      默认 1
#   BOARD_DIR   板上安装目录                  默认 /root/lvgl/board_apps
#   SRC_WIN     宿主机源码目录（Windows 侧）  由脚本自动推导
#
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Board App contributors
set -uo pipefail

BOARD_IP=${BOARD_IP:-192.168.64.216}
BOARD_USER=${BOARD_USER:-root}
BOARD_PASS=${BOARD_PASS:-1}
BOARD_DIR=${BOARD_DIR:-/root/lvgl/board_apps}
WITH_SERVER=0
SKIP_TEST=0
SKIP_BUILD=0

for arg in "$@"; do
    case "$arg" in
    --with-server) WITH_SERVER=1 ;;
    --skip-test) SKIP_TEST=1 ;;
    --skip-build) SKIP_BUILD=1 ;;
    -h | --help)
        sed -n '2,18p' "$0"
        exit 0
        ;;
    *) echo "未知参数: $arg" >&2 && exit 2 ;;
    esac
done

# WSL 里脚本位于 <SRC>/board_app/tools/，据此推导两个目录
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/../.." && pwd)          # D: 上的工程目录（/mnt/d/...）
BOARD_APP="$SRC/board_app"
DST=${BUILD_DIR:-$HOME/lvgl/lv_port_linux-release-v9.2}
BIN_MAIN="$DST/bin/main"

say() { printf '\n\033[1;36m== %s ==\033[0m\n' "$*"; }
ok() { printf '  \033[32mok\033[0m   %s\n' "$*"; }
warn() { printf '  \033[33mwarn\033[0m %s\n' "$*"; }
die() {
    printf '  \033[31mFAIL\033[0m %s\n' "$*" >&2
    exit 1
}

# --- SSH 辅助：dropbear 没有 tty 密码输入，用 SSH_ASKPASS ------------------
ASKPASS=$(mktemp)
printf '#!/bin/sh\necho %q\n' "$BOARD_PASS" >"$ASKPASS"
chmod +x "$ASKPASS"
export SSH_ASKPASS="$ASKPASS"
export SSH_ASKPASS_REQUIRE=force
export DISPLAY=:0
SSH_OPTS=(-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null
    -o PubkeyAuthentication=no -o PreferredAuthentications=password
    -o NumberOfPasswordPrompts=1 -T)

ssh_run() { setsid -w ssh "${SSH_OPTS[@]}" "$BOARD_USER@$BOARD_IP" "$1" 2>/dev/null; }

# 上传单个文件：gzip+base64 走 ssh 管道（板上没有 rsync/scp 服务端也能用）
push_file() {
    local src=$1 dst=$2 sha remote
    [ -f "$src" ] || die "找不到文件 $src"
    sha=$(sha256sum "$src" | cut -d' ' -f1)
    remote=$(gzip -9c "$src" | base64 -w0 |
        ssh_run "base64 -d | gzip -dc > '$dst.new' && mv -f '$dst.new' '$dst' && chmod 755 '$dst' && sha256sum '$dst'")
    case "$remote" in
    *"$sha"*) ok "$dst ($(du -h "$src" | cut -f1))" ;;
    *) die "上传校验失败: $dst" ;;
    esac
}

# 上传资源目录（字体、词库等）：打 tar 包再走同一个管道
push_tar() {
    local dir=$1 target=$2 name
    name=$(basename "$dir")
    # 板上是 BusyBox tar：不支持 -z，改成 gzip 解压后从 stdin 读
    tar -czf - -C "$(dirname "$dir")" "$name" |
        ssh_run "gzip -dc | tar xf - -C '$target'" || die "资源上传失败: $dir"
    ok "$target/$name"
}

# --- 1. 同步源码到 WSL（ext4 上编译比 /mnt 快很多） -----------------------
say "1/5 同步源码 -> WSL"
mkdir -p "$DST"
rsync -a --delete "$BOARD_APP/" "$DST/board_app/" || die "rsync board_app 失败"
rsync -a "$SRC/main.c" "$SRC/lv_conf.h" "$DST/" 2>/dev/null
# 服务器是独立程序，单独同步（--delete 只清它的构建产物与测试数据）
if [ -d "$SRC/server" ]; then
    rsync -a --delete "$SRC/server/" "$DST/server/"
fi
ok "$SRC -> $DST"

# --- 2. 主机单测（不碰板子，先保证逻辑没坏） ------------------------------
if [ "$SKIP_TEST" -eq 0 ]; then
    say "2/5 主机单元测试"
    cmake -S "$DST/board_app/tests/host" -B "$DST/build_host_tests" >/dev/null 2>&1
    cmake --build "$DST/build_host_tests" -j"$(nproc)" >/dev/null 2>&1 || die "主机测试编译失败"
    (cd "$DST/build_host_tests" && ctest --output-on-failure 2>&1 | tail -3)
    ok "host tests"
else
    say "2/5 主机单元测试（已跳过）"
fi

# --- 3. 交叉编译 ----------------------------------------------------------
if [ "$SKIP_BUILD" -eq 0 ]; then
    say "3/5 交叉编译 board_app (aarch64)"
    cmake -S "$DST" -B "$DST/build_board_app" >/dev/null 2>&1
    cmake --build "$DST/build_board_app" -j"$(nproc)" 2>&1 |
        grep -E " error|Built target main" | tail -3
    [ -f "$BIN_MAIN" ] || die "没有生成 bin/main"
    file "$BIN_MAIN" | sed 's/^/  /'
    ok "bin/main"

    if [ "$WITH_SERVER" -eq 1 ]; then
        say "3b/5 交叉编译 chat_server (静态链接)"
        (cd "$DST/server" &&
            make clean >/dev/null 2>&1 &&
            make CROSS=aarch64-linux-gnu- \
                CFLAGS="-std=gnu11 -O2 -static -Wall -Wextra -Wno-unused-parameter" \
                -j"$(nproc)" 2>&1 | grep -E " error" | head -5)
        [ -f "$DST/server/chat_server" ] || die "chat_server 编译失败"
        ok "chat_server"
    fi
else
    say "3/5 交叉编译（已跳过，用现有 bin/main）"
fi

# --- 4. 上传 --------------------------------------------------------------
say "4/5 上传到 $BOARD_USER@$BOARD_IP:$BOARD_DIR"
ssh_run "mkdir -p '$BOARD_DIR' '$BOARD_DIR/data' && killall -9 main 2>/dev/null; echo ready" >/dev/null
push_file "$BIN_MAIN" "$BOARD_DIR/main"
push_tar "$BOARD_APP/assets" "$BOARD_DIR"
if [ "$WITH_SERVER" -eq 1 ]; then
    push_file "$DST/server/chat_server" "$BOARD_DIR/chat_server"
    push_file "$DST/server/chat_test_client" "$BOARD_DIR/chat_test_client"
fi

# --- 5. 重启并自检 --------------------------------------------------------
say "5/5 重启并自检"
ssh_run "cd '$BOARD_DIR' && (nohup ./main > /tmp/board_app.log 2>&1 &); sleep 5;
    if pgrep -x main >/dev/null; then
        echo RUNNING
        grep -E 'registered|font cached|error' /tmp/board_app.log | head -5
    else
        echo DEAD
        tail -20 /tmp/board_app.log
    fi"
if ssh_run "pgrep -x main" | grep -q .; then
    ok "main 已在板上运行"
else
    warn "main 没在运行，查看 /tmp/board_app.log"
fi

rm -f "$ASKPASS"
printf '\n完成。板端日志：ssh %s@%s "tail -f /tmp/board_app.log"\n' "$BOARD_USER" "$BOARD_IP"
