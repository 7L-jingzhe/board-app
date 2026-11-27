/**
 * @file board_paths.c
 * @brief 解析资源/数据根目录，创建运行时目录。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "board_core/board_paths.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int is_dir(const char *path)
{
    struct stat st;
    return path != NULL && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int join_path(char *out, size_t cap, const char *a, const char *b)
{
    int n;
    if (out == NULL || cap == 0 || a == NULL || b == NULL) {
        return -1;
    }
    if (b[0] == '\0') {
        n = snprintf(out, cap, "%s", a);
    } else {
        n = snprintf(out, cap, "%s/%s", a, b);
    }
    if (n < 0 || (size_t)n >= cap) {
        return -1;
    }
    return 0;
}

static int make_abs(const char *in, char *out, size_t cap)
{
    char tmp[PATH_MAX];
    char resolved[PATH_MAX];

    if (in == NULL || out == NULL || cap == 0) {
        return -1;
    }
    if (in[0] == '/') {
        snprintf(tmp, sizeof(tmp), "%s", in);
    } else {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            return -1;
        }
        if (join_path(tmp, sizeof(tmp), cwd, in) != 0) {
            return -1;
        }
    }
    if (realpath(tmp, resolved) != NULL) {
        snprintf(out, cap, "%s", resolved);
    } else {
        snprintf(out, cap, "%s", tmp);
    }
    return 0;
}

static int mkdir_p(const char *path)
{
    char tmp[PATH_MAX];
    size_t len;
    char *p;

    if (path == NULL || path[0] == '\0') {
        return -1;
    }
    len = strlen(path);
    if (len >= sizeof(tmp)) {
        return -1;
    }
    memcpy(tmp, path, len + 1);
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }
    for (p = tmp + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

static int exe_dir(char *out, size_t cap, const char *argv0)
{
    char path[PATH_MAX];
    char *slash;

    if (argv0 != NULL && strchr(argv0, '/') != NULL) {
        if (make_abs(argv0, path, sizeof(path)) != 0) {
            return -1;
        }
        slash = strrchr(path, '/');
        if (slash == NULL) {
            return -1;
        }
        *slash = '\0';
        snprintf(out, cap, "%s", path);
        return 0;
    }
#ifdef __linux__
    {
        ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
        if (n > 0) {
            path[n] = '\0';
            slash = strrchr(path, '/');
            if (slash != NULL) {
                *slash = '\0';
                snprintf(out, cap, "%s", path);
                return 0;
            }
        }
    }
#endif
    return -1;
}

static int set_root(board_paths_t *paths, const char *root)
{
    if (root == NULL || root[0] == '\0') {
        return -1;
    }
    if (strlen(root) >= sizeof(paths->root)) {
        return -1;
    }
    snprintf(paths->root, sizeof(paths->root), "%s", root);
    if (join_path(paths->assets, sizeof(paths->assets), root, "assets") != 0) {
        return -1;
    }
    if (join_path(paths->data, sizeof(paths->data), root, "data") != 0) {
        return -1;
    }
    if (join_path(paths->photos, sizeof(paths->photos), root, "assets/photos") != 0) {
        return -1;
    }
    if (join_path(paths->cache, sizeof(paths->cache), paths->data, "cache") != 0) {
        return -1;
    }
    if (join_path(paths->edited, sizeof(paths->edited), paths->data, "edited") != 0) {
        return -1;
    }
    if (join_path(paths->font, sizeof(paths->font), paths->assets,
                  "fonts/NotoSansSC-Board.ttf") != 0) {
        return -1;
    }
    if (join_path(paths->settings, sizeof(paths->settings), paths->data,
                  "settings.conf") != 0) {
        return -1;
    }
    return 0;
}

int board_paths_init(board_paths_t *paths, const char *argv0)
{
    const char *env;
    char root[PATH_MAX];
    char dir[PATH_MAX];
    char candidate[PATH_MAX];

    if (paths == NULL) {
        return -1;
    }
    memset(paths, 0, sizeof(*paths));

    env = getenv("BOARD_APP_ROOT");
    if (env != NULL && env[0] != '\0') {
        /* 显式指定的根目录优先；调用方可能指向只读的资源目录树。 */
        if (make_abs(env, root, sizeof(root)) != 0) {
            return -1;
        }
        if (set_root(paths, root) != 0) {
            return -1;
        }
    } else {
        int found = 0;

        /* 开发默认值：从 board_app 目录运行。 */
        if (getcwd(dir, sizeof(dir)) != NULL) {
            if (join_path(candidate, sizeof(candidate), dir, "assets") == 0 && is_dir(candidate)) {
                snprintf(root, sizeof(root), "%s", dir);
                found = 1;
            }
        }
        if (!found && exe_dir(dir, sizeof(dir), argv0) == 0) {
            if (join_path(candidate, sizeof(candidate), dir, "assets") == 0 && is_dir(candidate)) {
                snprintf(root, sizeof(root), "%s", dir);
                found = 1;
            } else {
                char parent[PATH_MAX];
                snprintf(parent, sizeof(parent), "%s", dir);
                char *slash = strrchr(parent, '/');
                if (slash != NULL) {
                    *slash = '\0';
                    if (join_path(candidate, sizeof(candidate), parent, "assets") == 0 &&
                        is_dir(candidate)) {
                        snprintf(root, sizeof(root), "%s", parent);
                        found = 1;
                    }
                }
            }
        }
        if (!found) {
            return -1;
        }
        if (set_root(paths, root) != 0) {
            return -1;
        }
    }

    env = getenv("BOARD_APP_DATA_DIR");
    if (env != NULL && env[0] != '\0') {
        if (make_abs(env, paths->data, sizeof(paths->data)) != 0) {
            return -1;
        }
        if (join_path(paths->cache, sizeof(paths->cache), paths->data, "cache") != 0 ||
            join_path(paths->edited, sizeof(paths->edited), paths->data, "edited") != 0 ||
            join_path(paths->settings, sizeof(paths->settings), paths->data,
                      "settings.conf") != 0) {
            return -1;
        }
    }

    env = getenv("BOARD_APP_PHOTO_DIR");
    if (env != NULL && env[0] != '\0') {
        if (make_abs(env, paths->photos, sizeof(paths->photos)) != 0) {
            return -1;
        }
    }
    return 0;
}

const char *board_paths_asset(const board_paths_t *paths, const char *rel)
{
    static char buf[PATH_MAX];
    if (paths == NULL) {
        return NULL;
    }
    if (rel == NULL || rel[0] == '\0') {
        return paths->assets;
    }
    if (join_path(buf, sizeof(buf), paths->assets, rel) != 0) {
        return NULL;
    }
    return buf;
}

const char *board_paths_data(const board_paths_t *paths, const char *rel)
{
    static char buf[PATH_MAX];
    if (paths == NULL) {
        return NULL;
    }
    if (rel == NULL || rel[0] == '\0') {
        return paths->data;
    }
    if (join_path(buf, sizeof(buf), paths->data, rel) != 0) {
        return NULL;
    }
    return buf;
}

int board_paths_ensure_data_dirs(const board_paths_t *paths)
{
    if (paths == NULL) {
        return -1;
    }
    if (mkdir_p(paths->data) != 0 || mkdir_p(paths->cache) != 0 ||
        mkdir_p(paths->edited) != 0) {
        return -1;
    }
    return 0;
}
