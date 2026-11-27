/**
 * @file weather_mock.c
 * @brief 确定性演示天气。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "weather/weather_mock.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

void weather_mock_fill(weather_data_t *out)
{
    static const struct {
        const char *week;
        const char *type;
        int high;
        int low;
    } days[WEATHER_MAX_DAYS] = {
        { "今天", "晴", 7, -2 },
        { "明天", "多云", 12, 1 },
        { "周三", "多云", 11, 4 },
        { "周四", "晴", 10, 1 },
        { "周五", "多云", 9, 0 },
        { "周六", "小雨", 11, 2 },
        { "周日", "多云", 12, 3 },
    };

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    snprintf(out->city, sizeof(out->city), "%s", "北京");
    snprintf(out->city_code, sizeof(out->city_code), "%s", "101010100");
    snprintf(out->updated, sizeof(out->updated), "%s", "演示数据");
    out->temp_c = 6;
    out->humidity = 45;
    out->has_humidity = true;
    snprintf(out->condition, sizeof(out->condition), "%s", "晴");
    snprintf(out->quality, sizeof(out->quality), "%s", "优");
    out->aqi = 42;
    out->has_aqi = true;
    out->pm25 = 18;
    out->has_pm25 = true;
    snprintf(out->wind_dir, sizeof(out->wind_dir), "%s", "东南风");
    snprintf(out->wind_level, sizeof(out->wind_level), "%s", "1级");
    out->has_wind = true;
    out->high_c = 7;
    out->low_c = -2;
    out->has_high_low = true;
    out->day_count = WEATHER_MAX_DAYS;
    for (int i = 0; i < WEATHER_MAX_DAYS; i++) {
        snprintf(out->days[i].week, sizeof(out->days[i].week), "%s", days[i].week);
        snprintf(out->days[i].type, sizeof(out->days[i].type), "%s", days[i].type);
        out->days[i].high_c = days[i].high;
        out->days[i].low_c = days[i].low;
        out->days[i].aqi = 42 + i;
        out->days[i].has_aqi = true;
        snprintf(out->days[i].wind_dir, sizeof(out->days[i].wind_dir), "%s", "东南风");
        snprintf(out->days[i].wind_level, sizeof(out->days[i].wind_level), "%s", "1级");
    }
    out->offline = true;
    out->demo = true;
    out->fetched_at = time(NULL);
}
