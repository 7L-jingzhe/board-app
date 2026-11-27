/**
 * @file image_ops.h
 * @brief 非破坏性图像编辑操作。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef IMAGE_OPS_H
#define IMAGE_OPS_H

#include <stdbool.h>
#include "gallery/image.h"

#ifdef __cplusplus
extern "C" {
#endif

int image_rotate90(image_t **img, bool clockwise);
int image_flip(image_t *img, bool horizontal);
int image_grayscale(image_t *img);
int image_adjust(image_t *img, int brightness, int contrast);
int image_crop(image_t **img, int x, int y, int w, int h);

#ifdef __cplusplus
}
#endif

#endif /* IMAGE_OPS_H */
