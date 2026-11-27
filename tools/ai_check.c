/**
 * @file ai_check.c
 * @brief 板端 AI 自检：用真实 BOARD_APP_AI_KEY 请求一次，打印完整结果。
 *
 * 和聊天页走完全相同的路径（chat_ai_build_url + chat_ai_build_request +
 * http_post_ex + chat_ai_parse_response），只是不经过 LVGL，
 * 方便在 SSH 里定位“网络不可用”。
 *
 * 用法：
 *   BOARD_APP_AI_KEY=sk-xxx ./ai_check
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_core/http_client.h"
#include "board_core/tls_client.h"
#include "chat/chat_llm.h"
#include "chat/chat_service.h"
#include "chat/chat_types.h"

int main(void)
{
    chat_ai_config_t cfg;
    char url[768];
    char *body = NULL;
    http_response_t resp = { 0 };
    char err[192] = { 0 };
    char *reply = NULL;
    int rc;
    int ret;
    int prc;

    chat_service_load_config(&cfg);
    printf("base_url : %s\n", cfg.base_url);
    printf("model    : %s\n", cfg.model);
    printf("api_key  : %s\n", cfg.api_key[0] != '\0' ? "(已设置)" : "(未设置)");
    printf("enabled  : %s\n", cfg.enabled ? "yes" : "no");
    printf("tls      : %s\n", tls_available() ? "yes" : "no");
    if (!tls_available()) {
        printf("tls_error: %s\n", tls_last_error());
    }

    if (!cfg.enabled) {
        printf("RESULT: 没有配置大模型（只设 BOARD_APP_AI_KEY 即可）\n");
        return 1;
    }
    rc = chat_ai_build_url(&cfg, url, sizeof(url));
    printf("url      : %s (build=%d)\n", url, rc);
    if (rc != 0) {
        printf("RESULT: URL 不合法\n");
        return 1;
    }
    rc = chat_ai_build_request(&cfg, NULL, 0, "你好，请回复“收到”两个字", &body);
    printf("build    : rc=%d\n", rc);
    if (rc != 0 || body == NULL) {
        printf("RESULT: 请求构造失败\n");
        return 1;
    }

    {
        char auth[224];
        const char *extra = NULL;
        if (cfg.api_key[0] != '\0') {
            snprintf(auth, sizeof(auth), "Authorization: Bearer %s\r\n",
                     cfg.api_key);
            extra = auth;
        }
        ret = http_post_headers(url, body, -1, NULL, extra, 25000, NULL, &resp);
    }
    printf("http_post: rc=%d status=%d len=%zu\n", ret, resp.status_code, resp.len);
    if (ret != 0) {
        printf("tls_last_error: %s\n", tls_last_error());
        printf("RESULT: 请求失败 —— 这就是聊天页“网络不可用”的原因\n");
        free(body);
        http_response_free(&resp);
        return 1;
    }
    if (resp.body != NULL) {
        printf("body     : %.400s\n", resp.body);
    }
    prc = chat_ai_parse_response(resp.body, resp.len, &reply, err, sizeof(err));
    printf("parse    : rc=%d err=%s reply=\"%s\"\n", prc, err,
           reply != NULL ? reply : "(null)");
    {
        int ok = (ret == 0 && resp.status_code == 200 && prc == 0);
        free(reply);
        free(body);
        http_response_free(&resp);
        printf("RESULT: %s\n", ok ? "OK" : "FAILED");
        return ok ? 0 : 1;
    }
}
