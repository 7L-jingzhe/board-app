/**
 * @file board_async_queue.h
 * @brief 固定容量的线程安全指针队列（工作线程 -> UI）。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef BOARD_ASYNC_QUEUE_H
#define BOARD_ASYNC_QUEUE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct board_async_queue board_async_queue_t;

board_async_queue_t *board_async_queue_create(size_t capacity);
void board_async_queue_destroy(board_async_queue_t *q);

/** 压入一个元素。成功返回 0，队列满返回 -ENOSPC，参数非法返回 -EINVAL。 */
int board_async_queue_push(board_async_queue_t *q, void *item);

/** 弹出一个元素；队列为空时返回 NULL。 */
void *board_async_queue_pop(board_async_queue_t *q);

size_t board_async_queue_count(board_async_queue_t *q);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_ASYNC_QUEUE_H */
