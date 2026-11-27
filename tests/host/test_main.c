/**
 * @file test_main.c
 * @brief 主机端测试入口。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "test_common.h"

void test_game_2048(void);
void test_game_snake(void);
void test_game_breakout(void);
void test_weather_parse(void);
void test_image_ops(void);
void test_image_history(void);
void test_storage(void);
void test_async_queue(void);
void test_chat(void);
void test_weiqi(void);

int g_failures = 0;

int main(void)
{
    test_game_2048();
    test_game_snake();
    test_game_breakout();
    test_weather_parse();
    test_image_ops();
    test_image_history();
    test_storage();
    test_async_queue();
    test_chat();
    test_weiqi();

    if (g_failures == 0) {
        printf("all host tests passed\n");
        return 0;
    }
    fprintf(stderr, "%d host test failure(s)\n", g_failures);
    return 1;
}
