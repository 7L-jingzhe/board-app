/**
 * @file chat_offline.h
 * @brief 无网络时的确定性问答助手（规则 + 关键词 + 简单计算）。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef CHAT_OFFLINE_H
#define CHAT_OFFLINE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 生成一条离线回复。
 * @param input 用户输入（UTF-8）
 * @param out   输出缓冲，调用者提供
 * @param cap   输出缓冲大小
 * @return 写入的字节数（不含结尾 NUL）。
 */
size_t chat_offline_reply(const char *input, char *out, size_t cap);

/** 是否为可以帮助（问候/介绍）的短句；UI 用来决定欢迎语。 */
int chat_offline_is_greeting(const char *input);

#ifdef __cplusplus
}
#endif

#endif /* CHAT_OFFLINE_H */
