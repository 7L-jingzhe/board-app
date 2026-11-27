/**
 * @file board_time.h
 * @brief UI 与缓存使用的本地时间辅助函数。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef BOARD_TIME_H
#define BOARD_TIME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int year;
    int month;   /* 1..12 */
    int day;     /* 1..31 */
    int hour;
    int minute;
    int second;
    int weekday; /* 0=星期日 .. 6=星期六 */
} board_datetime_t;

int board_time_now(board_datetime_t *out);
int board_time_format_hhmm(char *buf, size_t cap);
int board_time_format_date_cn(char *buf, size_t cap);
int board_time_format_ymd(char *buf, size_t cap);
int board_time_weekday_cn(int weekday, char *buf, size_t cap);
int64_t board_time_now_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_TIME_H */
