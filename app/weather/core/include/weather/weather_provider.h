/**
 * @file weather_provider.h
 * @brief 天气接口选择与 URL 构建。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef WEATHER_PROVIDER_H
#define WEATHER_PROVIDER_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 默认 base 地址（wttr.in），或 BOARD_APP_WEATHER_URL（必须以 http:// 开头）。 */
const char *weather_provider_base_url(void);

/** 拼接 <base><城市查询名>?format=j1&lang=zh。返回 0 或负的错误码。 */
int weather_provider_build_url(const char *city_code, char *out, size_t cap);

/** 把 wttr.in 的英文天气短语换成中文简称；未命中返回 false。 */
bool weather_provider_localize(const char *in, char *out, size_t cap);

/** 内置城市代码对应的中文名（未知代码返回 NULL）。 */
const char *weather_provider_city_name(const char *city_code);

/** 16 方位英文缩写（N/NE…）换成中文风向；未知值原样返回。 */
const char *weather_provider_wind_dir_zh(const char *in);

/** 由 pm2.5 估算空气质量等级（优/良/轻度污染…）。 */
const char *weather_provider_quality_from_pm25(int pm25);

#ifdef __cplusplus
}
#endif

#endif /* WEATHER_PROVIDER_H */
