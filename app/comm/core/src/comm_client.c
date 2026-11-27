/**
 * @file comm_client.c
 * @brief 聊天客户端网络实现：TCP 收发线程 + UDP 收发线程 + 事件队列。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "comm/comm_client.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "board_core/board_log.h"
#include "cJSON.h"
#include "comm/comm_net.h"

struct comm_client {
    board_async_queue_t *queue;

    int tcp_fd;
    int udp_fd;
    uint16_t udp_port;
    char host[64];
    uint16_t port;

    pthread_t th_tcp;
    pthread_t th_udp;
    bool tcp_running;
    bool udp_running;
    volatile int stop;
    pthread_mutex_t send_lock;

    char user[COMM_MAX_NAME + 1];
    bool logged_in;
};

/* ------------------------------ 事件投递 ------------------------------ */

static void post(comm_client_t *c, comm_event_t *ev)
{
    if (c->queue == NULL || board_async_queue_push(c->queue, ev) != 0) {
        comm_event_free(ev);
        BOARD_LOGW("comm", "event queue full, event dropped");
    }
}

static comm_event_t *evt_new(comm_evt_kind_t kind)
{
    comm_event_t *ev = calloc(1, sizeof(*ev));
    if (ev != NULL) {
        ev->kind = kind;
    }
    return ev;
}

void comm_event_free(comm_event_t *ev)
{
    if (ev == NULL) {
        return;
    }
    free(ev->friend_names);
    free(ev->friend_online);
    free(ev);
}

/* ------------------------------ 发送 ------------------------------ */

static int send_json(comm_client_t *c, uint8_t type, cJSON *obj)
{
    char *text;
    uint8_t buf[COMM_MAX_FRAME + 8];
    size_t len = 0;
    int rc;

    if (c == NULL || c->tcp_fd < 0) {
        cJSON_Delete(obj);
        return -ENOTCONN;
    }
    text = obj != NULL ? cJSON_PrintUnformatted(obj) : strdup("{}");
    cJSON_Delete(obj);
    if (text == NULL) {
        return -ENOMEM;
    }
    rc = comm_frame_pack(type, text, strlen(text), buf, sizeof(buf), &len);
    free(text);
    if (rc != 0) {
        return rc;
    }
    pthread_mutex_lock(&c->send_lock);
    rc = comm_send_all(c->tcp_fd, buf, len);
    pthread_mutex_unlock(&c->send_lock);
    if (rc != 0) {
        BOARD_LOGW("comm", "send type=%u failed: %s", type, strerror(-rc));
    }
    return rc;
}

/* ------------------------------ TCP 收帧 ------------------------------ */

/** 处理一帧：解析 JSON，生成事件。 */
static void handle_frame(comm_client_t *c, uint8_t type, const char *body)
{
    cJSON *root = cJSON_Parse(body != NULL && body[0] != '\0' ? body : "{}");

    if (root == NULL) {
        BOARD_LOGW("comm", "bad json for type %u", type);
        return;
    }

    switch (type) {
    case COMM_T_OK: {
        comm_event_t *ev = evt_new(COMM_EVT_OK);
        const cJSON *msg = cJSON_GetObjectItemCaseSensitive(root, COMM_K_MSG);
        const cJSON *f = cJSON_GetObjectItemCaseSensitive(root, COMM_K_FOR);
        if (ev == NULL) {
            break;
        }
        ev->for_type = cJSON_IsNumber(f) ? (uint8_t)f->valueint : 0;
        snprintf(ev->msg, sizeof(ev->msg), "%s",
                 cJSON_IsString(msg) ? msg->valuestring : "");
        if (ev->for_type == COMM_T_LOGIN) {
            c->logged_in = true;
        }
        if (ev->for_type == COMM_T_LOGOUT) {
            c->logged_in = false;
        }
        post(c, ev);
        break;
    }
    case COMM_T_ERROR: {
        comm_event_t *ev = evt_new(COMM_EVT_ERROR);
        const cJSON *msg = cJSON_GetObjectItemCaseSensitive(root, COMM_K_MSG);
        const cJSON *f = cJSON_GetObjectItemCaseSensitive(root, COMM_K_FOR);
        const cJSON *code = cJSON_GetObjectItemCaseSensitive(root, COMM_K_CODE);
        if (ev == NULL) {
            break;
        }
        ev->for_type = cJSON_IsNumber(f) ? (uint8_t)f->valueint : 0;
        ev->code = cJSON_IsNumber(code) ? code->valueint : 0;
        snprintf(ev->msg, sizeof(ev->msg), "%s",
                 cJSON_IsString(msg) ? msg->valuestring : "");
        post(c, ev);
        break;
    }
    case COMM_T_FRIENDS: {
        const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, COMM_K_FRIENDS);
        int n = cJSON_IsArray(arr) ? cJSON_GetArraySize(arr) : 0;
        comm_event_t *ev = evt_new(COMM_EVT_FRIENDS);
        if (ev == NULL) {
            break;
        }
        if (n > 0) {
            ev->friend_names = calloc((size_t)n, sizeof(*ev->friend_names));
            ev->friend_online = calloc((size_t)n, sizeof(*ev->friend_online));
            if (ev->friend_names == NULL || ev->friend_online == NULL) {
                comm_event_free(ev);
                break;
            }
        }
        for (int i = 0; i < n; i++) {
            const cJSON *it = cJSON_GetArrayItem(arr, i);
            const cJSON *u = cJSON_GetObjectItemCaseSensitive(it, COMM_K_USER);
            const cJSON *on = cJSON_GetObjectItemCaseSensitive(it, COMM_K_ONLINE);
            if (!cJSON_IsString(u)) {
                continue;
            }
            snprintf(ev->friend_names[ev->friend_count],
                     sizeof(ev->friend_names[0]), "%s", u->valuestring);
            ev->friend_online[ev->friend_count] = cJSON_IsTrue(on);
            ev->friend_count++;
        }
        post(c, ev);
        break;
    }
    case COMM_T_INCOMING: {
        comm_event_t *ev = evt_new(COMM_EVT_INCOMING);
        const cJSON *from = cJSON_GetObjectItemCaseSensitive(root, COMM_K_FROM);
        const cJSON *text = cJSON_GetObjectItemCaseSensitive(root, COMM_K_TEXT);
        const cJSON *ts = cJSON_GetObjectItemCaseSensitive(root, COMM_K_TS);
        const cJSON *off = cJSON_GetObjectItemCaseSensitive(root, COMM_K_OFFLINE);
        if (ev == NULL) {
            break;
        }
        snprintf(ev->user, sizeof(ev->user), "%s",
                 cJSON_IsString(from) ? from->valuestring : "?");
        snprintf(ev->text, sizeof(ev->text), "%s",
                 cJSON_IsString(text) ? text->valuestring : "");
        ev->ts = cJSON_IsNumber(ts) ? (long)ts->valuedouble : 0;
        ev->offline_msg = cJSON_IsTrue(off);
        post(c, ev);
        break;
    }
    case COMM_T_PRESENCE: {
        comm_event_t *ev = evt_new(COMM_EVT_PRESENCE);
        const cJSON *u = cJSON_GetObjectItemCaseSensitive(root, COMM_K_USER);
        const cJSON *on = cJSON_GetObjectItemCaseSensitive(root, COMM_K_ONLINE);
        if (ev == NULL) {
            break;
        }
        snprintf(ev->user, sizeof(ev->user), "%s",
                 cJSON_IsString(u) ? u->valuestring : "?");
        ev->online = cJSON_IsTrue(on);
        post(c, ev);
        break;
    }
    case COMM_T_ADDED: {
        comm_event_t *ev = evt_new(COMM_EVT_ADDED);
        const cJSON *u = cJSON_GetObjectItemCaseSensitive(root, COMM_K_USER);
        if (ev == NULL) {
            break;
        }
        snprintf(ev->user, sizeof(ev->user), "%s",
                 cJSON_IsString(u) ? u->valuestring : "?");
        post(c, ev);
        break;
    }
    case COMM_T_SYS: {
        comm_event_t *ev = evt_new(COMM_EVT_SYS);
        const cJSON *m = cJSON_GetObjectItemCaseSensitive(root, COMM_K_MSG);
        if (ev == NULL) {
            break;
        }
        snprintf(ev->msg, sizeof(ev->msg), "%s",
                 cJSON_IsString(m) ? m->valuestring : "");
        post(c, ev);
        break;
    }
    case COMM_T_PONG:
        break;

    /* ---------------- 围棋对局 ---------------- */
    case COMM_T_GAME_INVITED: {
        comm_event_t *ev = evt_new(COMM_EVT_GAME_INVITED);
        const cJSON *from = cJSON_GetObjectItemCaseSensitive(root, COMM_K_FROM);
        const cJSON *size = cJSON_GetObjectItemCaseSensitive(root, COMM_K_SIZE);
        if (ev == NULL) {
            break;
        }
        snprintf(ev->user, sizeof(ev->user), "%s",
                 cJSON_IsString(from) ? from->valuestring : "?");
        ev->game_size = cJSON_IsNumber(size) ? size->valueint : 0;
        post(c, ev);
        break;
    }
    case COMM_T_GAME_START: {
        comm_event_t *ev = evt_new(COMM_EVT_GAME_START);
        const cJSON *opp = cJSON_GetObjectItemCaseSensitive(root, COMM_K_OPPONENT);
        const cJSON *size = cJSON_GetObjectItemCaseSensitive(root, COMM_K_SIZE);
        const cJSON *color = cJSON_GetObjectItemCaseSensitive(root, COMM_K_COLOR);
        if (ev == NULL) {
            break;
        }
        snprintf(ev->user, sizeof(ev->user), "%s",
                 cJSON_IsString(opp) ? opp->valuestring : "?");
        ev->game_size = cJSON_IsNumber(size) ? size->valueint : 0;
        ev->game_my_color = cJSON_IsNumber(color) ? color->valueint : 0;
        post(c, ev);
        break;
    }
    case COMM_T_GAME_STATE: {
        comm_event_t *ev = evt_new(COMM_EVT_GAME_STATE);
        const cJSON *board = cJSON_GetObjectItemCaseSensitive(root, COMM_K_BOARD);
        const cJSON *turn = cJSON_GetObjectItemCaseSensitive(root, COMM_K_TURN);
        const cJSON *bc = cJSON_GetObjectItemCaseSensitive(root, COMM_K_BLACK_CAP);
        const cJSON *wc = cJSON_GetObjectItemCaseSensitive(root, COMM_K_WHITE_CAP);
        const cJSON *lx = cJSON_GetObjectItemCaseSensitive(root, COMM_K_LAST_X);
        const cJSON *ly = cJSON_GetObjectItemCaseSensitive(root, COMM_K_LAST_Y);
        const cJSON *over = cJSON_GetObjectItemCaseSensitive(root, "over");
        const cJSON *result = cJSON_GetObjectItemCaseSensitive(root, COMM_K_RESULT);
        if (ev == NULL) {
            break;
        }
        snprintf(ev->game_board, sizeof(ev->game_board), "%s",
                 cJSON_IsString(board) ? board->valuestring : "");
        ev->game_turn = cJSON_IsNumber(turn) ? turn->valueint : 0;
        ev->game_black_cap = cJSON_IsNumber(bc) ? bc->valueint : 0;
        ev->game_white_cap = cJSON_IsNumber(wc) ? wc->valueint : 0;
        ev->game_last_x = cJSON_IsNumber(lx) ? lx->valueint : -1;
        ev->game_last_y = cJSON_IsNumber(ly) ? ly->valueint : -1;
        ev->game_over = cJSON_IsTrue(over);
        snprintf(ev->game_result, sizeof(ev->game_result), "%s",
                 cJSON_IsString(result) ? result->valuestring : "");
        post(c, ev);
        break;
    }
    case COMM_T_GAME_REJECT: {
        comm_event_t *ev = evt_new(COMM_EVT_GAME_REJECT);
        const cJSON *user = cJSON_GetObjectItemCaseSensitive(root, COMM_K_USER);
        const cJSON *msg = cJSON_GetObjectItemCaseSensitive(root, COMM_K_MSG);
        if (ev == NULL) {
            break;
        }
        snprintf(ev->user, sizeof(ev->user), "%s",
                 cJSON_IsString(user) ? user->valuestring : "?");
        snprintf(ev->msg, sizeof(ev->msg), "%s",
                 cJSON_IsString(msg) ? msg->valuestring : "对方拒绝了邀请");
        post(c, ev);
        break;
    }
    case COMM_T_GAME_END: {
        comm_event_t *ev = evt_new(COMM_EVT_GAME_END);
        const cJSON *result = cJSON_GetObjectItemCaseSensitive(root, COMM_K_RESULT);
        const cJSON *reason = cJSON_GetObjectItemCaseSensitive(root, COMM_K_REASON);
        if (ev == NULL) {
            break;
        }
        snprintf(ev->game_result, sizeof(ev->game_result), "%s",
                 cJSON_IsString(result) ? result->valuestring : "");
        snprintf(ev->msg, sizeof(ev->msg), "%s",
                 cJSON_IsString(reason) ? reason->valuestring : "");
        ev->game_over = true;
        post(c, ev);
        break;
    }
    case COMM_T_GAME_PEER_LEFT: {
        comm_event_t *ev = evt_new(COMM_EVT_GAME_PEER_LEFT);
        const cJSON *user = cJSON_GetObjectItemCaseSensitive(root, COMM_K_USER);
        if (ev == NULL) {
            break;
        }
        snprintf(ev->user, sizeof(ev->user), "%s",
                 cJSON_IsString(user) ? user->valuestring : "?");
        post(c, ev);
        break;
    }
    case COMM_T_GAME_INVITE_CANCEL: {
        comm_event_t *ev = evt_new(COMM_EVT_GAME_INVITE_CANCEL);
        const cJSON *user = cJSON_GetObjectItemCaseSensitive(root, COMM_K_USER);
        if (ev == NULL) {
            break;
        }
        snprintf(ev->user, sizeof(ev->user), "%s",
                 cJSON_IsString(user) ? user->valuestring : "?");
        post(c, ev);
        break;
    }
    default:
        BOARD_LOGD("comm", "unhandled frame type %u", type);
        break;
    }
    cJSON_Delete(root);
}

static void *tcp_reader(void *arg)
{
    comm_client_t *c = arg;
    uint8_t hdr[4];
    char *body = NULL;

    while (!c->stop) {
        uint32_t len;
        int rc = comm_recv_all(c->tcp_fd, hdr, sizeof(hdr), 1000);

        if (rc == -ETIMEDOUT) {
            continue;
        }
        if (rc != 0) {
            break;
        }
        len = ((uint32_t)hdr[0] << 24) | ((uint32_t)hdr[1] << 16) |
              ((uint32_t)hdr[2] << 8) | (uint32_t)hdr[3];
        if (len < 1 || len > COMM_MAX_FRAME) {
            break;
        }
        body = malloc(len + 1);
        if (body == NULL) {
            break;
        }
        rc = comm_recv_all(c->tcp_fd, body, len, 1000);
        if (rc != 0) {
            free(body);
            body = NULL;
            if (rc == -ETIMEDOUT) {
                continue;
            }
            break;
        }
        {
            uint8_t type = body[0];
            memmove(body, body + 1, len - 1);
            body[len - 1] = '\0';
            handle_frame(c, type, body);
        }
        free(body);
        body = NULL;
    }

    if (!c->stop) {
        comm_event_t *ev = evt_new(COMM_EVT_DISCONNECTED);
        if (ev != NULL) {
            post(c, ev);
        }
    }
    return NULL;
}

/* ------------------------------ UDP 收包 ------------------------------ */

static void *udp_reader(void *arg)
{
    comm_client_t *c = arg;
    uint8_t packet[2048];

    while (!c->stop) {
        char from_ip[INET_ADDRSTRLEN];
        uint16_t from_port;
        int n = comm_udp_recv(c->udp_fd, packet, sizeof(packet), 300, from_ip,
                             &from_port);

        if (n < 2) {
            continue;
        }
        packet[n] = '\0';
        {
            uint8_t type = packet[0];
            cJSON *root = cJSON_Parse((char *)packet + 1);
            if (root == NULL) {
                continue;
            }
            if (type == COMM_U_ONLINE) {
                comm_event_t *ev = evt_new(COMM_EVT_ONLINE);
                const cJSON *arr =
                    cJSON_GetObjectItemCaseSensitive(root, COMM_K_ONLINE);
                const cJSON *it;
                size_t used = 0;
                if (ev != NULL) {
                    cJSON_ArrayForEach(it, arr)
                    {
                        if (!cJSON_IsString(it)) {
                            continue;
                        }
                        size_t need = strlen(it->valuestring) + 2;
                        if (used + need + 1 >= sizeof(ev->online_csv)) {
                            break;
                        }
                        if (used > 0) {
                            ev->online_csv[used++] = ',';
                        }
                        memcpy(ev->online_csv + used, it->valuestring,
                               strlen(it->valuestring));
                        used += strlen(it->valuestring);
                    }
                    ev->online_csv[used] = '\0';
                    post(c, ev);
                }
            } else if (type == COMM_U_GROUP_INCOMING) {
                comm_event_t *ev = evt_new(COMM_EVT_GROUP);
                const cJSON *from =
                    cJSON_GetObjectItemCaseSensitive(root, COMM_K_FROM);
                const cJSON *text =
                    cJSON_GetObjectItemCaseSensitive(root, COMM_K_TEXT);
                const cJSON *ts = cJSON_GetObjectItemCaseSensitive(root, COMM_K_TS);
                if (ev != NULL) {
                    snprintf(ev->user, sizeof(ev->user), "%s",
                             cJSON_IsString(from) ? from->valuestring : "?");
                    snprintf(ev->text, sizeof(ev->text), "%s",
                             cJSON_IsString(text) ? text->valuestring : "");
                    ev->ts = cJSON_IsNumber(ts) ? (long)ts->valuedouble : 0;
                    post(c, ev);
                }
            }
            cJSON_Delete(root);
        }
        (void)from_ip;
        (void)from_port;
    }
    return NULL;
}

/* ------------------------------ 生命周期 ------------------------------ */

comm_client_t *comm_client_create(board_async_queue_t *queue)
{
    comm_client_t *c;

    if (queue == NULL) {
        return NULL;
    }
    c = calloc(1, sizeof(*c));
    if (c == NULL) {
        return NULL;
    }
    c->queue = queue;
    c->tcp_fd = -1;
    c->udp_fd = -1;
    pthread_mutex_init(&c->send_lock, NULL);
    return c;
}

void comm_client_disconnect(comm_client_t *c)
{
    if (c == NULL) {
        return;
    }
    c->stop = 1;
    if (c->tcp_fd >= 0) {
        shutdown(c->tcp_fd, SHUT_RDWR);
    }
    if (c->udp_fd >= 0) {
        shutdown(c->udp_fd, SHUT_RDWR);
    }
    if (c->tcp_running) {
        pthread_join(c->th_tcp, NULL);
        c->tcp_running = false;
    }
    if (c->udp_running) {
        pthread_join(c->th_udp, NULL);
        c->udp_running = false;
    }
    if (c->tcp_fd >= 0) {
        close(c->tcp_fd);
        c->tcp_fd = -1;
    }
    if (c->udp_fd >= 0) {
        close(c->udp_fd);
        c->udp_fd = -1;
    }
    c->logged_in = false;
}

void comm_client_destroy(comm_client_t *c)
{
    if (c == NULL) {
        return;
    }
    comm_client_disconnect(c);
    pthread_mutex_destroy(&c->send_lock);
    free(c);
}

int comm_client_connect(comm_client_t *c, const char *host, uint16_t port)
{
    int fd;

    if (c == NULL || host == NULL || port == 0) {
        return -EINVAL;
    }
    if (c->tcp_fd >= 0) {
        comm_client_disconnect(c);
    }
    c->stop = 0;
    snprintf(c->host, sizeof(c->host), "%s", host);
    c->port = port;

    fd = comm_tcp_connect(host, port, 4000);
    if (fd < 0) {
        BOARD_LOGW("comm", "connect %s:%u failed: %s", host, port, strerror(-fd));
        return fd;
    }
    c->tcp_fd = fd;

    /* 客户端 UDP 端口交给内核分配，登录时上报给服务器 */
    c->udp_fd = comm_udp_bind("0.0.0.0", 0);
    if (c->udp_fd < 0) {
        BOARD_LOGW("comm", "udp bind failed: %s", strerror(-c->udp_fd));
        close(c->tcp_fd);
        c->tcp_fd = -1;
        return c->udp_fd;
    }
    c->udp_port = (uint16_t)comm_local_port(c->udp_fd);

    if (pthread_create(&c->th_tcp, NULL, tcp_reader, c) != 0) {
        return -EAGAIN;
    }
    c->tcp_running = true;
    if (pthread_create(&c->th_udp, NULL, udp_reader, c) != 0) {
        return -EAGAIN;
    }
    c->udp_running = true;

    BOARD_LOGI("comm", "connected %s:%u (local udp %u)", host, port,
               (unsigned)c->udp_port);
    return 0;
}

bool comm_client_connected(const comm_client_t *c)
{
    return c != NULL && c->tcp_fd >= 0;
}

uint16_t comm_client_local_udp_port(const comm_client_t *c)
{
    return c != NULL ? c->udp_port : 0;
}

/* ------------------------------ 请求 ------------------------------ */

int comm_client_register(comm_client_t *c, const char *user, const char *pass)
{
    cJSON *o;

    if (!comm_name_valid(user) || pass == NULL || pass[0] == '\0') {
        return -EINVAL;
    }
    o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, COMM_K_USER, user);
    cJSON_AddStringToObject(o, COMM_K_PASS, pass);
    return send_json(c, COMM_T_REGISTER, o);
}

int comm_client_login(comm_client_t *c, const char *user, const char *pass)
{
    cJSON *o;

    if (!comm_name_valid(user) || pass == NULL || pass[0] == '\0') {
        return -EINVAL;
    }
    o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, COMM_K_USER, user);
    cJSON_AddStringToObject(o, COMM_K_PASS, pass);
    cJSON_AddNumberToObject(o, COMM_K_UDP_PORT, c->udp_port);
    snprintf(c->user, sizeof(c->user), "%s", user);
    return send_json(c, COMM_T_LOGIN, o);
}

int comm_client_add_friend(comm_client_t *c, const char *user)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, COMM_K_USER, user);
    return send_json(c, COMM_T_ADD_FRIEND, o);
}

int comm_client_del_friend(comm_client_t *c, const char *user)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, COMM_K_USER, user);
    return send_json(c, COMM_T_DEL_FRIEND, o);
}

int comm_client_request_friends(comm_client_t *c)
{
    return send_json(c, COMM_T_FRIEND_LIST, cJSON_CreateObject());
}

int comm_client_send_text(comm_client_t *c, const char *to, const char *text)
{
    cJSON *o;

    if (!comm_name_valid(to) || !comm_text_valid(text)) {
        return -EINVAL;
    }
    o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, COMM_K_TO, to);
    cJSON_AddStringToObject(o, COMM_K_TEXT, text);
    return send_json(c, COMM_T_PRIVATE, o);
}

int comm_client_send_group(comm_client_t *c, const char *text)
{
    cJSON *o;
    char *json;
    uint8_t packet[1500];
    size_t len;

    if (c == NULL || c->udp_fd < 0 || !comm_text_valid(text)) {
        return -EINVAL;
    }
    o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, COMM_K_TEXT, text);
    json = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (json == NULL) {
        return -ENOMEM;
    }
    len = strlen(json);
    if (len + 2 > sizeof(packet)) {
        free(json);
        return -ENOSPC;
    }
    packet[0] = COMM_U_GROUP_CHAT;
    memcpy(packet + 1, json, len);
    free(json);

    /* UDP 群聊发给服务器的 UDP 端口（约定 = TCP 端口 + 1） */
    return comm_udp_send(c->udp_fd, c->host, (uint16_t)(c->port + 1), packet,
                         len + 1);
}

int comm_client_ping(comm_client_t *c)
{
    return send_json(c, COMM_T_PING, cJSON_CreateObject());
}

/* ------------------------------ 围棋对局 ------------------------------ */

int comm_client_game_invite(comm_client_t *c, const char *user, int size)
{
    cJSON *o;

    if (!comm_name_valid(user) || (size != 9 && size != 13 && size != 19)) {
        return -EINVAL;
    }
    o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, COMM_K_USER, user);
    cJSON_AddNumberToObject(o, COMM_K_SIZE, size);
    return send_json(c, COMM_T_GAME_INVITE, o);
}

int comm_client_game_accept(comm_client_t *c, const char *from)
{
    cJSON *o;

    if (!comm_name_valid(from)) {
        return -EINVAL;
    }
    o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, COMM_K_FROM, from);
    return send_json(c, COMM_T_GAME_ACCEPT, o);
}

int comm_client_game_move(comm_client_t *c, int x, int y)
{
    cJSON *o;

    if (x < 0 || y < 0 || x >= 19 || y >= 19) {
        return -EINVAL;
    }
    o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, COMM_K_X, x);
    cJSON_AddNumberToObject(o, COMM_K_Y, y);
    return send_json(c, COMM_T_GAME_MOVE, o);
}

int comm_client_game_pass(comm_client_t *c)
{
    return send_json(c, COMM_T_GAME_PASS, cJSON_CreateObject());
}

int comm_client_game_resign(comm_client_t *c)
{
    return send_json(c, COMM_T_GAME_RESIGN, cJSON_CreateObject());
}

int comm_client_game_leave(comm_client_t *c)
{
    return send_json(c, COMM_T_GAME_LEAVE, cJSON_CreateObject());
}

int comm_client_game_cancel(comm_client_t *c, const char *user)
{
    cJSON *o;

    if (!comm_name_valid(user)) {
        return -EINVAL;
    }
    o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, COMM_K_USER, user);
    return send_json(c, COMM_T_GAME_CANCEL, o);
}
