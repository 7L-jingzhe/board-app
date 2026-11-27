/**
 * @file comm_client.h
 * @brief 聊天客户端网络核心：TCP 长连接（可靠）+ UDP（在线/群聊，不可靠）。
 *
 * 线程模型与天气/聊天一致：
 *   UI 线程调用 comm_client_* 发送 -> 内部加锁写 socket
 *   网络线程收帧 -> 解析成 comm_event_t -> board_async_queue -> UI 线程消费
 *
 * 这个模块不依赖 LVGL（core 层约束），UI 只处理事件。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef COMM_CLIENT_H
#define COMM_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board_core/board_async_queue.h"
#include "comm/comm_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COMM_MAX_FRIENDS 64

/** 网络层上报给 UI 的事件类型。 */
typedef enum {
    COMM_EVT_OK = 0,        /* 请求成功（for_type/code/msg） */
    COMM_EVT_ERROR,         /* 请求失败 */
    COMM_EVT_FRIENDS,       /* 好友列表（含在线状态） */
    COMM_EVT_INCOMING,      /* 收到私聊（offline=1 表示离线补发） */
    COMM_EVT_PRESENCE,      /* 好友上下线 */
    COMM_EVT_ONLINE,        /* UDP：当前在线名单 */
    COMM_EVT_GROUP,         /* UDP：群聊消息 */
    COMM_EVT_ADDED,         /* 有人加你为好友 */
    COMM_EVT_SYS,           /* 服务器公告 */
    COMM_EVT_DISCONNECTED,  /* 连接断开 */

    /* 围棋对局 */
    COMM_EVT_GAME_INVITED,  /* 有人邀请你对局 */
    COMM_EVT_GAME_START,    /* 对局开始（含你的执子颜色） */
    COMM_EVT_GAME_STATE,    /* 服务器下发的权威局面 */
    COMM_EVT_GAME_REJECT,   /* 邀请被拒绝 */
    COMM_EVT_GAME_END,      /* 对局结束（含结果） */
    COMM_EVT_GAME_PEER_LEFT,/* 对手离开 */
    COMM_EVT_GAME_INVITE_CANCEL, /* 对方撤回了邀请 */
} comm_evt_kind_t;

typedef struct {
    comm_evt_kind_t kind;
    uint8_t for_type;                  /* OK/ERROR 对应的请求类型 */
    int code;                          /* ERROR 的 errno 风格码 */
    char msg[192];                     /* OK/ERROR/SYS 的文本 */
    char user[COMM_MAX_NAME + 1];      /* 相关用户名（对手/邀请人） */
    char text[COMM_MAX_TEXT + 1];      /* 消息正文 */
    long ts;
    bool online;                       /* PRESENCE 的在线状态 */
    bool offline_msg;                  /* INCOMING 是否离线补发 */

    /* FRIENDS：条目数组（malloc，用 comm_event_free 释放） */
    int friend_count;
    char (*friend_names)[COMM_MAX_NAME + 1];
    bool *friend_online;

    /* ONLINE：逗号分隔的在线名单（够 UI 显示用） */
    char online_csv[512];

    /* ---- 围棋对局（GAME_* 事件） ---- */
    int game_size;                     /* 9 / 13 / 19 */
    int game_turn;                     /* 1 黑 2 白 */
    int game_black_cap;
    int game_white_cap;
    int game_last_x;                   /* -1 表示上一手是 pass */
    int game_last_y;
    int game_my_color;                 /* 1 黑 2 白（START 时告知） */
    bool game_over;
    char game_result[96];
    char game_board[COMM_BOARD_MAX];   /* 'B'/'W'/'.' 按行拼接 */
} comm_event_t;

typedef struct comm_client comm_client_t;

/** 创建客户端；queue 用于把事件投递给 UI 线程。 */
comm_client_t *comm_client_create(board_async_queue_t *queue);
void comm_client_destroy(comm_client_t *c);

/**
 * 连接服务器并启动收发线程（不阻塞登录）。
 * @return 0 成功；负值为 -errno。
 */
int comm_client_connect(comm_client_t *c, const char *host, uint16_t port);
void comm_client_disconnect(comm_client_t *c);
bool comm_client_connected(const comm_client_t *c);

/** 本机 UDP 端口（登录时自动上报给服务器）。 */
uint16_t comm_client_local_udp_port(const comm_client_t *c);

int comm_client_register(comm_client_t *c, const char *user, const char *pass);
int comm_client_login(comm_client_t *c, const char *user, const char *pass);
int comm_client_add_friend(comm_client_t *c, const char *user);
int comm_client_del_friend(comm_client_t *c, const char *user);
int comm_client_request_friends(comm_client_t *c);
/** 私聊（TCP，可靠；对方离线时服务器暂存）。 */
int comm_client_send_text(comm_client_t *c, const char *to, const char *text);
/** 群聊广播（UDP，不保证送达）。 */
int comm_client_send_group(comm_client_t *c, const char *text);
/** 心跳（可选，UI 定时调用）。 */
int comm_client_ping(comm_client_t *c);

/* ---- 围棋对局（服务器是权威判定方，客户端只发意图） ---- */

/** 邀请好友对局。@param size 9/13/19 */
int comm_client_game_invite(comm_client_t *c, const char *user, int size);
/** 接受邀请。 */
int comm_client_game_accept(comm_client_t *c, const char *from);
/** 落子。 */
int comm_client_game_move(comm_client_t *c, int x, int y);
/** 停一手。 */
int comm_client_game_pass(comm_client_t *c);
/** 认输。 */
int comm_client_game_resign(comm_client_t *c);
/** 离开对局（对手会收到通知）。 */
int comm_client_game_leave(comm_client_t *c);
/** 撤回自己发出的邀请。 */
int comm_client_game_cancel(comm_client_t *c, const char *user);

/** 释放事件里 malloc 的部分（UI 处理完必须调用）。 */
void comm_event_free(comm_event_t *ev);

#ifdef __cplusplus
}
#endif

#endif /* COMM_CLIENT_H */
