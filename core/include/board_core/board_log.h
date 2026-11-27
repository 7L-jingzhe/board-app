/**
 * @file board_log.h
 * @brief 精简的线程安全日志器。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef BOARD_LOG_H
#define BOARD_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOARD_LOG_ERROR = 0,
    BOARD_LOG_WARN  = 1,
    BOARD_LOG_INFO  = 2,
    BOARD_LOG_DEBUG = 3,
} board_log_level_t;

void board_log_set_level(board_log_level_t level);
board_log_level_t board_log_get_level(void);
void board_log(board_log_level_t level, const char *module, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

#define BOARD_LOGE(mod, ...) board_log(BOARD_LOG_ERROR, (mod), __VA_ARGS__)
#define BOARD_LOGW(mod, ...) board_log(BOARD_LOG_WARN,  (mod), __VA_ARGS__)
#define BOARD_LOGI(mod, ...) board_log(BOARD_LOG_INFO,  (mod), __VA_ARGS__)
#define BOARD_LOGD(mod, ...) board_log(BOARD_LOG_DEBUG, (mod), __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* BOARD_LOG_H */
