/**
 * @file weather_parse.h
 * @brief 把天气接口的 JSON 解析为 weather_data_t。
 *
 * 当前默认数据源是 wttr.in（worldweatheronline 格式）；同时仍能识别早期
 * itboy 格式，便于切换数据源或读取旧缓存。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef WEATHER_PARSE_H
#define WEATHER_PARSE_H

#include <stddef.h>
#include "weather/weather_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 自动识别接口格式并解析。返回 0 或负错误码。 */
int weather_parse_json(const char *json, size_t len, weather_data_t *out);

/** 同 weather_parse_json，并带上请求时用的城市代码，用于回填中文城市名。 */
int weather_parse_json_for_city(const char *json, size_t len,
                                weather_data_t *out, const char *city_code);

/** 解析 wttr.in / worldweatheronline 格式。 */
int weather_parse_wwonline(const char *json, size_t len, weather_data_t *out,
                           const char *city_code);

/** 解析 itboy 格式（启动时新增的兼容路径）。 */
int weather_parse_itboy(const char *json, size_t len, weather_data_t *out);

#ifdef __cplusplus
}
#endif

#endif /* WEATHER_PARSE_H */
