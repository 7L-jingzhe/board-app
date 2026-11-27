/**
 * @file ui_comm_session.h
 * @brief 全局共享的通信会话：一个账号只登录一次，多个应用共用。
 *
 * 为什么需要它：聊天室和联网围棋都要用账号/好友/连接。如果每个应用各开一个
 * `comm_client`，同名账号会被服务器踢下线（后登录者踢掉先登录者）。
 * 所以连接、登录态、好友列表、事件流统一放在这个单例里，应用只订阅事件。
 *
 * 使用约定：
 *   - 任何页面首次进入时调用 comm_session_init(queue)；
 *   - 页面销毁时只 unsubscribe，**不要**销毁 session（保持登录）；
 *   - 事件由 session 统一释放，订阅者只读。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef UI_COMM_SESSION_H
#define UI_COMM_SESSION_H

#include <stdbool.h>

#include "board_core/board_async_queue.h"
#include "board_core/board_paths.h"
#include "comm/comm_client.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COMM_SESSION_MAX_FRIENDS COMM_MAX_FRIENDS
#define COMM_SESSION_MAX_SUBS 4

typedef void (*comm_session_evt_fn)(void *user, comm_event_t *ev);

/**
 * 初始化（幂等）。内部自建事件队列，多个应用共用。
 *
 * @param paths 用于创建聊天记录目录（data/comm_history）；首次调用应传
 *              `board_app_paths()`。**必须在登录之前调用**——这样网络线程
 *              收到消息时立刻归档，不依赖任何页面是否打开。
 *              后续调用传 NULL 即可。
 */
int comm_session_init(const board_paths_t *paths);

/** 底层客户端（用于发送请求）；未初始化返回 NULL。 */
comm_client_t *comm_session_client(void);

bool comm_session_logged_in(void);
const char *comm_session_user(void);
void comm_session_set_user(const char *user);

/** 订阅事件。@return 0 成功；-ENOSPC 订阅者已满。 */
int comm_session_subscribe(comm_session_evt_fn fn, void *user);
void comm_session_unsubscribe(comm_session_evt_fn fn);

/** 好友缓存（由 FRIENDS 事件自动更新；未登录时返回 0）。 */
int comm_session_friend_count(void);
const char *comm_session_friend_name(int index);
bool comm_session_friend_online(int index);
/** 按名字查在线状态：0 离线 / 1 在线 / -1 不是好友。 */
int comm_session_friend_state(const char *name);

/**
 * 取走“待处理的对局邀请”。
 * 邀请到达时如果围棋页还没打开，事件会在这里暂存一次，
 * 等页面订阅后再消费，避免切页面就错过邀请。
 * @return 1 取到（user/size 输出）；0 没有待处理邀请。
 */
int comm_session_take_pending_invite(char *user, size_t user_cap, int *size);

/**
 * 自动登录（幂等，只尝试一次）。优先级：
 *   1. 环境变量 BOARD_APP_COMM_AUTOLOGIN="host:port,user,pass"（演示/看板覆盖）
 *   2. 本地记住的账号（上次登录成功后保存，开机默认登录）
 */
void comm_session_auto_login(void);

/**
 * 记住本次登录的账号，供下次开机自动登录。
 * 保存到本地配置（board_storage），密码仅做本地弱混淆，不是加密。
 */
void comm_session_remember(const char *user, const char *pass);

/** 是否记住了一个可用来自动登录的账号。 */
bool comm_session_has_saved_login(void);

/**
 * 取走一条“订阅者还没打开时收到的消息”。
 * 聊天页没创建时（比如开机自动登录后停在首页），服务器补发的离线消息
 * 会先缓存起来，页面订阅后再补投，避免丢消息。
 * @return 1 取到；0 没有了。
 */
int comm_session_take_pending_message(char *from, size_t from_cap, char *text,
                                      size_t text_cap, long *ts, bool *offline,
                                      bool *is_group);

/** 断开并清空登录态（退出账号时用）。 */
void comm_session_logout(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_COMM_SESSION_H */
