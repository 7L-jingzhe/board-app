/**
 * @file test_image.c
 * @brief 针对图像缓冲区、图像操作与撤销历史的主机端测试。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "test_common.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "gallery/image.h"
#include "gallery/image_history.h"
#include "gallery/image_ops.h"

static void set_px(image_t *img, int x, int y, int r, int g, int b, int a)
{
    uint8_t *p = img->rgba + (size_t)y * img->stride + (size_t)x * 4;
    p[0] = (uint8_t)r;
    p[1] = (uint8_t)g;
    p[2] = (uint8_t)b;
    p[3] = (uint8_t)a;
}

static void get_px(const image_t *img, int x, int y, int out[4])
{
    const uint8_t *p = img->rgba + (size_t)y * img->stride + (size_t)x * 4;
    out[0] = p[0];
    out[1] = p[1];
    out[2] = p[2];
    out[3] = p[3];
}

void test_image_ops(void)
{
    image_t *img;
    image_t *loaded;
    int px[4];
    uint8_t bgra[16];
    const char *path = "/tmp/board_app_test_image.png";

    img = image_create(3, 2);
    CHECK(img != NULL);
    set_px(img, 0, 0, 255, 0, 0, 255);
    set_px(img, 1, 0, 0, 255, 0, 255);
    set_px(img, 2, 0, 0, 0, 255, 255);
    set_px(img, 0, 1, 255, 255, 255, 255);
    set_px(img, 1, 1, 0, 0, 0, 255);
    set_px(img, 2, 1, 128, 128, 128, 255);

    CHECK_EQ_INT(image_rotate90(&img, true), 0);
    CHECK_EQ_INT(img->w, 2);
    CHECK_EQ_INT(img->h, 3);
    get_px(img, 1, 0, px);
    CHECK_EQ_INT(px[0], 255); /* 原 (0,0) 的红色 -> 新 (1,0) */
    get_px(img, 0, 0, px);
    CHECK_EQ_INT(px[0], 255);
    CHECK_EQ_INT(px[1], 255);
    CHECK_EQ_INT(px[2], 255); /* 原 (0,1) 的白色 -> 新 (0,0) */
    get_px(img, 1, 2, px);
    CHECK_EQ_INT(px[2], 255); /* 原 (2,0) 的蓝色 -> 新 (1,2) */

    image_free(img);
    img = image_create(2, 2);
    set_px(img, 0, 0, 10, 20, 30, 255);
    set_px(img, 1, 0, 40, 50, 60, 255);
    set_px(img, 0, 1, 70, 80, 90, 255);
    set_px(img, 1, 1, 100, 110, 120, 255);
    CHECK_EQ_INT(image_flip(img, true), 0);
    get_px(img, 0, 0, px);
    CHECK_EQ_INT(px[0], 40);
    CHECK_EQ_INT(image_flip(img, true), 0);
    get_px(img, 0, 0, px);
    CHECK_EQ_INT(px[0], 10);

    CHECK_EQ_INT(image_grayscale(img), 0);
    get_px(img, 0, 0, px);
    CHECK_EQ_INT(px[0], (299 * 10 + 587 * 20 + 114 * 30) / 1000);
    CHECK_EQ_INT(px[0], px[1]);
    CHECK_EQ_INT(px[1], px[2]);

    CHECK_EQ_INT(image_adjust(img, 0, 0), 0);
    get_px(img, 0, 0, px);
    CHECK_EQ_INT(px[0], (299 * 10 + 587 * 20 + 114 * 30) / 1000);

    CHECK_EQ_INT(image_crop(&img, 1, 0, 1, 1), 0);
    CHECK_EQ_INT(img->w, 1);
    CHECK_EQ_INT(img->h, 1);
    CHECK_EQ_INT(image_crop(&img, 0, 0, 2, 2), -EINVAL);

    set_px(img, 0, 0, 255, 0, 0, 255);
    memset(bgra, 0, sizeof(bgra));
    CHECK_EQ_INT(image_copy_to_bgra(img, bgra, sizeof(bgra)), 0);
    CHECK_EQ_INT(bgra[0], 0);
    CHECK_EQ_INT(bgra[1], 0);
    CHECK_EQ_INT(bgra[2], 255);
    CHECK_EQ_INT(bgra[3], 255);

    CHECK_EQ_INT(image_save_png(img, path), 0);
    loaded = image_load(path);
    CHECK(loaded != NULL);
    if (loaded != NULL) {
        CHECK_EQ_INT(loaded->w, 1);
        CHECK_EQ_INT(loaded->h, 1);
        get_px(loaded, 0, 0, px);
        CHECK_EQ_INT(px[0], 255);
        image_free(loaded);
    }
    unlink(path);
    image_free(img);
}

void test_image_history(void)
{
    image_t *img = image_create(2, 1);
    image_history_t *h;
    image_t *prev;

    CHECK(img != NULL);
    set_px(img, 0, 0, 255, 0, 0, 255);
    h = image_history_create(img);
    CHECK(h != NULL);
    CHECK(!image_history_can_undo(h));

    set_px(img, 0, 0, 0, 255, 0, 255);
    CHECK_EQ_INT(image_history_push(h, img), 0);
    CHECK(image_history_can_undo(h));
    prev = image_history_undo(h);
    CHECK(prev != NULL);
    if (prev != NULL) {
        int px[4];
        get_px(prev, 0, 0, px);
        CHECK_EQ_INT(px[0], 255);
        CHECK_EQ_INT(px[1], 0);
        image_free(prev);
    }
    CHECK(!image_history_can_undo(h));
    image_history_destroy(h);
    image_free(img);
}
