/**
 * @file ui_icons.h
 * @brief 用 LVGL 图元绘制的小型矢量图标（不依赖位图资源）。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef UI_ICONS_H
#define UI_ICONS_H

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_ICON_GENERIC = 0,
    UI_ICON_WEATHER,
    UI_ICON_GALLERY,
    UI_ICON_GAMES,
    UI_ICON_GAME_2048,
    UI_ICON_GAME_SNAKE,
    UI_ICON_GAME_BREAKOUT,
    UI_ICON_CHAT,
    UI_ICON_COMM,
    UI_ICON_WEIQI,   /* 围棋棋盘 */
} ui_icon_id_t;

/** 在一个 `size` x `size` 的新方形容器中绘制图标。 */
lv_obj_t *ui_icon_draw(lv_obj_t *parent, ui_icon_id_t id, int size, lv_color_t accent);

/** 天气状况图标（晴/云/雨/雪）。 */
lv_obj_t *ui_weather_icon_draw(lv_obj_t *parent, const char *condition, int size);

#ifdef __cplusplus
}
#endif

#endif /* UI_ICONS_H */
