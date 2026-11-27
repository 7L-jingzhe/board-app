/**
 * @file tls_check.c
 * @brief 板端 https 自检工具：向 DeepSeek 发一个假 key 请求，验证 TLS/HTTP 链路。
 *
 * 期望输出：
 *   tls_available: yes
 *   http_post rc=0 status=401 ...
 *   RESULT: TLS+HTTP OK (only key missing)
 *
 * 用法（板上）：
 *   ./tls_check                              # 默认 https://api.deepseek.com/v1
 *   ./tls_check https://example.com          # 任意 https 站点
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include <stdio.h>
#include <string.h>

#include "board_core/http_client.h"
#include "board_core/tls_client.h"

int main(int argc, char **argv)
{
    http_response_t r = { 0 };
    char url[512];
    const char *base = argc > 1 ? argv[1] : "https://api.deepseek.com/v1";
    const char *body =
        "{\"model\":\"deepseek-chat\",\"messages\":[{\"role\":\"user\","
        "\"content\":\"ping\"}],\"max_tokens\":8}";
    int rc;
    int ok;

    /* 去掉结尾的 /，拼上 chat/completions */
    snprintf(url, sizeof(url), "%s", base);
    {
        size_t n = strlen(url);
        while (n > 0 && url[n - 1] == '/') {
            url[--n] = '\0';
        }
    }
    if (strstr(url, "/chat/completions") == NULL) {
        snprintf(url + strlen(url), sizeof(url) - strlen(url), "/chat/completions");
    }

    printf("url: %s\n", url);
    printf("tls_available: %s", tls_available() ? "yes" : "no");
    if (!tls_available()) {
        printf(" (%s)", tls_last_error());
    }
    printf("\n");

    rc = http_post_ex(url, body, -1, "application/json", 20000, NULL, &r);
    printf("http_post rc=%d status=%d len=%zu\n", rc, r.status_code, r.len);
    if (r.body != NULL && r.len > 0) {
        printf("body: %.300s\n", r.body);
    }
    if (rc != 0) {
        printf("tls_last_error: %s\n", tls_last_error());
    }

    /* 401（鉴权失败）就说明 TLS + HTTP 全通，只差真 key */
    ok = (rc == 0 && (r.status_code == 401 || r.status_code == 200));
    http_response_free(&r);
    printf("%s\n", ok ? "RESULT: TLS+HTTP OK (only key missing)"
                      : "RESULT: FAILED");
    return ok ? 0 : 1;
}
