/**
 * @file gallery_scan.c
 * @brief 单层图片目录扫描，按文件名排序。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "gallery/gallery_scan.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static int cmp_item(const void *a, const void *b)
{
    const gallery_item_t *ia = a;
    const gallery_item_t *ib = b;
    int r = strcasecmp(ia->name, ib->name);
    return r != 0 ? r : strcmp(ia->name, ib->name);
}

bool gallery_is_supported(const char *filename)
{
    const char *dot;
    if (filename == NULL) {
        return false;
    }
    dot = strrchr(filename, '.');
    if (dot == NULL) {
        return false;
    }
    return strcasecmp(dot, ".png") == 0 || strcasecmp(dot, ".jpg") == 0 ||
           strcasecmp(dot, ".jpeg") == 0 || strcasecmp(dot, ".bmp") == 0;
}

int gallery_scan(const char *dir, gallery_item_t *items, int capacity)
{
    DIR *dp;
    struct dirent *ent;
    int count = 0;

    if (dir == NULL || items == NULL || capacity <= 0) {
        return -EINVAL;
    }
    dp = opendir(dir);
    if (dp == NULL) {
        return -errno;
    }
    while ((ent = readdir(dp)) != NULL) {
        char path[PATH_MAX];
        struct stat st;
        if (ent->d_name[0] == '.') {
            continue; /* 跳过 .、.. 与隐藏文件 */
        }
        if (!gallery_is_supported(ent->d_name)) {
            continue;
        }
        if (snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name) >= (int)sizeof(path)) {
            continue;
        }
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }
        if (count >= capacity) {
            break; /* capacity 是硬上限，调用方可加大容量后重新扫描 */
        }
        if (strlen(ent->d_name) >= sizeof(items[count].name)) {
            continue;
        }
        snprintf(items[count].path, sizeof(items[count].path), "%s", path);
        {
            size_t n = strlen(ent->d_name);
            memcpy(items[count].name, ent->d_name, n + 1);
        }
        count++;
    }
    closedir(dp);
    qsort(items, (size_t)count, sizeof(items[0]), cmp_item);
    return count;
}
