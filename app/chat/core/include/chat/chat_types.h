/**
 * @file chat_types.h
 * @brief AI 聊天页的数据模型与配置。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef CHAT_TYPES_H
#define CHAT_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CHAT_MSG_MAX 4096        /* 单条消息（含结尾）最大字节数 */
#define CHAT_HISTORY_MAX 240     /* 上下文条数上限 */
#define CHAT_CONTEXT_MESSAGES 8  /* 发给大模型的最近消息条数 */

typedef enum {
    CHAT_ROLE_USER = 0,
    CHAT_ROLE_ASSISTANT,
} chat_role_t;

typedef struct {
    chat_role_t role;
    char *text;                  /* 调用者持有 */
} chat_message_t;

/** 一条大模型配置：OpenAI 兼容的 /chat/completions 接口。 */
typedef struct {
    bool enabled;
    char base_url[512];          /* 例如 http://192.168.1.10:11434/v1 */
    char model[96];              /* 例如 qwen2.5:3b */
    char api_key[160];           /* 可空 */
    char system_prompt[512];
} chat_ai_config_t;

/** 回复来源，用于在 UI 上标注。 */
typedef enum {
    CHAT_AI_SOURCE_LLM = 0,      /* 在线大模型 */
    CHAT_AI_SOURCE_OFFLINE = 1,  /* 离线助手 */
} chat_ai_source_t;

/** 结果消息：worker 分配、UI 线程消费并 free。 */
typedef struct {
    uint32_t seq;                /* 请求序号，用于丢弃过期结果 */
    char *reply;                 /* NUL 结尾的完整回复 */
    chat_ai_source_t source;
    char note[128];              /* 给用户看的兜底说明，可空 */
} chat_reply_t;

#ifdef __cplusplus
}
#endif

#endif /* CHAT_TYPES_H */
