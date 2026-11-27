/**
 * @file board_time.c
 * @brief 本地时间辅助函数。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "board_core/board_time.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *const WEEKDAYS_CN[7] = {
    "星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"
};

int board_time_now(board_datetime_t *out)
{
    time_t now;
    struct tm tm_now;

    if (out == NULL) {
        return -1;
    }
    now = time(NULL);
    if (localtime_r(&now, &tm_now) == NULL) {
        return -1;
    }
    out->year = tm_now.tm_year + 1900;
    out->month = tm_now.tm_mon + 1;
    out->day = tm_now.tm_mday;
    out->hour = tm_now.tm_hour;
    out->minute = tm_now.tm_min;
    out->second = tm_now.tm_sec;
    out->weekday = tm_now.tm_wday;
    return 0;
}

int board_time_format_hhmm(char *buf, size_t cap)
{
    board_datetime_t dt;
    if (buf == NULL || cap == 0 || board_time_now(&dt) != 0) {
        return -1;
    }
    snprintf(buf, cap, "%02d:%02d", dt.hour, dt.minute);
    return 0;
}

int board_time_format_date_cn(char *buf, size_t cap)
{
    board_datetime_t dt;
    if (buf == NULL || cap == 0 || board_time_now(&dt) != 0) {
        return -1;
    }
    snprintf(buf, cap, "%d月%d日 %s", dt.month, dt.day,
             WEEKDAYS_CN[dt.weekday % 7]);
    return 0;
}

int board_time_format_ymd(char *buf, size_t cap)
{
    board_datetime_t dt;
    if (buf == NULL || cap == 0 || board_time_now(&dt) != 0) {
        return -1;
    }
    snprintf(buf, cap, "%04d-%02d-%02d", dt.year, dt.month, dt.day);
    return 0;
}

int board_time_weekday_cn(int weekday, char *buf, size_t cap)
{
    if (buf == NULL || cap == 0) {
        return -1;
    }
    snprintf(buf, cap, "%s", WEEKDAYS_CN[(weekday % 7 + 7) % 7]);
    return 0;
}

int64_t board_time_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000LL + (int64_t)ts.tv_nsec / 1000000LL;
}
