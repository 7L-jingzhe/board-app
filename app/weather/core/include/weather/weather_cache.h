/**
 * @file weather_cache.h
 * @brief 原始 JSON 天气缓存，位于 data/cache/。
 *
 * 除按城市保存原始响应外，还记录每天观测到的逐日预报，
 * 这样数据源只返回 3 天时也能把 7 天列表逐步补齐。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef WEATHER_CACHE_H
#define WEATHER_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "board_core/board_paths.h"
#include "weather/weather_types.h"

#ifdef __cplusplus
extern "C" {
#endif

int weather_cache_load(const board_paths_t *paths, const char *city_code,
                       weather_data_t *out);
int weather_cache_age(const board_paths_t *paths, const char *city_code,
                      int64_t *age_seconds);
/** 缓存里的接口日期比本地今天还新（板子时钟被回拨）时返回 true。 */
bool weather_cache_day_is_ahead(const board_paths_t *paths,
                                const char *city_code);
int weather_cache_save(const board_paths_t *paths, const char *city_code,
                       const char *raw_json, size_t len);
int weather_cache_clear(const board_paths_t *paths, const char *city_code);

/**
 * 把本次拿到的逐日预报记进“历史日历”，再用历史补齐 live 中缺失的日期。
 * @note live 的 days 必须已按日期升序排列。
 */
int weather_cache_merge_days(const board_paths_t *paths, const char *city_code,
                             weather_data_t *live);

#ifdef __cplusplus
}
#endif

#endif /* WEATHER_CACHE_H */
