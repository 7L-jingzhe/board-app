/**
 * @file ui_comm_session.c
 * @brief 共享通信会话实现（单例）。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "board_ui/ui_comm_session.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_core/board_log.h"
#include "board_core/board_storage.h"
#include "comm/comm_history.h"
#include "board_ui/ui_async.h"

typedef struct {
    comm_session_evt_fn fn;
    void *user;
} subscriber_t;

typedef struct {
    char name[COMM_MAX_NAME + 1];
    bool online;
} cached_friend_t;

static struct {
    bool ready;
    board_async_queue_t *queue;
    ui_async_pump_t *pump;
    comm_client_t *client;
    bool logged_in;
    char user[COMM_MAX_NAME + 1];

    subscriber_t subs[COMM_SESSION_MAX_SUBS];
    int sub_count;

    cached_friend_t friends[COMM_SESSION_MAX_FRIENDS];
    int friend_count;

    /* 待处理的对局邀请（围棋页未打开时先存着） */
    char pending_invite[COMM_MAX_NAME + 1];
    int pending_invite_size;

    /* 没有订阅者时收到的消息（聊天页未打开先存着，订阅后补投） */
    struct {
        char from[COMM_MAX_NAME + 1];
        char text[COMM_MAX_TEXT + 1];
        long ts;
        bool offline;
        bool group;
    } pending_msgs[16];
    int pending_count;
} s;

static void cache_friends(comm_event_t *ev)
{
    int i;

    s.friend_count = ev->friend_count < COMM_SESSION_MAX_FRIENDS ? ev->friend_count
                                                               : COMM_SESSION_MAX_FRIENDS;
    for (i = 0; i < s.friend_count; i++) {
        snprintf(s.friends[i].name, sizeof(s.friends[i].name), "%s",
                 ev->friend_names[i]);
        s.friends[i].online = ev->friend_online[i];
    }
}

static void update_presence(const char *name, bool online)
{
    int i;
    for (i = 0; i < s.friend_count; i++) {
        if (strcmp(s.friends[i].name, name) == 0) {
            s.friends[i].online = online;
            return;
        }
    }
}

static void pump_cb(void *item, void *user)
{
    comm_event_t *ev = item;
    int i;

    (void)user;
    if (ev == NULL) {
        return;
    }

    /* 会话自己维护的状态 */
    switch (ev->kind) {
    case COMM_EVT_FRIENDS:
        cache_friends(ev);
        break;
    case COMM_EVT_PRESENCE:
        update_presence(ev->user, ev->online);
        break;
    case COMM_EVT_ONLINE: {
        /* UDP 在线名单：把好友表里出现的名字标记为在线 */
        char buf[512];
        char *save = NULL;
        char *tok;
        int k;
        snprintf(buf, sizeof(buf), "%s", ev->online_csv);
        for (k = 0; k < s.friend_count; k++) {
            s.friends[k].online = false;
        }
        for (tok = strtok_r(buf, ",", &save); tok != NULL;
             tok = strtok_r(NULL, ",", &save)) {
            update_presence(tok, true);
        }
        break;
    }
    case COMM_EVT_OK:
        if (ev->for_type == COMM_T_LOGIN) {
            s.logged_in = true;
        }
        break;
    case COMM_EVT_INCOMING:
    case COMM_EVT_GROUP:
        /* 收到就立刻落盘：不管聊天页有没有打开、有没有订阅者。
         * 页面显示时直接读历史文件，内存和文件不会不一致。 */
        comm_history_append(ev->kind == COMM_EVT_GROUP ? COMM_HISTORY_GROUP : ev->user,
                            ev->user, false, ev->text, ev->ts);
        /* 订阅者还不存在时记一笔，页面订阅后可以据此重读历史（不做消息副本） */
        if (s.sub_count == 0) {
            s.pending_count++;
        }
        break;
    case COMM_EVT_GAME_INVITED:
        snprintf(s.pending_invite, sizeof(s.pending_invite), "%s", ev->user);
        s.pending_invite_size = ev->game_size;
        break;
    case COMM_EVT_DISCONNECTED:
        s.logged_in = false;
        s.pending_invite[0] = '\0';
        break;
    default:
        break;
    }

    /* 分发给订阅者（只读） */
    for (i = 0; i < s.sub_count; i++) {
        if (s.subs[i].fn != NULL) {
            s.subs[i].fn(s.subs[i].user, ev);
        }
    }
    comm_event_free(ev);
}

int comm_session_init(const board_paths_t *paths)
{
    if (s.ready) {
        return 0;
    }
    /* 关键：聊天记录在**登录之前**就初始化好，网络线程一收到消息就能归档，
     * 不依赖聊天页是否打开。 */
    if (paths != NULL) {
        comm_history_init(paths);
    }
    s.queue = board_async_queue_create(16);
    if (s.queue == NULL) {
        return -ENOMEM;
    }
    s.client = comm_client_create(s.queue);
    if (s.client == NULL) {
        return -ENOMEM;
    }
    s.pump = ui_async_pump_create(s.queue, pump_cb, NULL, 40);
    if (s.pump == NULL) {
        comm_client_destroy(s.client);
        s.client = NULL;
        board_async_queue_destroy(s.queue);
        s.queue = NULL;
        return -ENOMEM;
    }
    s.ready = true;
    BOARD_LOGI("comm", "session ready");
    return 0;
}

comm_client_t *comm_session_client(void)
{
    return s.client;
}

bool comm_session_logged_in(void)
{
    return s.logged_in;
}

const char *comm_session_user(void)
{
    return s.user;
}

void comm_session_set_user(const char *user)
{
    snprintf(s.user, sizeof(s.user), "%s", user != NULL ? user : "");
}

int comm_session_subscribe(comm_session_evt_fn fn, void *user)
{
    int i;

    if (fn == NULL) {
        return -EINVAL;
    }
    for (i = 0; i < s.sub_count; i++) {
        if (s.subs[i].fn == fn) {
            s.subs[i].user = user;
            return 0;
        }
    }
    if (s.sub_count >= COMM_SESSION_MAX_SUBS) {
        return -ENOSPC;
    }
    s.subs[s.sub_count].fn = fn;
    s.subs[s.sub_count].user = user;
    s.sub_count++;
    return 0;
}

void comm_session_unsubscribe(comm_session_evt_fn fn)
{
    int i;

    for (i = 0; i < s.sub_count; i++) {
        if (s.subs[i].fn == fn) {
            s.subs[i] = s.subs[s.sub_count - 1];
            s.sub_count--;
            return;
        }
    }
}

int comm_session_friend_count(void)
{
    return s.friend_count;
}

const char *comm_session_friend_name(int index)
{
    if (index < 0 || index >= s.friend_count) {
        return NULL;
    }
    return s.friends[index].name;
}

bool comm_session_friend_online(int index)
{
    if (index < 0 || index >= s.friend_count) {
        return false;
    }
    return s.friends[index].online;
}

int comm_session_friend_state(const char *name)
{
    int i;

    if (name == NULL) {
        return -1;
    }
    for (i = 0; i < s.friend_count; i++) {
        if (strcmp(s.friends[i].name, name) == 0) {
            return s.friends[i].online ? 1 : 0;
        }
    }
    return -1;
}

int comm_session_take_pending_message(char *from, size_t from_cap, char *text,
                                      size_t text_cap, long *ts, bool *offline,
                                      bool *is_group)
{
    int k;
    int m;

    if (s.pending_count <= 0) {
        return 0;
    }
    k = 0;
    if (from != NULL && from_cap > 0) {
        snprintf(from, from_cap, "%s", s.pending_msgs[k].from);
    }
    if (text != NULL && text_cap > 0) {
        snprintf(text, text_cap, "%s", s.pending_msgs[k].text);
    }
    if (ts != NULL) {
        *ts = s.pending_msgs[k].ts;
    }
    if (offline != NULL) {
        *offline = s.pending_msgs[k].offline;
    }
    if (is_group != NULL) {
        *is_group = s.pending_msgs[k].group;
    }
    /* 前移队列 */
    for (m = 1; m < s.pending_count; m++) {
        s.pending_msgs[m - 1] = s.pending_msgs[m];
    }
    s.pending_count--;
    return 1;
}

int comm_session_take_pending_invite(char *user, size_t user_cap, int *size)
{
    if (s.pending_invite[0] == '\0') {
        return 0;
    }
    if (user != NULL && user_cap > 0) {
        snprintf(user, user_cap, "%s", s.pending_invite);
    }
    if (size != NULL) {
        *size = s.pending_invite_size;
    }
    s.pending_invite[0] = '\0';
    s.pending_invite_size = 0;
    return 1;
}

/** 本地弱混淆：避免密码以明文躺在配置文件里（不是加密，仅防随手翻看）。 */
static void obscure(const char *in, char *out, size_t cap)
{
    static const char HEX[] = "0123456789abcdef";
    size_t i;
    size_t n = strlen(in);

    if (n * 2 + 1 > cap) {
        n = (cap - 1) / 2;
    }
    for (i = 0; i < n; i++) {
        uint8_t b = (uint8_t)((uint8_t)in[i] ^ (uint8_t)(0x5A + (i * 7u)));
        out[i * 2] = HEX[b >> 4];
        out[i * 2 + 1] = HEX[b & 0x0F];
    }
    out[n * 2] = '\0';
}

static void deobscure(const char *in, char *out, size_t cap)
{
    size_t len = strlen(in);
    size_t i;

    if (len % 2 != 0 || len / 2 + 1 > cap) {
        out[0] = '\0';
        return;
    }
    for (i = 0; i < len / 2; i++) {
        int hi = in[i * 2];
        int lo = in[i * 2 + 1];
        int v;
        hi = (hi >= '0' && hi <= '9') ? hi - '0' : (hi | 0x20) - 'a' + 10;
        lo = (lo >= '0' && lo <= '9') ? lo - '0' : (lo | 0x20) - 'a' + 10;
        v = ((hi << 4) | lo) & 0xFF;
        out[i] = (char)(v ^ (uint8_t)(0x5A + (i * 7u)));
    }
    out[len / 2] = '\0';
}

void comm_session_remember(const char *user, const char *pass)
{
    char enc[COMM_MAX_PASS * 2 + 2];

    if (user == NULL || pass == NULL || user[0] == '\0') {
        return;
    }
    board_storage_set_str("comm.user", user);
    obscure(pass, enc, sizeof(enc));
    board_storage_set_str("comm.pass", enc);
    board_storage_set_str("comm.remember", "1");
    board_storage_save();
}

/** board_storage_get_str 返回共享缓冲区，必须立刻拷走再取下一个键。 */
static void storage_copy(const char *key, char *out, size_t cap)
{
    const char *v = board_storage_get_str(key, "");

    snprintf(out, cap, "%s", v != NULL ? v : "");
}

bool comm_session_has_saved_login(void)
{
    char user[COMM_MAX_NAME + 1] = { 0 };
    char pass[COMM_MAX_PASS * 2 + 2] = { 0 };

    storage_copy("comm.user", user, sizeof(user));
    storage_copy("comm.pass", pass, sizeof(pass));
    return user[0] != '\0' && pass[0] != '\0';
}

void comm_session_auto_login(void)
{
    static bool tried;
    const char *env = NULL;
    char host[64] = { 0 };
    char user[COMM_MAX_NAME + 1] = { 0 };
    char pass[COMM_MAX_PASS + 1] = { 0 };
    char ip[64] = { 0 };
    uint16_t port = COMM_DEFAULT_PORT;
    const char *colon;

    if (tried || s.logged_in || s.client == NULL) {
        return;
    }
    tried = true;

    /* 1) 环境变量优先（演示/看板） */
    env = getenv("BOARD_APP_COMM_AUTOLOGIN");
    if (env != NULL && env[0] != '\0') {
        if (sscanf(env, "%63[^,],%24[^,],%64[^\n]", host, user, pass) != 3) {
            BOARD_LOGW("comm", "BOARD_APP_COMM_AUTOLOGIN 格式应为 host:port,user,pass");
            return;
        }
    } else {
        /* 2) 上次登录记住的账号（注意：storage 返回共享缓冲区，逐个拷走） */
        char saved_user[COMM_MAX_NAME + 1] = { 0 };
        char saved_pass[COMM_MAX_PASS * 2 + 2] = { 0 };
        char saved_host[64] = { 0 };

        storage_copy("comm.user", saved_user, sizeof(saved_user));
        storage_copy("comm.pass", saved_pass, sizeof(saved_pass));
        storage_copy("comm.host", saved_host, sizeof(saved_host));

        if (saved_user[0] == '\0' || saved_pass[0] == '\0') {
            return;
        }
        snprintf(user, sizeof(user), "%s", saved_user);
        deobscure(saved_pass, pass, sizeof(pass));
        /* 没有记住的服务器地址就不自动登录（地址不写死） */
        if (saved_host[0] == '\0' || pass[0] == '\0') {
            return;
        }
        snprintf(host, sizeof(host), "%s", saved_host);
        BOARD_LOGI("comm", "发现上次登录的账号 %s，尝试自动登录", user);
    }
    colon = strrchr(host, ':');
    if (colon != NULL) {
        size_t n = (size_t)(colon - host);
        if (n >= sizeof(ip)) {
            return;
        }
        memcpy(ip, host, n);
        ip[n] = '\0';
        port = (uint16_t)atoi(colon + 1);
    } else {
        snprintf(ip, sizeof(ip), "%s", host);
    }
    if (port == 0) {
        return;
    }
    if (comm_client_connect(s.client, ip, port) != 0) {
        BOARD_LOGW("comm", "autologin: connect %s:%u failed", ip, (unsigned)port);
        return;
    }
    comm_session_set_user(user);
    if (comm_client_login(s.client, user, pass) != 0) {
        BOARD_LOGW("comm", "autologin: login request failed");
        return;
    }
    /* 下次开机继续用这个账号 */
    comm_session_remember(user, pass);
    BOARD_LOGI("comm", "autologin as %s -> %s:%u", user, ip, (unsigned)port);
}

void comm_session_logout(void)
{
    if (s.client != NULL) {
        comm_client_disconnect(s.client);
    }
    s.logged_in = false;
    s.user[0] = '\0';
    s.friend_count = 0;
    /* 主动退出：清除记住的密码，下次开机不再自动登录（用户名保留便于重新登录） */
    board_storage_set_str("comm.pass", "");
    board_storage_set_str("comm.remember", "0");
    board_storage_save();
}
