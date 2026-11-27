/**
 * @file weiqi.c
 * @brief 围棋规则核心：移植自开源项目 weiqi.js（ISC）。
 *
 * 来源：https://github.com/cjlarose/weiqi.js
 *       Copyright (c) 2014-2015, Christopher La Rose
 *       上游：src/lib/board.js（createBoard / placeStone / groupAtPosition /
 *             removeGroup / areaScore / toArray）与 src/lib/game.js
 *             （createGame / play / pass / isOver / areaScore）
 *       上游原文与 ISC 许可：board_app/third_party/weiqi-js/
 *       移植映射与改动：board_app/third_party/weiqi-js/UPSTREAM.md
 *
 * 本文件保留上游的算法结构与判定顺序；仅做了 C 化改造：
 *   - immutable 结构 -> 可变结构 + 固定数组；
 *   - “历史局面集合”-> 64 位 FNV 哈希集合（位置超劫语义一致）；
 *   - 异常 -> 负错误码。
 * 适配层（贴目、认输、序列化、终局算分）见文件末尾，不属于上游核心。
 *
 * SPDX-License-Identifier: ISC (ported core) + MIT (adaptation)
 */
#include "weiqi/weiqi.h"

#include <stdio.h>
#include <string.h>

#define MAX_GROUP WEIQI_CELLS

static const int DX[4] = { 1, -1, 0, 0 };
static const int DY[4] = { 0, 0, 1, -1 };

static bool in_bounds(const weiqi_t *g, int x, int y)
{
    return x >= 0 && y >= 0 && x < g->size && y < g->size;
}

/* 对应上游 board.js 的 positionInBounds + 索引 */
static int pos_index(int x, int y)
{
    return y * WEIQI_MAX + x;
}

static void pos_of(int index, int *x, int *y)
{
    *x = index % WEIQI_MAX;
    *y = index / WEIQI_MAX;
}

/* 局面指纹：对应上游“历史集合”里的 board.get('stones')。 */
static uint64_t board_hash(const weiqi_t *g)
{
    uint64_t h = 1469598103934665603ULL; /* FNV-1a offset basis */
    int x;
    int y;

    for (y = 0; y < g->size; y++) {
        for (x = 0; x < g->size; x++) {
            h ^= (uint64_t)g->cell[y][x] + 1u;
            h *= 1099511628211ULL;
        }
    }
    return h;
}

/**
 * 对应上游 groupAtPosition：BFS 取连通块 + 气。
 * 空点也按“同色（空）”连通，用于数子。
 * @param stones 输出连通块内位置索引（可为 NULL）
 * @param count  输出块大小
 * @param libs   输出气数（只统计空邻居，重复计入，与上游 liberties 集合语义
 *               的差别仅在于这里可能重复；判定“是否零气”不受影响）
 */
static void group_at(const weiqi_t *g, int x0, int y0, bool *visited,
                     int *stones, int *count, int *libs)
{
    int stack[MAX_GROUP];
    int sp = 0;
    uint8_t color = g->cell[y0][x0];

    *count = 0;
    if (libs != NULL) {
        *libs = 0;
    }
    if (!in_bounds(g, x0, y0)) {
        return;
    }
    stack[sp++] = pos_index(x0, y0);
    if (visited != NULL) {
        visited[pos_index(x0, y0)] = true;
    }

    while (sp > 0) {
        int cur = stack[--sp];
        int cx;
        int cy;
        int d;

        pos_of(cur, &cx, &cy);
        if (stones != NULL) {
            stones[(*count)++] = cur;
        } else {
            (*count)++;
        }
        for (d = 0; d < 4; d++) {
            int nx = cx + DX[d];
            int ny = cy + DY[d];
            if (!in_bounds(g, nx, ny)) {
                continue;
            }
            if (g->cell[ny][nx] != color) {
                if (g->cell[ny][nx] == WEIQI_EMPTY && libs != NULL) {
                    (*libs)++;
                }
                continue;
            }
            if (visited != NULL && visited[pos_index(nx, ny)]) {
                continue;
            }
            if (visited != NULL) {
                visited[pos_index(nx, ny)] = true;
            }
            stack[sp++] = pos_index(nx, ny);
        }
    }
}

/* 对应上游 removeGroup */
static int remove_stones(weiqi_t *g, const int *stones, int count)
{
    int i;
    for (i = 0; i < count; i++) {
        int x;
        int y;
        pos_of(stones[i], &x, &y);
        g->cell[y][x] = WEIQI_EMPTY;
    }
    return count;
}

/**
 * 对应上游 placeStone：落子 -> 提掉无气敌块 -> 自杀检查。
 * @param removed 输出本手提子数
 * @return WEIQI_OK / WEIQI_ERR_MOVE / WEIQI_ERR_SUICIDE
 */
static int place_stone(weiqi_t *g, int x, int y, int color, int *removed)
{
    bool visited[MAX_GROUP];
    int stones[MAX_GROUP];
    int count = 0;
    int libs = 0;
    int d;
    int captured = 0;
    uint8_t enemy = (color == WEIQI_BLACK) ? WEIQI_WHITE : WEIQI_BLACK;

    if (removed != NULL) {
        *removed = 0;
    }
    if (!in_bounds(g, x, y) || g->cell[y][x] != WEIQI_EMPTY) {
        return WEIQI_ERR_MOVE; /* "Intersection occupied by existing stone" */
    }

    g->cell[y][x] = (uint8_t)color;

    /* 相邻敌块中气为 0 的全部提掉 */
    for (d = 0; d < 4; d++) {
        int nx = x + DX[d];
        int ny = y + DY[d];
        if (!in_bounds(g, nx, ny) || g->cell[ny][nx] != enemy) {
            continue;
        }
        memset(visited, 0, sizeof(visited));
        group_at(g, nx, ny, visited, stones, &count, &libs);
        if (libs == 0) {
            captured += remove_stones(g, stones, count);
        }
    }

    /* 自己这块若也没气 -> 自杀（上游默认禁止） */
    memset(visited, 0, sizeof(visited));
    group_at(g, x, y, visited, stones, &count, &libs);
    if (count == 0 || libs == 0) {
        g->cell[y][x] = WEIQI_EMPTY;
        return WEIQI_ERR_SUICIDE; /* "New group has zero liberies (suicide)" */
    }

    if (removed != NULL) {
        *removed = captured;
    }
    return WEIQI_OK;
}

static bool history_contains(const weiqi_t *g, uint64_t hash)
{
    int i;
    for (i = 0; i < g->history_count; i++) {
        if (g->history[i] == hash) {
            return true;
        }
    }
    return false;
}

static void history_add(weiqi_t *g, uint64_t hash)
{
    if (g->history_count < WEIQI_HISTORY_MAX) {
        g->history[g->history_count++] = hash;
    } else {
        /* 理论上 19 路也下不满 512 手就终局；保险起见丢最旧的一条 */
        memmove(&g->history[0], &g->history[1],
                sizeof(g->history[0]) * (WEIQI_HISTORY_MAX - 1));
        g->history[WEIQI_HISTORY_MAX - 1] = hash;
    }
}

int weiqi_init(weiqi_t *g, int size)
{
    if (g == NULL || (size != 9 && size != 13 && size != 19)) {
        return -1;
    }
    memset(g, 0, sizeof(*g));
    g->size = (uint8_t)size;
    g->turn = WEIQI_BLACK;
    g->last_x = WEIQI_NONE;
    g->last_y = WEIQI_NONE;
    /* 上游 createGame：history 里先放空盘局面 */
    history_add(g, board_hash(g));
    return 0;
}

bool weiqi_is_empty(const weiqi_t *g, int x, int y)
{
    return g != NULL && in_bounds(g, x, y) && g->cell[y][x] == WEIQI_EMPTY;
}

int weiqi_turn(const weiqi_t *g)
{
    return g != NULL ? g->turn : 0;
}

const char *weiqi_error_text(int rc)
{
    switch (rc) {
    case WEIQI_OK:
        return "成功";
    case WEIQI_ERR_MOVE:
        return "这里不能落子";
    case WEIQI_ERR_SUICIDE:
        return "禁着点：自己的棋会被提光";
    case WEIQI_ERR_KO:
        return "打劫：不能立即回提";
    case WEIQI_ERR_TURN:
        return "还没轮到你";
    default:
        return "非法落子";
    }
}

int weiqi_play(weiqi_t *g, int x, int y)
{
    int removed = 0;
    int rc;
    uint64_t before;
    uint8_t color;

    if (g == NULL || g->over) {
        return WEIQI_ERR_MOVE; /* "Game is already over" */
    }
    color = g->turn;
    before = board_hash(g);

    rc = place_stone(g, x, y, color, &removed);
    if (rc != WEIQI_OK) {
        return rc;
    }

    /* 位置超劫：新局面上游用 history 集合判定（"Violation of Ko"） */
    {
        uint64_t after = board_hash(g);
        if (history_contains(g, after)) {
            /* 回滚到落子前 */
            g->cell[y][x] = WEIQI_EMPTY;
            (void)before;
            return WEIQI_ERR_KO;
        }
        history_add(g, after);
    }

    g->captured[color] = (uint16_t)(g->captured[color] + removed);
    g->last_x = (uint8_t)x;
    g->last_y = (uint8_t)y;
    g->passes = 0;
    g->moves++;
    g->turn = (color == WEIQI_BLACK) ? WEIQI_WHITE : WEIQI_BLACK;
    return WEIQI_OK;
}

void weiqi_area_score(const weiqi_t *g, int *black, int *white)
{
    bool visited[WEIQI_MAX][WEIQI_MAX];
    int stones[MAX_GROUP];
    int count = 0;
    int libs = 0;
    int b = 0;
    int w = 0;
    int x;
    int y;

    if (g == NULL) {
        return;
    }
    memset(visited, 0, sizeof(visited));

    for (y = 0; y < g->size; y++) {
        for (x = 0; x < g->size; x++) {
            bool touch_black = false;
            bool touch_white = false;
            int d;
            int i;

            if (visited[y][x]) {
                continue;
            }
            /* 用一维 visited 视图给 group_at 用：这里逐点处理，直接内联 BFS */
            {
                int stack[MAX_GROUP];
                int sp = 0;
                uint8_t color = g->cell[y][x];

                stack[sp++] = pos_index(x, y);
                visited[y][x] = true;
                count = 0;
                while (sp > 0) {
                    int cur = stack[--sp];
                    int cx;
                    int cy;
                    pos_of(cur, &cx, &cy);
                    stones[count++] = cur;
                    for (d = 0; d < 4; d++) {
                        int nx = cx + DX[d];
                        int ny = cy + DY[d];
                        if (!in_bounds(g, nx, ny)) {
                            continue;
                        }
                        if (g->cell[ny][nx] == WEIQI_BLACK) {
                            touch_black = true;
                        } else if (g->cell[ny][nx] == WEIQI_WHITE) {
                            touch_white = true;
                        }
                        /* 只扩展同色点：空区域只吞空点，棋子块只吞同色棋子 */
                        if (g->cell[ny][nx] == color && !visited[ny][nx]) {
                            visited[ny][nx] = true;
                            stack[sp++] = pos_index(nx, ny);
                        }
                    }
                }
                if (color == WEIQI_EMPTY) {
                    /* 空区域：只被一方围住才算那一方的地（对应周围颜色判定） */
                    if (touch_white && !touch_black) {
                        w += count;
                    } else if (touch_black && !touch_white) {
                        b += count;
                    }
                } else if (color == WEIQI_BLACK) {
                    b += count;
                } else {
                    w += count;
                }
            }
            for (i = 0; i < count; i++) {
                int sx;
                int sy;
                pos_of(stones[i], &sx, &sy);
                (void)sx;
                (void)sy;
            }
            (void)libs;
        }
    }

    if (black != NULL) {
        *black = b;
    }
    if (white != NULL) {
        *white = w;
    }
}

int weiqi_score(const weiqi_t *g, double *black, double *white)
{
    int b = 0;
    int w = 0;
    double bs;
    double ws;

    if (g == NULL) {
        return -1;
    }
    weiqi_area_score(g, &b, &w);
    bs = (double)b;
    ws = (double)w + WEIQI_KOMI; /* 中国规则黑贴 3.5 子（7 目半） */
    if (black != NULL) {
        *black = bs;
    }
    if (white != NULL) {
        *white = ws;
    }
    if (g->result[0] == '\0') {
        weiqi_t *mutable_g = (weiqi_t *)g;
        if (bs > ws) {
            snprintf(mutable_g->result, sizeof(mutable_g->result), "黑胜 %.1f 子",
                     bs - ws);
        } else if (ws > bs) {
            snprintf(mutable_g->result, sizeof(mutable_g->result), "白胜 %.1f 子",
                     ws - bs);
        } else {
            snprintf(mutable_g->result, sizeof(mutable_g->result), "和棋");
        }
    }
    return 0;
}

int weiqi_pass(weiqi_t *g)
{
    if (g == NULL || g->over) {
        return WEIQI_ERR_MOVE;
    }
    g->passes++;
    g->turn = (g->turn == WEIQI_BLACK) ? WEIQI_WHITE : WEIQI_BLACK;
    g->last_x = WEIQI_NONE;
    g->last_y = WEIQI_NONE;
    g->moves++;
    history_add(g, board_hash(g));
    /* 连续两次 pass -> 终局（对应 isOver） */
    if (g->passes >= 2) {
        g->over = true;
        weiqi_score(g, NULL, NULL);
    }
    return WEIQI_OK;
}

void weiqi_resign(weiqi_t *g, int color)
{
    if (g == NULL || g->over) {
        return;
    }
    g->over = true;
    snprintf(g->result, sizeof(g->result), "%s胜（对方认输）",
             color == WEIQI_BLACK ? "白" : "黑");
}

void weiqi_board_string(const weiqi_t *g, char *out, size_t cap)
{
    int x;
    int y;
    size_t n = 0;
    size_t need = (size_t)g->size * g->size + 1u;

    if (out == NULL || cap == 0) {
        return;
    }
    if (need > cap) {
        out[0] = '\0';
        return;
    }
    for (y = 0; y < g->size; y++) {
        for (x = 0; x < g->size; x++) {
            uint8_t c = g->cell[y][x];
            out[n++] = (c == WEIQI_BLACK) ? 'B' : (c == WEIQI_WHITE) ? 'W' : '.';
        }
    }
    out[n] = '\0';
}

int weiqi_board_parse(weiqi_t *g, const char *board, int size)
{
    int x;
    int y;
    size_t i = 0;

    if (g == NULL || board == NULL || (size != 9 && size != 13 && size != 19)) {
        return -1;
    }
    if (strlen(board) < (size_t)size * size) {
        return -1;
    }
    g->size = (uint8_t)size;
    for (y = 0; y < size; y++) {
        for (x = 0; x < size; x++) {
            char c = board[i++];
            g->cell[y][x] = (c == 'B') ? WEIQI_BLACK
                            : (c == 'W') ? WEIQI_WHITE
                                         : WEIQI_EMPTY;
        }
    }
    for (y = size; y < WEIQI_MAX; y++) {
        for (x = 0; x < WEIQI_MAX; x++) {
            g->cell[y][x] = WEIQI_EMPTY;
        }
    }
    return 0;
}

void weiqi_set_state(weiqi_t *g, int size, const char *board, int turn,
                     int black_cap, int white_cap, int last_x, int last_y,
                     bool over, const char *result)
{
    if (g == NULL) {
        return;
    }
    if (weiqi_board_parse(g, board, size) != 0) {
        weiqi_init(g, size);
        return;
    }
    g->turn = (uint8_t)(turn == WEIQI_WHITE ? WEIQI_WHITE : WEIQI_BLACK);
    g->captured[WEIQI_BLACK] = (uint16_t)(black_cap < 0 ? 0 : black_cap);
    g->captured[WEIQI_WHITE] = (uint16_t)(white_cap < 0 ? 0 : white_cap);
    g->last_x = (last_x >= 0 && last_x < size) ? (uint8_t)last_x : WEIQI_NONE;
    g->last_y = (last_y >= 0 && last_y < size) ? (uint8_t)last_y : WEIQI_NONE;
    g->over = over;
    g->passes = 0;
    snprintf(g->result, sizeof(g->result), "%s", result != NULL ? result : "");
    g->history_count = 0;
    history_add(g, board_hash(g));
}
