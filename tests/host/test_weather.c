/**
 * @file test_weather.c
 * @brief 针对天气 JSON 解析器的主机端测试。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "test_common.h"

#include <errno.h>
#include <string.h>

#include "weather/weather_parse.h"

static const char *SAMPLE =
    "{"
    "\"time\":\"2026-11-25 08:35:17\","
    "\"cityInfo\":{\"city\":\"北京市\",\"citykey\":\"101010100\"},"
    "\"data\":{"
    "\"shidu\":\"59%\",\"pm25\":23.0,\"quality\":\"优\",\"wendu\":\"-6\","
    "\"forecast\":["
    "{\"ymd\":\"2026-11-25\",\"week\":\"星期三\",\"high\":\"高温 9℃\","
    "\"low\":\"低温 -4℃\",\"aqi\":53,\"fx\":\"东南风\",\"fl\":\"1级\",\"type\":\"晴\"},"
    "{\"ymd\":\"2026-11-26\",\"week\":\"星期四\",\"high\":\"高温 12℃\","
    "\"low\":\"低温 1℃\",\"aqi\":73,\"fx\":\"西北风\",\"fl\":\"1级\",\"type\":\"多云\"},"
    "{\"ymd\":\"2026-11-27\",\"week\":\"星期五\",\"high\":\"高温 11℃\","
    "\"low\":\"低温 4℃\",\"aqi\":36,\"fx\":\"西北风\",\"fl\":\"2级\",\"type\":\"多云\"}"
    "]}}";

void test_weather_parse(void)
{
    weather_data_t w;
    int ret;

    ret = weather_parse_json(SAMPLE, strlen(SAMPLE), &w);
    CHECK_EQ_INT(ret, 0);
    CHECK(strcmp(w.city, "北京市") == 0);
    CHECK(strcmp(w.city_code, "101010100") == 0);
    CHECK_EQ_INT(w.temp_c, -6);
    CHECK_EQ_INT(w.humidity, 59);
    CHECK(w.has_humidity);
    CHECK_EQ_INT(w.pm25, 23);
    CHECK(w.has_pm25);
    CHECK(strcmp(w.quality, "优") == 0);
    CHECK(strcmp(w.condition, "晴") == 0);
    CHECK_EQ_INT(w.high_c, 9);
    CHECK_EQ_INT(w.low_c, -4);
    CHECK(w.has_high_low);
    CHECK_EQ_INT(w.aqi, 53);
    CHECK(w.has_aqi);
    CHECK_EQ_INT(w.day_count, 3);
    CHECK(strcmp(w.days[0].week, "星期三") == 0);
    CHECK_EQ_INT(w.days[1].high_c, 12);
    CHECK_EQ_INT(w.days[2].low_c, 4);
    CHECK(!w.demo);
    CHECK(!w.offline);

    ret = weather_parse_json("{}", 2, &w);
    CHECK_EQ_INT(ret, -EBADMSG);
    ret = weather_parse_json("not json", 8, &w);
    CHECK_EQ_INT(ret, -EBADMSG);
    ret = weather_parse_json(NULL, 0, &w);
    CHECK_EQ_INT(ret, -EINVAL);
}
