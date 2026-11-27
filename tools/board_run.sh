#!/bin/bash
# 在开发板上执行一条命令（密码通过 SSH_ASKPASS 提供，避免交互式输入）。
# 用法：bash board_app/tools/board_run.sh "<远程命令>"
# 板子 IP 用 BOARD_IP 覆盖，默认自动扫描最常见的地址。
export DISPLAY=:0
export SSH_ASKPASS=/tmp/ap1.sh
export SSH_ASKPASS_REQUIRE=force

IP="${BOARD_IP:-192.168.64.216}"
CMD="${1:-echo hello}"

setsid -w ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
  -o PubkeyAuthentication=no -o PreferredAuthentications=password \
  -o NumberOfPasswordPrompts=1 -T "root@${IP}" "$CMD" < /dev/null 2>/dev/null
