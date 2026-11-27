/**
 * @file image.c
 * @brief RGBA8888 图像缓冲、stb 读写与双线性缩放。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "gallery/image.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "board_core/board_log.h"
#include "stb_image.h"
#include "stb_image_write.h"

image_t *image_create(int w, int h)
{
    image_t *img;
    size_t stride;
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) {
        return NULL;
    }
    stride = (size_t)w * 4u;
    if ((size_t)h > SIZE_MAX / stride) {
        return NULL;
    }
    img = calloc(1, sizeof(*img));
    if (img == NULL) {
        return NULL;
    }
    img->rgba = calloc((size_t)h, stride);
    if (img->rgba == NULL) {
        free(img);
        return NULL;
    }
    img->w = w;
    img->h = h;
    img->stride = (int)stride;
    return img;
}

image_t *image_load(const char *path)
{
    int w = 0;
    int h = 0;
    int comp = 0;
    unsigned char *pixels;
    image_t *img;

    if (path == NULL) {
        return NULL;
    }
    pixels = stbi_load(path, &w, &h, &comp, 4);
    if (pixels == NULL) {
        BOARD_LOGW("gallery", "decode failed: %s (%s)", path,
                   stbi_failure_reason() ? stbi_failure_reason() : "unknown");
        return NULL;
    }
    img = image_create(w, h);
    if (img == NULL) {
        stbi_image_free(pixels);
        return NULL;
    }
    memcpy(img->rgba, pixels, (size_t)w * (size_t)h * 4u);
    stbi_image_free(pixels);
    return img;
}

image_t *image_clone(const image_t *src)
{
    image_t *img;
    if (src == NULL) {
        return NULL;
    }
    img = image_create(src->w, src->h);
    if (img == NULL) {
        return NULL;
    }
    memcpy(img->rgba, src->rgba, (size_t)src->w * (size_t)src->h * 4u);
    return img;
}

void image_free(image_t *img)
{
    if (img == NULL) {
        return;
    }
    free(img->rgba);
    free(img);
}

int image_save_png(const image_t *img, const char *path)
{
    if (img == NULL || path == NULL || img->rgba == NULL) {
        return -EINVAL;
    }
    if (stbi_write_png(path, img->w, img->h, 4, img->rgba, img->stride) == 0) {
        BOARD_LOGE("image", "stbi_write_png failed: %s (errno=%d)", path, errno);
        return -EIO;
    }
    return 0;
}

image_t *image_resize_fit(const image_t *src, int max_w, int max_h)
{
    image_t *dst;
    double scale;
    int nw;
    int nh;

    if (src == NULL || max_w <= 0 || max_h <= 0) {
        return NULL;
    }
    if (src->w <= max_w && src->h <= max_h) {
        return image_clone(src);
    }
    /* 保持宽高比，且绝不放大。预览图和缩略图都经由这条路径生成，
     * 因此板子不会仅仅为了绘制一张卡片而保留全尺寸副本。 */
    scale = (double)max_w / (double)src->w;
    if ((double)max_h / (double)src->h < scale) {
        scale = (double)max_h / (double)src->h;
    }
    nw = (int)((double)src->w * scale + 0.5);
    nh = (int)((double)src->h * scale + 0.5);
    if (nw < 1) {
        nw = 1;
    }
    if (nh < 1) {
        nh = 1;
    }
    dst = image_create(nw, nh);
    if (dst == NULL) {
        return NULL;
    }
    /* 对于 1280x800 的预览图，双线性采样已经足够快，
     * 并且避免了最近邻插值在渐变区域出现的块状感。 */
    for (int y = 0; y < nh; y++) {
        double sy = ((double)y + 0.5) * (double)src->h / (double)nh - 0.5;
        int y0 = (int)floor(sy);
        double fy = sy - (double)y0;
        if (y0 < 0) {
            y0 = 0;
            fy = 0.0;
        }
        if (y0 >= src->h - 1) {
            y0 = src->h - 1;
            fy = 0.0;
        }
        int y1 = (y0 + 1 < src->h) ? y0 + 1 : y0;
        for (int x = 0; x < nw; x++) {
            double sx = ((double)x + 0.5) * (double)src->w / (double)nw - 0.5;
            int x0 = (int)floor(sx);
            double fx = sx - (double)x0;
            if (x0 < 0) {
                x0 = 0;
                fx = 0.0;
            }
            if (x0 >= src->w - 1) {
                x0 = src->w - 1;
                fx = 0.0;
            }
            int x1 = (x0 + 1 < src->w) ? x0 + 1 : x0;
            const uint8_t *p00 = src->rgba + (size_t)y0 * src->stride + (size_t)x0 * 4;
            const uint8_t *p10 = src->rgba + (size_t)y0 * src->stride + (size_t)x1 * 4;
            const uint8_t *p01 = src->rgba + (size_t)y1 * src->stride + (size_t)x0 * 4;
            const uint8_t *p11 = src->rgba + (size_t)y1 * src->stride + (size_t)x1 * 4;
            uint8_t *out = dst->rgba + (size_t)y * dst->stride + (size_t)x * 4;
            for (int c = 0; c < 4; c++) {
                double top = p00[c] * (1.0 - fx) + p10[c] * fx;
                double bot = p01[c] * (1.0 - fx) + p11[c] * fx;
                double v = top * (1.0 - fy) + bot * fy;
                if (v < 0.0) {
                    v = 0.0;
                }
                if (v > 255.0) {
                    v = 255.0;
                }
                out[c] = (uint8_t)(v + 0.5);
            }
        }
    }
    return dst;
}

int image_copy_to_bgra(const image_t *img, uint8_t *dst, size_t dst_size)
{
    size_t need;
    if (img == NULL || dst == NULL) {
        return -EINVAL;
    }
    need = (size_t)img->w * (size_t)img->h * 4u;
    if (dst_size < need) {
        return -ENOSPC;
    }
    for (int y = 0; y < img->h; y++) {
        const uint8_t *src = img->rgba + (size_t)y * img->stride;
        uint8_t *out = dst + (size_t)y * (size_t)img->w * 4u;
        for (int x = 0; x < img->w; x++) {
            out[0] = src[2];
            out[1] = src[1];
            out[2] = src[0];
            out[3] = src[3];
            src += 4;
            out += 4;
        }
    }
    return 0;
}
