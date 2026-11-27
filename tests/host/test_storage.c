/**
 * @file test_storage.c
 * @brief 针对存储与异步队列的主机端测试。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "test_common.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "board_core/board_async_queue.h"
#include "board_core/board_storage.h"

void test_storage(void)
{
    char tmpl[] = "/tmp/board_app_store_XXXXXX";
    char *dir;
    char path[PATH_MAX];
    char long_value[600];

    dir = mkdtemp(tmpl);
    CHECK(dir != NULL);
    if (dir == NULL) {
        return;
    }
    snprintf(path, sizeof(path), "%s/settings.conf", dir);

    CHECK_EQ_INT(board_storage_init(path), 0);
    CHECK_EQ_INT(board_storage_get_int("missing", 42), 42);
    CHECK_EQ_INT(board_storage_set_int("game.2048.best", 4096), 0);
    CHECK_EQ_INT(board_storage_set_str("weather.city_name", "上海"), 0);
    CHECK_EQ_INT(board_storage_set_str("special", "a=b"), 0);
    CHECK_EQ_INT(board_storage_get_int("game.2048.best", 0), 4096);
    CHECK(strcmp(board_storage_get_str("weather.city_name", ""), "上海") == 0);
    CHECK(strcmp(board_storage_get_str("special", ""), "a=b") == 0);
    CHECK_EQ_INT(board_storage_save(), 0);
    board_storage_shutdown();

    CHECK_EQ_INT(board_storage_init(path), 0);
    CHECK_EQ_INT(board_storage_get_int("game.2048.best", 0), 4096);
    CHECK(strcmp(board_storage_get_str("weather.city_name", ""), "上海") == 0);
    CHECK(strcmp(board_storage_get_str("special", ""), "a=b") == 0);

    memset(long_value, 'x', sizeof(long_value) - 1);
    long_value[sizeof(long_value) - 1] = '\0';
    CHECK_EQ_INT(board_storage_set_str("too.long", long_value), -EINVAL);
    board_storage_shutdown();
    unlink(path);
    rmdir(dir);
}

void test_async_queue(void)
{
    board_async_queue_t *q = board_async_queue_create(2);
    int a = 1;
    int b = 2;
    int c = 3;

    CHECK(q != NULL);
    if (q == NULL) {
        return;
    }
    CHECK_EQ_INT(board_async_queue_count(q), 0);
    CHECK_EQ_INT(board_async_queue_push(q, &a), 0);
    CHECK_EQ_INT(board_async_queue_push(q, &b), 0);
    CHECK_EQ_INT(board_async_queue_push(q, &c), -ENOSPC);
    CHECK_EQ_INT(board_async_queue_count(q), 2);
    CHECK(board_async_queue_pop(q) == &a);
    CHECK_EQ_INT(board_async_queue_count(q), 1);
    CHECK(board_async_queue_pop(q) == &b);
    CHECK(board_async_queue_pop(q) == NULL);
    board_async_queue_destroy(q);
}
