/**
 * @file game2048_ui.c
 * @brief 2048 界面。规则来自 arcade_2048.h（Little World Arcade，MIT）。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "board_ui/ui_app_manager.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arcade/arcade_2048.h"
#include "board_core/board_storage.h"
#include "board_core/board_time.h"
#include "board_ui/ui_theme.h"
#include "board_ui/ui_widgets.h"

typedef struct {
    lv_obj_t *page;
    arcade_2048_t game;
    arcade_rng_t rng;
    int best;
    bool won_shown;
    lv_obj_t *score_label;
    lv_obj_t *best_label;
    lv_obj_t *moves_label;
    lv_obj_t *max_label;
    lv_obj_t *cells[ARCADE_2048_CELLS];
    lv_obj_t *cell_labels[ARCADE_2048_CELLS];
    lv_obj_t *modal;
} g2048_app_t;

static g2048_app_t *s_2048;

/* 16 个格子都是普通的 LVGL 对象；数值/计分规则仍留在
 * arcade_2048.c 中。界面只负责把 cells[] 映射为文本和颜色。 */
static lv_color_t tile_bg(uint16_t v)
{
    switch (v) {
    /* 灰-青绿体系：2/4 为浅灰绿，越大的数字越接近主色青绿/深青。 */
    case 2: return lv_color_hex(0x343C3C);
    case 4: return lv_color_hex(0x3D4847);
    case 8: return lv_color_hex(0x2F5F58);
    case 16: return lv_color_hex(0x2A7268);
    case 32: return lv_color_hex(0x1E8A7A);
    case 64: return lv_color_hex(0x0E9F8C);
    case 128: return lv_color_hex(0x0D8C7C);
    case 256: return lv_color_hex(0x0B7F70);
    case 512: return lv_color_hex(0x0A7165);
    case 1024: return lv_color_hex(0x086157);
    case 2048: return lv_color_hex(0x0C7F6F);
    default: return lv_color_hex(0x2A3132);
    }
}

static lv_color_t tile_fg(uint16_t v)
{
    return v <= 4 ? UI_COLOR_TEXT : UI_COLOR_WHITE;
}

static void update_stats(g2048_app_t *app)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%u", app->game.score);
    lv_label_set_text(app->score_label, buf);
    snprintf(buf, sizeof(buf), "%d", app->best);
    lv_label_set_text(app->best_label, buf);
    snprintf(buf, sizeof(buf), "%u", app->game.moves);
    lv_label_set_text(app->moves_label, buf);
    snprintf(buf, sizeof(buf), "%u", arcade_2048_max_tile(&app->game));
    lv_label_set_text(app->max_label, buf);
}

static void refresh_board(g2048_app_t *app)
{
    for (int i = 0; i < ARCADE_2048_CELLS; i++) {
        uint16_t v = app->game.cells[i];
        lv_obj_t *cell = app->cells[i];
        lv_obj_t *label = app->cell_labels[i];
        lv_obj_set_style_bg_color(cell, tile_bg(v), 0);
        if (v == 0) {
            lv_label_set_text(label, "");
            continue;
        }
        char buf[8];
        snprintf(buf, sizeof(buf), "%u", v);
        lv_label_set_text(label, buf);
        lv_obj_set_style_text_color(label, tile_fg(v), 0);
        const lv_font_t *font = ui_font_h2();
        if (v >= 100) {
            font = ui_font_body();
        }
        if (v >= 1000) {
            font = ui_font_small();
        }
        lv_obj_set_style_text_font(label, font, 0);
    }
    update_stats(app);
}

static void close_modal(g2048_app_t *app)
{
    if (app->modal != NULL) {
        ui_modal_close(app->modal);
        app->modal = NULL;
    }
}

static void modal_delete_cb(lv_event_t *e)
{
    g2048_app_t *app = lv_event_get_user_data(e);
    if (app != NULL) {
        app->modal = NULL;
    }
}

static lv_obj_t *make_modal(g2048_app_t *app, const char *title, const char *detail)
{
    lv_obj_t *content = ui_modal_create(app->page, title, 520);
    app->modal = content;
    lv_obj_add_event_cb(content, modal_delete_cb, LV_EVENT_DELETE, app);
    lv_obj_t *d = lv_label_create(content);
    lv_label_set_text(d, detail);
    lv_obj_set_style_text_font(d, ui_font_body(), 0);
    lv_obj_set_style_text_color(d, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_align(d, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(d, lv_pct(100));
    return content;
}

static void restart_game(g2048_app_t *app)
{
    close_modal(app);
    arcade_2048_init(&app->game, &app->rng);
    app->won_shown = false;
    refresh_board(app);
}

static void restart_cb(lv_event_t *e)
{
    restart_game(lv_event_get_user_data(e));
}

static void resume_cb(lv_event_t *e)
{
    close_modal(lv_event_get_user_data(e));
}

static void back_center_cb(lv_event_t *e)
{
    g2048_app_t *app = lv_event_get_user_data(e);
    close_modal(app);
    (void)ui_app_manager_back();
}

static void continue_win_cb(lv_event_t *e)
{
    close_modal(lv_event_get_user_data(e));
}

static void show_pause(g2048_app_t *app)
{
    if (app->modal != NULL) {
        return;
    }
    lv_obj_t *content = make_modal(app, "已暂停", "休息一下，随时继续");
    lv_obj_t *row = lv_obj_create(content);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 56);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 12, 0);
    ui_button_secondary_create(row, "返回游戏中心", back_center_cb, app);
    ui_button_secondary_create(row, "重新开始", restart_cb, app);
    ui_button_primary_create(row, "继续", resume_cb, app);
}

static void show_end(g2048_app_t *app, bool won)
{
    if (app->modal != NULL) {
        return;
    }
    char detail[96];
    snprintf(detail, sizeof(detail), "本局得分 %u · 最高分 %d", app->game.score, app->best);
    lv_obj_t *content = make_modal(app, won ? "达成 2048！" : "棋局结束", detail);
    lv_obj_t *row = lv_obj_create(content);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 56);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 12, 0);
    if (won) {
        ui_button_secondary_create(row, "重新开始", restart_cb, app);
        ui_button_primary_create(row, "继续挑战", continue_win_cb, app);
    } else {
        ui_button_secondary_create(row, "返回游戏中心", back_center_cb, app);
        ui_button_primary_create(row, "再来一局", restart_cb, app);
    }
}

static void do_move(g2048_app_t *app, arcade_dir_t dir)
{
    if (app->modal != NULL) {
        return;
    }
    arcade_2048_result_t r = arcade_2048_move(&app->game, dir, &app->rng);
    if (!r.moved) {
        if (app->game.over) {
            show_end(app, false);
        }
        return;
    }
    if (app->game.score > (uint32_t)app->best) {
        app->best = (int)app->game.score;
        (void)board_storage_set_int("game.2048.best", app->best);
        (void)board_storage_save();
    }
    refresh_board(app);
    if (app->game.won && !app->won_shown) {
        app->won_shown = true;
        show_end(app, true);
    } else if (app->game.over) {
        show_end(app, false);
    }
}

static void gesture_cb(lv_event_t *e)
{
    g2048_app_t *app = lv_event_get_user_data(e);
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    arcade_dir_t d;
    if (dir == LV_DIR_LEFT) {
        d = ARCADE_DIR_LEFT;
    } else if (dir == LV_DIR_RIGHT) {
        d = ARCADE_DIR_RIGHT;
    } else if (dir == LV_DIR_TOP) {
        d = ARCADE_DIR_UP;
    } else if (dir == LV_DIR_BOTTOM) {
        d = ARCADE_DIR_DOWN;
    } else {
        return;
    }
    do_move(app, d);
}

static void dir_cb(lv_event_t *e)
{
    g2048_app_t *app = lv_event_get_user_data(e);
    arcade_dir_t d = (arcade_dir_t)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));
    do_move(app, d);
}

static void new_game_cb(lv_event_t *e)
{
    restart_game(lv_event_get_user_data(e));
}

static void pause_cb(lv_event_t *e)
{
    show_pause(lv_event_get_user_data(e));
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    (void)ui_app_manager_back();
}

static lv_obj_t *stat_card(lv_obj_t *parent, int y, const char *caption,
                           const lv_font_t *font, lv_obj_t **value_out)
{
    lv_obj_t *card = ui_card_create(parent, 216, 100);
    lv_obj_set_pos(card, 0, y);
    lv_obj_t *c = lv_label_create(card);
    lv_label_set_text(c, caption);
    lv_obj_set_style_text_font(c, ui_font_caption(), 0);
    lv_obj_set_style_text_color(c, UI_COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(c, 16, 12);
    lv_obj_t *v = lv_label_create(card);
    lv_label_set_text(v, "0");
    lv_obj_set_style_text_font(v, font, 0);
    lv_obj_set_style_text_color(v, UI_COLOR_TEXT, 0);
    lv_obj_set_pos(v, 16, 38);
    *value_out = v;
    return card;
}

static lv_obj_t *dpad_button(lv_obj_t *parent, int x, int y, const char *text,
                             arcade_dir_t dir, g2048_app_t *app)
{
    lv_obj_t *btn = ui_icon_button_create(parent, text, dir_cb, app);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_user_data(btn, (void *)(intptr_t)dir);
    return btn;
}

static lv_obj_t *g2048_create(lv_obj_t *parent)
{
    g2048_app_t *app = calloc(1, sizeof(*app));
    if (app == NULL) {
        return NULL;
    }
    s_2048 = app;
    app->page = parent;
    app->best = board_storage_get_int("game.2048.best", 0);
    lv_obj_set_style_bg_color(parent, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    arcade_rng_seed(&app->rng, (uint32_t)board_time_now_ms());
    arcade_2048_init(&app->game, &app->rng);

    lv_obj_t *bar = ui_topbar_create(parent, "2048", back_cb, app);
    ui_topbar_add_action(bar, "暂停", pause_cb, app);
    ui_topbar_add_action(bar, "新游戏", new_game_cb, app);

    lv_obj_t *left = lv_obj_create(parent);
    lv_obj_remove_style_all(left);
    lv_obj_set_pos(left, 32, 96);
    lv_obj_set_size(left, 216, 472);
    lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(left, LV_OBJ_FLAG_SCROLLABLE);
    stat_card(left, 0, "分数", ui_font_number(), &app->score_label);
    stat_card(left, 116, "步数", ui_font_h2(), &app->moves_label);

    lv_obj_t *pad = lv_obj_create(left);
    lv_obj_remove_style_all(pad);
    lv_obj_set_pos(pad, 8, 228);
    lv_obj_set_size(pad, 200, 200);
    lv_obj_set_style_bg_opa(pad, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(pad, LV_OBJ_FLAG_SCROLLABLE);
    dpad_button(pad, 72, 0, "上", ARCADE_DIR_UP, app);
    dpad_button(pad, 16, 72, "左", ARCADE_DIR_LEFT, app);
    dpad_button(pad, 128, 72, "右", ARCADE_DIR_RIGHT, app);
    dpad_button(pad, 72, 144, "下", ARCADE_DIR_DOWN, app);

    ui_button_primary_create(left, "新游戏", new_game_cb, app);
    lv_obj_t *new_btn = lv_obj_get_child(left, lv_obj_get_child_count(left) - 1);
    lv_obj_set_pos(new_btn, 0, 432);
    lv_obj_set_size(new_btn, 216, 52);

    lv_obj_t *board = lv_obj_create(parent);
    lv_obj_remove_style_all(board);
    lv_obj_set_pos(board, 272, 88);
    lv_obj_set_size(board, 480, 480);
    lv_obj_set_style_bg_color(board, lv_color_hex(0x232829), 0);
    lv_obj_set_style_bg_opa(board, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(board, 20, 0);
    lv_obj_add_flag(board, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(board, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(board, gesture_cb, LV_EVENT_GESTURE, app);

    for (int i = 0; i < ARCADE_2048_CELLS; i++) {
        int r = i / ARCADE_2048_N;
        int c = i % ARCADE_2048_N;
        lv_obj_t *cell = lv_obj_create(board);
        lv_obj_remove_style_all(cell);
        lv_obj_set_pos(cell, 12 + c * 117, 12 + r * 117);
        lv_obj_set_size(cell, 105, 105);
        lv_obj_set_style_radius(cell, 16, 0);
        lv_obj_set_style_bg_color(cell, tile_bg(0), 0);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *label = lv_label_create(cell);
        lv_obj_set_style_text_font(label, ui_font_h2(), 0);
        lv_obj_center(label);
        app->cells[i] = cell;
        app->cell_labels[i] = label;
    }

    lv_obj_t *right = lv_obj_create(parent);
    lv_obj_remove_style_all(right);
    lv_obj_set_pos(right, 776, 96);
    lv_obj_set_size(right, 216, 472);
    lv_obj_set_style_bg_opa(right, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(right, LV_OBJ_FLAG_SCROLLABLE);
    stat_card(right, 0, "最高分", ui_font_number(), &app->best_label);
    stat_card(right, 116, "最大数字", ui_font_h2(), &app->max_label);

    lv_obj_t *hint = ui_card_create(right, 216, 240);
    lv_obj_set_pos(hint, 0, 232);
    lv_obj_t *h = lv_label_create(hint);
    lv_label_set_text(h, "滑动棋盘\n或使用方向键\n合并相同数字\n\n目标 2048");
    lv_obj_set_style_text_font(h, ui_font_small(), 0);
    lv_obj_set_style_text_color(h, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_line_space(h, 8, 0);
    lv_obj_set_pos(h, 16, 16);

    refresh_board(app);
    return parent;
}

static void g2048_destroy(void)
{
    g2048_app_t *app = s_2048;
    if (app == NULL) {
        return;
    }
    if (app->game.score > (uint32_t)app->best) {
        app->best = (int)app->game.score;
    }
    (void)board_storage_set_int("game.2048.best", app->best);
    (void)board_storage_save();
    free(app);
    s_2048 = NULL;
}

static const board_app_desc_t s_2048_app = {
    .id = "g2048",
    .title = "2048",
    .subtitle = "合并数字，挑战 2048",
    .parent_id = "games",
    .best_score_key = "game.2048.best",
    .icon = UI_ICON_GAME_2048,
    .accent = 0x7C8F8C,
    .order = 10,
    .show_on_home = false,
    .create = g2048_create,
    .destroy = g2048_destroy,
};

BOARD_APP_REGISTER(s_2048_app);
