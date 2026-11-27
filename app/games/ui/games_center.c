/**
 * @file games_center.c
 * @brief 游戏中心。卡片依据描述符生成，描述符的 parent_id="games"。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "board_ui/ui_app_manager.h"

#include <stdio.h>
#include <string.h>

#include "board_core/board_storage.h"
#include "board_ui/ui_icons.h"
#include "board_ui/ui_theme.h"
#include "board_ui/ui_widgets.h"

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, int r,
                     lv_color_t color)
{
    /* 应用卡片根据 parent_id=="games" 自动发现；新增一个带
     * BOARD_APP_REGISTER 的游戏目录即可多出一张卡片，无需改动本文件。 */
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static void preview_2048(lv_obj_t *parent)
{
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            int v = (r * 4 + c) % 5;
            /* 预览沿用灰-青绿体系：空格深灰，数字块由浅到深。 */
            lv_color_t col = v == 0 ? lv_color_hex(0x2B3436)
                             : v == 1 ? lv_color_hex(0x343C3C)
                             : v == 2 ? lv_color_hex(0x3D4847)
                             : v == 3 ? lv_color_hex(0x2A7268)
                                      : lv_color_hex(0x0E9F8C);
            box(parent, 8 + c * 40, 8 + r * 32, 34, 28, 6, col);
        }
    }
}

static void preview_snake(lv_obj_t *parent)
{
    for (int r = 0; r < 5; r++) {
        for (int c = 0; c < 7; c++) {
            lv_obj_t *cell = box(parent, 6 + c * 38, 6 + r * 28, 34, 24, 4,
                                 lv_color_hex(0x1A2426));
            lv_obj_set_style_border_color(cell, lv_color_hex(0x273436), 0);
            lv_obj_set_style_border_width(cell, 1, 0);
        }
    }
    static const int pts[][2] = { { 1, 3 }, { 2, 3 }, { 3, 3 }, { 4, 3 }, { 4, 2 }, { 5, 2 } };
    for (int i = 0; i < 6; i++) {
        box(parent, 6 + pts[i][0] * 38 + 2, 6 + pts[i][1] * 28 + 2, 30, 20, 5,
            i == 0 ? lv_color_hex(0x1F8A76) : lv_color_hex(0x2FA37A));
    }
    box(parent, 6 + 2 * 38 + 10, 6 + 1 * 28 + 6, 14, 14, 7, lv_color_hex(0xD9534F));
}

static void preview_breakout(lv_obj_t *parent)
{
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 7; c++) {
            lv_color_t col = r == 0 ? lv_color_hex(0x4E7F79)
                             : r == 1 ? lv_color_hex(0x5FA99E)
                             : r == 2 ? lv_color_hex(0x8FBFB6)
                                      : lv_color_hex(0xB9D2CE);
            box(parent, 8 + c * 37, 8 + r * 18, 32, 12, 3, col);
        }
    }
    box(parent, 92, 116, 86, 10, 5, lv_color_hex(0x9FB5B2));
    box(parent, 130, 86, 12, 12, 6, lv_color_hex(0xFFFFFF));
}

static void preview_for(lv_obj_t *parent, ui_icon_id_t icon)
{
    if (icon == UI_ICON_GAME_SNAKE) {
        preview_snake(parent);
    } else if (icon == UI_ICON_GAME_BREAKOUT) {
        preview_breakout(parent);
    } else {
        preview_2048(parent);
    }
}

static void card_clicked_cb(lv_event_t *e)
{
    const board_app_desc_t *app = lv_event_get_user_data(e);
    if (app != NULL) {
        (void)ui_app_manager_open(app->id);
    }
}

static void games_back_cb(lv_event_t *e)
{
    (void)e;
    (void)ui_app_manager_back();
}

/**
 * 游戏卡片。
 * 卡片宽度按游戏数量自适应：4 款时用 224 宽（一行排得下，不滑出屏幕），
 * 少于 4 款用 304 宽（更舒展）。卡片高度固定，内部纵向位置跟着宽度算。
 */
static lv_obj_t *make_game_card(lv_obj_t *parent, const board_app_desc_t *app,
                                int width)
{
    const int pad = width >= 280 ? 16 : 12;
    const int inner = width - pad * 2;
    const int preview_h = width >= 280 ? 150 : 104;
    const int title_y = pad + preview_h + 10;
    const int sub_y = title_y + 34;
    const int best_y = sub_y + 28;
    const int btn_y = 292;

    lv_obj_t *card = ui_card_create(parent, width, 360);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, card_clicked_cb, LV_EVENT_CLICKED, (void *)app);

    lv_obj_t *preview = lv_obj_create(card);
    lv_obj_remove_style_all(preview);
    lv_obj_set_pos(preview, pad, pad);
    lv_obj_set_size(preview, inner, preview_h);
    lv_obj_set_style_radius(preview, 16, 0);
    lv_obj_set_style_bg_color(preview, UI_COLOR_BOARD_DARK, 0);
    lv_obj_set_style_bg_opa(preview, LV_OPA_COVER, 0);
    lv_obj_remove_flag(preview, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(preview, LV_OBJ_FLAG_CLICKABLE);
    preview_for(preview, app->icon);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, app->title != NULL ? app->title : app->id);
    lv_obj_set_style_text_font(title, width >= 280 ? ui_font_h2() : ui_font_h3(), 0);
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);
    lv_obj_set_pos(title, pad + 4, title_y);

    lv_obj_t *sub = lv_label_create(card);
    lv_label_set_text(sub, app->subtitle != NULL ? app->subtitle : "");
    lv_obj_set_style_text_font(sub, ui_font_caption(), 0);
    lv_obj_set_style_text_color(sub, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_pos(sub, pad + 4, sub_y);
    lv_obj_set_width(sub, inner - 4);
    lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);

    int best = app->best_score_key != NULL
                   ? board_storage_get_int(app->best_score_key, 0)
                   : 0;
    char best_buf[48];
    snprintf(best_buf, sizeof(best_buf), "最高分 %d", best);
    lv_obj_t *best_label = lv_label_create(card);
    lv_label_set_text(best_label, best_buf);
    lv_obj_set_style_text_font(best_label, ui_font_small(), 0);
    lv_obj_set_style_text_color(best_label, UI_COLOR_PRIMARY, 0);
    lv_obj_set_pos(best_label, pad + 4, best_y);

    lv_obj_t *btn = ui_button_primary_create(card, "开始游戏", NULL, NULL);
    lv_obj_set_pos(btn, pad, btn_y);
    lv_obj_set_size(btn, inner, 52);
    lv_obj_add_event_cb(btn, card_clicked_cb, LV_EVENT_CLICKED, (void *)app);
    return card;
}

static lv_obj_t *games_create(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_t *bar = ui_topbar_create(parent, "小游戏", games_back_cb, NULL);
    (void)bar;

    lv_obj_t *count = lv_label_create(parent);
    lv_obj_set_style_text_font(count, ui_font_caption(), 0);
    lv_obj_set_style_text_color(count, UI_COLOR_TEXT_MUTED, 0);
    lv_obj_align(count, LV_ALIGN_TOP_RIGHT, -32, 28);

    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_pos(row, 32, 120);
    lv_obj_set_size(row, 960, 388);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 24, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    int total = 0;
    for (size_t i = 0; i < ui_app_manager_count(); i++) {
        const board_app_desc_t *app = ui_app_manager_at(i);
        if (app != NULL && app->parent_id != NULL &&
            strcmp(app->parent_id, "games") == 0) {
            total++;
        }
    }
    /* 960 宽、间距 24：4 款用 222 宽刚好一行放得下 */
    int card_w = total >= 4 ? 222 : 304;
    int n = 0;
    for (size_t i = 0; i < ui_app_manager_count(); i++) {
        const board_app_desc_t *app = ui_app_manager_at(i);
        if (app == NULL || app->parent_id == NULL || strcmp(app->parent_id, "games") != 0) {
            continue;
        }
        make_game_card(row, app, card_w);
        n++;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%d 款游戏", n);
    lv_label_set_text(count, buf);

    if (n == 0) {
        ui_empty_state_show(parent, "暂无游戏",
                            "把游戏核心注册为 parent_id=\"games\" 即可自动出现");
    }

    lv_obj_t *footer = lv_label_create(parent);
    lv_label_set_text(footer, "规则核心来自 Little World Arcade（MIT）· 围棋来自 weiqi.js（ISC）");
    lv_obj_set_style_text_font(footer, ui_font_caption(), 0);
    lv_obj_set_style_text_color(footer, UI_COLOR_TEXT_MUTED, 0);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -18);
    return parent;
}

static void games_destroy(void)
{
}

static const board_app_desc_t s_games_app = {
    .id = "games",
    .title = "小游戏",
    .subtitle = "2048 · 贪吃蛇 · 打砖块",
    .parent_id = NULL,
    .best_score_key = NULL,
    .icon = UI_ICON_GAMES,
    .accent = 0x3FB8A3,
    .order = 30,
    .show_on_home = true,
    .create = games_create,
    .destroy = games_destroy,
};

BOARD_APP_REGISTER(s_games_app);
