/**
 * @file account_ui.c
 * @brief 系统账户：整个系统共用一套账号（聊天室 / 联网围棋都用它登录）。
 *
 * 之所以从聊天室里独立出来：账号、好友、连接本来就属于"系统级"资源，
 * 放在聊天室会导致 —— 一个应用在登录、另一个应用还得再登一次，同名账号
 * 还会被服务器互踢。现在：
 *   - 登录页只在这里出现（comm_session 持有连接与登录态）；
 *   - 聊天室 / 围棋只订阅事件，不再有登录界面；
 *   - 首页顶栏显示当前账号，点一下就能进来。
 *
 * 视图：
 *   未登录：服务器地址 / 用户名 / 密码 + 登录、注册
 *   已登录：当前账号、好友列表（在线状态、可删）、加好友、退出登录
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "board_ui/ui_app_manager.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_core/board_log.h"
#include "board_core/board_storage.h"
#include "board_ui/ui_comm_session.h"
#include "board_ui/ui_icons.h"
#include "board_ui/ui_theme.h"
#include "board_ui/ui_widgets.h"
#include "comm/comm_proto.h"

#define KB_H 250
#define FRIEND_ROW_H 64

typedef struct {
    lv_obj_t *page;

    lv_obj_t *login_box;
    lv_obj_t *main_box;
    lv_obj_t *ta_host;
    lv_obj_t *ta_user;
    lv_obj_t *ta_pass;
    lv_obj_t *status;
    lv_obj_t *kb;

    lv_obj_t *who_title;
    lv_obj_t *who_sub;
    lv_obj_t *friend_list;
    lv_obj_t *friend_count;

    comm_client_t *client;
} acc_app_t;

static acc_app_t *s_acc;

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

static void set_status(acc_app_t *app, const char *text, lv_color_t color)
{
    if (app->status == NULL) {
        return;
    }
    lv_label_set_text(app->status, text != NULL ? text : "");
    lv_obj_set_style_text_color(app->status, color, 0);
}

static void acc_back_cb(lv_event_t *e)
{
    (void)e;
    (void)ui_app_manager_back();
}

static void show_view(acc_app_t *app, bool logged_in)
{
    if (app->login_box != NULL) {
        if (logged_in) {
            lv_obj_add_flag(app->login_box, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(app->login_box, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (app->main_box != NULL) {
        if (logged_in) {
            lv_obj_remove_flag(app->main_box, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(app->main_box, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* -------------------------------------------------------------------------- */
/* 已登录视图                                                                  */
/* -------------------------------------------------------------------------- */

static void remove_friend_cb(lv_event_t *e)
{
    acc_app_t *app = lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_current_target(e);
    const char *name = lv_obj_get_user_data(btn);

    if (app == NULL || name == NULL) {
        return;
    }
    if (comm_client_del_friend(app->client, name) != 0) {
        ui_toast_show("删除失败");
    }
}

static void rebuild_friends(acc_app_t *app)
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
        lv_obj_t *dot;
        lv_obj_t *name_label;
        lv_obj_t *btn;

        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), FRIEND_ROW_H);
        lv_obj_set_style_bg_color(row, UI_COLOR_SURFACE_ALT, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 12, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        dot = lv_obj_create(row);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 10, 10);
        lv_obj_set_style_radius(dot, 5, 0);
        lv_obj_set_style_bg_color(dot,
                                  online ? UI_COLOR_SUCCESS : UI_COLOR_TEXT_MUTED, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_align(dot, LV_ALIGN_LEFT_MID, 16, 0);

        {
            char text[96];
            snprintf(text, sizeof(text), "%s%s", name, online ? "" : "（离线）");
            name_label = label_at(row, text, ui_font_small(), UI_COLOR_TEXT, 36, 20);
            (void)name_label;
        }

        btn = ui_button_secondary_create(row, "删除", remove_friend_cb, app);
        lv_obj_set_size(btn, 88, 44);
        lv_obj_align(btn, LV_ALIGN_RIGHT_MID, -12, 0);
        lv_obj_set_user_data(btn, (void *)name);
    }

    if (count == 0) {
        lv_obj_t *hint = label_at(app->friend_list, "还没有好友，点右上角「加好友」",
                                  ui_font_caption(), UI_COLOR_TEXT_MUTED, 8, 8);
        lv_obj_set_width(hint, LV_PCT(100));
    }
    if (app->friend_count != NULL) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d 位好友", count);
        lv_label_set_text(app->friend_count, buf);
    }
}

static void refresh_identity(acc_app_t *app)
{
    const char *user = comm_session_user();

    if (app->who_title != NULL) {
        char buf[96];
        snprintf(buf, sizeof(buf), "%s", user[0] != '\0' ? user : "已登录");
        lv_label_set_text(app->who_title, buf);
    }
    if (app->who_sub != NULL) {
        lv_label_set_text(app->who_sub, "这个账号在聊天室和围棋里通用");
    }
}

/* -------------------------------------------------------------------------- */
/* 加好友弹窗                                                                  */
/* -------------------------------------------------------------------------- */

static void add_friend_confirm_cb(lv_event_t *e)
{
    acc_app_t *app = lv_event_get_user_data(e);
    lv_obj_t *content = lv_obj_get_parent(lv_event_get_current_target(e));
    lv_obj_t *ta = content != NULL ? lv_obj_get_child(content, 0) : NULL;
    const char *name;

    if (ta == NULL) {
        return;
    }
    name = lv_textarea_get_text(ta);
    if (name[0] == '\0') {
        ui_toast_show("请输入对方用户名");
        return;
    }
    if (comm_client_add_friend(app->client, name) != 0) {
        ui_toast_show("发送失败");
        return;
    }
    ui_modal_close(content);
}

static void add_friend_cb(lv_event_t *e)
{
    acc_app_t *app = lv_event_get_user_data(e);
    lv_obj_t *content = ui_modal_create(app->page, "添加好友", 460);
    lv_obj_t *ta;
    lv_obj_t *kb;
    lv_obj_t *btn;

    if (content == NULL) {
        return;
    }
    ta = lv_textarea_create(content);
    lv_obj_set_size(ta, 400, 56);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 0);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, COMM_MAX_NAME);
    lv_textarea_set_placeholder_text(ta, "对方用户名（字母/数字）");
    lv_obj_set_style_text_font(ta, ui_font_small(), 0);

    kb = lv_keyboard_create(content);
    lv_obj_set_size(kb, 420, 190);
    lv_obj_align(kb, LV_ALIGN_TOP_MID, 0, 66);
    lv_keyboard_set_textarea(kb, ta);

    btn = ui_button_primary_create(content, "发送好友请求", NULL, NULL);
    lv_obj_set_size(btn, 220, 52);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 266);
    lv_obj_add_event_cb(btn, add_friend_confirm_cb, LV_EVENT_CLICKED, app);
}

static void logout_cb(lv_event_t *e)
{
    acc_app_t *app = lv_event_get_user_data(e);
    comm_session_logout();
    set_status(app, "已退出登录", UI_COLOR_TEXT_SECONDARY);
    show_view(app, false);
}

/* -------------------------------------------------------------------------- */
/* 登录                                                                        */
/* -------------------------------------------------------------------------- */

static bool split_host_port(const char *text, char *host, size_t cap, uint16_t *port)
{
    const char *colon = strrchr(text, ':');
    size_t n;

    if (colon == NULL) {
        snprintf(host, cap, "%s", text);
        *port = COMM_DEFAULT_PORT;
        return text[0] != '\0';
    }
    n = (size_t)(colon - text);
    if (n == 0 || n >= cap) {
        return false;
    }
    memcpy(host, text, n);
    host[n] = '\0';
    *port = (uint16_t)atoi(colon + 1);
    return *port != 0;
}

static int ensure_connected(acc_app_t *app, char *err, size_t cap)
{
    char host[64];
    uint16_t port;
    int rc;

    if (comm_client_connected(app->client)) {
        return 0;
    }
    if (!split_host_port(lv_textarea_get_text(app->ta_host), host, sizeof(host),
                         &port)) {
        snprintf(err, cap, "服务器地址格式：IP:端口");
        return -EINVAL;
    }
    rc = comm_client_connect(app->client, host, port);
    if (rc != 0) {
        snprintf(err, cap, "连不上 %s:%u", host, (unsigned)port);
        return rc;
    }
    return 0;
}

static void do_login(acc_app_t *app)
{
    char err[96] = { 0 };
    const char *user = lv_textarea_get_text(app->ta_user);
    const char *pass = lv_textarea_get_text(app->ta_pass);
    const char *host = lv_textarea_get_text(app->ta_host);

    if (host[0] == '\0') {
        set_status(app, "请填写服务器地址（IP:端口）", UI_COLOR_WARNING);
        return;
    }
    if (user[0] == '\0' || pass[0] == '\0') {
        set_status(app, "请填写用户名和密码", UI_COLOR_WARNING);
        return;
    }
    if (ensure_connected(app, err, sizeof(err)) != 0) {
        set_status(app, err, UI_COLOR_DANGER);
        return;
    }
    if (comm_client_login(app->client, user, pass) != 0) {
        set_status(app, "发送登录请求失败", UI_COLOR_DANGER);
        return;
    }
    set_status(app, "正在登录…", UI_COLOR_TEXT_SECONDARY);
}

static void do_register(acc_app_t *app)
{
    char err[96] = { 0 };
    const char *user = lv_textarea_get_text(app->ta_user);
    const char *pass = lv_textarea_get_text(app->ta_pass);
    const char *host = lv_textarea_get_text(app->ta_host);

    if (host[0] == '\0') {
        set_status(app, "请填写服务器地址（IP:端口）", UI_COLOR_WARNING);
        return;
    }
    if (user[0] == '\0' || pass[0] == '\0') {
        set_status(app, "请填写用户名和密码", UI_COLOR_WARNING);
        return;
    }
    if (ensure_connected(app, err, sizeof(err)) != 0) {
        set_status(app, err, UI_COLOR_DANGER);
        return;
    }
    if (comm_client_register(app->client, user, pass) != 0) {
        set_status(app, "发送注册请求失败", UI_COLOR_DANGER);
        return;
    }
    set_status(app, "正在注册…", UI_COLOR_TEXT_SECONDARY);
}

static void login_clicked_cb(lv_event_t *e)
{
    do_login(lv_event_get_user_data(e));
}

static void register_clicked_cb(lv_event_t *e)
{
    do_register(lv_event_get_user_data(e));
}

static void ta_focus_cb(lv_event_t *e)
{
    acc_app_t *app = lv_event_get_user_data(e);
    lv_obj_t *ta = lv_event_get_current_target(e);
    if (app->kb != NULL) {
        lv_keyboard_set_textarea(app->kb, ta);
    }
}

/* -------------------------------------------------------------------------- */
/* 事件                                                                        */
/* -------------------------------------------------------------------------- */

static void on_session_event(void *user_data, comm_event_t *ev)
{
    acc_app_t *app = user_data;

    if (app == NULL || ev == NULL) {
        return;
    }
    switch (ev->kind) {
    case COMM_EVT_OK:
        if (ev->for_type == COMM_T_LOGIN) {
            comm_session_set_user(lv_textarea_get_text(app->ta_user));
            board_storage_set_str("comm.host", lv_textarea_get_text(app->ta_host));
            /* 记住账号：下次开机自动登录 */
            comm_session_remember(lv_textarea_get_text(app->ta_user),
                                  lv_textarea_get_text(app->ta_pass));
            refresh_identity(app);
            show_view(app, true);
            rebuild_friends(app);
            comm_client_request_friends(app->client);
            set_status(app, "登录成功", UI_COLOR_SUCCESS);
            ui_toast_show("登录成功");
        } else if (ev->for_type == COMM_T_REGISTER) {
            set_status(app, "注册成功，请点「登录」", UI_COLOR_SUCCESS);
            ui_toast_show("注册成功");
        } else if (ev->for_type == COMM_T_ADD_FRIEND) {
            ui_toast_show("已添加好友");
            comm_client_request_friends(app->client);
        } else if (ev->for_type == COMM_T_DEL_FRIEND) {
            ui_toast_show("已删除好友");
            comm_client_request_friends(app->client);
        }
        break;

    case COMM_EVT_ERROR:
        if (ev->for_type == COMM_T_LOGIN || ev->for_type == COMM_T_REGISTER) {
            set_status(app, ev->msg, UI_COLOR_DANGER);
        } else {
            ui_toast_show(ev->msg);
        }
        break;

    case COMM_EVT_FRIENDS:
    case COMM_EVT_PRESENCE:
    case COMM_EVT_ONLINE:
        rebuild_friends(app);
        break;

    case COMM_EVT_ADDED:
        ui_toast_show("有人加你为好友");
        comm_client_request_friends(app->client);
        break;

    case COMM_EVT_DISCONNECTED:
        set_status(app, "与服务器断开，请重新登录", UI_COLOR_WARNING);
        show_view(app, false);
        break;

    default:
        break;
    }
}

/* -------------------------------------------------------------------------- */
/* 构建                                                                        */
/* -------------------------------------------------------------------------- */

static lv_obj_t *make_input(lv_obj_t *parent, const char *placeholder, int y,
                            bool password)
{
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_size(ta, 440, 52);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, y);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_textarea_set_max_length(ta, 64);
    lv_obj_set_style_text_font(ta, ui_font_small(), 0);
    lv_obj_set_style_bg_color(ta, UI_COLOR_SURFACE_ALT, 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ta, 12, 0);
    lv_obj_set_style_pad_all(ta, 12, 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_border_color(ta, UI_COLOR_LINE, 0);
    lv_obj_set_style_border_color(ta, UI_COLOR_PRIMARY, LV_STATE_FOCUSED);
    if (password) {
        lv_textarea_set_password_mode(ta, true);
        lv_textarea_set_password_bullet(ta, "*");
    }
    return ta;
}

static void build_login_view(acc_app_t *app, lv_obj_t *parent)
{
    lv_obj_t *box = lv_obj_create(parent);
    const char *saved_host;
    lv_obj_t *row;

    app->login_box = box;
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(box, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = label_at(box, "登录账号", ui_font_h1(), UI_COLOR_TEXT, 0, 16);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    {
        lv_obj_t *sub = label_at(box, "一个账号，聊天室与联网围棋通用", ui_font_caption(),
                                 UI_COLOR_TEXT_SECONDARY, 0, 58);
        lv_obj_set_width(sub, LV_PCT(100));
        lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
    }

    app->ta_host = make_input(box, "服务器地址，例如 192.168.1.10:20000", 86, false);
    app->ta_user = make_input(box, "用户名（字母/数字）", 144, false);
    app->ta_pass = make_input(box, "密码", 202, true);

    /* 不写死服务器地址：默认留空，只在记住过上次地址时自动带出。 */
    saved_host = board_storage_get_str("comm.host", "");
    if (saved_host != NULL && saved_host[0] != '\0') {
        lv_textarea_set_text(app->ta_host, saved_host);
    }
    /* 预填上次登录的账号，用户只需点「登录」 */
    {
        const char *saved_user = board_storage_get_str("comm.user", "");
        if (saved_user != NULL && saved_user[0] != '\0') {
            lv_textarea_set_text(app->ta_user, saved_user);
            set_status(app, "已记住上次的账号，点「登录」即可", UI_COLOR_TEXT_MUTED);
        }
    }

    row = lv_obj_create(box);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 440, 60);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 262);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 16, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    {
        lv_obj_t *b1 = ui_button_primary_create(row, "登录", login_clicked_cb, app);
        lv_obj_set_size(b1, 200, 56);
        lv_obj_t *b2 = ui_button_secondary_create(row, "注册", register_clicked_cb, app);
        lv_obj_set_size(b2, 200, 56);
    }

    app->status = label_at(box, "", ui_font_small(), UI_COLOR_TEXT_SECONDARY, 0, 326);
    lv_obj_set_width(app->status, LV_PCT(100));
    lv_obj_set_style_text_align(app->status, LV_TEXT_ALIGN_CENTER, 0);

    app->kb = lv_keyboard_create(box);
    lv_obj_set_size(app->kb, LV_PCT(100), KB_H);
    lv_obj_align(app->kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(app->kb, app->ta_host);
    {
        lv_obj_t *kb = app->kb;
        lv_obj_set_style_bg_color(kb, UI_COLOR_SURFACE, 0);
        lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(kb, 6, 0);
        lv_obj_set_style_pad_row(kb, 5, 0);
        lv_obj_set_style_pad_column(kb, 5, 0);
        lv_obj_set_style_bg_color(kb, UI_COLOR_SURFACE_ALT, LV_PART_ITEMS);
        lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS);
        lv_obj_set_style_radius(kb, 8, LV_PART_ITEMS);
        lv_obj_set_style_text_color(kb, UI_COLOR_TEXT, LV_PART_ITEMS);
        lv_obj_set_style_text_font(kb, &lv_font_montserrat_20, LV_PART_ITEMS);
    }
    lv_obj_add_event_cb(app->ta_host, ta_focus_cb, LV_EVENT_FOCUSED, app);
    lv_obj_add_event_cb(app->ta_user, ta_focus_cb, LV_EVENT_FOCUSED, app);
    lv_obj_add_event_cb(app->ta_pass, ta_focus_cb, LV_EVENT_FOCUSED, app);
}

static void build_main_view(acc_app_t *app, lv_obj_t *parent)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_t *card;
    lv_obj_t *add_btn;
    lv_obj_t *logout_btn;

    app->main_box = box;
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(box, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);

    card = ui_card_create(box, 620, 150);
    lv_obj_set_pos(card, 32, 96);
    app->who_title = label_at(card, "已登录", ui_font_h1(), UI_COLOR_TEXT, 24, 24);
    app->who_sub = label_at(card, "", ui_font_caption(), UI_COLOR_TEXT_SECONDARY, 24,
                            86);
    lv_obj_set_width(app->who_sub, 420);
    lv_label_set_long_mode(app->who_sub, LV_LABEL_LONG_DOT);

    logout_btn = ui_button_secondary_create(card, "退出登录", logout_cb, app);
    lv_obj_set_size(logout_btn, 140, 48);
    lv_obj_align(logout_btn, LV_ALIGN_RIGHT_MID, -20, 0);

    label_at(box, "好友", ui_font_h2(), UI_COLOR_TEXT, 32, 268);
    app->friend_count = label_at(box, "", ui_font_caption(), UI_COLOR_TEXT_MUTED, 96,
                                 276);

    add_btn = ui_button_primary_create(box, "加好友", add_friend_cb, app);
    lv_obj_set_size(add_btn, 140, 52);
    lv_obj_align(add_btn, LV_ALIGN_TOP_RIGHT, -32, 262);

    app->friend_list = lv_obj_create(box);
    lv_obj_remove_style_all(app->friend_list);
    lv_obj_set_size(app->friend_list, 960, 300);
    lv_obj_set_pos(app->friend_list, 32, 330);
    lv_obj_set_style_pad_row(app->friend_list, 8, 0);
    lv_obj_set_flex_flow(app->friend_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(app->friend_list, LV_DIR_VER);
}

static lv_obj_t *acc_create(lv_obj_t *parent)
{
    acc_app_t *app = calloc(1, sizeof(*app));

    if (app == NULL) {
        return NULL;
    }
    s_acc = app;
    app->page = parent;
    lv_obj_set_style_bg_color(parent, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    if (comm_session_init(NULL) != 0) {
        ui_toast_show("网络模块初始化失败");
    }
    app->client = comm_session_client();
    comm_session_subscribe(on_session_event, app);

    build_login_view(app, parent);
    build_main_view(app, parent);
    ui_topbar_create(parent, "账户", acc_back_cb, app);

    /* 顶栏放在视图之上；视图从 y=72 开始铺满 */
    lv_obj_set_y(app->login_box, 0);
    lv_obj_set_y(app->main_box, 0);

    comm_session_auto_login();
    if (comm_session_logged_in()) {
        refresh_identity(app);
        show_view(app, true);
        rebuild_friends(app);
        comm_client_request_friends(app->client);
    } else {
        show_view(app, false);
    }
    return parent;
}

static void acc_destroy(void)
{
    acc_app_t *app = s_acc;

    if (app == NULL) {
        return;
    }
    comm_session_unsubscribe(on_session_event);
    free(app);
    s_acc = NULL;
}

static const board_app_desc_t s_account_app = {
    .id = "account",
    .title = "账户",
    .subtitle = "登录 · 好友",
    .parent_id = NULL,
    .best_score_key = NULL,
    .icon = UI_ICON_COMM,
    .accent = 0x7FB0A9,
    .order = 0,
    .show_on_home = false, /* 从首页顶栏的账号按钮进入 */
    .create = acc_create,
    .destroy = acc_destroy,
};

BOARD_APP_REGISTER(s_account_app);
