/**
 * @file comm_proto.h
 * @brief 聊天协议：TCP 分帧 + JSON 负载 + 消息类型。
 *
 * 设计取舍：
 *   - TCP 只负责“必须可靠”的部分：登录/注册、好友列表、私聊消息、离线补发、心跳。
 *   - UDP 只负责“丢了也无所谓”的部分：在线状态广播、群聊广播。
 *   - 负载用 JSON（项目里已经有 cJSON），便于用 nc/脚本调试，也方便以后加字段。
 *
 * TCP 帧格式（大端）：
 *   +--------+--------+-------------------+
 *   | len:4  | type:1 | JSON body: len-1  |
 *   +--------+--------+-------------------+
 *   len 包含 type 字节本身，上限 COMM_MAX_FRAME。
 *
 * UDP 包格式： type:1 + JSON body（无长度前缀，靠数据报边界）。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef COMM_PROTO_H
#define COMM_PROTO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COMM_DEFAULT_PORT 20000
#define COMM_MAX_FRAME 65535
#define COMM_MAX_TEXT 900     /* 单条消息正文上限（UTF-8 字节） */
#define COMM_MAX_NAME 24      /* 用户名上限 */
#define COMM_MAX_PASS 64      /* 密码上限 */

/** TCP 消息类型（1..127 走 TCP，128+ 走 UDP）。 */
typedef enum {
    /* --- 客户端 -> 服务器 --- */
    COMM_T_REGISTER = 1,   /* {"user","pass"}                */
    COMM_T_LOGIN,          /* {"user","pass","udp_port"}     */
    COMM_T_ADD_FRIEND,     /* {"user"}                       */
    COMM_T_DEL_FRIEND,     /* {"user"}                       */
    COMM_T_FRIEND_LIST,    /* {}  主动拉取                    */
    COMM_T_PRIVATE,        /* {"to","text"}                  */
    COMM_T_PING,           /* {}                             */
    COMM_T_LOGOUT,         /* {}                             */

    /* --- 围棋对局（客户端 -> 服务器） --- */
    COMM_T_GAME_INVITE = 16, /* {"user","size"} 邀请对局      */
    COMM_T_GAME_ACCEPT,      /* {"from"} 接受邀请             */
    COMM_T_GAME_MOVE,        /* {"x","y"} 落子                */
    COMM_T_GAME_PASS,        /* {} 停一手                     */
    COMM_T_GAME_RESIGN,      /* {} 认输                       */
    COMM_T_GAME_LEAVE,       /* {} 离开对局                   */
    COMM_T_GAME_CANCEL,      /* {"user"} 取消发出的邀请       */

    /* --- 服务器 -> 客户端 --- */
    COMM_T_OK = 32,        /* {"for":<请求类型>,"msg"}        */
    COMM_T_ERROR,          /* {"for":<请求类型>,"code","msg"} */
    COMM_T_FRIENDS,        /* {"friends":[{"user","online"}]} */
    COMM_T_PRESENCE,       /* {"user","online"}              */
    COMM_T_INCOMING,       /* {"from","text","ts","offline"}  */
    COMM_T_ADDED,          /* {"user","by"}  对方加了你        */
    COMM_T_PONG,           /* {}                             */
    COMM_T_SYS,            /* {"msg"}                        */

    /* --- 围棋对局（服务器 -> 客户端） --- */
    COMM_T_GAME_INVITED = 48, /* {"from","size"}            */
    COMM_T_GAME_START,        /* {"opponent","size","color"} */
    COMM_T_GAME_STATE,        /* {"board","turn","bc","wc","last_x","last_y",
                                 "over","result"}             */
    COMM_T_GAME_REJECT,       /* {"user","msg"}              */
    COMM_T_GAME_END,          /* {"result","reason"}         */
    COMM_T_GAME_PEER_LEFT,    /* {"user"}                    */
    COMM_T_GAME_INVITE_CANCEL,/* {"user"}                    */

    /* --- UDP（不可靠，丢弃不影响功能） --- */
    COMM_U_ONLINE = 128,   /* 服务器 -> 客户端 {"online":["a","b"]} */
    COMM_U_GROUP_CHAT,     /* 客户端 -> 服务器 {"text"}              */
    COMM_U_GROUP_INCOMING, /* 服务器 -> 客户端 {"from","text","ts"}  */
} comm_msg_type_t;

#define COMM_UDP_IS_TYPE(t) ((t) >= 128)

/** 帧打包结果。 */
typedef enum {
    COMM_FRAME_PARTIAL = 0, /* 数据还不够一帧 */
    COMM_FRAME_READY = 1,   /* 已解析出一帧   */
    COMM_FRAME_BAD = -1,    /* 长度非法       */
} comm_frame_result_t;

/**
 * 把 type + JSON 打成一个 TCP 帧。
 * @param out     输出缓冲（调用者提供）
 * @param cap     缓冲大小，至少需要 body_len + 5
 * @param out_len 实际写入字节数
 * @return 0 成功；-EINVAL/-ENOSPC。
 */
int comm_frame_pack(uint8_t type, const char *json, size_t json_len,
                    uint8_t *out, size_t cap, size_t *out_len);

/**
 * 检查 buf 里是否已有一个完整帧。
 * @param payload_len 输出帧负载长度（含 type 字节）
 * @param body_off    输出 JSON body 在 buf 中的偏移（= 5）
 */
comm_frame_result_t comm_frame_peek(const uint8_t *buf, size_t len,
                                    size_t *payload_len, size_t *body_off);

/** 取帧类型（buf 必须至少 5 字节）。 */
static inline uint8_t comm_frame_type(const uint8_t *buf)
{
    return buf[4];
}

/** 常用字段名，避免各处手写字符串写错。 */
#define COMM_K_USER "user"
#define COMM_K_PASS "pass"
#define COMM_K_TEXT "text"
#define COMM_K_TO "to"
#define COMM_K_FROM "from"
#define COMM_K_TS "ts"
#define COMM_K_ONLINE "online"
#define COMM_K_OFFLINE "offline"
#define COMM_K_FRIENDS "friends"
#define COMM_K_MSG "msg"
#define COMM_K_CODE "code"
#define COMM_K_FOR "for"
#define COMM_K_UDP_PORT "udp_port"
#define COMM_K_BY "by"
#define COMM_K_SIZE "size"
#define COMM_K_BOARD "board"
#define COMM_K_TURN "turn"
#define COMM_K_BLACK_CAP "bc"
#define COMM_K_WHITE_CAP "wc"
#define COMM_K_LAST_X "last_x"
#define COMM_K_LAST_Y "last_y"
#define COMM_K_X "x"
#define COMM_K_Y "y"
#define COMM_K_COLOR "color"
#define COMM_K_OPPONENT "opponent"
#define COMM_K_RESULT "result"
#define COMM_K_REASON "reason"

/** 围棋棋盘文本上限（19×19 + NUL）。 */
#define COMM_BOARD_MAX (19 * 19 + 1)

/** 用户名是否合法：1..COMM_MAX_NAME 个可打印 ASCII，不含空白与 ':'。 */
int comm_name_valid(const char *name);

/** 文本是否可安全发送：非空、<= COMM_MAX_TEXT 字节、不含控制字符。 */
int comm_text_valid(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* COMM_PROTO_H */
