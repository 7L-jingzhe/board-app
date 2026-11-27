/**
 * @file comm_ui.c
 * @brief 聊天室客户端界面：登录/注册 -> 好友列表 + 聊天（复用 ui_ime 输入法）。
 *
 * 布局（1024×600）：
 *   ┌───────────────────────────────────────────────────────────┐
 *   │ 顶栏：返回 / 聊天室 · 用户名 / 添加好友 / 在线 n            │
 *   ├──────────┬──────────────────────┬───────────────────────┤
 *   │ 群聊(UDP) │ 消息区（可滚动）       │  虚拟键盘（ui_ime）    │
 *   │ 好友列表  ├──────────────────────┤  中文拼音候选          │
 *   │          │ 输入框        [发送]  │                       │
 *   └──────────┴──────────────────────┴───────────────────────┘
 *      192px            312px                 504px
 *
 * 网络线程 -> board_async_queue -> ui_async_pump -> 本文件的 on_event()。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "board_app/board_app.h"
#include "board_ui/ui_app_manager.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "board_core/board_log.h"
#include "board_core/board_storage.h"
#include "board_ui/ui_async.h"
#include "board_ui/ui_comm_session.h"
#include "board_ui/ui_icons.h"
#include "board_ui/ui_ime.h"
#include "board_ui/ui_theme.h"
#include "board_ui/ui_widgets.h"
#include "comm/comm_client.h"
#include "comm/comm_history.h"

#define CHAT_TOP 76
#define FRIEND_W 192
#define MSG_W 300
#define IME_W 504
#define LOG_MAX 200
#define FRIEND_MAX COMM_MAX_FRIENDS

#define GROUP_PEER "\x01group" /* 内部标记：群聊会话 */

typedef struct {
    char peer[COMM_MAX_NAME + 1];  /* 会话标识：好友名或 GROUP_PEER */
    char from[COMM_MAX_NAME + 1];  /* 发送者（群聊里是好友名） */
    bool mine;
    char text[COMM_MAX_TEXT + 1];
    long ts;
} log_entry_t;

typedef struct {
    char name[COMM_MAX_NAME + 1];
    bool online;
    bool unread;
} friend_t;

typedef struct {
    lv_obj_t *page;

    /* 登录视图 */
    lv_obj_t *login_box;
    /* 登录表单已移到系统「账户」页；这里只留未登录提示 */

    /* 会话视图 */
    lv_obj_t *chat_box;
    lv_obj_t *title_label;
    lv_obj_t *online_label;
    lv_obj_t *friend_list;
    lv_obj_t *msg_area;
    lv_obj_t *ta_input;
    lv_obj_t *send_btn;
    ui_ime_t *ime;

    /* 状态 */
    comm_client_t *client;   /* 由 comm_session 提供，不由本页销毁 */
    lv_timer_t *ping_timer;
    char self[COMM_MAX_NAME + 1];
    char peer[COMM_MAX_NAME + 1]; /* 当前会话；GROUP_PEER 表示群聊 */
    bool connected;

    friend_t friends[FRIEND_MAX];
    int friend_count;

    log_entry_t logs[LOG_MAX];
    int log_count;
} comm_app_t;

static comm_app_t *s_app;

/* -------------------------------------------------------------------------- */
/* 小工具                                                                      */
/* -------------------------------------------------------------------------- */

static lv_obj_t *small_label(lv_obj_t *parent, const char *text,
                             const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text != NULL ? text : "");
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

static void add_log(comm_app_t *app, const char *peer, const char *from, bool mine,
                    const char *text, long ts)
{
    log_entry_t *e;
    if (app->log_count >= LOG_MAX) {
        memmove(&app->logs[0], &app->logs[1], sizeof(app->logs[0]) * (LOG_MAX - 1));
        app->log_count = LOG_MAX - 1;
    }
    e = &app->logs[app->log_count++];
    snprintf(e->peer, sizeof(e->peer), "%s", peer);
    snprintf(e->from, sizeof(e->from), "%s", from != NULL ? from : "");
    e->mine = mine;
    snprintf(e->text, sizeof(e->text), "%s", text);
    e->ts = ts;
}

static bool peer_is_group(const char *peer)
{
    return strcmp(peer, GROUP_PEER) == 0;
}

static const char *peer_display(const char *peer)
{
    return peer_is_group(peer) ? "群聊" : peer;
}

/** 重画消息区（只显示当前会话的消息）。 */
static void render_messages(comm_app_t *app)
{
    int i;
    int shown = 0;

    if (app->msg_area == NULL) {
        return;
    }
    lv_obj_clean(app->msg_area);
    if (app->peer[0] == '\0') {
        small_label(app->msg_area, "选择左侧好友开始聊天", ui_font_small(),
                    UI_COLOR_TEXT_MUTED);
        return;
    }
    for (i = 0; i < app->log_count; i++) {
        const log_entry_t *e = &app->logs[i];
        lv_obj_t *row;
        lv_obj_t *label;
        char line[COMM_MAX_TEXT + 64];

        if (strcmp(e->peer, app->peer) != 0) {
            continue;
        }
        shown++;
        row = lv_obj_create(app->msg_area);
        lv_obj_remove_style_all(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        if (e->mine) {
            snprintf(line, sizeof(line), "%s", e->text);
        } else {
            snprintf(line, sizeof(line), "%s: %s", e->from, e->text);
        }
        label = lv_label_create(row);
        lv_label_set_text(label, line);
        lv_obj_set_style_text_font(label, ui_font_small(), 0);
        lv_obj_set_style_text_color(label,
                                    e->mine ? UI_COLOR_PRIMARY : UI_COLOR_TEXT, 0);
        lv_obj_set_width(label, LV_PCT(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    }
    if (shown == 0) {
        char hint[96];
        if (peer_is_group(app->peer)) {
            snprintf(hint, sizeof(hint), "群聊基于 UDP：所有在线用户都能收到，丢包不重传");
        } else {
            snprintf(hint, sizeof(hint), "和 %s 还没有消息，说点什么吧", app->peer);
        }
        small_label(app->msg_area, hint, ui_font_caption(), UI_COLOR_TEXT_MUTED);
    }
    lv_obj_update_layout(app->msg_area);
    lv_obj_scroll_to_y(app->msg_area, lv_obj_get_scroll_bottom(app->msg_area),
                       LV_ANIM_OFF);
}

/* -------------------------------------------------------------------------- */
/* 好友列表                                                                    */
/* -------------------------------------------------------------------------- */

static void select_peer(comm_app_t *app, const char *peer);

static void friend_clicked_cb(lv_event_t *e)
{
    comm_app_t *app = lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_current_target(e);
    const char *peer = lv_obj_get_user_data(btn);

    if (app == NULL || peer == NULL) {
        return;
    }
    select_peer(app, peer);
}

static void rebuild_friend_list(comm_app_t *app)
{
    int i;
    lv_obj_t *item;

    if (app->friend_list == NULL) {
        return;
    }
    lv_obj_clean(app->friend_list);

    /* 群聊（UDP 广播）固定在第一项 */
    item = lv_obj_create(app->friend_list);
    lv_obj_remove_style_all(item);
    lv_obj_set_size(item, LV_PCT(100), 52);
    lv_obj_set_style_radius(item, 12, 0);
    lv_obj_set_style_bg_color(item,
                              strcmp(app->peer, GROUP_PEER) == 0 ? UI_COLOR_PRIMARY_SOFT
                                                                 : UI_COLOR_SURFACE_ALT,
                              0);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(item, (void *)GROUP_PEER);
    lv_obj_add_event_cb(item, friend_clicked_cb, LV_EVENT_CLICKED, app);
    {
        lv_obj_t *dot = lv_obj_create(item);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 10, 10);
        lv_obj_set_style_radius(dot, 5, 0);
        lv_obj_set_style_bg_color(dot, UI_COLOR_ACCENT, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_pos(dot, 12, 21);
        small_label(item, "群聊 · UDP", ui_font_small(), UI_COLOR_TEXT);
        lv_obj_align(lv_obj_get_child(item, 1), LV_ALIGN_LEFT_MID, 30, 0);
    }

    for (i = 0; i < app->friend_count; i++) {
        lv_obj_t *row = lv_obj_create(app->friend_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), 52);
        lv_obj_set_style_radius(row, 12, 0);
        lv_obj_set_style_bg_color(row,
                                  strcmp(app->peer, app->friends[i].name) == 0
                                      ? UI_COLOR_PRIMARY_SOFT
                                      : UI_COLOR_SURFACE_ALT,
                                  0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_user_data(row, app->friends[i].name);
        lv_obj_add_event_cb(row, friend_clicked_cb, LV_EVENT_CLICKED, app);

        lv_obj_t *dot = lv_obj_create(row);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 10, 10);
        lv_obj_set_style_radius(dot, 5, 0);
        lv_obj_set_style_bg_color(dot,
                                  app->friends[i].online ? UI_COLOR_SUCCESS
                                                         : UI_COLOR_TEXT_MUTED,
                                  0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_pos(dot, 12, 21);

        {
            lv_obj_t *name = small_label(row, app->friends[i].name, ui_font_small(),
                                         UI_COLOR_TEXT);
            lv_obj_align(name, LV_ALIGN_LEFT_MID, 30, 0);
        }
        if (app->friends[i].unread) {
            lv_obj_t *badge = small_label(row, "新", ui_font_caption(), UI_COLOR_WHITE);
            lv_obj_set_style_bg_color(badge, UI_COLOR_DANGER, 0);
            lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(badge, 8, 0);
            lv_obj_set_style_pad_hor(badge, 6, 0);
            lv_obj_align(badge, LV_ALIGN_RIGHT_MID, -8, 0);
        }
    }

    if (app->friend_count == 0) {
        lv_obj_t *hint = small_label(app->friend_list, "还没有好友\n点右上角“加好友”",
                                     ui_font_caption(), UI_COLOR_TEXT_MUTED);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(hint, LV_PCT(100));
    }
}

/** 内部会话标记 -> 历史文件名（群聊用固定名 group） */
static const char *history_peer_of(const char *peer)
{
    return peer_is_group(peer) ? COMM_HISTORY_GROUP : peer;
}

/** 丢掉内存里某个会话的记录（切会话时用文件内容重建，避免重复）。 */
static void drop_logs_for_peer(comm_app_t *app, const char *peer)
{
    int i;
    int w = 0;

    for (i = 0; i < app->log_count; i++) {
        if (strcmp(app->logs[i].peer, peer) != 0) {
            app->logs[w++] = app->logs[i];
        }
    }
    app->log_count = w;
}

/** 从本地文件恢复某个会话最近的聊天记录。 */
static void load_history(comm_app_t *app, const char *peer)
{
    comm_history_item_t items[64];
    const char *key = history_peer_of(peer);
    int n;
    int i;

    drop_logs_for_peer(app, peer);
    n = comm_history_load(key, items, 64);
    for (i = 0; i < n; i++) {
        add_log(app, peer, items[i].from, items[i].mine, items[i].text, items[i].ts);
    }
}


static void select_peer(comm_app_t *app, const char *peer)
{
    int i;

    snprintf(app->peer, sizeof(app->peer), "%s", peer);
    for (i = 0; i < app->friend_count; i++) {
        if (strcmp(app->friends[i].name, peer) == 0) {
            app->friends[i].unread = false;
        }
    }
    load_history(app, peer);
    rebuild_friend_list(app);
    render_messages(app);
}

/* -------------------------------------------------------------------------- */
/* 发送                                                                        */
/* -------------------------------------------------------------------------- */

static void do_send(comm_app_t *app)
{
    const char *text;
    char copy[COMM_MAX_TEXT + 1];

    if (app == NULL || app->ta_input == NULL || app->peer[0] == '\0') {
        ui_toast_show("先选一个好友");
        return;
    }
    text = lv_textarea_get_text(app->ta_input);
    if (text == NULL || text[0] == '\0') {
        return;
    }
    snprintf(copy, sizeof(copy), "%s", text);
    lv_textarea_set_text(app->ta_input, "");

    if (peer_is_group(app->peer)) {
        if (comm_client_send_group(app->client, copy) != 0) {
            ui_toast_show("UDP 发送失败");
            return;
        }
        comm_history_append(COMM_HISTORY_GROUP, app->self, true, copy,
                            (long)time(NULL));
    } else {
        int rc = comm_client_send_text(app->client, app->peer, copy);
        if (rc != 0) {
            ui_toast_show("发送失败（网络断开？）");
            return;
        }
        comm_history_append(app->peer, app->self, true, copy,
                            (long)time(NULL));
    }
    load_history(app, app->peer);
    render_messages(app);
}

static void send_clicked_cb(lv_event_t *e)
{
    do_send(lv_event_get_user_data(e));
}

static void input_ready_cb(lv_event_t *e)
{
    do_send(lv_event_get_user_data(e));
}

static void ime_ready_cb(void *user_data)
{
    do_send(user_data);
}

/* -------------------------------------------------------------------------- */
/* 登录/注册                                                                   */
/* -------------------------------------------------------------------------- */

static void switch_to_login(comm_app_t *app);
static void switch_to_chat(comm_app_t *app);

/** 返回首页：先断开连接，避免后台线程继续收包。 */
static void comm_back_cb(lv_event_t *e)
{
    (void)e;
    /* 保持登录：连接与登录态由 comm_session 统一持有，返回首页不断线。 */
    (void)ui_app_manager_back();
}

/* -------------------------------------------------------------------------- */
/* 加好友弹窗                                                                  */
/* -------------------------------------------------------------------------- */

static void add_friend_confirm_cb(lv_event_t *e)
{
    comm_app_t *app = lv_event_get_user_data(e);
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

static void add_friend_clicked_cb(lv_event_t *e)
{
    comm_app_t *app = lv_event_get_user_data(e);
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
    lv_obj_set_size(kb, 420, 200);
    lv_obj_align(kb, LV_ALIGN_TOP_MID, 0, 68);
    lv_keyboard_set_textarea(kb, ta);

    btn = ui_button_primary_create(content, "发送好友请求", NULL, NULL);
    lv_obj_set_size(btn, 200, 52);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 280);
    lv_obj_add_event_cb(btn, add_friend_confirm_cb, LV_EVENT_CLICKED, app);
}

/* -------------------------------------------------------------------------- */
/* 视图切换                                                                    */
/* -------------------------------------------------------------------------- */

static void switch_to_chat(comm_app_t *app)
{
    lv_obj_t *root;

    if (app->login_box != NULL) {
        lv_obj_add_flag(app->login_box, LV_OBJ_FLAG_HIDDEN);
    }
    if (app->chat_box != NULL) {
        lv_obj_remove_flag(app->chat_box, LV_OBJ_FLAG_HIDDEN);
    }
    root = app->page;
    (void)root;
    if (app->title_label != NULL) {
        char title[64];
        snprintf(title, sizeof(title), "聊天室 · %s", app->self);
        lv_label_set_text(app->title_label, title);
    }
    rebuild_friend_list(app);
    render_messages(app);
}

static void switch_to_login(comm_app_t *app)
{
    if (app->chat_box != NULL) {
        lv_obj_add_flag(app->chat_box, LV_OBJ_FLAG_HIDDEN);
    }
    if (app->login_box != NULL) {
        lv_obj_remove_flag(app->login_box, LV_OBJ_FLAG_HIDDEN);
    }
}

/* -------------------------------------------------------------------------- */
/* 事件处理                                                                    */
/* -------------------------------------------------------------------------- */

static int find_friend(comm_app_t *app, const char *name)
{
    int i;
    for (i = 0; i < app->friend_count; i++) {
        if (strcmp(app->friends[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static void update_online_label(comm_app_t *app)
{
    int i;
    int n = 0;
    char buf[64];

    for (i = 0; i < app->friend_count; i++) {
        if (app->friends[i].online) {
            n++;
        }
    }
    snprintf(buf, sizeof(buf), "在线 %d/%d", n, app->friend_count);
    if (app->online_label != NULL) {
        lv_label_set_text(app->online_label, buf);
    }
}

static void apply_online_csv(comm_app_t *app, const char *csv)
{
    int i;
    char buf[512];
    char *save = NULL;
    char *tok;

    snprintf(buf, sizeof(buf), "%s", csv);
    for (i = 0; i < app->friend_count; i++) {
        app->friends[i].online = false;
    }
    for (tok = strtok_r(buf, ",", &save); tok != NULL; tok = strtok_r(NULL, ",", &save)) {
        int idx = find_friend(app, tok);
        if (idx >= 0) {
            app->friends[idx].online = true;
        }
    }
    rebuild_friend_list(app);
    update_online_label(app);
}

/** 会话事件回调（由 comm_session 分发，事件内存由 session 释放）。 */
static void on_session_event(void *user_data, comm_event_t *ev)
{
    comm_app_t *app = user_data;

    if (app == NULL || ev == NULL) {
        return;
    }

    switch (ev->kind) {
    case COMM_EVT_OK:
        if (ev->for_type == COMM_T_LOGIN) {
            snprintf(app->self, sizeof(app->self), "%s", comm_session_user());
            app->connected = true;
            switch_to_chat(app);
            comm_client_request_friends(app->client);
            ui_toast_show("登录成功");
        } else if (ev->for_type == COMM_T_ADD_FRIEND) {
            ui_toast_show("已添加好友");
        }
        break;

    case COMM_EVT_ERROR:
        ui_toast_show(ev->msg);
        break;

    case COMM_EVT_FRIENDS: {
        int i;
        app->friend_count = ev->friend_count < FRIEND_MAX ? ev->friend_count : FRIEND_MAX;
        for (i = 0; i < app->friend_count; i++) {
            snprintf(app->friends[i].name, sizeof(app->friends[i].name), "%s",
                     ev->friend_names[i]);
            app->friends[i].online = ev->friend_online[i];
            app->friends[i].unread = false;
        }
        if (app->peer[0] == '\0' && app->friend_count > 0) {
            select_peer(app, app->friends[0].name);
        }
        rebuild_friend_list(app);
        update_online_label(app);
        break;
    }

    case COMM_EVT_INCOMING: {
        int idx = find_friend(app, ev->user);
        add_log(app, ev->user, ev->user, false, ev->text, ev->ts);
        if (idx >= 0 && strcmp(app->peer, ev->user) == 0) {
            render_messages(app);
        } else {
            if (idx >= 0) {
                app->friends[idx].unread = true;
                rebuild_friend_list(app);
            }
            /* 未打开会话时也提示一下（离线补发很关键） */
            if (ev->offline_msg) {
                ui_toast_show("收到离线消息");
            }
        }
        break;
    }

    case COMM_EVT_PRESENCE: {
        int idx = find_friend(app, ev->user);
        if (idx >= 0) {
            app->friends[idx].online = ev->online;
            rebuild_friend_list(app);
            update_online_label(app);
        }
        break;
    }

    case COMM_EVT_ONLINE:
        apply_online_csv(app, ev->online_csv);
        break;

    case COMM_EVT_GROUP:
        add_log(app, GROUP_PEER, ev->user, false, ev->text, ev->ts);
        if (peer_is_group(app->peer)) {
            render_messages(app);
        } else {
            ui_toast_show("群聊有新消息");
        }
        break;

    case COMM_EVT_ADDED:
        ui_toast_show("有人加你为好友");
        comm_client_request_friends(app->client);
        break;

    case COMM_EVT_SYS:
        ui_toast_show(ev->msg);
        break;

    case COMM_EVT_DISCONNECTED:
        app->connected = false;
        switch_to_login(app);
        switch_to_login(app);
        break;
    }
}

/* -------------------------------------------------------------------------- */
/* 定时器                                                                      */
/* -------------------------------------------------------------------------- */

static void ping_timer_cb(lv_timer_t *timer)
{
    comm_app_t *app = lv_timer_get_user_data(timer);
    if (app != NULL && app->connected) {
        comm_client_ping(app->client);
    }
}

/* -------------------------------------------------------------------------- */
/* 页面构建                                                                    */
/* -------------------------------------------------------------------------- */

/** 未登录提示：账号是系统级的，登录入口在「账户」页。 */
static void goto_account_cb(lv_event_t *e)
{
    (void)e;
    (void)ui_app_manager_open("account");
}

static void build_need_login(comm_app_t *app)
{
    lv_obj_t *box = lv_obj_create(app->page);
    lv_obj_t *title;
    lv_obj_t *hint;
    lv_obj_t *btn;

    app->login_box = box;
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(box, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    title = small_label(box, "聊天室", ui_font_h1(), UI_COLOR_TEXT);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 120);

    hint = small_label(box, "还没有登录账号\n账号在「账户」页统一管理，聊天和围棋通用",
                       ui_font_small(), UI_COLOR_TEXT_SECONDARY);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 186);

    btn = ui_button_primary_create(box, "去登录", goto_account_cb, NULL);
    lv_obj_set_size(btn, 260, 64);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 262);
}
static void build_chat_view(comm_app_t *app)
{
    lv_obj_t *box = lv_obj_create(app->page);
    lv_obj_t *topbar;
    lv_obj_t *friends;
    lv_obj_t *msg_panel;
    lv_obj_t *input_row;

    app->chat_box = box;
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(box, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);

    topbar = ui_topbar_create(box, "聊天室", comm_back_cb, app);
    /* 有返回按钮时，标题是第 2 个子对象（back, title, ...） */
    app->title_label = lv_obj_get_child(topbar, 1);

    app->online_label = small_label(topbar, "在线 0/0", ui_font_caption(),
                                    UI_COLOR_TEXT_MUTED);

    {
        lv_obj_t *add = ui_topbar_add_action(topbar, "加好友", add_friend_clicked_cb, app);
        lv_obj_set_style_bg_color(add, UI_COLOR_SURFACE_ALT, 0);
    }

    /* 好友列表 */
    friends = lv_obj_create(box);
    lv_obj_remove_style_all(friends);
    lv_obj_set_size(friends, FRIEND_W, 600 - CHAT_TOP - 12);
    lv_obj_set_pos(friends, 8, CHAT_TOP);
    lv_obj_set_style_bg_color(friends, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(friends, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(friends, 14, 0);
    lv_obj_set_style_pad_all(friends, 8, 0);
    lv_obj_set_style_pad_row(friends, 6, 0);
    lv_obj_set_flex_flow(friends, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(friends, LV_DIR_VER);
    app->friend_list = friends;

    /* 消息面板 */
    msg_panel = lv_obj_create(box);
    lv_obj_remove_style_all(msg_panel);
    lv_obj_set_size(msg_panel, MSG_W, 600 - CHAT_TOP - 12);
    lv_obj_set_pos(msg_panel, 8 + FRIEND_W + 8, CHAT_TOP);
    lv_obj_set_style_bg_color(msg_panel, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(msg_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(msg_panel, 14, 0);
    lv_obj_remove_flag(msg_panel, LV_OBJ_FLAG_SCROLLABLE);

    app->msg_area = lv_obj_create(msg_panel);
    lv_obj_remove_style_all(app->msg_area);
    lv_obj_set_size(app->msg_area, MSG_W - 16, 600 - CHAT_TOP - 12 - 96);
    lv_obj_set_pos(app->msg_area, 8, 8);
    lv_obj_set_style_pad_row(app->msg_area, 8, 0);
    lv_obj_set_flex_flow(app->msg_area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(app->msg_area, LV_DIR_VER);
    lv_obj_set_style_bg_opa(app->msg_area, LV_OPA_TRANSP, 0);

    input_row = lv_obj_create(msg_panel);
    lv_obj_remove_style_all(input_row);
    lv_obj_set_size(input_row, MSG_W - 16, 72);
    lv_obj_set_pos(input_row, 8, 600 - CHAT_TOP - 12 - 84);
    lv_obj_set_style_bg_opa(input_row, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(input_row, LV_OBJ_FLAG_SCROLLABLE);

    app->ta_input = lv_textarea_create(input_row);
    lv_obj_set_size(app->ta_input, MSG_W - 16 - 76, 56);
    lv_obj_align(app->ta_input, LV_ALIGN_LEFT_MID, 0, 0);
    lv_textarea_set_one_line(app->ta_input, true);
    lv_textarea_set_max_length(app->ta_input, 200);
    lv_textarea_set_placeholder_text(app->ta_input, "说点什么…");
    lv_obj_set_style_text_font(app->ta_input, ui_font_small(), 0);
    lv_obj_set_style_bg_color(app->ta_input, UI_COLOR_SURFACE_ALT, 0);
    lv_obj_set_style_bg_opa(app->ta_input, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(app->ta_input, 12, 0);
    lv_obj_set_style_pad_all(app->ta_input, 10, 0);
    lv_obj_set_style_border_width(app->ta_input, 1, 0);
    lv_obj_set_style_border_color(app->ta_input, UI_COLOR_LINE, 0);

    app->send_btn = ui_button_primary_create(input_row, "发送", send_clicked_cb, app);
    lv_obj_set_size(app->send_btn, 68, 56);
    lv_obj_align(app->send_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(app->ta_input, input_ready_cb, LV_EVENT_READY, app);

    /* 右侧键盘（通用输入法组件） */
    app->ime = ui_ime_create(box, app->ta_input);
    if (app->ime != NULL) {
        ui_ime_set_geometry(app->ime, LV_ALIGN_TOP_RIGHT, -8, CHAT_TOP, IME_W,
                            600 - CHAT_TOP - 12);
        ui_ime_set_ready_cb(app->ime, ime_ready_cb, app);
    }
}

static lv_obj_t *comm_create(lv_obj_t *parent)
{
    comm_app_t *app = calloc(1, sizeof(*app));


    if (app == NULL) {
        return NULL;
    }
    s_app = app;
    app->page = parent;
    lv_obj_set_style_bg_color(parent, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    if (comm_session_init(board_app_paths()) != 0) {
        ui_toast_show("网络模块初始化失败");
    }
    /* 聊天记录目录（在 data/ 下），失败不影响聊天 */
    comm_history_init(board_app_paths());
    app->client = comm_session_client();
    if (comm_session_logged_in()) {
        app->connected = true;
    }
    if (comm_session_subscribe(on_session_event, app) != 0) {
        BOARD_LOGW("comm", "subscribe failed");
    }

    snprintf(app->peer, sizeof(app->peer), "%s", "");

    build_need_login(app);
    build_chat_view(app);
    switch_to_login(app);


    /* 会话已经登录（例如从围棋页回来）就不必再登录一次 */
    if (comm_session_logged_in()) {
        snprintf(app->self, sizeof(app->self), "%s", comm_session_user());
        switch_to_chat(app);
        comm_client_request_friends(app->client);
    }

    /* BOARD_APP_COMM_AUTOLOGIN="host:port,user,pass"：演示/看板场景直接登录 */
    /* 自动登录由 comm_session 统一处理（账户页 / 聊天室 / 围棋通用） */
    comm_session_auto_login();

    app->ping_timer = lv_timer_create(ping_timer_cb, 25000, app);
    return parent;
}

static void comm_destroy(void)
{
    comm_app_t *app = s_app;

    if (app == NULL) {
        return;
    }
    if (app->ping_timer != NULL) {
        lv_timer_delete(app->ping_timer);
        app->ping_timer = NULL;
    }
    comm_session_unsubscribe(on_session_event);
    app->client = NULL;
    ui_ime_destroy(app->ime);
    app->ime = NULL;
    free(app);
    s_app = NULL;
}

static const board_app_desc_t s_comm_app = {
    .id = "comm",
    .title = "聊天室",
    .subtitle = "TCP 私聊 · UDP 广播",
    .parent_id = NULL,
    .best_score_key = NULL,
    .icon = UI_ICON_COMM,
    .accent = 0x7FB0A9,
    .order = 4,
    .show_on_home = true,
    .create = comm_create,
    .destroy = comm_destroy,
};

BOARD_APP_REGISTER(s_comm_app);
