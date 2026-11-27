/**
 * @file weather_cities.c
 * @brief 内置城市预设。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "weather/weather_cities.h"

#include <string.h>

static const weather_city_t CITIES[] = {
    { "101010100", "北京" },
    { "101020100", "上海" },
    { "101280101", "广州" },
    { "101280601", "深圳" },
    { "101270101", "成都" },
    { "101210101", "杭州" },
    { "101200101", "武汉" },
    { "101110101", "西安" },
};

const weather_city_t *weather_cities(size_t *count)
{
    if (count != NULL) {
        *count = sizeof(CITIES) / sizeof(CITIES[0]);
    }
    return CITIES;
}

const weather_city_t *weather_city_find(const char *code)
{
    if (code == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(CITIES) / sizeof(CITIES[0]); i++) {
        if (strcmp(CITIES[i].code, code) == 0) {
            return &CITIES[i];
        }
    }
    return NULL;
}
