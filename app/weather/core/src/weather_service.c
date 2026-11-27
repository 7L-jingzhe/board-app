/**
 * @file weather_service.c
 * @brief 带缓存/演示数据回退的异步天气刷新。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "weather/weather_service.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "board_core/board_error.h"
#include "board_core/board_log.h"
#include "board_core/http_client.h"
#include "weather/weather_cache.h"
#include "weather/weather_mock.h"
#include "weather/weather_parse.h"
#include "weather/weather_provider.h"

struct weather_service {
    board_paths_t paths;
    board_async_queue_t *queue;
    pthread_mutex_t lock;
    pthread_t thread;
    bool thread_joinable;
    bool running;
    volatile int cancel;
    char city_code[16];
    char city_name[WEATHER_CITY_MAX];
    weather_data_t last;
    bool has_last;
};

static void join_worker(weather_service_t *svc)
{
    pthread_t th;
    bool joinable;

    pthread_mutex_lock(&svc->lock);
    th = svc->thread;
    joinable = svc->thread_joinable;
    svc->thread_joinable = false;
    pthread_mutex_unlock(&svc->lock);
    if (joinable) {
        pthread_join(th, NULL);
    }
}

static void publish_result(weather_service_t *svc, weather_result_t *res)
{
    if (svc->queue == NULL || board_async_queue_push(svc->queue, res) != 0) {
        free(res);
    }
}

/* 接口被限流时响应体形如 {"code":"40310003","msg":"too many requests"}。
 * 这种错误短时间内重试即可成功，不需要用户手动刷新。 */
static bool looks_like_rate_limit(const char *body, size_t len, int status)
{
    if (status != 403 && status != 429) {
        return false;
    }
    if (body == NULL || len == 0) {
        return status == 429;
    }
    if (len > 256) {
        len = 256;
    }
    for (size_t i = 0; i + 4 <= len; i++) {
        if (strncasecmp(body + i, "40310003", 8) == 0 ||
            strncasecmp(body + i, "too many", 8) == 0 ||
            strncasecmp(body + i, "rate", 4) == 0) {
            return true;
        }
    }
    return false;
}

/* 简单的指数退避等待；期间响应取消标志，避免退出页面时被卡住。 */
static void backoff_wait(weather_service_t *svc, int delay_ms)
{
    for (int waited = 0; waited < delay_ms; waited += 100) {
        if (svc->cancel) {
            return;
        }
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 100 * 1000 * 1000;
        nanosleep(&ts, NULL);
    }
}

static void *worker(void *arg)
{
    weather_service_t *svc = arg;
    char url[4096];
    char code[16];
    char name[WEATHER_CITY_MAX];
    http_response_t resp;
    weather_result_t *res;

    memset(&resp, 0, sizeof(resp));
    res = calloc(1, sizeof(*res));
    if (res == NULL) {
        pthread_mutex_lock(&svc->lock);
        svc->running = false;
        pthread_mutex_unlock(&svc->lock);
        return NULL;
    }

    pthread_mutex_lock(&svc->lock);
    snprintf(code, sizeof(code), "%s", svc->city_code);
    snprintf(name, sizeof(name), "%s", svc->city_name);
    pthread_mutex_unlock(&svc->lock);

    res->status = WEATHER_EVT_NETWORK_ERROR;
    snprintf(res->message, sizeof(res->message), "%s", "网络不可用");
    BOARD_LOGI("weather", "worker code='%s' name='%s' base='%s'", code, name,
               weather_provider_base_url());

    if (weather_provider_build_url(code, url, sizeof(url)) == 0) {
        int ret = 0;
        /* 接口偶尔会返回 403（响应体 {"code":"40310003","msg":"too many
         * requests"}）；这是短时限流，退避后再试两轮即可成功。 */
        for (int attempt = 0; attempt < 3; attempt++) {
            if (attempt > 0) {
                int delay_ms = attempt == 1 ? 1200 : 2500;
                BOARD_LOGW("weather", "rate limited, retry %d after %d ms",
                           attempt, delay_ms);
                backoff_wait(svc, delay_ms);
            }
            http_response_free(&resp);
            BOARD_LOGI("weather", "GET %s (attempt %d)", url, attempt + 1);
            ret = http_get_ex(url, 6000, &svc->cancel, &resp);
            BOARD_LOGI("weather", "http ret=%d status=%d len=%zu", ret,
                       resp.status_code, resp.len);
            if (svc->cancel) {
                break;
            }
            if (ret == 0 && resp.status_code == 200) {
                break;
            }
            if (!looks_like_rate_limit(resp.body, resp.len, resp.status_code)) {
                break;
            }
            if (ret != 0) {
                break;
            }
        }
        if (svc->cancel) {
            free(res);
            http_response_free(&resp);
            pthread_mutex_lock(&svc->lock);
            svc->running = false;
            pthread_mutex_unlock(&svc->lock);
            return NULL;
        }
        if (ret == 0 && resp.status_code == 200 && resp.body != NULL) {
            weather_data_t data;
            if (weather_parse_json_for_city(resp.body, resp.len, &data, code) == 0) {
                (void)weather_cache_save(&svc->paths, code, resp.body, resp.len);
                /* 数据源只给 3 天时，用历史日历把 7 天列表补齐。 */
                (void)weather_cache_merge_days(&svc->paths, code, &data);
                res->status = WEATHER_EVT_OK;
                res->data = data;
                res->from_cache = false;
                snprintf(res->message, sizeof(res->message), "%s", "在线数据");
                BOARD_LOGI("weather", "online ok: %s %d C", data.city,
                           data.temp_c);
                goto publish;
            }
            res->status = WEATHER_EVT_PARSE_ERROR;
            snprintf(res->message, sizeof(res->message), "%s", "天气数据格式异常");
        } else if (ret != 0) {
            snprintf(res->message, sizeof(res->message), "网络不可用（%s）",
                     strerror(ret < 0 ? -ret : ret));
        } else {
            snprintf(res->message, sizeof(res->message), "天气服务返回 HTTP %d",
                     resp.status_code);
            /* 403/5xx 时打印响应体开头：这类错误通常来自 CDN/防爬而非天气接口，
             * 日志里看不出原因时只能靠响应体判断。 */
            if (resp.body != NULL && resp.len > 0) {
                char preview[120];
                size_t n = resp.len < sizeof(preview) - 1 ? resp.len
                                                          : sizeof(preview) - 1;
                memcpy(preview, resp.body, n);
                preview[n] = '\0';
                BOARD_LOGW("weather", "http %d body: %s", resp.status_code,
                           preview);
            }
        }
    } else {
        snprintf(res->message, sizeof(res->message), "%s", "天气服务地址无效");
    }

    /* 依次回退到缓存，再回退到确定性演示数据。 */
    {
        weather_data_t cached;
        int cache_ret = weather_cache_load(&svc->paths, code, &cached);
        BOARD_LOGW("weather", "network failed, cache load rc=%d: %s", cache_ret,
                   res->message);
        if (cache_ret == 0) {
            res->status = WEATHER_EVT_OK;
            res->data = cached;
            res->from_cache = true;
            snprintf(res->message, sizeof(res->message), "离线数据（HTTP %d）",
                     resp.status_code);
            goto publish;
        }
    }
    weather_mock_fill(&res->data);
    snprintf(res->data.city, sizeof(res->data.city), "%s",
             name[0] != '\0' ? name : "北京");
    snprintf(res->data.city_code, sizeof(res->data.city_code), "%s", code);
    res->status = WEATHER_EVT_OK;
    res->from_cache = false;
    snprintf(res->message, sizeof(res->message), "%s", "演示数据");
    BOARD_LOGW("weather", "using demo data for %s", code);

publish:
    pthread_mutex_lock(&svc->lock);
    svc->last = res->data;
    svc->has_last = true;
    pthread_mutex_unlock(&svc->lock);
    publish_result(svc, res);
    http_response_free(&resp);
    pthread_mutex_lock(&svc->lock);
    svc->running = false;
    pthread_mutex_unlock(&svc->lock);
    return NULL;
}

weather_service_t *weather_service_create(const board_paths_t *paths,
                                          board_async_queue_t *queue)
{
    weather_service_t *svc;
    if (paths == NULL || queue == NULL) {
        return NULL;
    }
    svc = calloc(1, sizeof(*svc));
    if (svc == NULL) {
        return NULL;
    }
    svc->paths = *paths;
    svc->queue = queue;
    pthread_mutex_init(&svc->lock, NULL);
    return svc;
}

void weather_service_destroy(weather_service_t *svc)
{
    if (svc == NULL) {
        return;
    }
    weather_service_cancel(svc);
    pthread_mutex_destroy(&svc->lock);
    free(svc);
}

int weather_service_refresh(weather_service_t *svc, const char *city_code,
                            const char *city_name)
{
    int ret;
    pthread_t old_thread;
    bool old_joinable;

    if (svc == NULL || city_code == NULL || city_code[0] == '\0') {
        return -EINVAL;
    }
    if (strlen(city_code) >= sizeof(svc->city_code)) {
        return -EINVAL;
    }

    /* 回收已结束的工作线程，但绝不因正在运行的请求阻塞 UI。 */
    pthread_mutex_lock(&svc->lock);
    if (svc->running) {
        pthread_mutex_unlock(&svc->lock);
        return -EAGAIN;
    }
    old_thread = svc->thread;
    old_joinable = svc->thread_joinable;
    svc->thread_joinable = false;
    pthread_mutex_unlock(&svc->lock);
    if (old_joinable) {
        pthread_join(old_thread, NULL);
    }

    pthread_mutex_lock(&svc->lock);
    if (svc->running) {
        pthread_mutex_unlock(&svc->lock);
        return -EAGAIN;
    }
    svc->cancel = 0;
    snprintf(svc->city_code, sizeof(svc->city_code), "%s", city_code);
    snprintf(svc->city_name, sizeof(svc->city_name), "%s",
             city_name != NULL ? city_name : "");
    svc->running = true;
    ret = pthread_create(&svc->thread, NULL, worker, svc);
    if (ret != 0) {
        svc->running = false;
        pthread_mutex_unlock(&svc->lock);
        return -EAGAIN;
    }
    svc->thread_joinable = true;
    pthread_mutex_unlock(&svc->lock);
    return 0;
}

void weather_service_cancel(weather_service_t *svc)
{
    if (svc == NULL) {
        return;
    }
    pthread_mutex_lock(&svc->lock);
    svc->cancel = 1;
    pthread_mutex_unlock(&svc->lock);
    join_worker(svc);
}

const weather_data_t *weather_service_last(weather_service_t *svc)
{
    if (svc == NULL || !svc->has_last) {
        return NULL;
    }
    return &svc->last;
}
