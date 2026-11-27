/**
 * @file weather_parse.c
 * @brief 把天气接口 JSON 解析为 weather_data_t。
 *
 * 当前默认数据源是 wttr.in（worldweatheronline 格式），同时保留 itboy
 * 格式解析用于兼容旧缓存。weather_parse_json() 会按关键字自动选择。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "weather/weather_parse.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "board_core/board_time.h"
#include "cJSON.h"
#include "weather/weather_provider.h"

static void copy_str(char *dst, size_t cap, const char *src)
{
    if (dst == NULL || cap == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, cap, "%s", src);
}

static const char *item_str(const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

/* 把接口的 “date” 字段（标准接口为 yyyymmdd，部分镜像为 yyymmdd）换算成
 * “自公元 0 年起的分钟数”，便于和本地时间比较先后。返回 -1 表示无法解析。 */
static int parse_day_minutes(const char *s)
{
    struct tm tm_value;
    char year_buf[5];
    int y;
    int m;
    int d;
    size_t len;

    if (s == NULL) {
        return -1;
    }
    len = strlen(s);
    if (len < 8) {
        return -1;
    }
    memcpy(year_buf, s, 4);
    year_buf[4] = '\0';
    if (sscanf(year_buf, "%d", &y) != 1) {
        return -1;
    }
    if (y < 1900) { /* 形如 20261125 的前四位不是年份时，按 yyymmdd 处理 */
        char short_buf[3];
        memcpy(short_buf, s, 2);
        short_buf[2] = '\0';
        if (sscanf(short_buf, "%d", &y) != 1) {
            return -1;
        }
        y += 2000;
    }
    if (sscanf(s + len - 4, "%2d%2d", &m, &d) != 2) {
        return -1;
    }
    if (m < 1 || m > 12 || d < 1 || d > 31) {
        return -1;
    }
    memset(&tm_value, 0, sizeof(tm_value));
    tm_value.tm_year = y - 1900;
    tm_value.tm_mon = m - 1;
    tm_value.tm_mday = d;
    tm_value.tm_isdst = -1;
    return (int)(mktime(&tm_value) / 60);
}

static int parse_temp(const char *s, int *out)
{
    const char *p;
    char *end = NULL;
    long v;

    /* 该 API 返回形如 "高温 9℃" / "低温 -4℃" 的字符串。这里查找第一个
     * 带符号整数，而不是假定前缀固定。 */
    if (s == NULL || out == NULL) {
        return -1;
    }
    p = s;
    while (*p != '\0' && *p != '-' && !isdigit((unsigned char)*p)) {
        p++;
    }
    if (*p == '\0') {
        return -1;
    }
    errno = 0;
    v = strtol(p, &end, 10);
    if (errno != 0 || end == p) {
        return -1;
    }
    *out = (int)v;
    return 0;
}

static int parse_percent(const char *s, int *out)
{
    const char *p;
    char *end = NULL;
    long v;

    if (s == NULL || out == NULL) {
        return -1;
    }
    p = s;
    while (*p != '\0' && !isdigit((unsigned char)*p)) {
        p++;
    }
    if (*p == '\0') {
        return -1;
    }
    errno = 0;
    v = strtol(p, &end, 10);
    if (errno != 0 || end == p) {
        return -1;
    }
    *out = (int)v;
    return 0;
}

static int parse_number(const cJSON *obj, const char *key, int *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(item)) {
        return -1;
    }
    *out = (int)(item->valuedouble + (item->valuedouble >= 0 ? 0.5 : -0.5));
    return 0;
}

/* wttr.in 的数值字段大多是字符串（"17"），少数是数字，这里两者都接受。 */
static int wttr_number(const cJSON *obj, const char *key, int *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (item == NULL) {
        return -1;
    }
    if (cJSON_IsNumber(item)) {
        *out = (int)(item->valuedouble + (item->valuedouble >= 0 ? 0.5 : -0.5));
        return 0;
    }
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        char *end = NULL;
        long v = strtol(item->valuestring, &end, 10);
        if (end == item->valuestring) {
            return -1;
        }
        *out = (int)v;
        return 0;
    }
    return -1;
}

/* ["value"] 形式的字段：weatherDesc / value / areaName 都是这种结构。 */
static const char *wttr_value(const cJSON *obj, const char *key)
{
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(obj, key);
    const cJSON *first;
    if (!cJSON_IsArray(arr)) {
        return NULL;
    }
    first = cJSON_GetArrayItem(arr, 0);
    if (!cJSON_IsObject(first)) {
        return NULL;
    }
    return item_str(first, "value");
}

/* 把 "2026-11-25" 换成 "20261125"，并在无法识别星期时由日期推算。 */
static int wttr_day_minutes(const char *ymd)
{
    int y = 0;
    int m = 0;
    int d = 0;
    struct tm tm_value;

    if (ymd == NULL || sscanf(ymd, "%d-%d-%d", &y, &m, &d) != 3) {
        return -1;
    }
    if (y < 1970 || m < 1 || m > 12 || d < 1 || d > 31) {
        return -1;
    }
    memset(&tm_value, 0, sizeof(tm_value));
    tm_value.tm_year = y - 1900;
    tm_value.tm_mon = m - 1;
    tm_value.tm_mday = d;
    tm_value.tm_isdst = -1;
    return (int)(mktime(&tm_value) / 60);
}

static void wttr_condition(const cJSON *cond, char *out, size_t cap)
{
    const char *desc = wttr_value(cond, "weatherDesc");
    char lower[WEATHER_TEXT_MAX];

    if (desc == NULL) {
        copy_str(out, cap, "未知");
        return;
    }
    copy_str(lower, sizeof(lower), desc);
    if (!weather_provider_localize(lower, out, cap)) {
        copy_str(out, cap, desc);
    }
}

/* wttr.in 的每小时数据里选一个“当前时段”：取最接近现在的小时项。 */
static const cJSON *wttr_pick_hour(const cJSON *day)
{
    const cJSON *hourly = cJSON_GetObjectItemCaseSensitive(day, "hourly");
    const cJSON *item;
    const cJSON *best = NULL;
    time_t now;
    struct tm local;
    int now_hour;
    int best_diff = 1 << 30;

    if (!cJSON_IsArray(hourly)) {
        return NULL;
    }
    now = time(NULL);
    if (localtime_r(&now, &local) == NULL) {
        return cJSON_GetArrayItem(hourly, 0);
    }
    now_hour = local.tm_hour;
    cJSON_ArrayForEach(item, hourly) {
        int h;
        int diff;
        if (!cJSON_IsObject(item)) {
            continue;
        }
        if (wttr_number(item, "time", &h) != 0) {
            continue;
        }
        /* time 是 "0"、"300"、"600"… 形式的 3 小时档次。 */
        diff = abs(h / 100 - now_hour);
        if (diff < best_diff) {
            best_diff = diff;
            best = item;
        }
    }
    return best != NULL ? best : cJSON_GetArrayItem(hourly, 0);
}

int weather_parse_wwonline(const char *json, size_t len, weather_data_t *out,
                           const char *city_code)
{
    cJSON *root;
    const cJSON *current;
    const cJSON *nearest;
    const cJSON *weather;
    const cJSON *item;
    const char *s;
    int v;
    int n = 0;

    if (json == NULL || out == NULL || len == 0) {
        return -EINVAL;
    }
    memset(out, 0, sizeof(*out));
    out->updated_day_minutes = -1;
    root = cJSON_ParseWithLength(json, len);
    if (root == NULL) {
        return -EBADMSG;
    }
    current = cJSON_GetObjectItemCaseSensitive(root, "current_condition");
    weather = cJSON_GetObjectItemCaseSensitive(root, "weather");
    if (!cJSON_IsArray(current) || !cJSON_IsArray(weather)) {
        cJSON_Delete(root);
        return -EBADMSG;
    }
    current = cJSON_GetArrayItem(current, 0);
    if (!cJSON_IsObject(current)) {
        cJSON_Delete(root);
        return -EBADMSG;
    }

    /* 城市名：wttr.in 返回的 region/areaName 是英文地名，内置城市统一
     * 用中文名；未知城市才退回接口返回的英文地名。 */
    if (city_code != NULL && city_code[0] != '\0') {
        s = weather_provider_city_name(city_code);
        if (s != NULL) {
            copy_str(out->city, sizeof(out->city), s);
        }
    }
    if (out->city[0] == '\0') {
        nearest = cJSON_GetArrayItem(
            cJSON_GetObjectItemCaseSensitive(root, "nearest_area"), 0);
        s = wttr_value(nearest, "region");
        if (s == NULL || s[0] == '\0') {
            s = wttr_value(nearest, "areaName");
        }
        if (s != NULL && s[0] != '\0') {
            copy_str(out->city, sizeof(out->city), s);
        }
    }

    if (wttr_number(current, "temp_C", &v) == 0) {
        out->temp_c = v;
    }
    if (wttr_number(current, "humidity", &v) == 0) {
        out->humidity = v;
        out->has_humidity = true;
    }
    /* 空气质量：wttr.in 的免费 JSON 里没有 PM2.5/AQI 字段，这时不显示
     * 空气质量（UI 显示 “--”），避免把未知值当作优。 */
    if (wttr_number(current, "pm2_5", &v) == 0) {
        out->pm25 = v;
        out->has_pm25 = true;
        out->aqi = v;
        out->has_aqi = true;
        snprintf(out->quality, sizeof(out->quality), "%s",
                 weather_provider_quality_from_pm25(v));
    }
    wttr_condition(current, out->condition, sizeof(out->condition));

    s = item_str(current, "winddir16Point");
    if (s != NULL) {
        copy_str(out->wind_dir, sizeof(out->wind_dir),
                 weather_provider_wind_dir_zh(s));
    }
    if (wttr_number(current, "windspeedKmph", &v) == 0) {
        snprintf(out->wind_level, sizeof(out->wind_level), "%d km/h", v);
        out->has_wind = true;
    }

    /* 未来 N 天的逐日预报。 */
    cJSON_ArrayForEach(item, weather) {
        weather_day_t *day;
        const cJSON *hour;
        const char *ymd;
        int minutes;

        if (n >= WEATHER_MAX_DAYS) {
            break;
        }
        if (!cJSON_IsObject(item)) {
            continue;
        }
        day = &out->days[n];
        memset(day, 0, sizeof(*day));
        ymd = item_str(item, "date");
        if (ymd != NULL) {
            snprintf(day->ymd, sizeof(day->ymd), "%s", ymd);
            minutes = wttr_day_minutes(ymd);
            if (minutes >= 0) {
                struct tm tm_day;
                time_t t = (time_t)minutes * 60;
                if (localtime_r(&t, &tm_day) != NULL) {
                    char week[20];
                    if (board_time_weekday_cn(tm_day.tm_wday, week,
                                              sizeof(week)) == 0) {
                        copy_str(day->week, sizeof(day->week), week);
                    }
                }
                if (n == 0) {
                    out->updated_day_minutes = minutes;
                }
            }
        }
        if (wttr_number(item, "maxtempC", &v) == 0) {
            day->high_c = v;
        }
        if (wttr_number(item, "mintempC", &v) == 0) {
            day->low_c = v;
        }
        hour = wttr_pick_hour(item);
        if (hour != NULL) {
            wttr_condition(hour, day->type, sizeof(day->type));
        }
        if (day->type[0] == '\0') {
            copy_str(day->type, sizeof(day->type), out->condition);
        }
        n++;
    }
    if (n == 0) {
        cJSON_Delete(root);
        return -EBADMSG;
    }
    out->day_count = n;
    out->high_c = out->days[0].high_c;
    out->low_c = out->days[0].low_c;
    out->has_high_low = true;

    s = item_str(current, "observation_time");
    if (s != NULL) {
        copy_str(out->updated, sizeof(out->updated), s);
    }
    out->fetched_at = time(NULL);
    out->offline = false;
    out->demo = false;
    cJSON_Delete(root);
    return 0;
}

int weather_parse_json(const char *json, size_t len, weather_data_t *out)
{
    return weather_parse_json_for_city(json, len, out, NULL);
}

int weather_parse_json_for_city(const char *json, size_t len,
                                weather_data_t *out, const char *city_code)
{
    if (json == NULL || out == NULL || len == 0) {
        return -EINVAL;
    }
    /* 两种格式的顶层字段完全不同，按关键字选择解析器。 */
    if (strstr(json, "current_condition") != NULL) {
        return weather_parse_wwonline(json, len, out, city_code);
    }
    return weather_parse_itboy(json, len, out);
}

static int parse_day(const cJSON *day_obj, weather_day_t *day)
{
    const char *s;
    int v;

    memset(day, 0, sizeof(*day));
    s = item_str(day_obj, "ymd");
    if (s != NULL) {
        copy_str(day->ymd, sizeof(day->ymd), s);
    }
    s = item_str(day_obj, "week");
    if (s != NULL) {
        copy_str(day->week, sizeof(day->week), s);
    }
    s = item_str(day_obj, "type");
    if (s != NULL) {
        copy_str(day->type, sizeof(day->type), s);
    }
    s = item_str(day_obj, "high");
    if (parse_temp(s, &v) == 0) {
        day->high_c = v;
    }
    s = item_str(day_obj, "low");
    if (parse_temp(s, &v) == 0) {
        day->low_c = v;
    }
    if (parse_number(day_obj, "aqi", &v) == 0) {
        day->aqi = v;
        day->has_aqi = true;
    }
    s = item_str(day_obj, "fx");
    if (s != NULL) {
        copy_str(day->wind_dir, sizeof(day->wind_dir), s);
    }
    s = item_str(day_obj, "fl");
    if (s != NULL) {
        copy_str(day->wind_level, sizeof(day->wind_level), s);
    }
    return 0;
}

int weather_parse_itboy(const char *json, size_t len, weather_data_t *out)
{
    cJSON *root = NULL;
    const cJSON *city_info;
    const cJSON *data;
    const cJSON *forecast;
    const cJSON *first;
    const cJSON *item;
    const char *s;
    int temp;
    int n = 0;

    if (json == NULL || out == NULL || len == 0) {
        return -EINVAL;
    }
    memset(out, 0, sizeof(*out));
    root = cJSON_ParseWithLength(json, len);
    if (root == NULL) {
        return -EBADMSG;
    }
    city_info = cJSON_GetObjectItemCaseSensitive(root, "cityInfo");
    data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsObject(city_info) || !cJSON_IsObject(data)) {
        cJSON_Delete(root);
        return -EBADMSG;
    }
    s = item_str(city_info, "city");
    if (s == NULL || s[0] == '\0') {
        cJSON_Delete(root);
        return -EBADMSG;
    }
    copy_str(out->city, sizeof(out->city), s);
    s = item_str(city_info, "citykey");
    if (s != NULL) {
        copy_str(out->city_code, sizeof(out->city_code), s);
    }
    s = item_str(root, "time");
    if (s == NULL) {
        s = item_str(city_info, "updateTime");
    }
    if (s != NULL) {
        copy_str(out->updated, sizeof(out->updated), s);
    }
    out->updated_day_minutes = parse_day_minutes(item_str(root, "date"));

    s = item_str(data, "wendu");
    if (s != NULL) {
        double d = strtod(s, NULL);
        out->temp_c = (int)(d + (d >= 0 ? 0.5 : -0.5));
    }
    s = item_str(data, "shidu");
    if (parse_percent(s, &temp) == 0) {
        out->humidity = temp;
        out->has_humidity = true;
    }
    if (parse_number(data, "pm25", &temp) == 0) {
        out->pm25 = temp;
        out->has_pm25 = true;
    }
    s = item_str(data, "quality");
    if (s != NULL) {
        copy_str(out->quality, sizeof(out->quality), s);
    }

    forecast = cJSON_GetObjectItemCaseSensitive(data, "forecast");
    if (!cJSON_IsArray(forecast)) {
        cJSON_Delete(root);
        return -EBADMSG;
    }
    /* 该接口最多返回 15 天数据；UI 只需要前 7 天。 */
    cJSON_ArrayForEach(item, forecast) {
        if (n >= WEATHER_MAX_DAYS) {
            break;
        }
        if (!cJSON_IsObject(item)) {
            continue;
        }
        parse_day(item, &out->days[n]);
        n++;
    }
    if (n == 0) {
        cJSON_Delete(root);
        return -EBADMSG;
    }
    out->day_count = n;

    first = cJSON_GetArrayItem(forecast, 0);
    if (cJSON_IsObject(first)) {
        /* 当前的风向/天气/最高最低温取自今天的预报。 */
        s = item_str(first, "type");
        if (s != NULL) {
            copy_str(out->condition, sizeof(out->condition), s);
        }
        s = item_str(first, "fx");
        if (s != NULL) {
            copy_str(out->wind_dir, sizeof(out->wind_dir), s);
            out->has_wind = true;
        }
        s = item_str(first, "fl");
        if (s != NULL) {
            copy_str(out->wind_level, sizeof(out->wind_level), s);
        }
        out->high_c = out->days[0].high_c;
        out->low_c = out->days[0].low_c;
        out->has_high_low = true;
        if (out->days[0].has_aqi) {
            out->aqi = out->days[0].aqi;
            out->has_aqi = true;
        }
    }
    if (out->condition[0] == '\0') {
        copy_str(out->condition, sizeof(out->condition), "未知");
    }
    out->fetched_at = time(NULL);
    out->offline = false;
    out->demo = false;
    cJSON_Delete(root);
    return 0;
}
