/**
 * @file board_log.c
 * @brief 精简的线程安全日志器。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "board_core/board_log.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static board_log_level_t g_level = BOARD_LOG_INFO;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

/* 重复日志抑制：同一条消息在抑制窗口内只输出一次。
 * 渲染路径上的重复日志（例如某个字形每次绘制都报一次）会变成同步 write，
 * 在板上足以把一帧拖到几百毫秒。 */
#define LOG_DEDUP_WINDOW_MS 500
static char g_last_msg[768];
static int64_t g_last_ms;
static unsigned g_suppressed;

static int64_t mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void board_log_set_level(board_log_level_t level)
{
    if (level < BOARD_LOG_ERROR) {
        level = BOARD_LOG_ERROR;
    }
    if (level > BOARD_LOG_DEBUG) {
        level = BOARD_LOG_DEBUG;
    }
    g_level = level;
}

board_log_level_t board_log_get_level(void)
{
    return g_level;
}

void board_log(board_log_level_t level, const char *module, const char *fmt, ...)
{
    static const char tags[] = { 'E', 'W', 'I', 'D' };
    struct timespec ts;
    struct tm tm_now;
    char msg[768];
    char stamp[32];
    va_list ap;

    if (level > g_level || fmt == NULL) {
        return;
    }
    if (module == NULL) {
        module = "app";
    }

    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm_now);
    snprintf(stamp, sizeof(stamp), "%02d:%02d:%02d.%03ld",
             tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec, ts.tv_nsec / 1000000L);

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    pthread_mutex_lock(&g_lock);
    {
        int64_t now = mono_ms();
        if (strcmp(msg, g_last_msg) == 0 && now - g_last_ms < LOG_DEDUP_WINDOW_MS) {
            /* 同一条重复消息：窗口内不再输出，只累计次数。 */
            g_suppressed++;
            pthread_mutex_unlock(&g_lock);
            return;
        }
        if (g_suppressed > 0) {
            fprintf(stderr, "[%s] [W] [log] %u duplicate line(s) suppressed\n",
                    stamp, g_suppressed);
            g_suppressed = 0;
        }
        snprintf(g_last_msg, sizeof(g_last_msg), "%s", msg);
        g_last_ms = now;
    }
    fprintf(stderr, "[%s] [%c] [%s] %s\n", stamp,
            tags[(unsigned)level % 4u], module, msg);
    fflush(stderr);
    pthread_mutex_unlock(&g_lock);
}
