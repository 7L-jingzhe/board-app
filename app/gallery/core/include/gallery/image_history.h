/**
 * @file image_history.h
 * @brief 针对 image_t 快照的小型撤销历史。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef IMAGE_HISTORY_H
#define IMAGE_HISTORY_H

#include <stdbool.h>
#include "gallery/image.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct image_history image_history_t;

image_history_t *image_history_create(const image_t *initial);
int image_history_push(image_history_t *h, const image_t *current);
/** 弹出当前状态，并返回上一状态的克隆（调用方负责释放）。 */
image_t *image_history_undo(image_history_t *h);
bool image_history_can_undo(const image_history_t *h);
void image_history_reset(image_history_t *h, const image_t *initial);
void image_history_destroy(image_history_t *h);

#ifdef __cplusplus
}
#endif

#endif /* IMAGE_HISTORY_H */
