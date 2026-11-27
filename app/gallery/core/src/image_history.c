/**
 * @file image_history.c
 * @brief 图像快照的有界撤销栈。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "gallery/image_history.h"

#include <errno.h>
#include <stdlib.h>

#define IMAGE_HISTORY_MAX 4

struct image_history {
    image_t *frames[IMAGE_HISTORY_MAX];
    int count;
};

image_history_t *image_history_create(const image_t *initial)
{
    image_history_t *h = calloc(1, sizeof(*h));
    if (h == NULL) {
        return NULL;
    }
    /* 第一个快照是任何编辑之前的状态。 */
    if (initial != NULL) {
        h->frames[0] = image_clone(initial);
        if (h->frames[0] == NULL) {
            free(h);
            return NULL;
        }
        h->count = 1;
    }
    return h;
}

int image_history_push(image_history_t *h, const image_t *current)
{
    image_t *copy;
    if (h == NULL || current == NULL) {
        return -EINVAL;
    }
    copy = image_clone(current);
    if (copy == NULL) {
        return -ENOMEM;
    }
    if (h->count >= IMAGE_HISTORY_MAX) {
        /* 丢弃最旧的快照，使内存占用保持有界（4 倍图像大小）。 */
        image_free(h->frames[0]);
        for (int i = 1; i < IMAGE_HISTORY_MAX; i++) {
            h->frames[i - 1] = h->frames[i];
        }
        h->count = IMAGE_HISTORY_MAX - 1;
    }
    h->frames[h->count++] = copy;
    return 0;
}

image_t *image_history_undo(image_history_t *h)
{
    image_t *previous;
    if (h == NULL || h->count <= 1) {
        return NULL;
    }
    /* 最新的帧是当前状态；丢弃它并返回它之前的那一帧。
     * 上一帧仍保留在历史中，以支持继续撤销。 */
    image_free(h->frames[h->count - 1]);
    h->frames[h->count - 1] = NULL;
    h->count--;
    previous = image_clone(h->frames[h->count - 1]);
    return previous;
}

bool image_history_can_undo(const image_history_t *h)
{
    return h != NULL && h->count > 1;
}

void image_history_reset(image_history_t *h, const image_t *initial)
{
    if (h == NULL) {
        return;
    }
    for (int i = 0; i < h->count; i++) {
        image_free(h->frames[i]);
        h->frames[i] = NULL;
    }
    h->count = 0;
    if (initial != NULL) {
        h->frames[0] = image_clone(initial);
        if (h->frames[0] != NULL) {
            h->count = 1;
        }
    }
}

void image_history_destroy(image_history_t *h)
{
    if (h == NULL) {
        return;
    }
    for (int i = 0; i < h->count; i++) {
        image_free(h->frames[i]);
    }
    free(h);
}
