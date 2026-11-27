/**
 * @file weather_provider.c
 * @brief 构造天气接口 URL。
 *
 * 默认使用 wttr.in：返回 worldweatheronline 格式的 JSON，支持按城市名查询，
 * 且在国内网络/运营商线路上比部分 CDN 接口更不容易被限流。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "weather/weather_provider.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "board_core/board_error.h"
#include "board_core/board_log.h"
#include "cJSON.h"

#define WRTT_BASE "http://wttr.in/"
#define WRTT_FORMAT "?format=j1&lang=zh"

/* 内置城市代码到 wttr.in 城市名的映射。wttr.in 按地名查询，
 * 这里用拼音反而比中文名更稳定；同时提供中文城市名，用于接口没返回
 * 中文省份时回填 UI。 */
static const struct {
    const char *code;
    const char *query;
    const char *zh_name;
} CITY_QUERY[] = {
    { "101010100", "Beijing", "北京" },
    { "101020100", "Shanghai", "上海" },
    { "101280101", "Guangzhou", "广州" },
    { "101280601", "Shenzhen", "深圳" },
    { "101270101", "Chengdu", "成都" },
    { "101210101", "Hangzhou", "杭州" },
    { "101200101", "Wuhan", "武汉" },
    { "101110101", "Xi'an", "西安" },
};

const char *weather_provider_base_url(void)
{
    const char *env = getenv("BOARD_APP_WEATHER_URL");
    return (env != NULL && env[0] != '\0') ? env : WRTT_BASE;
}

static const char *city_query_for(const char *city_code)
{
    if (city_code == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(CITY_QUERY) / sizeof(CITY_QUERY[0]); i++) {
        if (strcmp(CITY_QUERY[i].code, city_code) == 0) {
            return CITY_QUERY[i].query;
        }
    }
    return NULL;
}

/* 把 wttr.in 的 16 方位英文缩写换成中文风向。 */
static const char *wind_dir_zh(const char *in)
{
    static const struct {
        const char *en;
        const char *zh;
    } DIRS[] = {
        { "N", "北风" },   { "NNE", "东北风" }, { "NE", "东北风" },
        { "ENE", "东北风" }, { "E", "东风" },   { "ESE", "东南风" },
        { "SE", "东南风" }, { "SSE", "东南风" }, { "S", "南风" },
        { "SSW", "西南风" }, { "SW", "西南风" }, { "WSW", "西南风" },
        { "W", "西风" },   { "WNW", "西北风" }, { "NW", "西北风" },
        { "NNW", "西北风" },
    };
    if (in == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(DIRS) / sizeof(DIRS[0]); i++) {
        if (strcasecmp(DIRS[i].en, in) == 0) {
            return DIRS[i].zh;
        }
    }
    return in;
}

const char *weather_provider_wind_dir_zh(const char *in)
{
    return wind_dir_zh(in);
}

/* 由 pm2.5 估算空气质量等级，接口本身没有 AQI 字段。 */
static const char *quality_from_pm25(int pm25)
{
    if (pm25 <= 35) {
        return "优";
    }
    if (pm25 <= 75) {
        return "良";
    }
    if (pm25 <= 115) {
        return "轻度污染";
    }
    if (pm25 <= 150) {
        return "中度污染";
    }
    if (pm25 <= 250) {
        return "重度污染";
    }
    return "严重污染";
}

const char *weather_provider_quality_from_pm25(int pm25)
{
    return quality_from_pm25(pm25);
}

const char *weather_provider_city_name(const char *city_code)
{
    if (city_code == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(CITY_QUERY) / sizeof(CITY_QUERY[0]); i++) {
        if (strcmp(CITY_QUERY[i].code, city_code) == 0) {
            return CITY_QUERY[i].zh_name;
        }
    }
    return NULL;
}

/* URL 中只允许字母、数字和少量安全字符，其余按 %XX 转义。 */
static int encode_query(const char *in, char *out, size_t cap)
{
    size_t pos = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p != '\0'; p++) {
        if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~') {
            if (pos + 1 >= cap) {
                return -ENAMETOOLONG;
            }
            out[pos++] = (char)*p;
        } else {
            if (pos + 3 >= cap) {
                return -ENAMETOOLONG;
            }
            snprintf(out + pos, 4, "%%%02X", *p);
            pos += 3;
        }
    }
    out[pos] = '\0';
    return 0;
}

int weather_provider_build_url(const char *city_code, char *out, size_t cap)
{
    const char *base;
    const char *query;
    char encoded[64];
    size_t base_len;
    int n;

    if (city_code == NULL || out == NULL || cap == 0) {
        return -EINVAL;
    }
    for (const char *p = city_code; *p != '\0'; p++) {
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-') {
            return -EINVAL;
        }
    }
    base = weather_provider_base_url();
    if (strncmp(base, "http://", 7) != 0) {
        return -EINVAL;
    }
    /* 城市代码优先查内置映射；未知代码直接当作城市名查询。 */
    query = city_query_for(city_code);
    if (query == NULL) {
        query = city_code;
    }
    if (encode_query(query, encoded, sizeof(encoded)) != 0) {
        return -EINVAL;
    }
    base_len = strlen(base);
    n = snprintf(out, cap, "%s%s%s%s",
                 base, (base_len > 0 && base[base_len - 1] == '/') ? "" : "/",
                 encoded, WRTT_FORMAT);
    if (n < 0 || (size_t)n >= cap) {
        return -ENAMETOOLONG;
    }
    return 0;
}

/* wttr.in 的 "lang=zh" 会把 weatherDesc 也变成中文；解析前把它还原成
 * 国内常见的天气短语，避免 UI 出现长句。返回 false 表示无需替换。 */
bool weather_provider_localize(const char *in, char *out, size_t cap)
{
    static const struct {
        const char *en;
        const char *zh;
    } MAP[] = {
        { "Sunny", "晴" },
        { "Clear", "晴" },
        { "Partly cloudy", "多云" },
        { "Cloudy", "阴" },
        { "Overcast", "阴" },
        { "Mist", "雾" },
        { "Fog", "雾" },
        { "Freezing fog", "冻雾" },
        { "Haze", "霾" },
        { "Hazy", "霾" },
        { "Smoky haze", "霾" },
        { "Smoke", "霾" },
        { "Smoky", "霾" },
        { "Dust", "浮尘" },
        { "Sand", "沙尘" },
        { "Duststorm", "沙尘暴" },
        { "Windy", "大风" },
        { "Breezy", "微风" },
        { "Cold", "寒冷" },
        { "Hot", "炎热" },
        { "Patchy rain nearby", "小雨" },
        { "Patchy rain possible", "小雨" },
        { "Light rain", "小雨" },
        { "Light rain shower", "阵雨" },
        { "Patchy light drizzle", "毛毛雨" },
        { "Light drizzle", "毛毛雨" },
        { "Moderate rain", "中雨" },
        { "Moderate rain at times", "中雨" },
        { "Heavy rain", "大雨" },
        { "Heavy rain at times", "大雨" },
        { "Moderate or heavy rain shower", "雷阵雨" },
        { "Patchy snow nearby", "小雪" },
        { "Light snow", "小雪" },
        { "Patchy light snow", "小雪" },
        { "Moderate snow", "中雪" },
        { "Heavy snow", "大雪" },
        { "Blizzard", "暴雪" },
        { "Patchy sleet nearby", "雨夹雪" },
        { "Light sleet", "雨夹雪" },
        { "Thundery outbreaks possible", "雷阵雨" },
        { "Patchy light rain with thunder", "雷阵雨" },
        { "Moderate or heavy rain with thunder", "雷阵雨" },
        { "Patchy light rain", "小雨" },
        { "Patchy light rain in area with thunder", "雷阵雨" },
    };
    if (in == NULL || out == NULL || cap == 0) {
        return false;
    }
    for (size_t i = 0; i < sizeof(MAP) / sizeof(MAP[0]); i++) {
        if (strcasecmp(MAP[i].en, in) == 0) {
            snprintf(out, cap, "%s", MAP[i].zh);
            return true;
        }
    }
    return false;
}
