/**
 * @file weather_types.h
 * @brief 解析器、缓存与 UI 共用的天气数据模型。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef WEATHER_TYPES_H
#define WEATHER_TYPES_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WEATHER_MAX_DAYS 7
#define WEATHER_CITY_MAX 32
#define WEATHER_TEXT_MAX 40

typedef struct {
    char ymd[12];
    char week[20];
    char type[WEATHER_TEXT_MAX];
    int high_c;
    int low_c;
    int aqi;
    bool has_aqi;
    char wind_dir[16];
    char wind_level[16];
} weather_day_t;

typedef struct {
    char city[WEATHER_CITY_MAX];
    char city_code[16];
    char updated[32];
    /* 接口返回的 “date”（换算成分钟），用来判断数据是不是今天的新数据。 */
    int updated_day_minutes;
    int temp_c;
    int humidity;
    char condition[WEATHER_TEXT_MAX];
    char quality[16];
    int aqi;
    int pm25;
    char wind_dir[16];
    char wind_level[16];
    int high_c;
    int low_c;
    bool has_humidity;
    bool has_aqi;
    bool has_pm25;
    bool has_wind;
    bool has_high_low;
    weather_day_t days[WEATHER_MAX_DAYS];
    int day_count;
    bool offline;
    bool demo;
    time_t fetched_at;
} weather_data_t;

#ifdef __cplusplus
}
#endif

#endif /* WEATHER_TYPES_H */
