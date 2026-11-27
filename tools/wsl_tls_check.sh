#!/bin/bash
# 在 WSL(x86) 上验证 http_client 的 https 支持：向 DeepSeek 发一个假 key 请求
# 期望：TLS 握手成功 + 服务器返回 401 + JSON 错误体（说明链路全通，只差真 key）
set -uo pipefail
S=/mnt/d/data/code-files/c/embedded-systems-study/lvgl/lv_port_linux-release-v9.2
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

    printf("tls_available: %s\n", tls_available() ? "yes" : "no");
    if (!tls_available()) {
        printf("tls_last_error: %s\n", tls_last_error());
    }
    rc = http_post_ex("https://api.deepseek.com/v1/chat/completions", body, -1,
                      "application/json", 20000, NULL, &r);
    printf("http_post rc=%d status=%d len=%zu\n", rc, r.status_code, r.len);
    if (r.body != NULL) {
        printf("body(前 300 字节): %.300s\n", r.body);
    }
    if (rc != 0) {
        printf("tls_last_error: %s\n", tls_last_error());
    }
    http_response_free(&r);
    return (rc == 0 && r.status_code == 401) ? 0 : 1;
}
CEND
gcc -std=gnu11 -O1 -I"$S/board_app/core/include" -o test_tls test_tls.c \
    "$S/board_app/core/src/http_client.c" "$S/board_app/core/src/tls_client.c" \
    -ldl -lpthread 2>&1 | head -10
echo "--- run ---"
./test_tls
echo "exit=$?"
