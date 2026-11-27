/**
 * @file weather_cache.c
 * @brief 原始 JSON 天气缓存。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "weather/weather_cache.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "board_core/board_error.h"
#include "weather/weather_parse.h"

#define CACHE_MAX_BYTES (256u * 1024u)

static int cache_path(const board_paths_t *paths, const char *city_code,
                      char *out, size_t cap)
{
    if (paths == NULL || city_code == NULL || out == NULL) {
        return -EINVAL;
    }
    for (const char *p = city_code; *p != '\0'; p++) {
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-') {
            return -EINVAL;
        }
    }
    if (snprintf(out, cap, "%s/weather_%s.json", paths->cache, city_code) >= (int)cap) {
        return -ENAMETOOLONG;
    }
    return 0;
}

static int mkdir_if_needed(const char *path)
{
    char tmp[PATH_MAX];
    char *slash;
    if (path == NULL) {
        return -EINVAL;
    }
    snprintf(tmp, sizeof(tmp), "%s", path);
    slash = strrchr(tmp, '/');
    if (slash == NULL) {
        return 0;
    }
    *slash = '\0';
    for (char *p = tmp + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return -EIO;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return -EIO;
    }
    return 0;
}

int weather_cache_load(const board_paths_t *paths, const char *city_code,
                       weather_data_t *out)
{
    char path[PATH_MAX];
    FILE *fp;
    long size;
    char *buf;
    size_t got;
    struct stat st;
    int ret;

    if (out == NULL) {
        return -EINVAL;
    }
    ret = cache_path(paths, city_code, path, sizeof(path));
    if (ret != 0) {
        return ret;
    }
    fp = fopen(path, "rb");
    if (fp == NULL) {
        return (errno == ENOENT) ? -ENOENT : -EIO;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -EIO;
    }
    size = ftell(fp);
    if (size <= 0 || (unsigned long)size > CACHE_MAX_BYTES) {
        fclose(fp);
        return -EIO;
    }
    rewind(fp);
    buf = malloc((size_t)size + 1);
    if (buf == NULL) {
        fclose(fp);
        return -ENOMEM;
    }
    got = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    if (got != (size_t)size) {
        free(buf);
        return -EIO;
    }
    buf[got] = '\0';
    ret = weather_parse_json(buf, got, out);
    free(buf);
    if (ret != 0) {
        return ret;
    }
    out->offline = true;
    out->fetched_at = time(NULL);
    if (stat(path, &st) == 0) {
        out->fetched_at = st.st_mtime;
    }
    return 0;
}

int weather_cache_age(const board_paths_t *paths, const char *city_code,
                      int64_t *age_seconds)
{
    char path[PATH_MAX];
    struct stat st;
    int ret;
    time_t now;

    if (age_seconds == NULL) {
        return -EINVAL;
    }
    ret = cache_path(paths, city_code, path, sizeof(path));
    if (ret != 0) {
        return ret;
    }
    if (stat(path, &st) != 0) {
        return (errno == ENOENT) ? -ENOENT : -EIO;
    }
    now = time(NULL);
    *age_seconds = (now > st.st_mtime) ? (int64_t)(now - st.st_mtime) : 0;
    return 0;
}

/* 接口返回的 date 只能是今天或更早。缓存里存的是比今天还新的日期，
 * 说明系统的墙上时钟被回拨过（常见于没有 RTC 的板子断电重启），
 * 此时按 mtime 算出的“新鲜度”并不可信。 */
bool weather_cache_day_is_ahead(const board_paths_t *paths,
                                const char *city_code)
{
    weather_data_t cached;
    struct tm today;
    time_t now;
    int today_minutes;

    if (weather_cache_load(paths, city_code, &cached) != 0) {
        return false;
    }
    if (cached.updated_day_minutes < 0) {
        return false;
    }
    now = time(NULL);
    if (localtime_r(&now, &today) == NULL) {
        return false;
    }
    today_minutes = (int)(now / 60);
    return cached.updated_day_minutes > today_minutes;
}

int weather_cache_save(const board_paths_t *paths, const char *city_code,
                       const char *raw_json, size_t len)
{
    char path[PATH_MAX];
    char tmp[PATH_MAX + 8];
    FILE *fp;
    int fd;
    int ret;

    if (raw_json == NULL || len == 0) {
        return -EINVAL;
    }
    ret = cache_path(paths, city_code, path, sizeof(path));
    if (ret != 0) {
        return ret;
    }
    ret = mkdir_if_needed(path);
    if (ret != 0) {
        return ret;
    }
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    fp = fopen(tmp, "wb");
    if (fp == NULL) {
        return -EIO;
    }
    if (fwrite(raw_json, 1, len, fp) != len) {
        fclose(fp);
        unlink(tmp);
        return -EIO;
    }
    if (fflush(fp) != 0) {
        fclose(fp);
        unlink(tmp);
        return -EIO;
    }
    fd = fileno(fp);
    if (fd >= 0) {
        (void)fsync(fd);
    }
    if (fclose(fp) != 0) {
        unlink(tmp);
        return -EIO;
    }
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return -EIO;
    }
    return 0;
}

int weather_cache_clear(const board_paths_t *paths, const char *city_code)
{
    char path[PATH_MAX];
    int ret = cache_path(paths, city_code, path, sizeof(path));
    if (ret != 0) {
        return ret;
    }
    return (unlink(path) == 0 || errno == ENOENT) ? 0 : -EIO;
}

/* ------------------------------------------------------------------ 历史日历 */
/*
 * 数据源（wttr.in）只返回 3 天预报，而 UI 需要 7 天。这里把每次成功拿到的
 * 逐日预报按日期累积到 data/cache/forecast_<city>.txt，再用它补齐缺少的日期。
 * 同一日期始终以本次实时数据为准，历史只填空缺。
 */

#define FORECAST_MAX_DAYS 16

typedef struct {
    int minutes; /* 日期换算出的分钟数，作为主键 */
    weather_day_t day;
} forecast_entry_t;

static int forecast_path(const board_paths_t *paths, const char *city_code,
                         char *out, size_t cap)
{
    if (paths == NULL || city_code == NULL || out == NULL) {
        return -EINVAL;
    }
    for (const char *p = city_code; *p != '\0'; p++) {
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-') {
            return -EINVAL;
        }
    }
    if (snprintf(out, cap, "%s/forecast_%s.txt", paths->cache, city_code) >=
        (int)cap) {
        return -ENAMETOOLONG;
    }
    return 0;
}

/* 每行：分钟数|ymd|星期|天气|最低|最高|风向|风力 */
static int forecast_load(const char *path, forecast_entry_t *items, int cap)
{
    FILE *fp = fopen(path, "r");
    char line[256];
    int n = 0;

    if (fp == NULL) {
        return 0;
    }
    while (n < cap && fgets(line, sizeof(line), fp) != NULL) {
        forecast_entry_t *e = &items[n];
        memset(e, 0, sizeof(*e));
        if (sscanf(line, "%d|%11[^|]|%19[^|]|%39[^|]|%d|%d|%15[^|]|%15[^|]",
                   &e->minutes, e->day.ymd, e->day.week, e->day.type,
                   &e->day.low_c, &e->day.high_c, e->day.wind_dir,
                   e->day.wind_level) != 8) {
            continue;
        }
        n++;
    }
    fclose(fp);
    return n;
}

static int forecast_save(const char *path, const forecast_entry_t *items, int n)
{
    char tmp[PATH_MAX + 8];
    FILE *fp;

    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    fp = fopen(tmp, "w");
    if (fp == NULL) {
        return -EIO;
    }
    for (int i = 0; i < n; i++) {
        fprintf(fp, "%d|%s|%s|%s|%d|%d|%s|%s\n", items[i].minutes,
                items[i].day.ymd, items[i].day.week, items[i].day.type,
                items[i].day.low_c, items[i].day.high_c, items[i].day.wind_dir,
                items[i].day.wind_level);
    }
    if (fclose(fp) != 0) {
        unlink(tmp);
        return -EIO;
    }
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return -EIO;
    }
    return 0;
}

/* 用 “2026-11-25” 或 “20261125” 推算日期分钟数，失败返回 -1。 */
static int ymd_to_minutes(const char *ymd)
{
    int y = 0;
    int m = 0;
    int d = 0;
    struct tm tm_value;

    if (ymd == NULL) {
        return -1;
    }
    if (sscanf(ymd, "%d-%d-%d", &y, &m, &d) != 3 &&
        sscanf(ymd, "%4d%2d%2d", &y, &m, &d) != 3) {
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

static int compare_minutes(const void *a, const void *b)
{
    const forecast_entry_t *ea = a;
    const forecast_entry_t *eb = b;
    return (ea->minutes > eb->minutes) - (ea->minutes < eb->minutes);
}

int weather_cache_merge_days(const board_paths_t *paths, const char *city_code,
                             weather_data_t *live)
{
    char path[PATH_MAX];
    forecast_entry_t stored[FORECAST_MAX_DAYS];
    forecast_entry_t merged[FORECAST_MAX_DAYS];
    int stored_n = 0;
    int live_n;
    int merged_n = 0;
    int today;
    time_t now;

    if (paths == NULL || city_code == NULL || live == NULL) {
        return -EINVAL;
    }
    if (live->day_count <= 0) {
        return 0;
    }
    if (forecast_path(paths, city_code, path, sizeof(path)) != 0) {
        return -EINVAL;
    }
    stored_n = forecast_load(path, stored, FORECAST_MAX_DAYS);

    now = time(NULL);
    today = (int)(now / 60);
    live_n = live->day_count;

    /* 先放入实时数据（它的日期一定是权威的）。 */
    for (int i = 0; i < live_n && merged_n < FORECAST_MAX_DAYS; i++) {
        int minutes = ymd_to_minutes(live->days[i].ymd);
        merged[merged_n].minutes = minutes;
        merged[merged_n].day = live->days[i];
        merged_n++;
    }
    /* 再把历史里“实时没有、并且不早于今天”的日期补进去。 */
    for (int i = 0; i < stored_n && merged_n < FORECAST_MAX_DAYS; i++) {
        bool exists = false;
        if (stored[i].minutes < today) {
            continue;
        }
        for (int j = 0; j < merged_n; j++) {
            if (merged[j].minutes == stored[i].minutes) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            merged[merged_n++] = stored[i];
        }
    }
    qsort(merged, (size_t)merged_n, sizeof(merged[0]), compare_minutes);

    /* 顺手把过期（早于今天）的历史清掉，文件不会无限增长。 */
    {
        forecast_entry_t keep[FORECAST_MAX_DAYS];
        int keep_n = 0;
        for (int i = 0; i < merged_n; i++) {
            if (merged[i].minutes >= today) {
                keep[keep_n++] = merged[i];
            }
        }
        (void)forecast_save(path, keep, keep_n);
    }

    /* 用合并结果重写 live（UI 只用前 WEATHER_MAX_DAYS 天）。 */
    live->day_count = 0;
    for (int i = 0; i < merged_n && i < WEATHER_MAX_DAYS; i++) {
        live->days[i] = merged[i].day;
        live->day_count++;
    }
    if (live->day_count > 0 && !live->has_high_low) {
        live->high_c = live->days[0].high_c;
        live->low_c = live->days[0].low_c;
        live->has_high_low = true;
    }
    return 0;
}
