/**
 * @file board_storage.c
 * @brief 带原子持久化的线程安全键值存储。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "board_core/board_storage.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define STORAGE_KEY_MAX 64
#define STORAGE_VALUE_MAX 512
#define STORAGE_MAX_ENTRIES 128

typedef struct {
    char key[STORAGE_KEY_MAX];
    char value[STORAGE_VALUE_MAX];
} storage_entry_t;

static struct {
    pthread_mutex_t lock;
    int initialized;
    char path[PATH_MAX];
    storage_entry_t entries[STORAGE_MAX_ENTRIES];
    int count;
} g_store = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
};

static int mkdir_parent(const char *path)
{
    char tmp[PATH_MAX];
    char *slash;
    if (path == NULL) {
        return -1;
    }
    snprintf(tmp, sizeof(tmp), "%s", path);
    slash = strrchr(tmp, '/');
    if (slash == NULL || slash == tmp) {
        return 0;
    }
    *slash = '\0';
    /* 逐级创建目录 */
    for (char *p = tmp + 1; *p != '\0'; p++) {
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

static void trim_eol(char *s)
{
    size_t n;
    if (s == NULL) {
        return;
    }
    n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
}

static int find_locked(const char *key)
{
    for (int i = 0; i < g_store.count; i++) {
        if (strcmp(g_store.entries[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

static int set_locked(const char *key, const char *value)
{
    int idx;
    if (key == NULL || value == NULL) {
        return -EINVAL;
    }
    if (strlen(key) >= STORAGE_KEY_MAX || strlen(value) >= STORAGE_VALUE_MAX) {
        return -EINVAL;
    }
    idx = find_locked(key);
    if (idx < 0) {
        if (g_store.count >= STORAGE_MAX_ENTRIES) {
            return -ENOSPC;
        }
        idx = g_store.count++;
        snprintf(g_store.entries[idx].key, sizeof(g_store.entries[idx].key), "%s", key);
    }
    snprintf(g_store.entries[idx].value, sizeof(g_store.entries[idx].value), "%s", value);
    return 0;
}

static int load_file(void)
{
    FILE *fp;
    char line[STORAGE_KEY_MAX + STORAGE_VALUE_MAX + 8];

    fp = fopen(g_store.path, "r");
    if (fp == NULL) {
        return (errno == ENOENT) ? 0 : -errno;
    }
    while (fgets(line, sizeof(line), fp) != NULL) {
        char *eq;
        trim_eol(line);
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }
        eq = strchr(line, '=');
        if (eq == NULL) {
            continue;
        }
        *eq = '\0';
        (void)set_locked(line, eq + 1);
    }
    fclose(fp);
    return 0;
}

int board_storage_init(const char *settings_path)
{
    int ret;
    if (settings_path == NULL || settings_path[0] == '\0') {
        return -EINVAL;
    }
    if (strlen(settings_path) >= sizeof(g_store.path)) {
        return -EINVAL;
    }
    pthread_mutex_lock(&g_store.lock);
    if (g_store.initialized) {
        pthread_mutex_unlock(&g_store.lock);
        return 0;
    }
    snprintf(g_store.path, sizeof(g_store.path), "%s", settings_path);
    g_store.count = 0;
    (void)mkdir_parent(g_store.path);
    ret = load_file();
    if (ret == 0) {
        g_store.initialized = 1;
    }
    pthread_mutex_unlock(&g_store.lock);
    return ret;
}

void board_storage_shutdown(void)
{
    pthread_mutex_lock(&g_store.lock);
    g_store.initialized = 0;
    g_store.count = 0;
    g_store.path[0] = '\0';
    pthread_mutex_unlock(&g_store.lock);
}

const char *board_storage_get_str(const char *key, const char *default_value)
{
    static __thread char out[STORAGE_VALUE_MAX];
    int idx;
    if (key == NULL) {
        return default_value;
    }
    pthread_mutex_lock(&g_store.lock);
    idx = find_locked(key);
    if (idx >= 0) {
        snprintf(out, sizeof(out), "%s", g_store.entries[idx].value);
        pthread_mutex_unlock(&g_store.lock);
        return out;
    }
    pthread_mutex_unlock(&g_store.lock);
    return default_value;
}

int board_storage_set_str(const char *key, const char *value)
{
    int ret;
    pthread_mutex_lock(&g_store.lock);
    ret = set_locked(key, value);
    pthread_mutex_unlock(&g_store.lock);
    return ret;
}

int board_storage_get_int(const char *key, int default_value)
{
    const char *s = board_storage_get_str(key, NULL);
    char *end = NULL;
    long v;
    if (s == NULL || s[0] == '\0') {
        return default_value;
    }
    errno = 0;
    v = strtol(s, &end, 10);
    if (errno != 0 || end == s) {
        return default_value;
    }
    return (int)v;
}

int board_storage_set_int(const char *key, int value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    return board_storage_set_str(key, buf);
}

int board_storage_save(void)
{
    char tmp[PATH_MAX + 8];
    FILE *fp;
    int fd;
    int ret = 0;

    pthread_mutex_lock(&g_store.lock);
    if (!g_store.initialized) {
        pthread_mutex_unlock(&g_store.lock);
        return -EINVAL;
    }
    snprintf(tmp, sizeof(tmp), "%s.tmp", g_store.path);
    fp = fopen(tmp, "w");
    if (fp == NULL) {
        ret = -errno;
        pthread_mutex_unlock(&g_store.lock);
        return ret;
    }
    for (int i = 0; i < g_store.count; i++) {
        if (fprintf(fp, "%s=%s\n", g_store.entries[i].key, g_store.entries[i].value) < 0) {
            ret = -EIO;
            break;
        }
    }
    if (ret == 0 && fflush(fp) != 0) {
        ret = -EIO;
    }
    fd = fileno(fp);
    if (ret == 0 && fd >= 0 && fsync(fd) != 0) {
        ret = -EIO;
    }
    if (fclose(fp) != 0 && ret == 0) {
        ret = -EIO;
    }
    if (ret == 0 && rename(tmp, g_store.path) != 0) {
        ret = -errno;
    }
    if (ret != 0) {
        unlink(tmp);
    }
    pthread_mutex_unlock(&g_store.lock);
    return ret;
}
