/**
 * @file board_paths.h
 * @brief 解析只读资源目录与可写运行时数据目录。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef BOARD_PATHS_H
#define BOARD_PATHS_H

#include <limits.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char root[PATH_MAX];
    char assets[PATH_MAX];
    char data[PATH_MAX];
    char photos[PATH_MAX];
    char edited[PATH_MAX];
    char cache[PATH_MAX];
    char font[PATH_MAX];
    char settings[PATH_MAX];
} board_paths_t;

/**
 * 解析 root/assets/data。查找顺序：
 *   1. BOARD_APP_ROOT
 *   2. 包含 assets/ 的当前工作目录
 *   3. 包含 assets/ 的可执行文件所在目录
 *   4. 包含 assets/ 的可执行文件所在目录的父目录
 *
 * BOARD_APP_DATA_DIR 覆盖 data/；BOARD_APP_PHOTO_DIR 覆盖 photos/。
 * 成功返回 0，失败返回负错误码。
 */
int board_paths_init(board_paths_t *paths, const char *argv0);

/** 拼接资源相对路径。返回的指针是静态缓冲区，非线程安全。 */
const char *board_paths_asset(const board_paths_t *paths, const char *rel);

/** 拼接数据相对路径。返回的指针是静态缓冲区，非线程安全。 */
const char *board_paths_data(const board_paths_t *paths, const char *rel);

/** 若不存在则创建 data/、cache/、edited/。 */
int board_paths_ensure_data_dirs(const board_paths_t *paths);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_PATHS_H */
