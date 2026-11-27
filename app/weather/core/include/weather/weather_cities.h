/**
 * @file weather_cities.h
 * @brief 内置城市预设。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef WEATHER_CITIES_H
#define WEATHER_CITIES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *code;
    const char *name;
} weather_city_t;

const weather_city_t *weather_cities(size_t *count);
const weather_city_t *weather_city_find(const char *code);

#ifdef __cplusplus
}
#endif

#endif /* WEATHER_CITIES_H */
