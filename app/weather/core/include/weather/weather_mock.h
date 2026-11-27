/**
 * @file weather_mock.h
 * @brief 离线且无缓存时使用的确定性演示天气。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef WEATHER_MOCK_H
#define WEATHER_MOCK_H

#include "weather/weather_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void weather_mock_fill(weather_data_t *out);

#ifdef __cplusplus
}
#endif

#endif /* WEATHER_MOCK_H */
