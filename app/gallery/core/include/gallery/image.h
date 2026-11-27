/**
 * @file image.h
 * @brief RGBA8888 图像缓冲与基于 stb 的读写。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef IMAGE_H
#define IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int w;
    int h;
    int stride;      /* 字节数，通常为 w*4 */
    uint8_t *rgba;   /* 自上而下排列，R,G,B,A */
} image_t;

image_t *image_create(int w, int h);
image_t *image_load(const char *path);
image_t *image_clone(const image_t *src);
void image_free(image_t *img);
int image_save_png(const image_t *img, const char *path);
image_t *image_resize_fit(const image_t *src, int max_w, int max_h);

/** 将 RGBA 转换为 LVGL ARGB8888 的内存顺序（B,G,R,A）。 */
int image_copy_to_bgra(const image_t *img, uint8_t *dst, size_t dst_size);

#ifdef __cplusplus
}
#endif

#endif /* IMAGE_H */
