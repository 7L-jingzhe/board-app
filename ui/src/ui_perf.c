/**
 * @file ui_perf.c
 * @brief 帧率 / 帧耗时 / LVGL 堆统计（BOARD_APP_PERF=1 时启用）。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "board_ui/ui_perf.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "board_core/board_log.h"
#include "board_ui/ui_theme.h"
#include "lvgl/lvgl.h"

#define PERF_REPORT_MS 2000

static bool s_enabled;
static uint32_t s_frames;
static uint64_t s_total_us;
static uint32_t s_max_us;
static uint32_t s_slow_frames;   /* 超过 33 ms（<30fps）的帧数 */
static lv_timer_t *s_timer;

static uint32_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000ULL);
}

static void report_cb(lv_timer_t *timer)
{
    lv_mem_monitor_t mon;
    uint32_t avg;
    (void)timer;

    if (s_frames == 0) {
        return;
    }
    avg = (uint32_t)(s_total_us / s_frames);
    lv_mem_monitor(&mon);
    BOARD_LOGI("perf",
               "fps=%.1f frame avg=%uus max=%uus slow(>33ms)=%u | heap used=%uKB free=%uKB "
               "max_used=%uKB frag=%u%% used=%u%%",
               (double)s_frames * 1000.0 / PERF_REPORT_MS, (unsigned)avg,
               (unsigned)s_max_us, (unsigned)s_slow_frames,
               (unsigned)((mon.total_size - mon.free_size) / 1024),
               (unsigned)(mon.free_size / 1024), (unsigned)(mon.max_used / 1024),
               (unsigned)mon.frag_pct, (unsigned)mon.used_pct);
    s_frames = 0;
    s_total_us = 0;
    s_max_us = 0;
    s_slow_frames = 0;
}

void ui_perf_init(void)
{
    const char *env = getenv("BOARD_APP_PERF");
    if (env == NULL || env[0] == '\0' || env[0] == '0') {
        s_enabled = false;
        return;
    }
    s_enabled = true;
    s_timer = lv_timer_create(report_cb, PERF_REPORT_MS, NULL);
    BOARD_LOGI("perf", "enabled (BOARD_APP_PERF=%s)", env);
}

bool ui_perf_enabled(void)
{
    return s_enabled;
}

uint32_t ui_perf_begin(void)
{
    return s_enabled ? now_us() : 0;
}

void ui_perf_end(uint32_t start_us)
{
    uint32_t cost;
    if (!s_enabled) {
        return;
    }
    cost = now_us() - start_us;
    s_frames++;
    s_total_us += cost;
    if (cost > s_max_us) {
        s_max_us = cost;
    }
    if (cost > 33000) {
        s_slow_frames++;
    }
}

void ui_perf_mark(const char *name)
{
    static uint32_t last;
    uint32_t now;

    if (!s_enabled) {
        return;
    }
    now = now_us();
    BOARD_LOGI("perf", "mark %-12s %u.%03u ms (since last)", name != NULL ? name : "?",
               (unsigned)((now - last) / 1000), (unsigned)((now - last) % 1000));
    last = now;
}

void ui_perf_font_probe(void)
{
    static const uint32_t CJK[] = {
        0x4F60, 0x597D, 0x6211, 0x662F, 0x8FD9, 0x5F00, 0x53D1, 0x677F,
        0x7684, 0x5E2E, 0x624B, 0x8F93, 0x5165, 0x6CD5, 0x5019, 0x9009,
    };
    const lv_font_t *font;
    lv_font_glyph_dsc_t g;
    uint32_t t0;
    uint32_t first_us;
    uint32_t second_us;
    size_t i;

    if (!s_enabled) {
        return;
    }
    font = ui_font_small();
    if (font == NULL || font->get_glyph_dsc == NULL) {
        return;
    }

    t0 = now_us();
    for (i = 0; i < sizeof(CJK) / sizeof(CJK[0]); i++) {
        font->get_glyph_dsc(font, &g, CJK[i], 0);
    }
    first_us = now_us() - t0;

    t0 = now_us();
    for (i = 0; i < sizeof(CJK) / sizeof(CJK[0]); i++) {
        font->get_glyph_dsc(font, &g, CJK[i], 0);
    }
    second_us = now_us() - t0;

    BOARD_LOGI("perf",
               "glyph probe: first=%u us (%.2f us/glyph) cached=%u us (%.2f us/glyph)",
               (unsigned)first_us, (double)first_us / (double)(sizeof(CJK) / sizeof(CJK[0])),
               (unsigned)second_us,
               (double)second_us / (double)(sizeof(CJK) / sizeof(CJK[0])));
}

void ui_perf_object_probe(lv_obj_t *parent)
{
    enum { N = 20 };
    uint32_t t0;
    uint32_t plain_us;
    uint32_t label_us;
    uint32_t styled_us;
    lv_obj_t *made[N];
    int i;

    if (!s_enabled || parent == NULL) {
        return;
    }

    /* 1) 纯空对象 */
    t0 = now_us();
    for (i = 0; i < N; i++) {
        made[i] = lv_obj_create(parent);
    }
    for (i = 0; i < N; i++) {
        lv_obj_delete(made[i]);
    }
    plain_us = now_us() - t0;

    /* 2) 带中文文本 + 主题字体的 label */
    t0 = now_us();
    for (i = 0; i < N; i++) {
        made[i] = lv_label_create(parent);
        lv_label_set_text(made[i], "你好世界");
        lv_obj_set_style_text_font(made[i], ui_font_small(), 0);
        lv_obj_set_width(made[i], 120);
        lv_label_set_long_mode(made[i], LV_LABEL_LONG_WRAP);
    }
    for (i = 0; i < N; i++) {
        lv_obj_delete(made[i]);
    }
    label_us = now_us() - t0;

    /* 3) flex 容器 + 百分比宽度 + 内容高度（聊天气泡的写法） */
    t0 = now_us();
    for (i = 0; i < N; i++) {
        lv_obj_t *box = lv_obj_create(parent);
        lv_obj_remove_style_all(box);
        lv_obj_set_width(box, LV_PCT(84));
        lv_obj_set_height(box, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
        made[i] = box;
    }
    for (i = 0; i < N; i++) {
        lv_obj_delete(made[i]);
    }
    styled_us = now_us() - t0;

    BOARD_LOGI("perf",
               "object probe (N=%d): plain=%.2f label=%.2f flex_pct=%.2f ms/obj",
               (int)N, (double)plain_us / 1000.0 / N,
               (double)label_us / 1000.0 / N, (double)styled_us / 1000.0 / N);

    /* 4) label 每一步分别计时（用未预热的字符串，避免缓存干扰） */
    {
        uint32_t t_create = 0, t_text = 0, t_font = 0, t_width = 0, t_mode = 0;
        uint32_t a, b;
        lv_obj_t *l = NULL;

        t0 = now_us();
        for (i = 0; i < N; i++) {
            a = now_us();
            l = lv_label_create(parent);
            b = now_us();
            t_create += b - a;

            a = now_us();
            lv_label_set_text(l, "欢迎使用拼音输入");
            b = now_us();
            t_text += b - a;

            a = now_us();
            lv_obj_set_style_text_font(l, ui_font_small(), 0);
            b = now_us();
            t_font += b - a;

            a = now_us();
            lv_obj_set_width(l, 120);
            b = now_us();
            t_width += b - a;

            a = now_us();
            lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
            b = now_us();
            t_mode += b - a;

            lv_obj_delete(l);
        }
        (void)t0;
        BOARD_LOGI("perf",
                   "label steps (ms/obj): create=%.2f set_text=%.2f set_font=%.2f "
                   "set_width=%.2f long_mode=%.2f",
                   (double)t_create / 1000.0 / N, (double)t_text / 1000.0 / N,
                   (double)t_font / 1000.0 / N, (double)t_width / 1000.0 / N,
                   (double)t_mode / 1000.0 / N);
    }
}
