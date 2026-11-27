/**
 * @file gallery_store.c
 * @brief 相册文件列表与 PNG 输出命名。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "gallery/gallery_store.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "board_core/board_error.h"
#include "board_core/board_time.h"

static board_paths_t g_paths;
static int g_ready;

int gallery_store_init(const board_paths_t *paths)
{
    if (paths == NULL) {
        return -EINVAL;
    }
    g_paths = *paths;
    /* 浏览过程是只读的。只读根文件系统可能导致 data/ 无法创建；
     * 此时仍保持相册可用，把写入错误留到保存时再报告。 */
    (void)board_paths_ensure_data_dirs(&g_paths);
    g_ready = 1;
    return 0;
}

int gallery_store_list(gallery_item_t *items, int capacity)
{
    if (!g_ready) {
        return -EINVAL;
    }
    return gallery_scan(g_paths.photos, items, capacity);
}

int gallery_store_save_near(const image_t *img, const char *source_path,
                            char *out_path, size_t cap)
{
    const char *slash;
    const char *name;
    const char *dot;
    size_t stem;
    size_t dirlen;
    board_datetime_t dt;
    int n;

    if (!g_ready || img == NULL || source_path == NULL || out_path == NULL ||
        cap == 0) {
        return -EINVAL;
    }
    slash = strrchr(source_path, '/');
    if (slash == NULL || slash[1] == '\0') {
        return -EINVAL;
    }
    dirlen = (size_t)(slash - source_path);
    name = slash + 1;
    dot = strrchr(name, '.');
    stem = dot != NULL ? (size_t)(dot - name) : strlen(name);
    if (stem == 0 || board_time_now(&dt) != 0) {
        return -EIO;
    }
    for (int i = 0; i < 100; i++) {
        if (i == 0) {
            n = snprintf(out_path, cap,
                         "%.*s/%.*s_edited_%04d%02d%02d_%02d%02d%02d.png",
                         (int)dirlen, source_path, (int)stem, name, dt.year,
                         dt.month, dt.day, dt.hour, dt.minute, dt.second);
        } else {
            n = snprintf(out_path, cap,
                         "%.*s/%.*s_edited_%04d%02d%02d_%02d%02d%02d_%02d.png",
                         (int)dirlen, source_path, (int)stem, name, dt.year,
                         dt.month, dt.day, dt.hour, dt.minute, dt.second, i);
        }
        if (n < 0 || (size_t)n >= cap) {
            return -ENAMETOOLONG;
        }
        if (access(out_path, F_OK) != 0) {
            return image_save_png(img, out_path);
        }
    }
    return -EEXIST;
}

int gallery_store_can_delete(const char *path)
{
    size_t root_len;
    if (!g_ready || path == NULL) {
        return 0;
    }
    /* 可编辑照片目录下直接存放的任意受支持图片，
     * 在 UI 确认后都可删除。 */
    root_len = strlen(g_paths.edited);
    if (strncmp(path, g_paths.edited, root_len) == 0 &&
        path[root_len] == '/' &&
        strchr(path + root_len + 1, '/') == NULL &&
        gallery_is_supported(path + root_len + 1)) {
        return 1;
    }
    root_len = strlen(g_paths.photos);
    if (strncmp(path, g_paths.photos, root_len) == 0 &&
        path[root_len] == '/' &&
        strchr(path + root_len + 1, '/') == NULL &&
        gallery_is_supported(path + root_len + 1)) {
        return 1;
    }
    return 0;
}

int gallery_store_overwrite_edited(const image_t *img, const char *path)
{
    if (!g_ready || img == NULL || path == NULL || !gallery_store_can_delete(path)) {
        return -EACCES;
    }
    return image_save_png(img, path);
}

int gallery_store_delete_edited(const char *path)
{
    if (!g_ready || path == NULL || !gallery_store_can_delete(path)) {
        return -EACCES;
    }
    return unlink(path) == 0 ? 0 : -EIO;
}

int gallery_store_save_edited(const image_t *img, char *out_path, size_t cap)
{
    board_datetime_t dt;
    char base[PATH_MAX];
    int n;

    if (!g_ready || img == NULL || out_path == NULL || cap == 0) {
        return -EINVAL;
    }
    if (board_time_now(&dt) != 0) {
        return -EIO;
    }
    n = snprintf(base, sizeof(base), "%s/edited_%04d%02d%02d_%02d%02d%02d",
                 g_paths.edited, dt.year, dt.month, dt.day,
                 dt.hour, dt.minute, dt.second);
    if (n < 0 || (size_t)n >= sizeof(base)) {
        return -ENAMETOOLONG;
    }

    for (int i = 0; i < 100; i++) {
        if (i == 0) {
            n = snprintf(out_path, cap, "%s.png", base);
        } else {
            n = snprintf(out_path, cap, "%s_%02d.png", base, i);
        }
        if (n < 0 || (size_t)n >= cap) {
            return -ENAMETOOLONG;
        }
        if (access(out_path, F_OK) != 0) {
            break; /* 找到空闲位置 */
        }
        if (i == 99) {
            return -EEXIST;
        }
    }
    if (image_save_png(img, out_path) != 0) {
        return -EIO;
    }
    return 0;
}
