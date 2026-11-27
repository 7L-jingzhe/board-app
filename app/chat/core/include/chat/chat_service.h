/**
 * @file chat_service.h
 * @brief AI 聊天请求：可在后台线程调用大模型，失败自动回退离线助手。
 *
 * 线程模型与天气服务一致：
 *   UI 线程 chat_service_ask() -> worker 线程 HTTP/解析 -> board_async_queue
 *   -> UI 线程的 ui_async_pump 消费 chat_reply_t 并 free。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef CHAT_SERVICE_H
#define CHAT_SERVICE_H

#include <stddef.h>

#include "board_core/board_async_queue.h"
#include <stdbool.h>

#include "chat/chat_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct chat_service chat_service_t;

/** 读取环境变量/设置项得到当前 AI 配置（enabled=false 表示走离线助手）。 */
void chat_service_load_config(chat_ai_config_t *cfg);

/* ---- API key 的本地持久化（data/settings.conf）----
 * 优先级：环境变量 BOARD_APP_AI_KEY > 本地保存的 key。
 * 用 tools/ai_key 写入，避免每次启动都带环境变量（也避免出现在 ps 里）。 */

/** 保存 key（同时补上默认 url/model）。返回 0 成功。 */
int chat_ai_save_key(const char *key);
/** 保存自定义 url/model（可选）。 */
int chat_ai_save_endpoint(const char *url, const char *model);
/** 清除本地 key。 */
int chat_ai_clear_key(void);
/** 本地是否已保存 key。 */
bool chat_ai_has_stored_key(void);

chat_service_t *chat_service_create(board_async_queue_t *queue);
void chat_service_destroy(chat_service_t *svc);

/**
 * 异步发起一轮对话。
 * @param cfg      AI 配置；未启用或失败时回退离线助手。
 * @param history  历史消息（可为 NULL）
 * @param count    历史条数
 * @param user     本次用户输入
 * @param seq      请求序号，原样带回 chat_reply_t
 * @return 0 已发起；-EAGAIN 上一次请求仍在进行；其他负值为错误。
 */
int chat_service_ask(chat_service_t *svc, const chat_ai_config_t *cfg,
                     const chat_message_t *history, size_t count,
                     const char *user, uint32_t seq);

/** 取消并等待工作线程退出（页面销毁时调用）。 */
void chat_service_cancel(chat_service_t *svc);

/** 是否正在等待回复。 */
int chat_service_busy(chat_service_t *svc);

#ifdef __cplusplus
}
#endif

#endif /* CHAT_SERVICE_H */
