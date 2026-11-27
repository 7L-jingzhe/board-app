/**
 * @file image_ops.c
 * @brief 图像旋转、翻转、灰度、亮度/对比度调整与裁剪。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "gallery/image_ops.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static int clamp255(int v)
{
    if (v < 0) {
        return 0;
    }
    if (v > 255) {
        return 255;
    }
    return v;
}

int image_rotate90(image_t **img, bool clockwise)
{
    image_t *old;
    image_t *out;
    if (img == NULL || *img == NULL) {
        return -EINVAL;
    }
    old = *img;
    out = image_create(old->h, old->w);
    if (out == NULL) {
        return -ENOMEM;
    }
    for (int y = 0; y < old->h; y++) {
        const uint8_t *src = old->rgba + (size_t)y * old->stride;
        for (int x = 0; x < old->w; x++) {
            int nx = clockwise ? (old->h - 1 - y) : y;
            int ny = clockwise ? x : (old->w - 1 - x);
            uint8_t *dst = out->rgba + (size_t)ny * out->stride + (size_t)nx * 4;
            memcpy(dst, src + (size_t)x * 4, 4);
        }
    }
    image_free(old);
    *img = out;
    return 0;
}

int image_flip(image_t *img, bool horizontal)
{
    if (img == NULL || img->rgba == NULL) {
        return -EINVAL;
    }
    if (horizontal) {
        for (int y = 0; y < img->h; y++) {
            uint8_t *row = img->rgba + (size_t)y * img->stride;
            for (int x = 0; x < img->w / 2; x++) {
                uint8_t *a = row + (size_t)x * 4;
                uint8_t *b = row + (size_t)(img->w - 1 - x) * 4;
                for (int c = 0; c < 4; c++) {
                    uint8_t t = a[c];
                    a[c] = b[c];
                    b[c] = t;
                }
            }
        }
    } else {
        uint8_t *tmp = malloc((size_t)img->w * 4u);
        if (tmp == NULL) {
            return -ENOMEM;
        }
        for (int y = 0; y < img->h / 2; y++) {
            uint8_t *a = img->rgba + (size_t)y * img->stride;
            uint8_t *b = img->rgba + (size_t)(img->h - 1 - y) * img->stride;
            memcpy(tmp, a, (size_t)img->w * 4u);
            memcpy(a, b, (size_t)img->w * 4u);
            memcpy(b, tmp, (size_t)img->w * 4u);
        }
        free(tmp);
    }
    return 0;
}

int image_grayscale(image_t *img)
{
    if (img == NULL || img->rgba == NULL) {
        return -EINVAL;
    }
    for (int y = 0; y < img->h; y++) {
        uint8_t *row = img->rgba + (size_t)y * img->stride;
        for (int x = 0; x < img->w; x++) {
            uint8_t *p = row + (size_t)x * 4;
            int gray = (299 * p[0] + 587 * p[1] + 114 * p[2]) / 1000;
            p[0] = p[1] = p[2] = (uint8_t)clamp255(gray);
        }
    }
    return 0;
}

int image_adjust(image_t *img, int brightness, int contrast)
{
    int b;
    int c;
    double factor;

    if (img == NULL || img->rgba == NULL) {
        return -EINVAL;
    }
    if (brightness < -100) {
        brightness = -100;
    }
    if (brightness > 100) {
        brightness = 100;
    }
    if (contrast < -100) {
        contrast = -100;
    }
    if (contrast > 100) {
        contrast = 100;
    }
    b = brightness * 255 / 100;
    c = contrast * 255 / 100;
    if (c > 254) {
        c = 254;
    }
    if (c < -254) {
        c = -254;
    }
    factor = (259.0 * (double)(c + 255)) / (255.0 * (double)(259 - c));
    for (int y = 0; y < img->h; y++) {
        uint8_t *row = img->rgba + (size_t)y * img->stride;
        for (int x = 0; x < img->w; x++) {
            uint8_t *p = row + (size_t)x * 4;
            for (int k = 0; k < 3; k++) {
                int v = (int)(factor * ((double)p[k] - 128.0) + 128.0) + b;
                p[k] = (uint8_t)clamp255(v);
            }
        }
    }
    return 0;
}

int image_crop(image_t **img, int x, int y, int w, int h)
{
    image_t *old;
    image_t *out;

    if (img == NULL || *img == NULL) {
        return -EINVAL;
    }
    old = *img;
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > old->w || y + h > old->h) {
        return -EINVAL;
    }
    out = image_create(w, h);
    if (out == NULL) {
        return -ENOMEM;
    }
    for (int row = 0; row < h; row++) {
        memcpy(out->rgba + (size_t)row * out->stride,
               old->rgba + (size_t)(y + row) * old->stride + (size_t)x * 4,
               (size_t)w * 4u);
    }
    image_free(old);
    *img = out;
    return 0;
}
