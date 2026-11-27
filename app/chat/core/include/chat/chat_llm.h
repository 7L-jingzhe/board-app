/**
 * @file chat_llm.h
 * @brief 构造 OpenAI 兼容的聊天请求，并解析回答。
 *
 * 纯字符串处理，不联网，方便在主机上做单元测试。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef CHAT_LLM_H
#define CHAT_LLM_H

#include <stddef.h>

#include "chat/chat_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 由 base_url 拼出 /chat/completions 完整地址。返回 0 成功。 */
int chat_ai_build_url(const chat_ai_config_t *cfg, char *out, size_t cap);

/**
 * 生成 JSON 请求体（含 system prompt 与最近若干条历史）。
 * 调用者用 free() 释放返回的缓冲。
 */
int chat_ai_build_request(const chat_ai_config_t *cfg,
                          const chat_message_t *history, size_t count,
                          const char *user_text, char **json_out);

/**
 * 从 OpenAI 兼容响应中取出 choices[0].message.content。
 * 也兼容 Ollama 原生 /api/chat 的 message.content。
 * @return 0 成功（*reply_out 为堆分配字符串），否则负错误码。
 */
int chat_ai_parse_response(const char *body, size_t len, char **reply_out,
                           char *err, size_t err_cap);

#ifdef __cplusplus
}
#endif

#endif /* CHAT_LLM_H */
