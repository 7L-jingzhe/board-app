/**
 * @file ui_theme.c
 * @brief 字体加载与主题初始化。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "board_ui/ui_theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_core/board_log.h"

LV_FONT_DECLARE(lv_font_simsun_16_cjk);
LV_FONT_DECLARE(lv_font_montserrat_28);
LV_FONT_DECLARE(lv_font_montserrat_48);

static const lv_font_t *s_display;
static const lv_font_t *s_h1;
static const lv_font_t *s_h2;
static const lv_font_t *s_h3;
static const lv_font_t *s_body;
static const lv_font_t *s_small;
static const lv_font_t *s_caption;
static int s_initialized;

/* 字体文件一次性读进 RAM，5 个字号共用同一份数据。
 *
 * 为什么不用 lv_tiny_ttf_create_file()：TinyTTF 会为**每个字形**按偏移去
 * lv_fs 读字体表，而 lv_conf 里 LV_FS_STDIO_CACHE_SIZE 必须保持 0
 * （9.2 的 lv_fs_read_cached 遇到短读会死循环）。字体子集 2.2 MB、
 * 一屏上百个汉字就是上千次 fread，板上实测打开一页要 40 秒。
 * 放进内存后 stb 直接读 RAM，同一页面降到毫秒级。 */
static void *s_font_data;
static size_t s_font_size;

static int load_font_data(const char *path)
{
    FILE *f;
    long total;
    void *buf;

    if (path == NULL || path[0] == '\0') {
        return -1;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (total = ftell(f)) <= 0) {
        fclose(f);
        return -1;
    }
    rewind(f);
    buf = malloc((size_t)total);
    if (buf == NULL) {
        fclose(f);
        return -1;
    }
    if (fread(buf, 1, (size_t)total, f) != (size_t)total) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    s_font_data = buf;
    s_font_size = (size_t)total;
    BOARD_LOGI("ui", "font cached in RAM: %ld bytes", total);
    return 0;
}

/* 每个字号单独给字形缓存条目数：大字号位图大、数量少，小字号相反。
 * 这样总内存可控，又不会因为共享同一个宏而把小字号撑爆。 */
static size_t cache_entries_for(int size)
{
    if (size <= 18) {
        return 512;
    }
    if (size <= 22) {
        return 384;
    }
    if (size <= 26) {
        return 256;
    }
    if (size <= 34) {
        return 128;
    }
    return 96;
}

static lv_font_t *try_load(const char *path, int size)
{
    lv_font_t *font = NULL;

    if (s_font_data != NULL) {
        /* 关键：LV_FONT_KERNING_NONE。
         * 字体带头 kernel 表时，TinyTTF 会对**每一对相邻字符**调用
         * stbtt_GetGlyphKernAdvance，那是逐对 seek+解析 kern 子表的昂贵操作。
         * 板上实测：label 改一次字体/宽度/换行模式 = 重新量测整段文本，
         * 带 kerning 要 9.9 ms，关掉后降到微秒级。
         * 中文排版本来就不用字距对，关掉没有任何视觉损失。 */
        font = lv_tiny_ttf_create_data_ex(s_font_data, s_font_size, size,
                                          LV_FONT_KERNING_NONE,
                                          cache_entries_for(size));
    }
    if (font == NULL && path != NULL && path[0] != '\0') {
        /* 回退：直接从文件流式读取（慢，但字体过大/内存不足时仍可用）。 */
        char lv_path[4200];
        snprintf(lv_path, sizeof(lv_path), "A:%s", path);
        font = lv_tiny_ttf_create_file_ex(lv_path, size, LV_FONT_KERNING_NONE,
                                          cache_entries_for(size));
    }
    if (font != NULL) {
        /* 回退字体负责 LVGL 的内置符号（键盘的退格/回车/键盘/方向键等
         * LV_SYMBOL_*，属于私用区码点）。Noto Sans SC 里没有这些字形，
         * 回退到 Montserrat 可以一次命中，避免每次布局都去查一圈。 */
        font->fallback = &lv_font_montserrat_16;
    }
    return font;
}

static const lv_font_t *pick(lv_font_t *loaded, const lv_font_t *fallback)
{
    return loaded != NULL ? loaded : fallback;
}

void ui_theme_init(const char *font_path)
{
    lv_font_t *f18;
    lv_font_t *f22;
    lv_font_t *f26;
    lv_font_t *f34;
    lv_font_t *f48;

    if (s_initialized) {
        return;
    }
    if (load_font_data(font_path) != 0) {
        BOARD_LOGW("ui", "cannot read font into RAM, stream from file: %s",
                   font_path != NULL ? font_path : "(null)");
    }
    f18 = try_load(font_path, 18);
    f22 = try_load(font_path, 22);
    f26 = try_load(font_path, 26);
    f34 = try_load(font_path, 34);
    f48 = try_load(font_path, 48);
    if (f18 == NULL) {
        BOARD_LOGW("ui", "CJK font not loaded, fallback to SimSun 16: %s",
                   font_path != NULL ? font_path : "(null)");
    } else {
        BOARD_LOGI("ui", "loaded CJK font: %s", font_path);
    }

    s_display = pick(f48, &lv_font_simsun_16_cjk);
    s_h1 = pick(f34, &lv_font_simsun_16_cjk);
    s_h2 = pick(f26, &lv_font_simsun_16_cjk);
    s_h3 = pick(f26, &lv_font_simsun_16_cjk);
    s_body = pick(f22, &lv_font_simsun_16_cjk);
    s_small = pick(f18, &lv_font_simsun_16_cjk);
    s_caption = pick(f18, &lv_font_simsun_16_cjk);
    s_initialized = 1;
}

void ui_theme_deinit(void)
{
    /* 字体由 LVGL 的 TinyTTF 缓存持有；进程通常不调用本函数就退出。
     * 保留该函数以便测试/将来做热重载。 */
    s_display = NULL;
    s_h1 = NULL;
    s_h2 = NULL;
    s_h3 = NULL;
    s_body = NULL;
    s_small = NULL;
    s_caption = NULL;
    s_initialized = 0;
}

const lv_font_t *ui_font_display(void) { return s_display; }
const lv_font_t *ui_font_h1(void) { return s_h1; }
const lv_font_t *ui_font_h2(void) { return s_h2; }
const lv_font_t *ui_font_h3(void) { return s_h3; }
const lv_font_t *ui_font_body(void) { return s_body; }
const lv_font_t *ui_font_small(void) { return s_small; }
const lv_font_t *ui_font_caption(void) { return s_caption; }
const lv_font_t *ui_font_number_large(void) { return &lv_font_montserrat_48; }
const lv_font_t *ui_font_number(void) { return &lv_font_montserrat_28; }
