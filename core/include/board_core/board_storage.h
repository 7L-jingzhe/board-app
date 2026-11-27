/**
 * @file board_storage.h
 * @brief 线程安全键值存储，以 key=value 行的形式持久化。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef BOARD_STORAGE_H
#define BOARD_STORAGE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int board_storage_init(const char *settings_path);
void board_storage_shutdown(void);

/* 返回的指针指向每个线程共享的缓冲区。再次调用 board_storage_get_str()
 * 之前请先复制该字符串。 */
const char *board_storage_get_str(const char *key, const char *default_value);
int board_storage_set_str(const char *key, const char *value);
int board_storage_get_int(const char *key, int default_value);
int board_storage_set_int(const char *key, int value);
int board_storage_save(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_STORAGE_H */
