/**
 * @file breakout_ui.c
 * @brief 打砖块界面。规则来自 arcade_breakout.h（MIT）。
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

#include "arcade/arcade_breakout.h"
#include "board_core/board_storage.h"
#include "board_core/board_time.h"
#include "board_ui/ui_theme.h"
#include "board_ui/ui_widgets.h"

typedef struct {
    lv_obj_t *page;
    arcade_breakout_t game;
    int best;
    bool paused;
    lv_timer_t *timer;
    lv_obj_t *playfield;
    lv_obj_t *bricks[ARCADE_BREAKOUT_ROWS][ARCADE_BREAKOUT_COLS];
    lv_obj_t *paddle;
    lv_obj_t *ball;
    lv_obj_t *score_label;
    lv_obj_t *best_label;
    lv_obj_t *lives_label;
    lv_obj_t *ready_label;
    lv_obj_t *modal;
} breakout_app_t;

static breakout_app_t *s_breakout;

/* 内核以 8.8 定点数（1/256 px）存储坐标。仅在摆放 LVGL 对象时做换算；
 * 碰撞/反弹计算仍留在内核中完成。 */
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

static lv_color_t row_color(int row)
{
    /* 六档砖块由浅到深，全部落在灰-青绿色域内。 */
    static const uint32_t colors[6] = {
        0xB9D2CE, 0x8FBFB6, 0x5FA99E, 0x3F9A8C, 0x0E9F8C, 0x0B7F70
    };
    return lv_color_hex(colors[row % 6]);
}

static void save_best(breakout_app_t *app)
{
    if ((int)app->game.score > app->best) {
        app->best = (int)app->game.score;
        (void)board_storage_set_int("game.breakout.best", app->best);
        (void)board_storage_save();
    }
}

static void update_hud(breakout_app_t *app)
{
    char buf[48];
    snprintf(buf, sizeof(buf), "得分 %u", app->game.score);
    lv_label_set_text(app->score_label, buf);
    snprintf(buf, sizeof(buf), "最高 %d", app->best);
    lv_label_set_text(app->best_label, buf);
    snprintf(buf, sizeof(buf), "生命 %u", app->game.lives);
    lv_label_set_text(app->lives_label, buf);
}

static void update_positions(breakout_app_t *app)
{
    int px = (int)(app->game.paddle_x / ARCADE_FP_ONE);
    int bx = (int)(app->game.x / ARCADE_FP_ONE);
    int by = (int)(app->game.y / ARCADE_FP_ONE);
    lv_obj_set_pos(app->paddle, px, ARCADE_BREAKOUT_PADDLE_Y);
    lv_obj_set_pos(app->ball, bx - ARCADE_BREAKOUT_BALL_R,
                   by - ARCADE_BREAKOUT_BALL_R);
    for (int r = 0; r < ARCADE_BREAKOUT_ROWS; r++) {
        for (int c = 0; c < ARCADE_BREAKOUT_COLS; c++) {
            if (app->game.brick[r][c]) {
                lv_obj_remove_flag(app->bricks[r][c], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(app->bricks[r][c], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    if (app->game.state == ARCADE_STATE_READY) {
        lv_obj_remove_flag(app->ready_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(app->ready_label, LV_OBJ_FLAG_HIDDEN);
    }
    update_hud(app);
}

static void close_modal(breakout_app_t *app)
{
    if (app->modal != NULL) {
        ui_modal_close(app->modal);
        app->modal = NULL;
    }
}

static void modal_delete_cb(lv_event_t *e)
{
    breakout_app_t *app = lv_event_get_user_data(e);
    if (app != NULL) {
        app->modal = NULL;
    }
}

static lv_obj_t *make_modal(breakout_app_t *app, const char *title, const char *detail)
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

static void restart_game(breakout_app_t *app)
{
    close_modal(app);
    arcade_breakout_init(&app->game);
    app->paused = false;
    if (app->timer != NULL) {
        lv_timer_resume(app->timer);
    }
    update_positions(app);
}

static void restart_cb(lv_event_t *e)
{
    restart_game(lv_event_get_user_data(e));
}

static void resume_cb(lv_event_t *e)
{
    breakout_app_t *app = lv_event_get_user_data(e);
    close_modal(app);
    app->paused = false;
    if (app->timer != NULL) {
        lv_timer_resume(app->timer);
    }
}

static void back_center_cb(lv_event_t *e)
{
    breakout_app_t *app = lv_event_get_user_data(e);
    close_modal(app);
    (void)ui_app_manager_back();
}

static void show_pause(breakout_app_t *app)
{
    if (app->modal != NULL || app->timer == NULL) {
        return;
    }
    lv_timer_pause(app->timer);
    app->paused = true;
    lv_obj_t *content = make_modal(app, "已暂停", "挡板也需要休息");
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

static void show_end(breakout_app_t *app, bool won)
{
    if (app->timer != NULL) {
        lv_timer_pause(app->timer);
    }
    app->paused = true;
    save_best(app);
    update_hud(app);
    char detail[96];
    snprintf(detail, sizeof(detail), "本局得分 %u · 最高分 %d", app->game.score, app->best);
    lv_obj_t *content = make_modal(app, won ? "清屏！" : "游戏结束", detail);
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
    breakout_app_t *app = lv_timer_get_user_data(timer);
    if (app == NULL || app->paused) {
        return;
    }
    arcade_state_t before = app->game.state;
    arcade_breakout_tick(&app->game);
    update_positions(app);
    if (app->game.state == ARCADE_STATE_OVER && before != ARCADE_STATE_OVER) {
        lv_timer_pause(timer);
        show_end(app, false);
    } else if (app->game.state == ARCADE_STATE_WON && before != ARCADE_STATE_WON) {
        lv_timer_pause(timer);
        show_end(app, true);
    }
}

static void launch_cb(lv_event_t *e)
{
    breakout_app_t *app = lv_event_get_user_data(e);
    if (app != NULL) {
        arcade_breakout_launch(&app->game);
        update_positions(app);
    }
}

static void drag_cb(lv_event_t *e)
{
    breakout_app_t *app = lv_event_get_user_data(e);
    lv_obj_t *target = lv_event_get_current_target(e);
    lv_point_t p;
    lv_area_t area;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_obj_get_coords(target, &area);
    int x = p.x - area.x1;
    if (x < 0) {
        x = 0;
    }
    arcade_breakout_move_paddle(&app->game, x * ARCADE_FP_ONE);
    update_positions(app);
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

static lv_obj_t *breakout_create(lv_obj_t *parent)
{
    breakout_app_t *app = calloc(1, sizeof(*app));
    if (app == NULL) {
        return NULL;
    }
    s_breakout = app;
    app->page = parent;
    app->best = board_storage_get_int("game.breakout.best", 0);
    lv_obj_set_style_bg_color(parent, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    arcade_breakout_init(&app->game);

    lv_obj_t *bar = ui_topbar_create(parent, "打砖块", back_cb, app);
    ui_topbar_add_action(bar, "暂停", pause_cb, app);
    ui_topbar_add_action(bar, "重开", restart_clicked_cb, app);

    app->score_label = lv_label_create(parent);
    lv_obj_set_style_text_font(app->score_label, ui_font_small(), 0);
    lv_obj_set_style_text_color(app->score_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_pos(app->score_label, 300, 24);
    app->best_label = lv_label_create(parent);
    lv_obj_set_style_text_font(app->best_label, ui_font_small(), 0);
    lv_obj_set_style_text_color(app->best_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_pos(app->best_label, 440, 24);
    app->lives_label = lv_label_create(parent);
    lv_obj_set_style_text_font(app->lives_label, ui_font_small(), 0);
    lv_obj_set_style_text_color(app->lives_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_pos(app->lives_label, 560, 24);

    app->playfield = lv_obj_create(parent);
    lv_obj_remove_style_all(app->playfield);
    lv_obj_set_pos(app->playfield, 8, 88);
    lv_obj_set_size(app->playfield, ARCADE_BREAKOUT_W, ARCADE_BREAKOUT_H);
    lv_obj_set_style_bg_color(app->playfield, UI_COLOR_BOARD_DARK, 0);
    lv_obj_set_style_bg_opa(app->playfield, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(app->playfield, 16, 0);
    lv_obj_add_flag(app->playfield, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(app->playfield, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(app->playfield, launch_cb, LV_EVENT_CLICKED, app);
    lv_obj_add_event_cb(app->playfield, drag_cb, LV_EVENT_PRESSING, app);

    for (int r = 0; r < ARCADE_BREAKOUT_ROWS; r++) {
        for (int c = 0; c < ARCADE_BREAKOUT_COLS; c++) {
            int x, y, w, h;
            arcade_breakout_brick_rect(r, c, &x, &y, &w, &h);
            app->bricks[r][c] = box(app->playfield, x, y, w, h, 6, row_color(r));
        }
    }
    app->paddle = box(app->playfield, 0, ARCADE_BREAKOUT_PADDLE_Y,
                      ARCADE_BREAKOUT_PADDLE_W, ARCADE_BREAKOUT_PADDLE_H, 9,
                      lv_color_hex(0x9FB5B2));
    app->ball = box(app->playfield, 0, 0, ARCADE_BREAKOUT_BALL_R * 2,
                    ARCADE_BREAKOUT_BALL_R * 2, ARCADE_BREAKOUT_BALL_R,
                    lv_color_hex(0xE8EDEC));

    app->ready_label = lv_label_create(app->playfield);
    lv_label_set_text(app->ready_label, "点击发球");
    lv_obj_set_style_text_font(app->ready_label, ui_font_h3(), 0);
    lv_obj_set_style_text_color(app->ready_label, UI_COLOR_WHITE, 0);
    lv_obj_align(app->ready_label, LV_ALIGN_BOTTOM_MID, 0, -110);

    lv_obj_t *hint = lv_label_create(parent);
    lv_label_set_text(hint, "拖动挡板 · 点击发球");
    lv_obj_set_style_text_font(hint, ui_font_caption(), 0);
    lv_obj_set_style_text_color(hint, UI_COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(hint, 8, 566);

    update_positions(app);
    app->timer = lv_timer_create(tick_cb, ARCADE_BREAKOUT_TICK_MS, app);
    return parent;
}

static void breakout_destroy(void)
{
    breakout_app_t *app = s_breakout;
    if (app == NULL) {
        return;
    }
    if (app->timer != NULL) {
        lv_timer_delete(app->timer);
    }
    save_best(app);
    free(app);
    s_breakout = NULL;
}

static const board_app_desc_t s_breakout_app = {
    .id = "breakout",
    .title = "打砖块",
    .subtitle = "拖动挡板，清空砖块",
    .parent_id = "games",
    .best_score_key = "game.breakout.best",
    .icon = UI_ICON_GAME_BREAKOUT,
    .accent = 0x4E7F79,
    .order = 30,
    .show_on_home = false,
    .create = breakout_create,
    .destroy = breakout_destroy,
};

BOARD_APP_REGISTER(s_breakout_app);
