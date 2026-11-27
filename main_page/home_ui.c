/**
 * @file home_ui.c
 * @brief 首页：时钟、品牌标题与自动生成的应用卡片。
 *
 * 卡片列表由 ui_app_manager_count/at 构建。新增一个带有 BOARD_APP_REGISTER
 * 的应用目录，就会自动出现一张新卡片，无需改动本文件。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "board_ui/ui_app_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_core/board_time.h"
#include "board_ui/ui_comm_session.h"
#include "board_ui/ui_icons.h"
#include "board_ui/ui_theme.h"
#include "board_ui/ui_widgets.h"
#include "lvgl/lvgl.h"

/* --- 首页顶栏的系统账号入口 --- */
static lv_obj_t *s_account_btn;
static lv_obj_t *s_account_label;
static lv_obj_t *s_account_dot;

static void account_clicked_cb(lv_event_t *e)
{
    (void)e;
    (void)ui_app_manager_open("account");
}

/** 顶栏账号状态跟着共享会话走（登录/退出后 1 秒内刷新）。 */
static void refresh_account_chip(void)
{
    if (s_account_label == NULL) {
        return;
    }
    if (comm_session_logged_in() && comm_session_user()[0] != '\0') {
        lv_label_set_text(s_account_label, comm_session_user());
        lv_obj_set_style_text_color(s_account_label, UI_COLOR_TEXT, 0);
        lv_obj_set_style_bg_color(s_account_dot, UI_COLOR_SUCCESS, 0);
    } else {
        lv_label_set_text(s_account_label, "未登录");
        lv_obj_set_style_text_color(s_account_label, UI_COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_bg_color(s_account_dot, UI_COLOR_TEXT_MUTED, 0);
    }
}

#ifndef BOARD_APP_VERSION
#define BOARD_APP_VERSION "1.0.0"
#endif

typedef struct {
    lv_obj_t *page;
    lv_obj_t *time_label;
    lv_obj_t *date_label;
    lv_timer_t *clock_timer;
} home_state_t;

static home_state_t *s_home;

static void update_clock(void)
{
    char time_buf[16];
    char date_buf[48];
    if (s_home == NULL) {
        return;
    }
    if (board_time_format_hhmm(time_buf, sizeof(time_buf)) == 0) {
        lv_label_set_text(s_home->time_label, time_buf);
    }
    if (board_time_format_date_cn(date_buf, sizeof(date_buf)) == 0) {
        lv_label_set_text(s_home->date_label, date_buf);
    }
}

static void clock_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    update_clock();
    refresh_account_chip();
}

static void card_pressed_cb(lv_event_t *e)
{
    lv_obj_t *card = lv_event_get_current_target(e);
    lv_obj_set_style_transform_scale(card, 250, 0);
}

static void card_released_cb(lv_event_t *e)
{
    lv_obj_t *card = lv_event_get_current_target(e);
    lv_obj_set_style_transform_scale(card, 256, 0);
}

static void card_clicked_cb(lv_event_t *e)
{
    const board_app_desc_t *app = lv_event_get_user_data(e);
    if (app != NULL) {
        (void)ui_app_manager_open(app->id);
    }
}

static void make_icon_tile_at(lv_obj_t *card, const board_app_desc_t *app, int x,
                              int y, int tile, int icon_size)
{
    lv_obj_t *box = lv_obj_create(card);
    lv_obj_remove_style_all(box);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, tile, tile);
    lv_obj_set_style_radius(box, 20, 0);
    lv_obj_set_style_bg_color(box, UI_COLOR_SURFACE_ALT, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_color_t accent = app->accent != 0 ? lv_color_hex(app->accent) : UI_COLOR_PRIMARY;
    lv_obj_t *icon = ui_icon_draw(box, app->icon, icon_size, accent);
    lv_obj_center(icon);
}

static void make_icon_tile(lv_obj_t *card, const board_app_desc_t *app, int tile,
                           int icon_size)
{
    make_icon_tile_at(card, app, 24, 24, tile, icon_size);
}

/* 紧凑卡片（4~6 个应用时两行并排）用横向排版：图标在左、文字在右，
 * 这样 158px 高也能放下标题、副标题和“打开”。 */
static void make_card_rows(lv_obj_t *card, const board_app_desc_t *app, int h)
{
    lv_color_t accent = app->accent != 0 ? lv_color_hex(app->accent) : UI_COLOR_PRIMARY;
    int tile = 64;
    int text_x = 16 + tile + 12;

    make_icon_tile_at(card, app, 16, 18, tile, 44);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, app->title != NULL ? app->title : app->id);
    lv_obj_set_style_text_font(title, ui_font_h3(), 0);
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);
    lv_obj_set_pos(title, text_x, 18);
    lv_obj_set_width(title, 304 - text_x - 24);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);

    lv_obj_t *sub = lv_label_create(card);
    lv_label_set_text(sub, app->subtitle != NULL ? app->subtitle : "");
    lv_obj_set_style_text_font(sub, ui_font_caption(), 0);
    lv_obj_set_style_text_color(sub, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_pos(sub, text_x, 52);
    lv_obj_set_width(sub, 304 - text_x - 24);
    lv_obj_set_height(sub, 42);
    lv_label_set_long_mode(sub, LV_LABEL_LONG_WRAP);

    lv_obj_t *action = lv_label_create(card);
    lv_label_set_text(action, "打开 ›");
    lv_obj_set_style_text_font(action, ui_font_caption(), 0);
    lv_obj_set_style_text_color(action, accent, 0);
    lv_obj_set_pos(action, 16, h - 30);
}

static lv_obj_t *make_card(lv_obj_t *parent, const board_app_desc_t *app, int h)
{
    lv_color_t accent = app->accent != 0 ? lv_color_hex(app->accent) : UI_COLOR_PRIMARY;
    lv_obj_t *card = ui_card_create(parent, 304, h);
    lv_obj_set_style_bg_color(card, lv_color_mix(accent, UI_COLOR_SURFACE_ALT, 60), 0);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, card_clicked_cb, LV_EVENT_CLICKED, (void *)app);
    lv_obj_add_event_cb(card, card_pressed_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(card, card_released_cb, LV_EVENT_RELEASED, NULL);

    int compact = (h < 280);
    int tile = compact ? 72 : 96;
    int icon = compact ? 48 : 64;

    if (h <= 200) {
        /* 4~6 个应用：两行并排，用横向排版的矮卡片。 */
        make_card_rows(card, app, h);
        return card;
    }
    make_icon_tile(card, app, tile, icon);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, app->title != NULL ? app->title : app->id);
    lv_obj_set_style_text_font(title, ui_font_h2(), 0);
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);
    lv_obj_set_pos(title, 24, compact ? 112 : 140);
    lv_obj_set_width(title, 256);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);

    lv_obj_t *sub = lv_label_create(card);
    lv_label_set_text(sub, app->subtitle != NULL ? app->subtitle : "");
    lv_obj_set_style_text_font(sub, ui_font_small(), 0);
    lv_obj_set_style_text_color(sub, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_pos(sub, 24, compact ? 156 : 184);
    lv_obj_set_width(sub, 256);
    lv_label_set_long_mode(sub, LV_LABEL_LONG_WRAP);

    lv_obj_t *action = lv_label_create(card);
    lv_label_set_text(action, "打开");
    lv_obj_set_style_text_font(action, ui_font_body(), 0);
    lv_obj_set_style_text_color(action, accent, 0);
    lv_obj_set_pos(action, 24, h - 44);
    return card;
}

static lv_obj_t *home_create(lv_obj_t *parent)
{
    home_state_t *h = calloc(1, sizeof(*h));
    if (h == NULL) {
        return NULL;
    }
    s_home = h;
    h->page = parent;
    lv_obj_set_style_bg_color(parent, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    /* 状态栏：09:42 / 11月25日 星期三 / 开发板信息。 */
    h->time_label = lv_label_create(parent);
    lv_label_set_text(h->time_label, "--:--");
    lv_obj_set_style_text_font(h->time_label, ui_font_h2(), 0);
    lv_obj_set_style_text_color(h->time_label, UI_COLOR_TEXT, 0);
    lv_obj_set_pos(h->time_label, 32, 18);

    h->date_label = lv_label_create(parent);
    lv_label_set_text(h->date_label, "加载中…");
    lv_obj_set_style_text_font(h->date_label, ui_font_small(), 0);
    lv_obj_set_style_text_color(h->date_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_pos(h->date_label, 132, 28);

    lv_obj_t *board = lv_label_create(parent);
    lv_label_set_text(board, "LVGL 9.2 · 1024×600");
    lv_obj_set_style_text_font(board, ui_font_caption(), 0);
    lv_obj_set_style_text_color(board, UI_COLOR_TEXT_MUTED, 0);
    lv_obj_align(board, LV_ALIGN_TOP_RIGHT, -232, 28);

    /* 系统账号入口：一个账号，聊天室与围棋通用。 */
    s_account_btn = lv_obj_create(parent);
    lv_obj_remove_style_all(s_account_btn);
    lv_obj_set_size(s_account_btn, 180, 44);
    lv_obj_align(s_account_btn, LV_ALIGN_TOP_RIGHT, -32, 12);
    lv_obj_set_style_bg_color(s_account_btn, UI_COLOR_SURFACE_ALT, 0);
    lv_obj_set_style_bg_opa(s_account_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_account_btn, 14, 0);
    lv_obj_add_flag(s_account_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_account_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_account_btn, account_clicked_cb, LV_EVENT_CLICKED, NULL);
    s_account_label = lv_label_create(s_account_btn);
    lv_label_set_text(s_account_label, "未登录");
    lv_obj_set_style_text_font(s_account_label, ui_font_caption(), 0);
    lv_obj_set_style_text_color(s_account_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(s_account_label, LV_ALIGN_LEFT_MID, 14, 0);
    s_account_dot = lv_obj_create(s_account_btn);
    lv_obj_remove_style_all(s_account_dot);
    lv_obj_set_size(s_account_dot, 8, 8);
    lv_obj_set_style_radius(s_account_dot, 4, 0);
    lv_obj_set_style_bg_color(s_account_dot, UI_COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_bg_opa(s_account_dot, LV_OPA_COVER, 0);
    lv_obj_align(s_account_dot, LV_ALIGN_RIGHT_MID, -14, 0);

    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "智慧生活");
    lv_obj_set_style_text_font(title, ui_font_h1(), 0);
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);
    lv_obj_set_pos(title, 32, 92);

    lv_obj_t *subtitle = lv_label_create(parent);
    lv_label_set_text(subtitle, "开发板应用中心");
    lv_obj_set_style_text_font(subtitle, ui_font_small(), 0);
    lv_obj_set_style_text_color(subtitle, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_pos(subtitle, 32, 138);

    /* 收集顶层应用。只需新增带 BOARD_APP_REGISTER 的
     * app/<name>/ui/<name>_ui.c；新增卡片无需修改本文件。 */
    const board_app_desc_t *apps[16];
    int app_count = 0;
    for (size_t i = 0; i < ui_app_manager_count() && app_count < 16; i++) {
        const board_app_desc_t *a = ui_app_manager_at(i);
        if (a != NULL && a->parent_id == NULL && a->show_on_home) {
            apps[app_count++] = a;
        }
    }

    lv_obj_t *grid = lv_obj_create(parent);
    lv_obj_remove_style_all(grid);
    lv_obj_set_pos(grid, 32, 200);
    /* 超过三个顶层应用时需要可滚动的两行网格。页脚保持在其下方，
     * 使新增卡片不会与版本/提示行重叠。 */
    lv_obj_set_size(grid, 960, app_count > 3 ? 372 : 300);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(grid, app_count > 3 ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(grid, 24, 0);
    lv_obj_set_style_pad_row(grid, 24, 0);
    if (app_count > 3) {
        lv_obj_set_scroll_dir(grid, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_AUTO);
    } else {
        lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    }

    if (app_count == 0) {
        ui_empty_state_show(parent, "暂无应用",
                            "在 board_app/app/ 下新建应用并 BOARD_APP_REGISTER");
    } else {
        /* 4~6 个应用时排两行矮卡片（横向排版），正好放进 grid 高度；
         * 超过 6 个才退回纵向滚动。 */
        int card_h = 300;
        if (app_count > 3) {
            card_h = app_count > 6 ? 220 : (372 - 24) / 2;
        }
        for (int i = 0; i < app_count; i++) {
            make_card(grid, apps[i], card_h);
        }
    }

    /* 页脚放在网格下方：两行卡片时网格到 572，页脚贴底避免压住卡片。 */
    int footer_y = app_count > 3 ? 570 : 552;
    lv_obj_t *hint = lv_label_create(parent);
    lv_label_set_text(hint, "点击卡片进入应用");
    lv_obj_set_style_text_font(hint, ui_font_caption(), 0);
    lv_obj_set_style_text_color(hint, UI_COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(hint, 32, footer_y);

    lv_obj_t *version = lv_label_create(parent);
    lv_label_set_text(version, "v" BOARD_APP_VERSION);
    lv_obj_set_style_text_font(version, ui_font_caption(), 0);
    lv_obj_set_style_text_color(version, UI_COLOR_TEXT_MUTED, 0);
    lv_obj_align(version, LV_ALIGN_TOP_RIGHT, -32, footer_y);

    update_clock();
    h->clock_timer = lv_timer_create(clock_timer_cb, 1000, NULL);
    return parent;
}

static void home_destroy(void)
{
    if (s_home == NULL) {
        return;
    }
    if (s_home->clock_timer != NULL) {
        lv_timer_delete(s_home->clock_timer);
    }
    free(s_home);
    s_home = NULL;
}

static const board_app_desc_t s_home_app = {
    .id = "home",
    .title = "首页",
    .subtitle = NULL,
    .parent_id = NULL,
    .best_score_key = NULL,
    .icon = UI_ICON_GENERIC,
    .accent = 0x0E9F8C,
    .order = -100,
    .show_on_home = false,
    .create = home_create,
    .destroy = home_destroy,
};

BOARD_APP_REGISTER(s_home_app);
