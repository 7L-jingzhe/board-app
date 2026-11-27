/**
 * @file weiqi_ui.c
 * @brief 联网围棋界面：选择好友邀请 -> 对方接受 -> 双方在服务器判定的棋盘上对弈。
 *
 * 三种视图：
 *   1) 未登录：提示先去「聊天室」登录（连接与登录态由 comm_session 共享）
 *   2) 大厅：好友列表 + 棋盘路数选择（9/13/19），点「邀请」发起对局
 *   3) 对局：棋盘 + 状态（执子颜色/提子/轮次）+ 停一手/认输/退出
 *
 * 服务器是规则权威：本地落子只发请求，收到服务器下发的 GAME_STATE 才更新棋盘，
 * 因此两边永远一致，客户端也不需要实现规则（规则核心用于本地显示与单测）。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "board_ui/ui_app_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_core/board_log.h"
#include "board_ui/ui_comm_session.h"
#include "board_ui/ui_icons.h"
#include "board_ui/ui_theme.h"
#include "board_ui/ui_widgets.h"
#include "weiqi/weiqi.h"

#define BOARD_PX 512
#define BOARD_X 16
#define BOARD_Y 76
#define INFO_X 544
#define INFO_W 464
#define INFO_H 512

typedef enum {
    WQ_VIEW_NEED_LOGIN = 0,
    WQ_VIEW_LOBBY,
    WQ_VIEW_GAME,
} wq_view_t;

typedef struct {
    lv_obj_t *page;

    /* 三个视图容器 */
    lv_obj_t *login_box;
    lv_obj_t *lobby_box;
    lv_obj_t *game_box;

    /* 大厅 */
    lv_obj_t *friend_list;
    lv_obj_t *size_hint;
    lv_obj_t *size_btns[3];  /* 9 / 13 / 19 三个按钮，用来切换高亮 */
    int size_btn_count;
    int size_choice; /* 9 / 13 / 19 */

    /* 棋盘 */
    lv_obj_t *board_area;
    lv_obj_t *stone[WEIQI_CELLS];
    lv_obj_t *lines[WEIQI_MAX * 2];
    int line_count;
    int cell;
    int margin;

    /* 信息面板 */
    lv_obj_t *info_title;
    lv_obj_t *info_detail;
    lv_obj_t *info_turn;
    lv_obj_t *info_result;
    lv_obj_t *btn_pass;
    lv_obj_t *btn_resign;
    lv_obj_t *btn_leave;

    /* 状态 */
    comm_client_t *client;
    wq_view_t view;
    char opponent[COMM_MAX_NAME + 1];
    int my_color;
    bool in_game;
    int invited_from_ok;
    weiqi_t g;
} wq_app_t;

static wq_app_t *s_app;

/* -------------------------------------------------------------------------- */
/* 小工具                                                                      */
/* -------------------------------------------------------------------------- */

static lv_obj_t *label_at(lv_obj_t *parent, const char *text, const lv_font_t *font,
                          lv_color_t color, int x, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text != NULL ? text : "");
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

static void show_view(wq_app_t *app, wq_view_t view)
{
    app->view = view;
    if (app->login_box != NULL) {
        if (view == WQ_VIEW_NEED_LOGIN) {
            lv_obj_remove_flag(app->login_box, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(app->login_box, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (app->lobby_box != NULL) {
        if (view == WQ_VIEW_LOBBY) {
            lv_obj_remove_flag(app->lobby_box, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(app->lobby_box, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (app->game_box != NULL) {
        if (view == WQ_VIEW_GAME) {
            lv_obj_remove_flag(app->game_box, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(app->game_box, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* -------------------------------------------------------------------------- */
/* 棋盘                                                                        */
/* -------------------------------------------------------------------------- */

static void compute_geometry(wq_app_t *app)
{
    int size = app->g.size > 0 ? app->g.size : app->size_choice;

    app->cell = BOARD_PX / size;
    app->margin = (BOARD_PX - app->cell * (size - 1)) / 2;
}

/** 重新创建线（切对局/换路数时调用）。 */
static void rebuild_board(wq_app_t *app)
{
    static lv_point_precise_t h_pts[WEIQI_MAX][2];
    static lv_point_precise_t v_pts[WEIQI_MAX][2];
    int size = app->g.size;
    int i;
    int k = 0;

    if (app->board_area == NULL) {
        return;
    }
    lv_obj_clean(app->board_area);
    app->line_count = 0;
    compute_geometry(app);

    for (i = 0; i < size; i++) {
        int pos = app->margin + i * app->cell;
        lv_obj_t *line;

        h_pts[i][0].x = app->margin;
        h_pts[i][0].y = pos;
        h_pts[i][1].x = app->margin + (size - 1) * app->cell;
        h_pts[i][1].y = pos;
        line = lv_line_create(app->board_area);
        lv_line_set_points(line, h_pts[i], 2);
        lv_obj_set_style_line_width(line, 1, 0);
        lv_obj_set_style_line_color(line, UI_COLOR_LINE, 0);
        app->lines[app->line_count++] = line;

        v_pts[i][0].x = pos;
        v_pts[i][0].y = app->margin;
        v_pts[i][1].x = pos;
        v_pts[i][1].y = app->margin + (size - 1) * app->cell;
        line = lv_line_create(app->board_area);
        lv_line_set_points(line, v_pts[i], 2);
        lv_obj_set_style_line_width(line, 1, 0);
        lv_obj_set_style_line_color(line, UI_COLOR_LINE, 0);
        app->lines[app->line_count++] = line;
    }

    /* 棋子：全部预建，按局面显示/隐藏 */
    {
        int stone_px = app->cell - 4;
        if (stone_px < 8) {
            stone_px = 8;
        }
        for (k = 0; k < WEIQI_CELLS; k++) {
            lv_obj_t *o = lv_obj_create(app->board_area);
            lv_obj_remove_style_all(o);
            lv_obj_set_size(o, stone_px, stone_px);
            lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(o, UI_COLOR_TEXT, 0);
            lv_obj_set_style_border_width(o, 1, 0);
            lv_obj_set_style_border_color(o, UI_COLOR_TEXT_MUTED, 0);
            lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
            app->stone[k] = o;
        }
    }
}

/** 按当前局面刷新棋子与最后一手标记。 */
static void refresh_board(wq_app_t *app)
{
    int size = app->g.size;
    int x;
    int y;

    if (app->board_area == NULL || size == 0) {
        return;
    }
    for (y = 0; y < WEIQI_MAX; y++) {
        for (x = 0; x < WEIQI_MAX; x++) {
            int k = y * WEIQI_MAX + x;
            lv_obj_t *o = app->stone[k];
            if (o == NULL) {
                continue;
            }
            if (x >= size || y >= size || app->g.cell[y][x] == WEIQI_EMPTY) {
                lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
                continue;
            }
            lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(o, app->g.cell[y][x] == WEIQI_BLACK
                                             ? lv_color_hex(0x1A1D21)
                                             : lv_color_hex(0xF2F4F7),
                                      0);
            lv_obj_set_pos(o, app->margin + x * app->cell - (app->cell - 4) / 2,
                           app->margin + y * app->cell - (app->cell - 4) / 2);
            /* 最后一手：描一圈强调色 */
            lv_obj_set_style_outline_width(
                o, (app->g.last_x == x && app->g.last_y == y) ? 2 : 0, 0);
            lv_obj_set_style_outline_color(o, UI_COLOR_ACCENT, 0);
            lv_obj_set_style_outline_pad(o, 1, 0);
        }
    }
}

static void update_info(wq_app_t *app)
{
    char buf[160];

    if (app->info_title != NULL) {
        snprintf(buf, sizeof(buf), "%s%s", app->opponent[0] ? "对手：" : "",
                 app->opponent);
        lv_label_set_text(app->info_title, buf);
    }
    if (app->info_detail != NULL) {
        snprintf(buf, sizeof(buf), "%d 路 · 你执%s\n黑提 %d   白提 %d", app->g.size,
                 app->my_color == WEIQI_BLACK ? "黑" : "白",
                 app->g.captured[WEIQI_BLACK], app->g.captured[WEIQI_WHITE]);
        lv_label_set_text(app->info_detail, buf);
    }
    if (app->info_turn != NULL) {
        if (app->g.over) {
            lv_label_set_text(app->info_turn, "对局结束");
            lv_obj_set_style_text_color(app->info_turn, UI_COLOR_TEXT_MUTED, 0);
        } else if (app->g.turn == app->my_color) {
            lv_label_set_text(app->info_turn, "轮到你落子");
            lv_obj_set_style_text_color(app->info_turn, UI_COLOR_SUCCESS, 0);
        } else {
            lv_label_set_text(app->info_turn, "等对方落子…");
            lv_obj_set_style_text_color(app->info_turn, UI_COLOR_TEXT_SECONDARY, 0);
        }
    }
    if (app->info_result != NULL) {
        lv_label_set_text(app->info_result, app->g.result);
        if (app->g.over) {
            lv_obj_remove_flag(app->info_result, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(app->info_result, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (app->btn_pass != NULL) {
        if (app->g.over) {
            lv_obj_add_flag(app->btn_pass, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(app->btn_resign, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(app->btn_pass, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(app->btn_resign, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/** 点击棋盘：坐标反算交叉点并发出落子请求。 */
static void board_clicked_cb(lv_event_t *e)
{
    wq_app_t *app = lv_event_get_user_data(e);
    lv_obj_t *target = lv_event_get_current_target(e);
    lv_indev_t *indev = lv_indev_active();
    lv_point_t p;
    int x;
    int y;
    int size;

    if (app == NULL || indev == NULL || app->g.size == 0) {
        return;
    }
    lv_indev_get_point(indev, &p);
    lv_area_t coords;
    lv_obj_get_coords(target, &coords);
    p.x -= coords.x1;
    p.y -= coords.y1;

    size = app->g.size;
    x = (p.x - app->margin + app->cell / 2) / app->cell;
    y = (p.y - app->margin + app->cell / 2) / app->cell;
    if (x < 0 || y < 0 || x >= size || y >= size) {
        return;
    }
    if (app->g.over) {
        ui_toast_show("对局已结束");
        return;
    }
    if (app->g.turn != app->my_color) {
        ui_toast_show("还没轮到你");
        return;
    }
    if (!weiqi_is_empty(&app->g, x, y)) {
        return;
    }
    /* 本地只做即时反馈，权威判定在服务器 */
    if (comm_client_game_move(app->client, x, y) != 0) {
        ui_toast_show("发送失败，连接可能已断开");
    }
}

/* -------------------------------------------------------------------------- */
/* 操作按钮                                                                    */
/* -------------------------------------------------------------------------- */

static void pass_clicked_cb(lv_event_t *e)
{
    wq_app_t *app = lv_event_get_user_data(e);
    if (app->g.turn != app->my_color) {
        ui_toast_show("还没轮到你");
        return;
    }
    comm_client_game_pass(app->client);
}

static void resign_clicked_cb(lv_event_t *e)
{
    wq_app_t *app = lv_event_get_user_data(e);
    comm_client_game_resign(app->client);
    ui_toast_show("已认输");
}

static void leave_clicked_cb(lv_event_t *e)
{
    wq_app_t *app = lv_event_get_user_data(e);
    if (app->in_game) {
        comm_client_game_leave(app->client);
    }
    app->in_game = false;
    app->g.over = true;
    show_view(app, WQ_VIEW_LOBBY);
}

/* -------------------------------------------------------------------------- */
/* 大厅                                                                        */
/* -------------------------------------------------------------------------- */

static void invite_clicked_cb(lv_event_t *e)
{
    wq_app_t *app = lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_current_target(e);
    const char *name = lv_obj_get_user_data(btn);

    if (name == NULL || app == NULL) {
        return;
    }
    if (app->in_game) {
        ui_toast_show("你正在对局中");
        return;
    }
    if (comm_client_game_invite(app->client, name, app->size_choice) != 0) {
        ui_toast_show("邀请发送失败");
        return;
    }
    {
        char buf[96];
        snprintf(buf, sizeof(buf), "已邀请 %s（%d 路），等对方接受", name,
                 app->size_choice);
        ui_toast_show(buf);
    }
}

static void size_clicked_cb(lv_event_t *e)
{
    wq_app_t *app = lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_current_target(e);
    int size = (int)(intptr_t)lv_obj_get_user_data(btn);

    app->size_choice = size;
    if (app->size_hint != NULL) {
        char buf[64];
        snprintf(buf, sizeof(buf), "邀请时使用 %d 路棋盘", size);
        lv_label_set_text(app->size_hint, buf);
    }
    /* 高亮选中的按钮：按钮保存在 size_btns[] 里（它们在 row 容器内，
     * 不是 lobby_box 的直接子对象）。 */
    {
        int i;
        for (i = 0; i < app->size_btn_count; i++) {
            lv_obj_t *b = app->size_btns[i];
            int v;
            if (b == NULL) {
                continue;
            }
            v = (int)(intptr_t)lv_obj_get_user_data(b);
            lv_obj_set_style_bg_color(b,
                                      v == size ? UI_COLOR_PRIMARY_SOFT
                                                : UI_COLOR_SURFACE_ALT,
                                      0);
        }
    }
}

static void rebuild_lobby(wq_app_t *app)
{
    int i;
    int count;

    if (app->friend_list == NULL) {
        return;
    }
    lv_obj_clean(app->friend_list);
    count = comm_session_friend_count();

    for (i = 0; i < count; i++) {
        const char *name = comm_session_friend_name(i);
        bool online = comm_session_friend_online(i);
        lv_obj_t *row = lv_obj_create(app->friend_list);
        lv_obj_t *name_label;
        lv_obj_t *btn;

        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), 64);
        lv_obj_set_style_bg_color(row, UI_COLOR_SURFACE_ALT, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 12, 0);
        lv_obj_set_style_pad_all(row, 10, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        name_label = lv_label_create(row);
        lv_obj_set_style_text_font(name_label, ui_font_small(), 0);
        lv_obj_set_style_text_color(name_label,
                                    online ? UI_COLOR_TEXT : UI_COLOR_TEXT_MUTED, 0);
        {
            char text[96];
            snprintf(text, sizeof(text), "%s%s", name, online ? "" : "（离线）");
            lv_label_set_text(name_label, text);
        }
        lv_obj_align(name_label, LV_ALIGN_LEFT_MID, 0, 0);

        btn = ui_button_primary_create(row, online ? "邀请" : "离线", invite_clicked_cb,
                                       app);
        lv_obj_set_size(btn, 96, 44);
        lv_obj_align(btn, LV_ALIGN_RIGHT_MID, 0, 0);
        if (!online) {
            lv_obj_add_state(btn, LV_STATE_DISABLED);
        }
        lv_obj_set_user_data(btn, (void *)name);
    }

    if (count == 0) {
        lv_obj_t *hint = label_at(app->friend_list, "还没有好友\n先去「聊天室」加好友",
                                  ui_font_caption(), UI_COLOR_TEXT_MUTED, 0, 8);
        lv_obj_set_width(hint, LV_PCT(100));
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    }
}

/* -------------------------------------------------------------------------- */
/* 事件                                                                        */
/* -------------------------------------------------------------------------- */

static void start_game(wq_app_t *app, const char *opponent, int size, int color)
{
    snprintf(app->opponent, sizeof(app->opponent), "%s", opponent != NULL ? opponent : "");
    app->my_color = color;
    app->in_game = true;
    weiqi_init(&app->g, size);
    rebuild_board(app);
    refresh_board(app);
    update_info(app);
    show_view(app, WQ_VIEW_GAME);
}

static void on_session_event(void *user_data, comm_event_t *ev)
{
    wq_app_t *app = user_data;

    if (app == NULL || ev == NULL) {
        return;
    }
    switch (ev->kind) {
    case COMM_EVT_OK:
        if (ev->for_type == COMM_T_LOGIN && !comm_session_logged_in()) {
            break;
        }
        if (ev->for_type == COMM_T_LOGIN && app->view == WQ_VIEW_NEED_LOGIN) {
            show_view(app, WQ_VIEW_LOBBY);
            rebuild_lobby(app);
            comm_client_request_friends(app->client);
            {
                char inviter[COMM_MAX_NAME + 1] = { 0 };
                int inv_size = 0;
                if (comm_session_take_pending_invite(inviter, sizeof(inviter),
                                                    &inv_size) == 1 &&
                    inviter[0] != '\0') {
                    comm_client_game_accept(app->client, inviter);
                }
            }
        }
        break;

    case COMM_EVT_GAME_INVITED: {
        char buf[128];
        snprintf(buf, sizeof(buf), "%s 邀请你下 %d 路围棋", ev->user, ev->game_size);
        /* 自动接受：客户端只有一台设备/一个账号在用，弹窗反而多一步 */
        if (app->in_game) {
            ui_toast_show("你正在对局中，已忽略邀请");
            break;
        }
        if (comm_client_game_accept(app->client, ev->user) == 0) {
            ui_toast_show(buf);
        }
        break;
    }
    case COMM_EVT_GAME_START:
        start_game(app, ev->user, ev->game_size, ev->game_my_color);
        ui_toast_show("对局开始");
        break;

    case COMM_EVT_GAME_STATE:
        if (!app->in_game) {
            /* 服务器主动下发：同步进对局视图 */
            start_game(app, ev->user, ev->game_size > 0 ? ev->game_size
                                                        : app->g.size,
                       0);
        }
        weiqi_set_state(&app->g, app->g.size, ev->game_board, ev->game_turn,
                        ev->game_black_cap, ev->game_white_cap, ev->game_last_x,
                        ev->game_last_y, ev->game_over, ev->game_result);
        if (app->g.size != 0 && app->cell == 0) {
            compute_geometry(app);
        }
        refresh_board(app);
        update_info(app);
        break;

    case COMM_EVT_GAME_END:
        app->g.over = true;
        snprintf(app->g.result, sizeof(app->g.result), "%s", ev->game_result);
        update_info(app);
        ui_toast_show(ev->game_result[0] ? ev->game_result : "对局结束");
        break;

    case COMM_EVT_GAME_REJECT:
        ui_toast_show(ev->msg);
        break;

    case COMM_EVT_GAME_PEER_LEFT:
        app->in_game = false;
        ui_toast_show("对手离开了对局");
        show_view(app, WQ_VIEW_LOBBY);
        break;

    case COMM_EVT_GAME_INVITE_CANCEL:
        ui_toast_show("对方撤回了邀请");
        break;

    case COMM_EVT_ERROR:
        if (ev->for_type == COMM_T_GAME_MOVE || ev->for_type == COMM_T_GAME_PASS ||
            ev->for_type == COMM_T_GAME_INVITE || ev->for_type == COMM_T_GAME_RESIGN) {
            ui_toast_show(ev->msg[0] ? ev->msg : "操作失败");
        }
        break;

    case COMM_EVT_DISCONNECTED:
        if (app->in_game) {
            app->in_game = false;
            ui_toast_show("连接断开");
            show_view(app, WQ_VIEW_LOBBY);
        }
        break;

    case COMM_EVT_FRIENDS:
    case COMM_EVT_PRESENCE:
    case COMM_EVT_ONLINE:
        if (app->view == WQ_VIEW_LOBBY) {
            rebuild_lobby(app);
        }
        break;

    default:
        break;
    }
}

/* -------------------------------------------------------------------------- */
/* 视图构建                                                                    */
/* -------------------------------------------------------------------------- */

static void wq_back_cb(lv_event_t *e)
{
    (void)e;
    (void)ui_app_manager_back();
}

static void goto_comm_cb(lv_event_t *e)
{
    (void)e;
    (void)ui_app_manager_open("account");
}

static void build_login_view(wq_app_t *app, lv_obj_t *parent)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_t *title;
    lv_obj_t *hint;
    lv_obj_t *btn;

    app->login_box = box;
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(box, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    title = label_at(box, "联网围棋", ui_font_h1(), UI_COLOR_TEXT, 0, 120);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    hint = label_at(box, "需要先登录账号（账号在首页右上角「账户」里统一管理）",
                    ui_font_small(), UI_COLOR_TEXT_SECONDARY, 0, 180);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);

    btn = ui_button_primary_create(box, "去账户登录", goto_comm_cb, NULL);
    lv_obj_set_size(btn, 260, 64);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 240);
}

static void build_lobby_view(wq_app_t *app, lv_obj_t *parent)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_t *list;
    lv_obj_t *row;
    int sizes[3] = { 9, 13, 19 };
    int i;

    app->lobby_box = box;
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(box, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    label_at(box, "选择好友对弈", ui_font_h2(), UI_COLOR_TEXT, 24, 84);

    /* 路数选择 */
    row = lv_obj_create(box);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 300, 56);
    lv_obj_set_pos(row, 24, 132);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    app->size_btn_count = 0;
    for (i = 0; i < 3; i++) {
        char text[16];
        lv_obj_t *b;
        snprintf(text, sizeof(text), "%d 路", sizes[i]);
        b = ui_button_secondary_create(row, text, size_clicked_cb, app);
        lv_obj_set_size(b, 90, 52);
        lv_obj_set_user_data(b, (void *)(intptr_t)sizes[i]);
        lv_obj_set_style_bg_color(b,
                                  sizes[i] == app->size_choice ? UI_COLOR_PRIMARY_SOFT
                                                               : UI_COLOR_SURFACE_ALT,
                                  0);
        if (app->size_btn_count < 3) {
            app->size_btns[app->size_btn_count++] = b;
        }
    }

    app->size_hint = label_at(box, "邀请时使用 13 路棋盘", ui_font_caption(),
                              UI_COLOR_TEXT_MUTED, 336, 146);
    (void)app->size_hint;

    list = lv_obj_create(box);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 972, 380);
    lv_obj_set_pos(list, 24, 200);
    lv_obj_set_style_pad_row(list, 8, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    app->friend_list = list;

    (void)label_at;
}

static void build_game_view(wq_app_t *app, lv_obj_t *parent)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_t *panel;

    app->game_box = box;
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(box, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    /* 棋盘容器 */
    app->board_area = lv_obj_create(box);
    lv_obj_remove_style_all(app->board_area);
    lv_obj_set_size(app->board_area, BOARD_PX, BOARD_PX);
    lv_obj_set_pos(app->board_area, BOARD_X, BOARD_Y);
    lv_obj_set_style_bg_color(app->board_area, lv_color_hex(0xD9B382), 0);
    lv_obj_set_style_bg_opa(app->board_area, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(app->board_area, 10, 0);
    lv_obj_set_style_pad_all(app->board_area, 0, 0);
    lv_obj_add_flag(app->board_area, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(app->board_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(app->board_area, board_clicked_cb, LV_EVENT_CLICKED, app);

    /* 信息面板 */
    panel = lv_obj_create(box);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, INFO_W, INFO_H);
    lv_obj_set_pos(panel, INFO_X, BOARD_Y);
    lv_obj_set_style_bg_color(panel, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 16, 0);
    lv_obj_set_style_pad_all(panel, 20, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    app->info_title = label_at(panel, "等待对手", ui_font_h2(), UI_COLOR_TEXT, 0, 0);
    app->info_detail = label_at(panel, "", ui_font_small(), UI_COLOR_TEXT_SECONDARY,
                                0, 52);
    app->info_turn = label_at(panel, "", ui_font_h3(), UI_COLOR_TEXT, 0, 130);
    app->info_result = label_at(panel, "", ui_font_h3(), UI_COLOR_SUCCESS, 0, 176);
    lv_obj_add_flag(app->info_result, LV_OBJ_FLAG_HIDDEN);

    app->btn_pass = ui_button_primary_create(panel, "停一手", pass_clicked_cb, app);
    lv_obj_set_size(app->btn_pass, 180, 60);
    lv_obj_set_pos(app->btn_pass, 0, 250);

    app->btn_resign = ui_button_secondary_create(panel, "认输", resign_clicked_cb, app);
    lv_obj_set_size(app->btn_resign, 180, 60);
    lv_obj_set_pos(app->btn_resign, 200, 250);

    app->btn_leave = ui_button_secondary_create(panel, "退出对局", leave_clicked_cb, app);
    lv_obj_set_size(app->btn_leave, 380, 56);
    lv_obj_set_pos(app->btn_leave, 0, 330);

    (void)label_at;
}

static lv_obj_t *wq_create(lv_obj_t *parent)
{
    wq_app_t *app = calloc(1, sizeof(*app));
    lv_obj_t *top;

    if (app == NULL) {
        return NULL;
    }
    s_app = app;
    app->page = parent;
    app->size_choice = 13;
    lv_obj_set_style_bg_color(parent, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    if (comm_session_init(NULL) != 0) {
        ui_toast_show("网络模块初始化失败");
    }
    app->client = comm_session_client();
    comm_session_subscribe(on_session_event, app);

    build_login_view(app, parent);
    build_lobby_view(app, parent);
    build_game_view(app, parent);

    /* 顶栏：返回 + 标题 */
    top = ui_topbar_create(parent, "联网围棋", wq_back_cb, app);
    (void)top;

    comm_session_auto_login();
    if (comm_session_logged_in()) {
        char inviter[COMM_MAX_NAME + 1] = { 0 };
        int inv_size = 0;
        show_view(app, WQ_VIEW_LOBBY);
        rebuild_lobby(app);
        comm_client_request_friends(app->client);
        /* 页面没打开时收到的邀请在这里补处理 */
        if (comm_session_take_pending_invite(inviter, sizeof(inviter), &inv_size) == 1 &&
            inviter[0] != '\0') {
            if (comm_client_game_accept(app->client, inviter) == 0) {
                ui_toast_show("已接受对局邀请");
            }
        }
    } else {
        show_view(app, WQ_VIEW_NEED_LOGIN);
    }
    return parent;
}

static void wq_destroy(void)
{
    wq_app_t *app = s_app;

    if (app == NULL) {
        return;
    }
    comm_session_unsubscribe(on_session_event);
    free(app);
    s_app = NULL;
}

static const board_app_desc_t s_weiqi_app = {
    .id = "weiqi",
    .title = "联网围棋",
    .subtitle = "好友对弈 · 服务器判定",
    .parent_id = "games",
    .best_score_key = NULL,
    .icon = UI_ICON_WEIQI,
    .accent = 0xC9A227,
    .order = 4,
    .show_on_home = false,
    .create = wq_create,
    .destroy = wq_destroy,
};

BOARD_APP_REGISTER(s_weiqi_app);
