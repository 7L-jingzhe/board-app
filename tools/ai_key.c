/**
 * @file ai_key.c
 * @brief 把 DeepSeek（或兼容 OpenAI 的）API key 保存到本地配置，免去每次带环境变量。
 *
 * 数据落在 `<data>/settings.conf`（和聊天记录、记住的账号同一个文件），
 * 键名：`ai.key` / `ai.url` / `ai.model`。
 *
 * 用法（板上）：
 *   ./ai_key sk-xxxxxxxx                 # 保存 key（自动补默认 url/model）
 *   ./ai_key                             # 查看当前配置（key 做掩码）
 *   ./ai_key --clear                     # 删除本地 key
 *   ./ai_key --url https://api.deepseek.com/v1 --model deepseek-flash
 *
 * 环境变量 BOARD_APP_AI_KEY 优先级更高，可临时覆盖本地保存的 key。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include <stdio.h>
#include <string.h>

#include "board_core/board_log.h"
#include "board_core/board_paths.h"
#include "board_core/board_storage.h"
#include "chat/chat_service.h"

static void mask_key(const char *key, char *out, size_t cap)
{
    size_t n = strlen(key);

    if (n <= 10) {
        snprintf(out, cap, "****");
        return;
    }
    snprintf(out, cap, "%.6s...%.4s", key, key + n - 4);
}

int main(int argc, char **argv)
{
    board_paths_t paths;
    char masked[64];
    char key[160] = { 0 };
    char url[512] = { 0 };
    char model[96] = { 0 };
    int i;

    if (board_paths_init(&paths, argv[0]) != 0) {
        fprintf(stderr, "无法定位数据目录\n");
        return 1;
    }
    board_log_set_level(BOARD_LOG_WARN);
    if (board_storage_init(paths.settings) != 0) {
        fprintf(stderr, "无法读取配置：%s\n", paths.settings);
        return 1;
    }

    if (argc >= 2 && strcmp(argv[1], "--clear") == 0) {
        chat_ai_clear_key();
        printf("已清除本地 API key（%s）\n", paths.settings);
        return 0;
    }

    /* --url / --model 可与 key 一起给 */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--url") == 0 && i + 1 < argc) {
            snprintf(url, sizeof(url), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--model") == 0 && i + 1 < argc) {
            snprintf(model, sizeof(model), "%s", argv[++i]);
        } else if (argv[i][0] != '-' && key[0] == '\0') {
            snprintf(key, sizeof(key), "%s", argv[i]);
        }
    }
    if (url[0] != '\0' || model[0] != '\0') {
        chat_ai_save_endpoint(url[0] != '\0' ? url : NULL,
                              model[0] != '\0' ? model : NULL);
        printf("已更新接口：url=%s model=%s\n",
               url[0] != '\0' ? url : "(不变)", model[0] != '\0' ? model : "(不变)");
        if (key[0] == '\0') {
            return 0;
        }
    }

    if (key[0] != '\0') {
        if (chat_ai_save_key(key) != 0) {
            fprintf(stderr, "写入失败：%s\n", paths.settings);
            return 1;
        }
        mask_key(key, masked, sizeof(masked));
        printf("已保存 API key：%s\n", masked);
        printf("配置文件：%s\n", paths.settings);
        printf("以后直接 ./main 就会用这个 key，不再需要环境变量。\n");
        return 0;
    }

    /* 没给参数：显示当前配置 */
    {
        const char *v;
        char tmp[512];

        v = board_storage_get_str("ai.url", "");
        snprintf(tmp, sizeof(tmp), "%s", v != NULL ? v : "");
        printf("ai.url  : %s\n", tmp[0] != '\0' ? tmp : "(默认 https://api.deepseek.com/v1)");
        v = board_storage_get_str("ai.model", "");
        snprintf(tmp, sizeof(tmp), "%s", v != NULL ? v : "");
        printf("ai.model: %s\n", tmp[0] != '\0' ? tmp : "(默认 deepseek-flash)");
        v = board_storage_get_str("ai.key", "");
        snprintf(tmp, sizeof(tmp), "%s", v != NULL ? v : "");
        if (tmp[0] == '\0') {
            printf("ai.key  : (未设置)\n");
        } else {
            mask_key(tmp, masked, sizeof(masked));
            printf("ai.key  : %s\n", masked);
        }
        printf("配置文件: %s\n", paths.settings);
    }
    return 0;
}
