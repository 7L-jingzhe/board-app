/**
 * @file weather_service.h
 * @brief 异步天气刷新服务。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef WEATHER_SERVICE_H
#define WEATHER_SERVICE_H

#include <stdbool.h>
#include "board_core/board_async_queue.h"
#include "board_core/board_paths.h"
#include "weather/weather_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct weather_service weather_service_t;

typedef enum {
    WEATHER_EVT_OK = 0,
    WEATHER_EVT_NETWORK_ERROR,
    WEATHER_EVT_PARSE_ERROR,
    WEATHER_EVT_CANCELLED,
} weather_event_status_t;

typedef struct {
    weather_event_status_t status;
    weather_data_t data;
    bool from_cache;
    char message[128];
} weather_result_t;

weather_service_t *weather_service_create(const board_paths_t *paths,
                                          board_async_queue_t *queue);
void weather_service_destroy(weather_service_t *svc);

/** 启动一次刷新。若已有请求正在运行，返回 -EAGAIN。 */
int weather_service_refresh(weather_service_t *svc,
                            const char *city_code,
                            const char *city_name);

/** 请求取消并等待工作线程结束（约 <= 1.5 s）。 */
void weather_service_cancel(weather_service_t *svc);

/** 借用指针，指向最近一次成功获取的数据；可能为 NULL。 */
const weather_data_t *weather_service_last(weather_service_t *svc);

#ifdef __cplusplus
}
#endif

#endif /* WEATHER_SERVICE_H */
