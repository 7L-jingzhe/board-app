/**
 * @file ui_icons.c
 * @brief 卡片与天气行使用的图元图标。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "board_ui/ui_icons.h"

#include <string.h>

#include "board_ui/ui_theme.h"

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, int r,
                     lv_color_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *ring(lv_obj_t *parent, int x, int y, int w, int h, int r,
                      lv_color_t color, int border)
{
    lv_obj_t *o = box(parent, x, y, w, h, r, color);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(o, color, 0);
    lv_obj_set_style_border_width(o, border, 0);
    lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
    return o;
}

static lv_obj_t *sub(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(o, LV_LAYOUT_NONE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static void draw_sun(lv_obj_t *root, int size, lv_color_t color)
{
    int r = size / 6;
    box(root, size / 2 - r, size / 2 - r, r * 2, r * 2, r, color);
    for (int i = 0; i < 8; i++) {
        static const int dx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
        static const int dy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
        int len = size / 7;
        int cx = size / 2 + dx[i] * (size / 3);
        int cy = size / 2 + dy[i] * (size / 3);
        lv_obj_t *ray = box(root, cx - len / 2, cy - len / 2, len, len, len / 2, color);
        lv_obj_set_style_transform_scale(ray, 60, 0);
    }
}

static void draw_cloud(lv_obj_t *root, int size, lv_color_t color)
{
    int w = size * 2 / 3;
    int h = size / 4;
    int x = size / 6;
    int y = size / 2;
    box(root, x + w / 10, y - h, w * 4 / 10, h * 2, h, color);
    box(root, x + w * 3 / 10, y - h * 3 / 2, w * 4 / 10, h * 2, h, color);
    box(root, x + w * 5 / 10, y - h, w * 4 / 10, h * 2, h, color);
    box(root, x + w / 10, y - h / 2, w * 8 / 10, h, h / 2, color);
}

static void draw_rain(lv_obj_t *root, int size, lv_color_t color)
{
    draw_cloud(root, size, color);
    for (int i = 0; i < 3; i++) {
        lv_obj_t *drop = box(root, size * 35 / 100 + i * size / 5,
                             size * 78 / 100, size / 18, size / 6, 2, color);
        lv_obj_set_style_transform_rotation(drop, 200, 0);
    }
}

static void draw_snow(lv_obj_t *root, int size, lv_color_t color)
{
    draw_cloud(root, size, color);
    for (int i = 0; i < 3; i++) {
        box(root, size * 36 / 100 + i * size / 5, size * 80 / 100,
            size / 10, size / 10, size / 10, color);
    }
}

lv_obj_t *ui_icon_draw(lv_obj_t *parent, ui_icon_id_t id, int size, lv_color_t accent)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, size, size);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(root, LV_LAYOUT_NONE);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    switch (id) {
    case UI_ICON_WEATHER: {
        /* 太阳在左上、云在右下：与整朵满尺寸云遮住太阳相比，
         * 这两种形状在 48-64 px 下依然清晰可辨。 */
        int sun_size = size * 55 / 100;
        int cloud_size = size * 78 / 100;
        lv_obj_t *sun = sub(root, 0, 0, sun_size, sun_size);
        draw_sun(sun, sun_size, accent);
        lv_obj_t *cloud = sub(root, size * 22 / 100, size * 28 / 100,
                              cloud_size, cloud_size);
        draw_cloud(cloud, cloud_size, lv_color_hex(0xC3D2D0));
        break;
    }
    case UI_ICON_GALLERY:
        ring(root, size / 8, size / 4, size * 3 / 4, size / 2, size / 10, accent, 3);
        box(root, size * 22 / 100, size * 34 / 100, size / 7, size / 7, size / 14,
            UI_COLOR_PRIMARY);
        {
            lv_obj_t *m1 = box(root, size * 26 / 100, size * 58 / 100,
                               size * 28 / 100, size * 28 / 100, 4, accent);
            lv_obj_set_style_transform_rotation(m1, 450, 0);
            lv_obj_t *m2 = box(root, size * 48 / 100, size * 62 / 100,
                               size * 22 / 100, size * 22 / 100, 4,
                               lv_color_mix(accent, UI_COLOR_SURFACE, 90));
            lv_obj_set_style_transform_rotation(m2, 450, 0);
        }
        break;
    case UI_ICON_GAMES:
        box(root, size * 18 / 100, size * 18 / 100, size * 28 / 100, size * 28 / 100,
            size / 12, accent);
        box(root, size * 54 / 100, size * 18 / 100, size * 28 / 100, size * 28 / 100,
            size / 12, UI_COLOR_ACCENT);
        box(root, size * 18 / 100, size * 54 / 100, size * 28 / 100, size * 28 / 100,
            size / 12, lv_color_mix(accent, UI_COLOR_SURFACE, 90));
        box(root, size * 54 / 100, size * 54 / 100, size * 28 / 100, size * 28 / 100,
            size / 12, accent);
        break;
    case UI_ICON_GAME_2048:
        for (int r = 0; r < 2; r++) {
            for (int c = 0; c < 2; c++) {
                box(root, size * (12 + c * 40) / 100, size * (12 + r * 40) / 100,
                    size * 34 / 100, size * 34 / 100, size / 14,
                    (r + c) % 2 == 0 ? accent : lv_color_mix(accent, UI_COLOR_SURFACE, 90));
            }
        }
        break;
    case UI_ICON_GAME_SNAKE:
        box(root, size * 18 / 100, size * 52 / 100, size * 22 / 100, size * 22 / 100,
            size / 14, accent);
        box(root, size * 38 / 100, size * 52 / 100, size * 22 / 100, size * 22 / 100,
            size / 14, accent);
        box(root, size * 58 / 100, size * 52 / 100, size * 22 / 100, size * 22 / 100,
            size / 14, accent);
        box(root, size * 58 / 100, size * 32 / 100, size * 22 / 100, size * 22 / 100,
            size / 14, accent);
        box(root, size * 78 / 100, size * 32 / 100, size / 12, size / 12, size / 24,
            UI_COLOR_DANGER);
        break;
    case UI_ICON_GAME_BREAKOUT:
        for (int i = 0; i < 3; i++) {
            box(root, size * (12 + i * 28) / 100, size * 20 / 100,
                size * 24 / 100, size * 12 / 100, 3,
                i == 0 ? accent : lv_color_mix(accent, UI_COLOR_SURFACE, 90));
        }
        box(root, size * 38 / 100, size * 78 / 100, size * 28 / 100, size / 10,
            size / 20, UI_COLOR_WHITE);
        box(root, size * 46 / 100, size * 45 / 100, size / 10, size / 10, size / 20,
            UI_COLOR_WHITE);
        break;
    case UI_ICON_CHAT:
        /* 对话气泡：圆角矩形 + 左下角小尾巴 + 三个点。 */
        box(root, size * 10 / 100, size * 16 / 100, size * 80 / 100,
            size * 56 / 100, size / 6, accent);
        box(root, size * 22 / 100, size * 62 / 100, size * 16 / 100,
            size * 18 / 100, size / 10, accent);
        for (int i = 0; i < 3; i++) {
            box(root, size * (32 + i * 15) / 100, size * (40 - 4) / 100,
                size / 9, size / 9, size / 18, UI_COLOR_SURFACE);
        }
        break;
    case UI_ICON_COMM:
        /* 两个人的对话：大气泡 + 小气泡，和 AI 聊天的单个气泡区分开 */
        box(root, size * 6 / 100, size * 14 / 100, size * 56 / 100,
            size * 40 / 100, size / 8, accent);
        box(root, size * 14 / 100, size * 50 / 100, size * 12 / 100,
            size * 14 / 100, size / 14, accent);
        box(root, size * 40 / 100, size * 46 / 100, size * 54 / 100,
            size * 38 / 100, size / 8,
            lv_color_mix(accent, UI_COLOR_SURFACE, 55));
        box(root, size * 74 / 100, size * 80 / 100, size * 12 / 100,
            size * 14 / 100, size / 14,
            lv_color_mix(accent, UI_COLOR_SURFACE, 55));
        break;
    case UI_ICON_WEIQI:
        /* 棋盘网格 + 一黑一白两子 */
        box(root, size * 12 / 100, size * 12 / 100, size * 76 / 100,
            size * 76 / 100, size / 14, lv_color_hex(0xD9B382));
        {
            int i;
            for (i = 0; i < 4; i++) {
                int p = size * 16 / 100 + i * size * 23 / 100;
                box(root, p, size * 14 / 100, size * 2 / 100, size * 72 / 100, 0,
                    lv_color_hex(0x6B5233));
                box(root, size * 14 / 100, p, size * 72 / 100, size * 2 / 100, 0,
                    lv_color_hex(0x6B5233));
            }
        }
        box(root, size * 24 / 100, size * 24 / 100, size * 26 / 100,
            size * 26 / 100, size, lv_color_hex(0x14171A));
        box(root, size * 50 / 100, size * 50 / 100, size * 26 / 100,
            size * 26 / 100, size, lv_color_hex(0xF7F9FB));
        break;
    case UI_ICON_GENERIC:
    default:
        ring(root, size / 8, size / 8, size * 3 / 4, size * 3 / 4, size / 5, accent, 3);
        box(root, size * 42 / 100, size * 42 / 100, size / 6, size / 6, size / 12, accent);
        break;
    }
    return root;
}

lv_obj_t *ui_weather_icon_draw(lv_obj_t *parent, const char *condition, int size)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, size, size);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(root, LV_LAYOUT_NONE);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    if (condition == NULL) {
        condition = "";
    }
    if (strstr(condition, "雨") != NULL) {
        draw_rain(root, size, lv_color_hex(0x5FD0C2));
    } else if (strstr(condition, "雪") != NULL) {
        draw_snow(root, size, lv_color_hex(0xC3D6D3));
    } else if (strstr(condition, "云") != NULL || strstr(condition, "阴") != NULL ||
               strstr(condition, "雾") != NULL || strstr(condition, "霾") != NULL) {
        draw_cloud(root, size, lv_color_hex(0xB8C7C5));
    } else {
        draw_sun(root, size, lv_color_hex(0xE8BE5A));
    }
    return root;
}
