/**
 * @file test_game.c
 * @brief 针对内置街机核心的主机端测试。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "test_common.h"

#include <string.h>

#include "arcade/arcade_2048.h"
#include "arcade/arcade_breakout.h"
#include "arcade/arcade_common.h"
#include "arcade/arcade_snake.h"

static void seed_board(arcade_2048_t *g, const uint16_t cells[16])
{
    memset(g, 0, sizeof(*g));
    memcpy(g->cells, cells, sizeof(g->cells));
}

void test_game_2048(void)
{
    arcade_rng_t rng;
    arcade_2048_t g;
    arcade_2048_result_t r;
    int nonzero = 0;

    arcade_rng_seed(&rng, 12345);
    arcade_2048_init(&g, &rng);
    for (int i = 0; i < ARCADE_2048_CELLS; i++) {
        if (g.cells[i] != 0) {
            nonzero++;
        }
    }
    CHECK_EQ_INT(nonzero, 2);
    CHECK_EQ_INT(g.score, 0);

    {
        const uint16_t cells[16] = { 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        seed_board(&g, cells);
        r = arcade_2048_move(&g, ARCADE_DIR_LEFT, &rng);
        CHECK_EQ_INT(r.moved, 1);
        CHECK_EQ_INT(g.cells[0], 4);
        CHECK(g.score >= 4);
    }
    {
        /* 四个相同的方块必须合并成两个，而不是一个 8。 */
        const uint16_t cells[16] = { 2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        seed_board(&g, cells);
        r = arcade_2048_move(&g, ARCADE_DIR_LEFT, &rng);
        CHECK_EQ_INT(r.moved, 1);
        CHECK_EQ_INT(g.cells[0], 4);
        CHECK_EQ_INT(g.cells[1], 4);
        CHECK_EQ_INT(g.score, 8);
    }
    {
        /* 没有空格子且相邻方块都不相同：棋盘已无法移动。 */
        const uint16_t cells[16] = { 2, 4, 2, 4, 4, 2, 4, 2, 2, 4, 2, 4, 4, 2, 4, 2 };
        seed_board(&g, cells);
        CHECK_EQ_INT(arcade_2048_can_move(&g), 0);
        r = arcade_2048_move(&g, ARCADE_DIR_LEFT, &rng);
        CHECK_EQ_INT(r.moved, 0);
        CHECK_EQ_INT(g.over, 1);
    }
}

void test_game_snake(void)
{
    arcade_rng_t rng;
    arcade_snake_t s;
    int events;

    arcade_rng_seed(&rng, 7);
    arcade_snake_init(&s, &rng);
    CHECK_EQ_INT(s.length, 3);
    CHECK_EQ_INT(s.alive, 1);
    CHECK_EQ_INT(s.dir, ARCADE_DIR_RIGHT);
    for (int i = 0; i < s.length; i++) {
        CHECK(!(s.body[i].x == s.food.x && s.body[i].y == s.food.y));
    }
    /* 直接 180 度掉头必须被拒绝。 */
    CHECK_EQ_INT(arcade_snake_turn(&s, ARCADE_DIR_LEFT), 0);
    CHECK_EQ_INT(arcade_snake_turn(&s, ARCADE_DIR_UP), 1);

    events = arcade_snake_step(&s, &rng);
    CHECK(events & ARCADE_SNAKE_EVENT_MOVED);
    CHECK(s.length >= 3);
    CHECK(s.alive);

    /* 径直撞向墙壁必须结束本局。 */
    for (int i = 0; i < 100 && s.alive; i++) {
        (void)arcade_snake_step(&s, &rng);
    }
    CHECK_EQ_INT(s.alive, 0);
    CHECK(arcade_snake_step_ms(&s) >= ARCADE_SNAKE_MIN_MS);
}

void test_game_breakout(void)
{
    arcade_breakout_t g;
    int x = 0, y = 0, w = 0, h = 0;

    arcade_breakout_init(&g);
    CHECK_EQ_INT(g.bricks_left, ARCADE_BREAKOUT_BRICKS);
    CHECK_EQ_INT(g.lives, ARCADE_BREAKOUT_LIVES);
    CHECK_EQ_INT(g.state, ARCADE_STATE_READY);

    arcade_breakout_move_paddle(&g, -10000);
    CHECK_EQ_INT(g.paddle_x, 0);
    arcade_breakout_move_paddle(&g, 1000000);
    CHECK_EQ_INT(g.paddle_x, ARCADE_FP(ARCADE_BREAKOUT_W - ARCADE_BREAKOUT_PADDLE_W));

    arcade_breakout_launch(&g);
    CHECK_EQ_INT(g.state, ARCADE_STATE_RUNNING);
    CHECK(g.vy < 0);

    for (int i = 0; i < 500; i++) {
        arcade_breakout_tick(&g);
    }
    CHECK(g.state == ARCADE_STATE_RUNNING || g.state == ARCADE_STATE_READY ||
          g.state == ARCADE_STATE_OVER || g.state == ARCADE_STATE_WON);
    CHECK(g.lives <= ARCADE_BREAKOUT_LIVES);

    arcade_breakout_brick_rect(0, 0, &x, &y, &w, &h);
    CHECK_EQ_INT(x, ARCADE_BREAKOUT_BRICK_X0);
    CHECK_EQ_INT(y, ARCADE_BREAKOUT_BRICK_Y0);
    CHECK_EQ_INT(w, ARCADE_BREAKOUT_BRICK_W);
    CHECK_EQ_INT(h, ARCADE_BREAKOUT_BRICK_H);
}
