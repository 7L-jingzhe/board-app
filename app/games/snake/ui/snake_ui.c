/**
 * @file snake_ui.c
 * @brief 贪吃蛇界面。规则来自 arcade_snake.h（Little World Arcade，MIT）。
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

#include "arcade/arcade_snake.h"
#include "board_core/board_storage.h"
#include "board_core/board_time.h"
#include "board_ui/ui_theme.h"
#include "board_ui/ui_widgets.h"

#define CELL 30

typedef struct {
    lv_obj_t *page;
    arcade_snake_t game;
    arcade_rng_t rng;
    int best;
    bool paused;
    lv_timer_t *timer;
    lv_obj_t *board;
    lv_obj_t *segments[ARCADE_SNAKE_MAX_LEN];
    lv_obj_t *food;
    lv_obj_t *eye1;
    lv_obj_t *eye2;
    lv_obj_t *score_label;
    lv_obj_t *best_label;
    lv_obj_t *len_label;
    lv_obj_t *modal;
} snake_app_t;

static snake_app_t *s_snake;

/* 棋盘为每个可能的网格位置预建一个蛇身段对象，从而避免蛇移动时
 * 每 90-220 ms 反复创建/删除对象带来的开销。 */
static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, int r,
                     lv_color_t color)
{
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

static void save_best(snake_app_t *app)
{
    if ((int)app->game.score > app->best) {
        app->best = (int)app->game.score;
        (void)board_storage_set_int("game.snake.best", app->best);
        (void)board_storage_save();
    }
}

static void update_stats(snake_app_t *app)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "得分 %u", app->game.score);
    lv_label_set_text(app->score_label, buf);
    snprintf(buf, sizeof(buf), "最高 %d", app->best);
    lv_label_set_text(app->best_label, buf);
    snprintf(buf, sizeof(buf), "长度 %d", app->game.length);
    lv_label_set_text(app->len_label, buf);
}

static void refresh(snake_app_t *app)
{
    for (int i = 0; i < ARCADE_SNAKE_MAX_LEN; i++) {
        lv_obj_add_flag(app->segments[i], LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < app->game.length; i++) {
        lv_obj_t *seg = app->segments[i];
        int x = app->game.body[i].x * CELL + 2;
        int y = app->game.body[i].y * CELL + 2;
        lv_obj_set_pos(seg, x, y);
        lv_obj_set_style_bg_color(seg, i == 0 ? lv_color_hex(0x0B7F70)
                                              : lv_color_hex(0x2FA37A), 0);
        lv_obj_remove_flag(seg, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_pos(app->food, app->game.food.x * CELL + 4,
                   app->game.food.y * CELL + 4);

    int hx = app->game.body[0].x * CELL;
    int hy = app->game.body[0].y * CELL;
    lv_obj_set_pos(app->eye1, hx + 7, hy + 8);
    lv_obj_set_pos(app->eye2, hx + 17, hy + 8);
    update_stats(app);
}

static void close_modal(snake_app_t *app)
{
    if (app->modal != NULL) {
        ui_modal_close(app->modal);
        app->modal = NULL;
    }
}

static void modal_delete_cb(lv_event_t *e)
{
    snake_app_t *app = lv_event_get_user_data(e);
    if (app != NULL) {
        app->modal = NULL;
    }
}

static lv_obj_t *make_modal(snake_app_t *app, const char *title, const char *detail)
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

static void restart_game(snake_app_t *app)
{
    close_modal(app);
    arcade_snake_init(&app->game, &app->rng);
    app->paused = false;
    if (app->timer != NULL) {
        lv_timer_set_period(app->timer, arcade_snake_step_ms(&app->game));
        lv_timer_resume(app->timer);
    }
    refresh(app);
}

static void restart_cb(lv_event_t *e)
{
    restart_game(lv_event_get_user_data(e));
}

static void resume_cb(lv_event_t *e)
{
    snake_app_t *app = lv_event_get_user_data(e);
    close_modal(app);
    app->paused = false;
    if (app->timer != NULL) {
        lv_timer_resume(app->timer);
    }
}

static void back_center_cb(lv_event_t *e)
{
    snake_app_t *app = lv_event_get_user_data(e);
    close_modal(app);
    (void)ui_app_manager_back();
}

static void show_pause(snake_app_t *app)
{
    if (app->modal != NULL || app->timer == NULL) {
        return;
    }
    lv_timer_pause(app->timer);
    app->paused = true;
    lv_obj_t *content = make_modal(app, "已暂停", "蛇在等你回来");
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

static void show_end(snake_app_t *app, bool won)
{
    if (app->timer != NULL) {
        lv_timer_pause(app->timer);
    }
    app->paused = true;
    save_best(app);
    update_stats(app);
    char detail[96];
    snprintf(detail, sizeof(detail), "本局得分 %u · 最高分 %d", app->game.score, app->best);
    lv_obj_t *content = make_modal(app, won ? "胜利！" : "游戏结束", detail);
    lv_obj_t *row = lv_obj_create(content);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 56);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 12, 0);
    ui_button_secondary_create(row, "返回游戏中心", back_center_cb, app);
    ui_button_primary_create(row, "再来一局", restart_cb, app);
}

static void tick_cb(lv_timer_t *timer)
{
    snake_app_t *app = lv_timer_get_user_data(timer);
    if (app == NULL || app->paused) {
        return;
    }
    int events = arcade_snake_step(&app->game, &app->rng);
    if (events & ARCADE_SNAKE_EVENT_MOVED) {
        refresh(app);
    }
    if (events & ARCADE_SNAKE_EVENT_ATE) {
        lv_timer_set_period(timer, arcade_snake_step_ms(&app->game));
    }
    if (events & (ARCADE_SNAKE_EVENT_DIED | ARCADE_SNAKE_EVENT_WON)) {
        lv_timer_pause(timer);
        show_end(app, (events & ARCADE_SNAKE_EVENT_WON) != 0);
    }
}

static void turn_cb(lv_event_t *e)
{
    snake_app_t *app = lv_event_get_user_data(e);
    arcade_dir_t dir = (arcade_dir_t)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));
    (void)arcade_snake_turn(&app->game, dir);
}

static void gesture_cb(lv_event_t *e)
{
    snake_app_t *app = lv_event_get_user_data(e);
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
    (void)arcade_snake_turn(&app->game, d);
}

static void pause_cb(lv_event_t *e)
{
    show_pause(lv_event_get_user_data(e));
}

static void restart_clicked_cb(lv_event_t *e)
{
    restart_game(lv_event_get_user_data(e));
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    (void)ui_app_manager_back();
}

static lv_obj_t *turn_button(lv_obj_t *parent, int x, const char *text,
                             arcade_dir_t dir, snake_app_t *app)
{
    lv_obj_t *btn = ui_icon_button_create(parent, text, turn_cb, app);
    lv_obj_set_pos(btn, x, 0);
    lv_obj_set_user_data(btn, (void *)(intptr_t)dir);
    return btn;
}

static lv_obj_t *snake_create(lv_obj_t *parent)
{
    snake_app_t *app = calloc(1, sizeof(*app));
    if (app == NULL) {
        return NULL;
    }
    s_snake = app;
    app->page = parent;
    app->best = board_storage_get_int("game.snake.best", 0);
    lv_obj_set_style_bg_color(parent, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    arcade_rng_seed(&app->rng, (uint32_t)board_time_now_ms());
    arcade_snake_init(&app->game, &app->rng);

    lv_obj_t *bar = ui_topbar_create(parent, "贪吃蛇", back_cb, app);
    ui_topbar_add_action(bar, "暂停", pause_cb, app);

    app->score_label = lv_label_create(parent);
    lv_obj_set_style_text_font(app->score_label, ui_font_small(), 0);
    lv_obj_set_style_text_color(app->score_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_pos(app->score_label, 300, 24);
    app->best_label = lv_label_create(parent);
    lv_obj_set_style_text_font(app->best_label, ui_font_small(), 0);
    lv_obj_set_style_text_color(app->best_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_pos(app->best_label, 440, 24);
    app->len_label = lv_label_create(parent);
    lv_obj_set_style_text_font(app->len_label, ui_font_small(), 0);
    lv_obj_set_style_text_color(app->len_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_pos(app->len_label, 560, 24);

    app->board = lv_obj_create(parent);
    lv_obj_remove_style_all(app->board);
    lv_obj_set_pos(app->board, 92, 92);
    lv_obj_set_size(app->board, ARCADE_SNAKE_COLS * CELL, ARCADE_SNAKE_ROWS * CELL);
    lv_obj_set_style_bg_color(app->board, UI_COLOR_BOARD_DARK, 0);
    lv_obj_set_style_bg_opa(app->board, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(app->board, 16, 0);
    lv_obj_add_flag(app->board, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(app->board, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(app->board, gesture_cb, LV_EVENT_GESTURE, app);

    for (int c = 1; c < ARCADE_SNAKE_COLS; c++) {
        box(app->board, c * CELL, 0, 1, ARCADE_SNAKE_ROWS * CELL, 0,
            lv_color_hex(0x273436));
    }
    for (int r = 1; r < ARCADE_SNAKE_ROWS; r++) {
        box(app->board, 0, r * CELL, ARCADE_SNAKE_COLS * CELL, 1, 0,
            lv_color_hex(0x273436));
    }

    for (int i = 0; i < ARCADE_SNAKE_MAX_LEN; i++) {
        lv_obj_t *seg = box(app->board, 0, 0, 26, 26, 7, lv_color_hex(0x2FA37A));
        lv_obj_add_flag(seg, LV_OBJ_FLAG_HIDDEN);
        app->segments[i] = seg;
    }
    app->food = box(app->board, 4, 4, 22, 22, 11, lv_color_hex(0xD9534F));
    app->eye1 = box(app->board, 0, 0, 4, 4, 2, UI_COLOR_WHITE);
    app->eye2 = box(app->board, 0, 0, 4, 4, 2, UI_COLOR_WHITE);

    lv_obj_t *controls = lv_obj_create(parent);
    lv_obj_remove_style_all(controls);
    lv_obj_set_pos(controls, 280, 506);
    lv_obj_set_size(controls, 300, 64);
    lv_obj_set_style_bg_opa(controls, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(controls, 12, 0);
    lv_obj_remove_flag(controls, LV_OBJ_FLAG_SCROLLABLE);
    turn_button(controls, 0, "左", ARCADE_DIR_LEFT, app);
    turn_button(controls, 68, "上", ARCADE_DIR_UP, app);
    turn_button(controls, 136, "下", ARCADE_DIR_DOWN, app);
    turn_button(controls, 204, "右", ARCADE_DIR_RIGHT, app);

    lv_obj_t *restart = ui_button_secondary_create(parent, "重开", restart_clicked_cb, app);
    lv_obj_set_pos(restart, 700, 506);
    lv_obj_set_size(restart, 140, 64);
    lv_obj_t *pause = ui_button_secondary_create(parent, "暂停", pause_cb, app);
    lv_obj_set_pos(pause, 856, 506);
    lv_obj_set_size(pause, 140, 64);

    lv_obj_t *hint = lv_label_create(parent);
    lv_label_set_text(hint, "方向键 / 滑动棋盘转向 · 撞墙或撞自己结束");
    lv_obj_set_style_text_font(hint, ui_font_caption(), 0);
    lv_obj_set_style_text_color(hint, UI_COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(hint, 92, 578);

    refresh(app);
    app->timer = lv_timer_create(tick_cb, arcade_snake_step_ms(&app->game), app);
    return parent;
}

static void snake_destroy(void)
{
    snake_app_t *app = s_snake;
    if (app == NULL) {
        return;
    }
    if (app->timer != NULL) {
        lv_timer_delete(app->timer);
    }
    save_best(app);
    free(app);
    s_snake = NULL;
}

static const board_app_desc_t s_snake_app = {
    .id = "snake",
    .title = "贪吃蛇",
    .subtitle = "越长越快，别撞墙",
    .parent_id = "games",
    .best_score_key = "game.snake.best",
    .icon = UI_ICON_GAME_SNAKE,
    .accent = 0x2FA37A,
    .order = 20,
    .show_on_home = false,
    .create = snake_create,
    .destroy = snake_destroy,
};

BOARD_APP_REGISTER(s_snake_app);
