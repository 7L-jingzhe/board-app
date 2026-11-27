#!/bin/bash
# 板端验证 https：修 DNS -> 交叉编译小测试程序 -> 部署 -> 向 DeepSeek 发假 key 请求
set -uo pipefail
S=/mnt/d/data/code-files/c/embedded-systems-study/lvgl/lv_port_linux-release-v9.2
SYSROOT=$HOME/aarch64-bionic
BOX=192.168.64.216
APP=/root/lvgl/board_apps

export DISPLAY=:0 SSH_ASKPASS=/tmp/ap1.sh SSH_ASKPASS_REQUIRE=force
printf '#!/bin/sh\necho 1\n' > /tmp/ap1.sh
chmod +x /tmp/ap1.sh
ssh_cmd() {
    setsid -w ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -o PubkeyAuthentication=no -o PreferredAuthentications=password \
        -o NumberOfPasswordPrompts=1 -T "root@${BOX}" "$1" 2>/dev/null
}

echo "== 1) 修板端 DNS（resolv.conf 之前是空的） =="
ssh_cmd "echo 'nameserver 192.168.64.254' > /etc/resolv.conf; echo 'nameserver 8.8.8.8' >> /etc/resolv.conf; cat /etc/resolv.conf; echo '--- 解析测试 ---'; nslookup api.deepseek.com 2>&1 | tail -4"

echo
echo "== 2) 交叉编译 https 测试程序 =="
cd /tmp || exit 1
cat > test_tls.c <<'CEND'
#include <stdio.h>
#include "board_core/http_client.h"
#include "board_core/tls_client.h"

int main(void)
{
    http_response_t r = { 0 };
    const char *body =
        "{\"model\":\"deepseek-chat\",\"messages\":[{\"role\":\"user\","
        "\"content\":\"ping\"}],\"max_tokens\":8}";
    int rc;
    int ok;

    printf("tls_available: %s", tls_available() ? "yes" : "no");
    if (!tls_available()) {
        printf(" (%s)", tls_last_error());
    }
    printf("\n");
    rc = http_post_ex("https://api.deepseek.com/v1/chat/completions", body, -1,
                      "application/json", 20000, NULL, &r);
    printf("http_post rc=%d status=%d len=%zu\n", rc, r.status_code, r.len);
    if (r.body != NULL) {
        printf("body: %.300s\n", r.body);
    }
    ok = (rc == 0 && r.status_code == 401);
    http_response_free(&r);
    printf("%s\n", ok ? "RESULT: TLS+HTTP OK (only key missing)" : "RESULT: FAILED");
    return ok ? 0 : 1;
}
CEND
aarch64-linux-gnu-gcc -std=gnu11 -O2 \
    -B"$SYSROOT/usr/aarch64-linux-gnu/lib/" -L"$SYSROOT/usr/aarch64-linux-gnu/lib/" \
    -I"$S/board_app/core/include" -o test_tls_aarch64 test_tls.c \
    "$S/board_app/core/src/http_client.c" "$S/board_app/core/src/tls_client.c" \
    -ldl -lpthread 2>&1 | head -10
file test_tls_aarch64 | cut -c1-80

echo
echo "== 3) 部署并运行 =="
gzip -9c test_tls_aarch64 | base64 -w0 | ssh_cmd "base64 -d | gzip -dc > $APP/test_tls && chmod 755 $APP/test_tls && echo uploaded"
ssh_cmd "cd $APP && ./test_tls 2>&1"
