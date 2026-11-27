/**
 * @file comm_history.c
 * @brief 聊天记录持久化实现：JSON Lines，按会话分文件。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "comm/comm_history.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "board_core/board_log.h"
#include "board_core/board_paths.h"
#include "cJSON.h"
#include "comm/comm_net.h"

#define HISTORY_DIRNAME "comm_history"

static char s_dir[512];
static int s_ready;
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;

/** 会话名只允许字母数字（和用户名规则一致），群聊用固定名。 */
static bool peer_ok(const char *peer)
{
    return peer != NULL && (comm_name_valid(peer) || strcmp(peer, COMM_HISTORY_GROUP) == 0);
}

static int path_of(const char *peer, char *out, size_t cap)
{
    if (!s_ready || !peer_ok(peer)) {
        return -EINVAL;
    }
    if (snprintf(out, cap, "%s/%s.jsonl", s_dir, peer) >= (int)cap) {
        return -ENAMETOOLONG;
    }
    return 0;
}

int comm_history_init(const board_paths_t *paths)
{
    pthread_mutex_lock(&s_lock);
    if (s_ready) {
        pthread_mutex_unlock(&s_lock);
        return 0;
    }
    if (paths == NULL || paths->data[0] == '\0') {
        pthread_mutex_unlock(&s_lock);
        return -EINVAL;
    }
    if (snprintf(s_dir, sizeof(s_dir), "%s/%s", paths->data, HISTORY_DIRNAME) >=
        (int)sizeof(s_dir)) {
        pthread_mutex_unlock(&s_lock);
        return -ENAMETOOLONG;
    }
    mkdir(s_dir, 0755);
    s_ready = 1;
    BOARD_LOGI("comm", "chat history dir: %s", s_dir);
    pthread_mutex_unlock(&s_lock);
    return 0;
}

/** 读整个文件到内存；不存在返回 NULL。 */
static char *read_all(const char *path)
{
    FILE *f = fopen(path, "rb");
    long total;
    char *buf;

    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (total = ftell(f)) < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    buf = malloc((size_t)total + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    if (total > 0 && fread(buf, 1, (size_t)total, f) != (size_t)total) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[total] = '\0';
    fclose(f);
    return buf;
}

/** 把文本里的换行替换成空格，保证一行一条 JSON。 */
static void sanitize(char *text)
{
    char *p;

    for (p = text; *p != '\0'; p++) {
        if (*p == '\n' || *p == '\r') {
            *p = ' ';
        }
    }
}

int comm_history_append(const char *peer, const char *from, bool mine,
                        const char *text, long ts)
{
    char path[600];
    char *existing = NULL;
    char *line = NULL;
    char *buf = NULL;
    cJSON *item;
    int count = 0;
    int drop = 0;
    size_t need = 0;
    int rc = 0;

    if (path_of(peer, path, sizeof(path)) != 0 || text == NULL) {
        return -EINVAL;
    }
    pthread_mutex_lock(&s_lock);
    existing = read_all(path);
    item = cJSON_CreateObject();
    if (item == NULL) {
        free(existing);
        pthread_mutex_unlock(&s_lock);
        return -ENOMEM;
    }
    cJSON_AddStringToObject(item, COMM_K_FROM, from != NULL ? from : "");
    cJSON_AddNumberToObject(item, "mine", mine ? 1 : 0);
    cJSON_AddNumberToObject(item, COMM_K_TS, (double)ts);
    {
        char copy[COMM_MAX_TEXT + 1];
        snprintf(copy, sizeof(copy), "%s", text);
        sanitize(copy);
        cJSON_AddStringToObject(item, COMM_K_TEXT, copy);
    }
    line = cJSON_PrintUnformatted(item);
    cJSON_Delete(item);
    if (line == NULL) {
        free(existing);
        pthread_mutex_unlock(&s_lock);
        return -ENOMEM;
    }

    /* 数一下现有行数，超限就只保留最后 (MAX-1) 行 */
    if (existing != NULL) {
        char *p;
        for (p = existing; *p != '\0'; p++) {
            if (*p == '\n') {
                count++;
            }
        }
    }
    if (count >= COMM_HISTORY_MAX) {
        drop = count - (COMM_HISTORY_MAX - 1);
    }

    need = strlen(line) + 2 + (existing != NULL ? strlen(existing) : 0) + 1;
    buf = malloc(need);
    if (buf == NULL) {
        free(line);
        free(existing);
        pthread_mutex_unlock(&s_lock);
        return -ENOMEM;
    }
    buf[0] = '\0';
    if (existing != NULL && drop > 0) {
        /* 跳过前 drop 行 */
        int seen = 0;
        char *p = existing;
        while (*p != '\0' && seen < drop) {
            if (*p == '\n') {
                seen++;
            }
            p++;
        }
        snprintf(buf, need, "%s", p);
    } else if (existing != NULL) {
        snprintf(buf, need, "%s", existing);
    }
    {
        size_t used = strlen(buf);
        if (used == 0 || buf[used - 1] != '\n') {
            buf[used++] = '\n';
            buf[used] = '\0';
        }
        snprintf(buf + used, need - used, "%s\n", line);
    }
    rc = comm_write_file_atomic(path, buf, strlen(buf));
    free(buf);
    free(line);
    free(existing);
    pthread_mutex_unlock(&s_lock);
    if (rc != 0) {
        BOARD_LOGW("comm", "history append %s failed: %s", peer, strerror(-rc));
    }
    return rc;
}

int comm_history_load(const char *peer, comm_history_item_t *items, int max)
{
    char path[600];
    char *buf;
    char *line;
    char *save = NULL;
    int n = 0;
    int total = 0;

    if (items == NULL || max <= 0 || path_of(peer, path, sizeof(path)) != 0) {
        return 0;
    }
    pthread_mutex_lock(&s_lock);
    buf = read_all(path);
    if (buf == NULL) {
        pthread_mutex_unlock(&s_lock);
        return 0;
    }

    /* 先数行，只要最后 max 条 */
    {
        char *p;
        for (p = buf; *p != '\0'; p++) {
            if (*p == '\n') {
                total++;
            }
        }
    }
    {
        int skip = total > max ? total - max : 0;
        int seen = 0;
        for (line = strtok_r(buf, "\n", &save); line != NULL;
             line = strtok_r(NULL, "\n", &save)) {
            cJSON *item;
            const cJSON *from;
            const cJSON *text;
            const cJSON *ts;
            const cJSON *mine;

            if (seen++ < skip) {
                continue;
            }
            item = cJSON_Parse(line);
            if (item == NULL) {
                continue;
            }
            from = cJSON_GetObjectItemCaseSensitive(item, COMM_K_FROM);
            text = cJSON_GetObjectItemCaseSensitive(item, COMM_K_TEXT);
            ts = cJSON_GetObjectItemCaseSensitive(item, COMM_K_TS);
            mine = cJSON_GetObjectItemCaseSensitive(item, "mine");
            if (cJSON_IsString(text) && n < max) {
                snprintf(items[n].from, sizeof(items[n].from), "%s",
                         cJSON_IsString(from) ? from->valuestring : "");
                snprintf(items[n].text, sizeof(items[n].text), "%s",
                         text->valuestring);
                items[n].ts = cJSON_IsNumber(ts) ? (long)ts->valuedouble : 0;
                items[n].mine = cJSON_IsTrue(mine);
                n++;
            }
            cJSON_Delete(item);
        }
    }
    free(buf);
    pthread_mutex_unlock(&s_lock);
    return n;
}

int comm_history_clear(const char *peer)
{
    char path[600];

    if (path_of(peer, path, sizeof(path)) != 0) {
        return -EINVAL;
    }
    pthread_mutex_lock(&s_lock);
    unlink(path);
    pthread_mutex_unlock(&s_lock);
    return 0;
}
