/**
 * @file http_client.h
 * @brief 精简的 HTTP/1.1 客户端（仅支持明文 HTTP，GET / POST）。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *body;       /* NUL 结尾，free 后由调用者接管所有权 */
    size_t len;
    int status_code;
} http_response_t;

/**
 * 对 http:// URL 发起 GET 请求。
 * @param url        绝对 URL，仅支持 http://
 * @param timeout_ms 连接/读取超时；<=0 时取 6000
 * @param cancel     可选标志，每约 200 ms 轮询一次；非零时中止
 * @param resp       输出响应（body 已分配）
 * @return 收到完整 HTTP 响应（任意状态码）返回 0，否则返回负错误码。
 */
int http_get_ex(const char *url, int timeout_ms, volatile int *cancel,
                http_response_t *resp);

/** 不支持取消的便捷封装。 */
int http_get(const char *url, int timeout_ms, http_response_t *resp);

/**
 * POST JSON（application/json）到 http:// URL。
 * @param body_len   -1 表示 body 以 NUL 结尾，自动求长度
 * @param content_type 可选；NULL 时用 "application/json"
 */
int http_post_ex(const char *url, const char *body, int body_len,
                 const char *content_type, int timeout_ms, volatile int *cancel,
                 http_response_t *resp);

/**
 * POST JSON，并附加自定义请求头（例如 `Authorization: Bearer xxx\r\n`）。
 *
 * @param extra_headers 原样拼进请求头的字符串，必须以 \r\n 结尾；
 *                      可为 NULL。这个参数是发 API key 的唯一途径。
 */
int http_post_headers(const char *url, const char *body, int body_len,
                      const char *content_type, const char *extra_headers,
                      int timeout_ms, volatile int *cancel,
                      http_response_t *resp);

void http_response_free(http_response_t *resp);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_CLIENT_H */
