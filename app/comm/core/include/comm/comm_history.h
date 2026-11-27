/**
 * @file comm_history.h
 * @brief 聊天记录本地持久化（按会话分文件，JSON Lines）。
 *
 * 存在 `<data>/comm_history/<会话>.jsonl`，每行一条：
 *   {"from":"alice","mine":0,"text":"你好","ts":1795673372}
 *
 * 会话名：好友用户名，群聊固定为 `group`。
 * 只保留最近 COMM_HISTORY_MAX 条（超过就重写文件，丢弃最旧的）。
 *
 * 放在 core 层（不依赖 LVGL），UI 只负责调用 append/load。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef COMM_HISTORY_H
#define COMM_HISTORY_H

#include <stdbool.h>
#include <stddef.h>

#include "board_core/board_paths.h"
#include "comm/comm_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COMM_HISTORY_MAX 200      /* 每个会话保留的条数 */
#define COMM_HISTORY_GROUP "group"

/** 一条历史记录。 */
typedef struct {
    char from[COMM_MAX_NAME + 1];
    bool mine;
    long ts;
    char text[COMM_MAX_TEXT + 1];
} comm_history_item_t;

/** 初始化（幂等）：创建 <data>/comm_history 目录（paths 由 UI 层传入）。 */
int comm_history_init(const board_paths_t *paths);

/** 追加一条记录（写失败只记日志，不影响聊天）。 */
int comm_history_append(const char *peer, const char *from, bool mine,
                        const char *text, long ts);

/**
 * 读取某个会话的最近记录（按时间从旧到新）。
 * @param items 输出数组
 * @param max   数组容量
 * @return 实际条数（0 表示没有历史）
 */
int comm_history_load(const char *peer, comm_history_item_t *items, int max);

/** 清空某个会话的记录。 */
int comm_history_clear(const char *peer);

#ifdef __cplusplus
}
#endif

#endif /* COMM_HISTORY_H */
