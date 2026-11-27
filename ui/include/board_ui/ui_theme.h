/**
 * @file ui_theme.h
 * @brief 整个 UI 使用的设计令牌与字体获取接口。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef UI_THEME_H
#define UI_THEME_H

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 暗色 + 青绿主题：
 *   - 背景/卡片/弹窗全部是中性深灰，不再出现大面积白色；
 *   - 主色保持青绿（teal），用于按钮、选中态、强调数字；
 *   - *_SOFT 令牌在暗色下表示“带一点主色的深底”，用于浅色高亮块。 */
#define UI_COLOR_BG             lv_color_hex(0x171B1C)
#define UI_COLOR_SURFACE        lv_color_hex(0x232829)
#define UI_COLOR_SURFACE_ALT    lv_color_hex(0x2D3334)
/* 暗色下青绿整体提亮一档：原 0E9F8C 在深灰底上偏暗，板上观感发闷。 */
#define UI_COLOR_PRIMARY        lv_color_hex(0x2EC4AE)
#define UI_COLOR_PRIMARY_DARK   lv_color_hex(0x22A894)
#define UI_COLOR_PRIMARY_SOFT   lv_color_hex(0x1D4A45)
#define UI_COLOR_ACCENT         lv_color_hex(0x5BD3C0)
#define UI_COLOR_ACCENT_SOFT    lv_color_hex(0x22463F)
#define UI_COLOR_PURPLE         lv_color_hex(0x7FB0A9)
#define UI_COLOR_PURPLE_SOFT    lv_color_hex(0x2A3B39)
#define UI_COLOR_SUCCESS        lv_color_hex(0x62C896)
#define UI_COLOR_SUCCESS_SOFT   lv_color_hex(0x203F33)
#define UI_COLOR_WARNING        lv_color_hex(0xE5BA5E)
#define UI_COLOR_WARNING_SOFT   lv_color_hex(0x3D3320)
#define UI_COLOR_DANGER         lv_color_hex(0xEE7570)
#define UI_COLOR_TEXT           lv_color_hex(0xF0F4F3)
#define UI_COLOR_TEXT_SECONDARY lv_color_hex(0xB8C3C1)
#define UI_COLOR_TEXT_MUTED     lv_color_hex(0x8A9794)
#define UI_COLOR_LINE           lv_color_hex(0x3A4243)
#define UI_COLOR_BOARD_DARK     lv_color_hex(0x0F1314)
#define UI_COLOR_WHITE          lv_color_hex(0xFFFFFF)
/* 暗色下“提亮/压暗”的基准色：与原浅色主题里 mix(accent, WHITE) 语义对应。 */
#define UI_COLOR_TINT           lv_color_hex(0xFFFFFF)

/** 通过 TinyTTF 加载 Noto Sans SC 子集；失败时回退到内置的 SimSun 16。 */
void ui_theme_init(const char *font_path);
void ui_theme_deinit(void);

const lv_font_t *ui_font_display(void);
const lv_font_t *ui_font_h1(void);
const lv_font_t *ui_font_h2(void);
const lv_font_t *ui_font_h3(void);
const lv_font_t *ui_font_body(void);
const lv_font_t *ui_font_small(void);
const lv_font_t *ui_font_caption(void);
const lv_font_t *ui_font_number_large(void);
const lv_font_t *ui_font_number(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_THEME_H */
