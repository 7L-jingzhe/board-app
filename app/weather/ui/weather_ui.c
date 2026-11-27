/**
 * @file weather_ui.c
 * @brief 天气页面：当前天气、7 天预报、城市选择器。
 *
 * 天气的网络/解析/缓存逻辑位于 app/weather/core。本文件只负责
 * 渲染数据，并把触摸事件转换为 core 调用。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "board_ui/ui_app_manager.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_app/board_app.h"
#include "board_core/board_log.h"
#include "board_core/board_storage.h"
#include "board_ui/ui_async.h"
#include "board_ui/ui_icons.h"
#include "board_ui/ui_theme.h"
#include "board_ui/ui_widgets.h"
#include "weather/weather_cache.h"
#include "weather/weather_cities.h"
#include "weather/weather_service.h"
#include "weather/weather_types.h"

#define WEATHER_CACHE_TTL_SECONDS 3600

typedef struct {
    lv_obj_t *page;
    const board_paths_t *paths;
    weather_service_t *service;
    board_async_queue_t *queue;
    ui_async_pump_t *pump;
    char city_code[16];
    char city_name[WEATHER_CITY_MAX];
    weather_data_t data;
    bool has_data;
    bool loading;

    lv_obj_t *city_btn;
    lv_obj_t *refresh_btn;
    lv_obj_t *status_pill;
    lv_obj_t *status_label;
    lv_obj_t *city_label;
    lv_obj_t *temp_label;
    lv_obj_t *condition_label;
    lv_obj_t *highlow_label;
    lv_obj_t *humidity_label;
    lv_obj_t *wind_label;
    lv_obj_t *air_label;
    lv_obj_t *updated_label;
    lv_obj_t *loading_box;
    lv_obj_t *error_label;

    lv_obj_t *rows[WEATHER_MAX_DAYS];
    lv_obj_t *week_labels[WEATHER_MAX_DAYS];
    lv_obj_t *cond_labels[WEATHER_MAX_DAYS];
    lv_obj_t *temp_labels[WEATHER_MAX_DAYS];
    lv_obj_t *icon_boxes[WEATHER_MAX_DAYS];
} weather_app_t;

static void weather_render(weather_app_t *app);
static void weather_refresh(weather_app_t *app);
static void city_chosen_cb(lv_event_t *e);

/* 管理器同一时刻只创建一个天气页面；destroy() 通过该指针
 * 访问状态，而无需把它存到 LVGL 对象上。 */
static weather_app_t *s_weather;

static void weather_back_cb(lv_event_t *e)
{
    (void)e;
    (void)ui_app_manager_back();
}

static lv_obj_t *small_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                             lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text != NULL ? text : "");
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

static void add_metric(lv_obj_t *parent, int x, int y, const char *name,
                       lv_obj_t **value_out)
{
    lv_obj_t *n = small_label(parent, name, ui_font_caption(), UI_COLOR_TEXT_MUTED);
    lv_obj_set_pos(n, x, y);
    lv_obj_t *v = small_label(parent, "--", ui_font_body(), UI_COLOR_TEXT);
    lv_obj_set_pos(v, x, y + 22);
    *value_out = v;
}

static lv_obj_t *forecast_row(lv_obj_t *parent, int index, weather_app_t *app)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 464, 52);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_pad_hor(row, 12, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);

    app->week_labels[index] = small_label(row, "--", ui_font_small(), UI_COLOR_TEXT_SECONDARY);
    lv_obj_set_width(app->week_labels[index], 64);

    lv_obj_t *icon = lv_obj_create(row);
    lv_obj_remove_style_all(icon);
    lv_obj_set_size(icon, 30, 30);
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
    app->icon_boxes[index] = icon;

    app->cond_labels[index] = small_label(row, "--", ui_font_small(), UI_COLOR_TEXT_SECONDARY);
    lv_obj_set_width(app->cond_labels[index], 110);
    lv_label_set_long_mode(app->cond_labels[index], LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(app->cond_labels[index], 1);

    app->temp_labels[index] = small_label(row, "--", ui_font_small(), UI_COLOR_TEXT);
    lv_obj_set_width(app->temp_labels[index], 120);
    lv_obj_set_style_text_align(app->temp_labels[index], LV_TEXT_ALIGN_RIGHT, 0);
    app->rows[index] = row;
    return row;
}

static void update_status(weather_app_t *app, const char *text, lv_color_t fg,
                          lv_color_t bg)
{
    if (app->status_label != NULL) {
        lv_label_set_text(app->status_label, text);
        lv_obj_set_style_text_color(app->status_label, fg, 0);
    }
    if (app->status_pill != NULL) {
        lv_obj_set_style_bg_color(app->status_pill, bg, 0);
    }
}

static void weather_render(weather_app_t *app)
{
    char buf[128];
    const weather_data_t *w = &app->data;

    if (!app->has_data) {
        return;
    }
    lv_label_set_text(app->city_label, w->city[0] != '\0' ? w->city : app->city_name);
    snprintf(buf, sizeof(buf), "%d°", w->temp_c);
    lv_label_set_text(app->temp_label, buf);
    lv_label_set_text(app->condition_label, w->condition[0] != '\0' ? w->condition : "未知");
    snprintf(buf, sizeof(buf), "最高 %d°  ·  最低 %d°", w->high_c, w->low_c);
    lv_label_set_text(app->highlow_label, buf);

    if (w->has_humidity) {
        snprintf(buf, sizeof(buf), "%d%%", w->humidity);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(app->humidity_label, buf);

    if (w->has_wind) {
        snprintf(buf, sizeof(buf), "%s %s", w->wind_dir, w->wind_level);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(app->wind_label, buf);

    if (w->has_aqi) {
        snprintf(buf, sizeof(buf), "%s · AQI %d", w->quality, w->aqi);
    } else if (w->has_pm25) {
        snprintf(buf, sizeof(buf), "PM2.5 %d", w->pm25);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(app->air_label, buf);

    if (w->demo) {
        update_status(app, "演示数据", UI_COLOR_TEXT_MUTED, UI_COLOR_SURFACE_ALT);
        snprintf(buf, sizeof(buf), "演示数据 · 不会写入真实缓存");
    } else if (w->offline) {
        update_status(app, "离线数据", UI_COLOR_WARNING, UI_COLOR_WARNING_SOFT);
        snprintf(buf, sizeof(buf), "离线 · 上次更新 %s",
                 w->updated[0] != '\0' ? w->updated : "未知");
    } else {
        update_status(app, "在线", UI_COLOR_SUCCESS, UI_COLOR_SUCCESS_SOFT);
        snprintf(buf, sizeof(buf), "更新于 %s",
                 w->updated[0] != '\0' ? w->updated : "刚刚");
    }
    lv_label_set_text(app->updated_label, buf);

    for (int i = 0; i < WEATHER_MAX_DAYS; i++) {
        if (i >= w->day_count) {
            lv_obj_add_flag(app->rows[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(app->rows[i], LV_OBJ_FLAG_HIDDEN);
        const weather_day_t *d = &w->days[i];
        lv_label_set_text(app->week_labels[i],
                          d->week[0] != '\0' ? d->week : (i == 0 ? "今天" : d->ymd));
        lv_label_set_text(app->cond_labels[i], d->type[0] != '\0' ? d->type : "--");
        snprintf(buf, sizeof(buf), "%d° / %d°", d->low_c, d->high_c);
        lv_label_set_text(app->temp_labels[i], buf);
        if (i == 0) {
            lv_obj_set_style_bg_color(app->rows[i], UI_COLOR_PRIMARY_SOFT, 0);
            lv_obj_set_style_bg_opa(app->rows[i], LV_OPA_COVER, 0);
        } else {
            lv_obj_set_style_bg_opa(app->rows[i], LV_OPA_TRANSP, 0);
        }
        /* 重绘该行的天气小图标。 */
        lv_obj_clean(app->icon_boxes[i]);
        ui_weather_icon_draw(app->icon_boxes[i], d->type, 30);
    }
}

static void set_loading(weather_app_t *app, bool loading)
{
    app->loading = loading;
    if (app->loading_box != NULL) {
        if (loading) {
            lv_obj_remove_flag(app->loading_box, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(app->loading_box, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (app->refresh_btn != NULL) {
        if (loading) {
            lv_obj_add_state(app->refresh_btn, LV_STATE_DISABLED);
        } else {
            lv_obj_remove_state(app->refresh_btn, LV_STATE_DISABLED);
        }
    }
}

static void weather_result_cb(void *item, void *user_data)
{
    /* 由 UI 线程中的 ui_async_pump 执行，绝不会由工作线程执行。 */
    weather_app_t *app = user_data;
    weather_result_t *res = item;
    if (res == NULL || app == NULL) {
        free(res);
        return;
    }
    if (res->status == WEATHER_EVT_OK) {
        app->data = res->data;
        app->has_data = true;
        if (app->data.city_code[0] != '\0') {
            snprintf(app->city_code, sizeof(app->city_code), "%s", app->data.city_code);
        }
        if (app->data.city[0] != '\0') {
            snprintf(app->city_name, sizeof(app->city_name), "%s", app->data.city);
        }
        weather_render(app);
        if (res->from_cache) {
            /* 联网失败后用旧缓存兜底：让用户知道现在看到的是旧数据。 */
            ui_toast_show(res->message[0] != '\0' ? res->message : "离线数据");
        }
    } else {
        ui_toast_show(res->message[0] != '\0' ? res->message : "天气获取失败");
        if (!app->has_data && app->error_label != NULL) {
            lv_obj_remove_flag(app->error_label, LV_OBJ_FLAG_HIDDEN);
        }
        if (app->has_data) {
            /* 已有旧数据时退化为离线标注，避免把过期数据当成在线数据。 */
            app->data.offline = true;
            weather_render(app);
        }
    }
    set_loading(app, false);
    free(res);
}

static void weather_refresh(weather_app_t *app)
{
    if (app == NULL || app->loading) {
        return;
    }
    set_loading(app, true);
    if (app->error_label != NULL) {
        lv_obj_add_flag(app->error_label, LV_OBJ_FLAG_HIDDEN);
    }
    int ret = weather_service_refresh(app->service, app->city_code, app->city_name);
    if (ret != 0 && ret != -EAGAIN) {
        set_loading(app, false);
        ui_toast_show("无法发起天气请求");
    }
}

static void refresh_clicked_cb(lv_event_t *e)
{
    weather_app_t *app = lv_event_get_user_data(e);
    weather_refresh(app);
}

static void city_clicked_cb(lv_event_t *e)
{
    weather_app_t *app = lv_event_get_user_data(e);
    if (app == NULL) {
        return;
    }
    lv_obj_t *content = ui_modal_create(app->page, "选择城市", 560);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(content, 12, 0);
    lv_obj_set_style_pad_row(content, 12, 0);

    size_t count = 0;
    const weather_city_t *cities = weather_cities(&count);
    for (size_t i = 0; i < count; i++) {
        lv_obj_t *btn = ui_button_secondary_create(content, cities[i].name, NULL, NULL);
        lv_obj_set_size(btn, 156, 56);
        lv_obj_set_user_data(btn, (void *)&cities[i]);
        lv_obj_add_event_cb(btn, city_chosen_cb, LV_EVENT_CLICKED, app);
        if (strcmp(cities[i].code, app->city_code) == 0) {
            lv_obj_set_style_bg_color(btn, UI_COLOR_PRIMARY_SOFT, 0);
        }
    }
}

static void city_chosen_cb(lv_event_t *e)
{
    weather_app_t *app = lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_current_target(e);
    const weather_city_t *city = lv_obj_get_user_data(btn);
    if (app == NULL || city == NULL) {
        return;
    }
    snprintf(app->city_code, sizeof(app->city_code), "%s", city->code);
    snprintf(app->city_name, sizeof(app->city_name), "%s", city->name);
    lv_label_set_text(lv_obj_get_child(app->city_btn, 0), city->name);
    (void)board_storage_set_str("weather.city_code", city->code);
    (void)board_storage_set_str("weather.city_name", city->name);
    (void)board_storage_save();

    lv_obj_t *content = lv_obj_get_parent(btn);
    if (content != NULL) {
        ui_modal_close(content);
    }
    weather_refresh(app);
}

static lv_obj_t *weather_create(lv_obj_t *parent)
{
    weather_app_t *app = calloc(1, sizeof(*app));
    if (app == NULL) {
        return NULL;
    }
    s_weather = app;
    app->page = parent;
    app->paths = board_app_paths();
    lv_obj_set_style_bg_color(parent, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    /* board_storage_get_str 返回每线程共享的缓冲区；先复制每个
     * 值，再请求下一个。 */
    snprintf(app->city_code, sizeof(app->city_code), "%s",
             board_storage_get_str("weather.city_code", "101010100"));
    snprintf(app->city_name, sizeof(app->city_name), "%s",
             board_storage_get_str("weather.city_name", "北京"));
    BOARD_LOGI("weather", "city code='%s' name='%s'", app->city_code,
               app->city_name);

    lv_obj_t *bar = ui_topbar_create(parent, "天气", weather_back_cb, app);
    app->city_btn = ui_topbar_add_action(bar, app->city_name, city_clicked_cb, app);
    /* 把城市操作按钮重绘为柔和的胶囊样式。 */
    lv_obj_set_style_bg_color(app->city_btn, UI_COLOR_PRIMARY_SOFT, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(app->city_btn, 0), UI_COLOR_PRIMARY, 0);
    lv_obj_set_height(app->city_btn, 44);
    lv_obj_set_style_radius(app->city_btn, 22, 0);
    app->refresh_btn = ui_topbar_add_action(bar, "刷新", refresh_clicked_cb, app);

    /* 左侧：当前天气。 */
    lv_obj_t *hero = ui_card_create(parent, 440, 472);
    lv_obj_set_pos(hero, 32, 96);
    lv_obj_set_style_bg_color(hero, lv_color_mix(UI_COLOR_PRIMARY, UI_COLOR_SURFACE, 36), 0);

    app->city_label = small_label(hero, app->city_name, ui_font_h2(), UI_COLOR_TEXT);
    lv_obj_set_pos(app->city_label, 24, 20);

    app->status_pill = ui_pill_create(hero, "在线", UI_COLOR_SUCCESS,
                                      UI_COLOR_SUCCESS_SOFT);
    lv_obj_align(app->status_pill, LV_ALIGN_TOP_RIGHT, -24, 20);
    app->status_label = lv_obj_get_child(app->status_pill, 0);

    app->temp_label = small_label(hero, "--", ui_font_number_large(), UI_COLOR_TEXT);
    lv_obj_set_pos(app->temp_label, 24, 80);
    app->condition_label = small_label(hero, "--", ui_font_h2(), UI_COLOR_TEXT);
    lv_obj_set_pos(app->condition_label, 220, 108);
    app->highlow_label = small_label(hero, "--", ui_font_body(), UI_COLOR_TEXT_SECONDARY);
    lv_obj_set_pos(app->highlow_label, 24, 174);

    lv_obj_t *line = lv_obj_create(hero);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, 392, 1);
    lv_obj_set_pos(line, 24, 220);
    lv_obj_set_style_bg_color(line, UI_COLOR_LINE, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);

    add_metric(hero, 24, 244, "湿度", &app->humidity_label);
    add_metric(hero, 24, 306, "风向风力", &app->wind_label);
    add_metric(hero, 24, 368, "空气质量", &app->air_label);

    app->updated_label = small_label(hero, "", ui_font_caption(), UI_COLOR_TEXT_MUTED);
    lv_obj_set_pos(app->updated_label, 24, 438);

    app->error_label = small_label(hero, "暂时无法获取天气", ui_font_small(), UI_COLOR_DANGER);
    lv_obj_add_flag(app->error_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(app->error_label, LV_ALIGN_BOTTOM_LEFT, 24, -24);

    app->loading_box = ui_loading_create(hero, "正在获取天气…");
    lv_obj_set_size(app->loading_box, 392, 260);
    lv_obj_align(app->loading_box, LV_ALIGN_CENTER, 0, 30);
    lv_obj_add_flag(app->loading_box, LV_OBJ_FLAG_HIDDEN);

    /* 右侧：7 天预报。 */
    lv_obj_t *forecast = ui_card_create(parent, 496, 472);
    lv_obj_set_pos(forecast, 496, 96);
    lv_obj_t *ftitle = small_label(forecast, "未来 7 天", ui_font_h3(), UI_COLOR_TEXT);
    lv_obj_set_pos(ftitle, 24, 20);

    lv_obj_t *rows = lv_obj_create(forecast);
    lv_obj_remove_style_all(rows);
    lv_obj_set_pos(rows, 16, 64);
    lv_obj_set_size(rows, 464, 392);
    lv_obj_set_style_bg_opa(rows, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(rows, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(rows, 0, 0);
    lv_obj_remove_flag(rows, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < WEATHER_MAX_DAYS; i++) {
        forecast_row(rows, i, app);
    }

    app->queue = board_async_queue_create(8);
    app->service = weather_service_create(app->paths, app->queue);
    app->pump = ui_async_pump_create(app->queue, weather_result_cb, app, 30);
    if (app->queue == NULL || app->service == NULL || app->pump == NULL) {
        ui_toast_show("天气服务初始化失败");
    }

    /* 先立即显示最近的缓存记录，然后在后台刷新。
     * 超过一小时的记录不作为当前天气显示；等待网络结果
     *（工作线程仍会把它们用作离线回退）。
     * 另外，板子没有 RTC 时墙上时钟可能被回拨：接口日期比本地今天还新的缓存
     * 说明 mtime 不可信，这时也按过期处理并强制重新请求。 */
    weather_data_t cached;
    if (weather_cache_load(app->paths, app->city_code, &cached) == 0) {
        int64_t age = -1;
        (void)weather_cache_age(app->paths, app->city_code, &age);
        if (weather_cache_day_is_ahead(app->paths, app->city_code)) {
            BOARD_LOGW("weather", "cache date is ahead of local clock, refreshing");
        } else if (age >= 0 && age <= WEATHER_CACHE_TTL_SECONDS) {
            app->data = cached;
            app->has_data = true;
            weather_render(app);
            BOARD_LOGI("weather", "using cache age=%lld s", (long long)age);
        } else {
            BOARD_LOGI("weather", "cache stale age=%lld s, refreshing",
                       (long long)age);
        }
    }
    weather_refresh(app);
    return parent;
}

static void weather_destroy(void)
{
    weather_app_t *app = s_weather;
    if (app == NULL) {
        return;
    }
    /* 先停止工作线程，再释放它仍可能访问的任何资源。 */
    weather_service_cancel(app->service);
    weather_service_destroy(app->service);
    if (app->queue != NULL) {
        void *item;
        while ((item = board_async_queue_pop(app->queue)) != NULL) {
            free(item);
        }
    }
    ui_async_pump_destroy(app->pump);
    board_async_queue_destroy(app->queue);
    free(app);
    s_weather = NULL;
}

static const board_app_desc_t s_weather_app = {
    .id = "weather",
    .title = "天气",
    .subtitle = "实时天气与 7 日预报",
    .parent_id = NULL,
    .best_score_key = NULL,
    .icon = UI_ICON_WEATHER,
    .accent = 0x0E9F8C,
    .order = 10,
    .show_on_home = true,
    .create = weather_create,
    .destroy = weather_destroy,
};

BOARD_APP_REGISTER(s_weather_app);
