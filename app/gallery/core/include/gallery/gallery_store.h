/**
 * @file gallery_store.h
 * @brief 相册目录与输出文件管理。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef GALLERY_STORE_H
#define GALLERY_STORE_H

#include <stddef.h>
#include "board_core/board_paths.h"
#include "gallery/gallery_scan.h"
#include "gallery/image.h"

#ifdef __cplusplus
extern "C" {
#endif

int gallery_store_init(const board_paths_t *paths);
int gallery_store_list(gallery_item_t *items, int capacity);
int gallery_store_save_edited(const image_t *img, char *out_path, size_t cap);
int gallery_store_save_near(const image_t *img, const char *source_path,
                            char *out_path, size_t cap);
int gallery_store_overwrite_edited(const image_t *img, const char *path);
int gallery_store_can_delete(const char *path);
int gallery_store_delete_edited(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* GALLERY_STORE_H */
