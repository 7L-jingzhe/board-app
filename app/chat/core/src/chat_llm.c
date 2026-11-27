/**
 * @file chat_llm.c
 * @brief OpenAI 兼容协议的请求构造与响应解析。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "chat/chat_llm.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "board_core/board_log.h"
#include "cJSON.h"

/**
 * 把 UTF-8 文本转义为 JSON 字符串字面量（含首尾引号）。
 * UTF-8 多字节字符原样保留（JSON 允许），只转义控制字符和双引号/反斜杠。
 * @return 堆分配字符串，调用者 free()；失败返回 NULL。
 */
static char *json_quote(const char *s)
{
    size_t cap = 3;   /* 两个引号 + NUL */
    char *out;
    size_t n = 0;
    if (s == NULL) {
        s = "";
    }
    for (const char *p = s; *p != '\0'; p++) {
        cap += ((unsigned char)*p < 0x20) ? 6 : 2;
    }
    out = malloc(cap);
    if (out == NULL) {
        return NULL;
    }
    out[n++] = '"';
    for (; *s != '\0'; s++) {
        unsigned char ch = (unsigned char)*s;
        switch (ch) {
        case '"':
            out[n++] = '\\';
            out[n++] = '"';
            break;
        case '\\':
            out[n++] = '\\';
            out[n++] = '\\';
            break;
        case '\n':
            out[n++] = '\\';
            out[n++] = 'n';
            break;
        case '\r':
            out[n++] = '\\';
            out[n++] = 'r';
            break;
        case '\t':
            out[n++] = '\\';
            out[n++] = 't';
            break;
        default:
            if (ch < 0x20) {
                n += (size_t)snprintf(out + n, cap - n, "\\u%04x", ch);
            } else {
                out[n++] = (char)ch;
            }
            break;
        }
    }
    out[n++] = '"';
    out[n] = '\0';
    return out;
}

static int json_append_string(cJSON *parent, const char *key, const char *value)
{
    char *quoted;
    cJSON *item;
    if (parent == NULL || key == NULL) {
        return -EINVAL;
    }
    quoted = json_quote(value);
    if (quoted == NULL) {
        return -ENOMEM;
    }
    /* 用 cJSON 解析这个字符串字面量得到正确转义后的 cJSON 节点，
     * 最终由 cJSON_PrintUnformatted 输出，避免手写转义与输出层叠加出错。 */
    item = cJSON_Parse(quoted);
    free(quoted);
    if (item == NULL) {
        return -EINVAL;
    }
    cJSON_AddItemToObject(parent, key, item);
    return 0;
}

int chat_ai_build_url(const chat_ai_config_t *cfg, char *out, size_t cap)
{
    const char *base;
    size_t len;
    if (cfg == NULL || out == NULL || cap == 0) {
        return -EINVAL;
    }
    base = cfg->base_url;
    while (*base == ' ') {
        base++;
    }
    /* 板端 http_client 支持明文 HTTP 与 https（https 走板端自带的 OpenSSL，
     * 见 core/src/tls_client.c；没有 libssl 时会连接失败并回退离线助手）。 */
    if (strncasecmp(base, "http://", 7) != 0 &&
        strncasecmp(base, "https://", 8) != 0) {
        return -EINVAL;
    }
    len = strlen(base);
    while (len > 0 && (base[len - 1] == '/' || base[len - 1] == ' ')) {
        len--;
    }
    if (len + sizeof("/chat/completions") > cap) {
        return -EINVAL;
    }
    snprintf(out, cap, "%.*s/chat/completions", (int)len, base);
    return 0;
}

static cJSON *build_messages(const chat_ai_config_t *cfg,
                             const chat_message_t *history, size_t count,
                             const char *user_text)
{
    cJSON *messages = cJSON_CreateArray();
    size_t start = 0;
    if (messages == NULL) {
        return NULL;
    }
    if (cfg->system_prompt[0] != '\0') {
        cJSON *sys = cJSON_CreateObject();
        if (sys == NULL || json_append_string(sys, "role", "system") != 0 ||
            json_append_string(sys, "content", cfg->system_prompt) != 0) {
            cJSON_Delete(sys);
            cJSON_Delete(messages);
            return NULL;
        }
        cJSON_AddItemToArray(messages, sys);
    }
    if (count > CHAT_CONTEXT_MESSAGES) {
        start = count - CHAT_CONTEXT_MESSAGES;
    }
    for (size_t i = start; i < count; i++) {
        cJSON *item = cJSON_CreateObject();
        if (item == NULL ||
            json_append_string(item, "role",
                               history[i].role == CHAT_ROLE_USER
                                   ? "user"
                                   : "assistant") != 0 ||
            json_append_string(item, "content",
                               history[i].text != NULL ? history[i].text : "") != 0) {
            cJSON_Delete(item);
            cJSON_Delete(messages);
            return NULL;
        }
        cJSON_AddItemToArray(messages, item);
    }
    {
        /* 最后一条是用户本次输入。 */
        cJSON *item = cJSON_CreateObject();
        if (item == NULL || json_append_string(item, "role", "user") != 0 ||
            json_append_string(item, "content", user_text != NULL ? user_text : "") !=
                0) {
            cJSON_Delete(item);
            cJSON_Delete(messages);
            return NULL;
        }
        cJSON_AddItemToArray(messages, item);
    }
    return messages;
}

int chat_ai_build_request(const chat_ai_config_t *cfg,
                          const chat_message_t *history, size_t count,
                          const char *user_text, char **json_out)
{
    cJSON *root;
    cJSON *messages;
    char *printed;
    int ret = 0;

    if (cfg == NULL || json_out == NULL || user_text == NULL) {
        return -EINVAL;
    }
    *json_out = NULL;
    root = cJSON_CreateObject();
    if (root == NULL) {
        return -ENOMEM;
    }
    messages = build_messages(cfg, history, count, user_text);
    if (messages == NULL) {
        cJSON_Delete(root);
        return -ENOMEM;
    }
    cJSON_AddItemToObject(root, "messages", messages);
    if (json_append_string(root, "model",
                           cfg->model[0] != '\0' ? cfg->model : "qwen2.5:3b") !=
        0) {
        cJSON_Delete(root);
        return -ENOMEM;
    }
    cJSON_AddBoolToObject(root, "stream", 0);
    cJSON_AddNumberToObject(root, "temperature", 0.7);
    cJSON_AddNumberToObject(root, "max_tokens", 512);

    printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (printed == NULL) {
        return -ENOMEM;
    }
    *json_out = printed;
    return ret;
}

/* 取出 detail / error.message 之类的人类可读错误。 */
static void extract_error(const cJSON *root, char *err, size_t err_cap)
{
    const cJSON *detail = cJSON_GetObjectItemCaseSensitive(root, "detail");
    const cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
    const cJSON *msg = NULL;
    if (err == NULL || err_cap == 0) {
        return;
    }
    err[0] = '\0';
    if (cJSON_IsString(detail) && detail->valuestring != NULL) {
        msg = detail;
    } else if (cJSON_IsObject(error)) {
        msg = cJSON_GetObjectItemCaseSensitive(error, "message");
    } else if (cJSON_IsString(error)) {
        msg = error;
    }
    if (msg != NULL && msg->valuestring != NULL) {
        snprintf(err, err_cap, "%s", msg->valuestring);
    }
}

int chat_ai_parse_response(const char *body, size_t len, char **reply_out,
                           char *err, size_t err_cap)
{
    cJSON *root;
    const cJSON *choices;
    const cJSON *message;
    const cJSON *content;
    char *text;

    if (body == NULL || reply_out == NULL) {
        return -EINVAL;
    }
    *reply_out = NULL;
    if (err != NULL && err_cap > 0) {
        err[0] = '\0';
    }
    /* 有些服务端会在末尾补空白；直接按字符串解析即可。 */
    root = cJSON_ParseWithLength(body, len);
    if (root == NULL) {
        if (err != NULL && err_cap > 0) {
            snprintf(err, err_cap, "%s", "响应不是合法 JSON");
        }
        return -EBADMSG;
    }

    extract_error(root, err, err_cap);

    /* OpenAI: choices[0].message.content */
    choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
    if (cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        message = cJSON_GetObjectItemCaseSensitive(
            cJSON_GetArrayItem(choices, 0), "message");
        content = message != NULL
                      ? cJSON_GetObjectItemCaseSensitive(message, "content")
                      : NULL;
        if (cJSON_IsString(content) && content->valuestring != NULL) {
            text = strdup(content->valuestring);
            cJSON_Delete(root);
            if (text == NULL) {
                return -ENOMEM;
            }
            *reply_out = text;
            return 0;
        }
    }
    /* Ollama 原生: message.content */
    message = cJSON_GetObjectItemCaseSensitive(root, "message");
    content = message != NULL ? cJSON_GetObjectItemCaseSensitive(message, "content")
                              : NULL;
    if (cJSON_IsString(content) && content->valuestring != NULL) {
        text = strdup(content->valuestring);
        cJSON_Delete(root);
        if (text == NULL) {
            return -ENOMEM;
        }
        *reply_out = text;
        return 0;
    }

    cJSON_Delete(root);
    if (err != NULL && err[0] == '\0') {
        snprintf(err, err_cap, "%s", "响应缺少 choices[0].message.content");
    }
    return -EBADMSG;
}
