#!/bin/bash
# 逐步检查「记住账号」在每一步之后是否还在
set -uo pipefail
BOX=192.168.64.216
APP=/root/lvgl/board_apps
DATA=$APP/data
export DISPLAY=:0 SSH_ASKPASS=/tmp/ap1.sh SSH_ASKPASS_REQUIRE=force
printf '#!/bin/sh\necho 1\n' > /tmp/ap1.sh; chmod +x /tmp/ap1.sh
ssh_cmd() {
    setsid -w ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -o PubkeyAuthentication=no -o PreferredAuthentications=password \
        -o NumberOfPasswordPrompts=1 -T "root@${BOX}" "$1" 2>/dev/null
}

echo "== 0) 当前状态 =="
ssh_cmd "killall -9 main 2>/dev/null; sleep 0.3; grep -E 'comm\.' $DATA/settings.conf || echo '  (空)'"

echo "== 1) 清空后重来 =="
ssh_cmd "sed -i '/^comm\./d' $DATA/settings.conf; grep -c comm $DATA/settings.conf || echo 0"

echo "== 2) 显式登录（AUTOLOGIN） =="
ssh_cmd "cd $APP && (BOARD_APP_COMM_AUTOLOGIN='127.0.0.1:20000,alice,alice123' nohup ./main > /tmp/t1.log 2>&1 &); sleep 7; grep -E 'autologin' /tmp/t1.log; echo '  settings:'; grep -E 'comm\.' $DATA/settings.conf"

echo "== 3) 正常退出（kill -TERM 而不是 -9） =="
ssh_cmd "killall -TERM main 2>/dev/null; sleep 1; echo '  settings:'; grep -E 'comm\.' $DATA/settings.conf"

echo "== 4) 重启（不带 env），看是否自动登录 =="
ssh_cmd "cd $APP && (nohup ./main > /tmp/t2.log 2>&1 &); sleep 7; grep -E '发现上次登录|autologin' /tmp/t2.log; echo '  settings:'; grep -E 'comm\.' $DATA/settings.conf"

echo "== 5) 再 kill -9 后重启一次 =="
ssh_cmd "killall -9 main 2>/dev/null; sleep 1; cd $APP && (nohup ./main > /tmp/t3.log 2>&1 &); sleep 7; grep -E '发现上次登录|autologin' /tmp/t3.log; echo '  settings:'; grep -E 'comm\.' $DATA/settings.conf; killall -9 main 2>/dev/null"
