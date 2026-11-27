/**
 * @file board_async_queue.c
 * @brief 固定容量的线程安全指针队列。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "board_core/board_async_queue.h"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

struct board_async_queue {
    pthread_mutex_t lock;
    void **items;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
};

board_async_queue_t *board_async_queue_create(size_t capacity)
{
    board_async_queue_t *q;
    if (capacity == 0) {
        return NULL;
    }
    q = calloc(1, sizeof(*q));
    if (q == NULL) {
        return NULL;
    }
    q->items = calloc(capacity, sizeof(void *));
    if (q->items == NULL) {
        free(q);
        return NULL;
    }
    pthread_mutex_init(&q->lock, NULL);
    q->capacity = capacity;
    return q;
}

void board_async_queue_destroy(board_async_queue_t *q)
{
    if (q == NULL) {
        return;
    }
    pthread_mutex_destroy(&q->lock);
    free(q->items);
    free(q);
}

int board_async_queue_push(board_async_queue_t *q, void *item)
{
    if (q == NULL || item == NULL) {
        return -EINVAL;
    }
    pthread_mutex_lock(&q->lock);
    if (q->count >= q->capacity) {
        pthread_mutex_unlock(&q->lock);
        return -ENOSPC;
    }
    q->items[q->tail] = item;
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;
    pthread_mutex_unlock(&q->lock);
    return 0;
}

void *board_async_queue_pop(board_async_queue_t *q)
{
    void *item;
    if (q == NULL) {
        return NULL;
    }
    pthread_mutex_lock(&q->lock);
    if (q->count == 0) {
        pthread_mutex_unlock(&q->lock);
        return NULL;
    }
    item = q->items[q->head];
    q->items[q->head] = NULL;
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    pthread_mutex_unlock(&q->lock);
    return item;
}

size_t board_async_queue_count(board_async_queue_t *q)
{
    size_t n;
    if (q == NULL) {
        return 0;
    }
    pthread_mutex_lock(&q->lock);
    n = q->count;
    pthread_mutex_unlock(&q->lock);
    return n;
}
