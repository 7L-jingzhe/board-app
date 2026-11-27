#!/bin/bash
# 把本地文件安全地传到开发板（不依赖板端的 tar/scp 扩展）。
# 用法：bash board_app/tools/board_put.sh <本地文件> <远程路径>
# 原理：gzip + base64 后经 ssh 写入，板端只用 busybox 自带的 base64/gzip。
set -euo pipefail
export DISPLAY=:0
export SSH_ASKPASS=/tmp/ap1.sh
export SSH_ASKPASS_REQUIRE=force

IP="${BOARD_IP:-192.168.64.216}"
LOCAL="${1:?本地文件}"
REMOTE="${2:?远程路径}"

TMPB64="$(mktemp)"
trap 'rm -f "$TMPB64"' EXIT

gzip -c "$LOCAL" | base64 -w0 > "$TMPB64"
SHA="$(sha256sum "$LOCAL" | cut -d' ' -f1)"

setsid -w ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
  -o PubkeyAuthentication=no -o PreferredAuthentications=password \
  -o NumberOfPasswordPrompts=1 -T "root@${IP}" \
  "base64 -d > '${REMOTE}.b64' && gzip -dc '${REMOTE}.b64' > '${REMOTE}' && rm -f '${REMOTE}.b64' && sha256sum '${REMOTE}'" \
  < "$TMPB64" 2>/dev/null | tee /tmp/board_put.sha

echo "expected: ${SHA}"
grep -q "$SHA" /tmp/board_put.sha && echo "OK: ${REMOTE} 校验一致"
