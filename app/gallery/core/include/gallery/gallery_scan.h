/**
 * @file gallery_scan.h
 * @brief 单层图片目录扫描。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef GALLERY_SCAN_H
#define GALLERY_SCAN_H

#include <stdbool.h>
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GALLERY_MAX_ITEMS 128
#define GALLERY_NAME_MAX 128

typedef struct {
    char path[PATH_MAX];
    char name[GALLERY_NAME_MAX];
} gallery_item_t;

int gallery_scan(const char *dir, gallery_item_t *items, int capacity);
bool gallery_is_supported(const char *filename);

#ifdef __cplusplus
}
#endif

#endif /* GALLERY_SCAN_H */
