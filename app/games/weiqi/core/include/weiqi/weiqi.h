/**
 * @file weiqi.h
 * @brief 围棋规则核心（纯 C，无 LVGL；客户端只做显示，服务器用它做权威判定）。
 *
 * 规则核心移植自开源项目 weiqi.js（ISC 许可）：
 *   https://github.com/cjlarose/weiqi.js
 *   Copyright (c) 2014-2015, Christopher La Rose
 *   上游文件：src/lib/board.js、src/lib/game.js
 *   上游原文与许可见 board_app/third_party/weiqi-js/
 *   移植映射与改动说明见同目录 UPSTREAM.md
 *
 * 上游语义（保持一致）：
 *   - 9/13/19 路棋盘；
 *   - placeStone：落子后先提掉无气的敌方块，再看自己是否自杀；
 *   - game.play：历史局面集合做**位置超劫**（positional superko）；
 *   - 连续两次 pass 终局；
 *   - areaScore：中国规则数子（子 + 单方围空）。
 * 本项目在上游之上补的适配层（非规则核心）：
 *   - 胜负判定与中国规则贴目 3.5 子、认输、棋盘文本序列化。
 *
 * 坐标：x 横向、y 纵向，左上角 (0,0)。
 *
 * SPDX-License-Identifier: ISC (ported core) + MIT (adaptation)
 */
#ifndef WEIQI_H
#define WEIQI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WEIQI_MAX 19
#define WEIQI_EMPTY 0
#define WEIQI_BLACK 1
#define WEIQI_WHITE 2
#define WEIQI_NONE 0xFF
#define WEIQI_KOMI 3.5
#define WEIQI_HISTORY_MAX 512      /* 位置超劫用的历史局面数（够下满一盘） */
#define WEIQI_CELLS (WEIQI_MAX * WEIQI_MAX)

/** 落子结果码。
 *  注意：故意避开 errno 区间（-EPERM=-1、-ENOENT=-2 等），
 *  否则业务错误和规则错误会混淆（曾经把“没有对局”显示成“禁着点”）。 */
enum {
    WEIQI_OK = 0,
    WEIQI_ERR_MOVE = -101,    /* 越界 / 已有子 / 终局 */
    WEIQI_ERR_SUICIDE = -102, /* 自杀（上游 allowSuicide=false） */
    WEIQI_ERR_KO = -103,      /* 位置超劫 */
    WEIQI_ERR_TURN = -104,    /* 不是该方的回合 */
};

typedef struct {
    uint8_t size;                          /* 9 / 13 / 19 */
    uint8_t cell[WEIQI_MAX][WEIQI_MAX];    /* 0 空 / 1 黑 / 2 白 */

    /* ---- game 层（对应上游 createGame） ---- */
    uint8_t turn;                          /* 1 黑 2 白 */
    uint8_t passes;                        /* consecutivePasses */
    bool over;
    char result[96];
    uint32_t moves;
    uint16_t captured[3];                  /* [1]=黑提掉的白子, [2]=白提掉的黑子 */

    uint8_t last_x;                        /* 最后一手（WEIQI_NONE 表示 pass） */
    uint8_t last_y;

    /* 位置超劫：历史局面哈希 */
    uint64_t history[WEIQI_HISTORY_MAX];
    int history_count;
} weiqi_t;

/** 初始化空棋盘，黑先。@return 0；size 非法返回 -1。 */
int weiqi_init(weiqi_t *g, int size);

bool weiqi_is_empty(const weiqi_t *g, int x, int y);
int weiqi_turn(const weiqi_t *g);

/**
 * 落子（对应 game.play）。已内含提子、自杀、位置超劫判定。
 * @return WEIQI_OK 或负的错误码。
 */
int weiqi_play(weiqi_t *g, int x, int y);

/** 停一手；双方连续停手则终局并算分（对应 game.pass + isOver）。 */
int weiqi_pass(weiqi_t *g);

/** 认输（本项目扩展）。@param color 认输方。 */
void weiqi_resign(weiqi_t *g, int color);

/**
 * 中国规则数子（对应 board.areaScore）：返回双方“子 + 围空”。
 * 白方已加贴目 WEIQI_KOMI；同时把胜负写进 g->result。
 */
int weiqi_score(const weiqi_t *g, double *black, double *white);

/** 只算地/子，不加贴目、不判胜负（对应上游 areaScore 原值）。 */
void weiqi_area_score(const weiqi_t *g, int *black, int *white);

/** 棋盘转文本：'B' 黑 / 'W' 白 / '.' 空，按行拼接，共 size*size 字节。 */
void weiqi_board_string(const weiqi_t *g, char *out, size_t cap);

/** 文本转棋盘（只填充 cell，不动 game 状态）。@return 0 成功。 */
int weiqi_board_parse(weiqi_t *g, const char *board, int size);

/** 客户端复刻服务器状态（不做规则校验）。 */
void weiqi_set_state(weiqi_t *g, int size, const char *board, int turn,
                     int black_cap, int white_cap, int last_x, int last_y,
                     bool over, const char *result);

const char *weiqi_error_text(int rc);

#ifdef __cplusplus
}
#endif

#endif /* WEIQI_H */
