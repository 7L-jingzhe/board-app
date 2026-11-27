/**
 * @file test_chat.c
 * @brief 聊天核心的主机端测试：离线助手、JSON 请求构造、响应解析。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "test_common.h"

#include <stdlib.h>
#include <string.h>

#include "chat/chat_llm.h"
#include "chat/chat_offline.h"

static void test_offline_calc(void)
{
    char out[512];
    size_t n;

    n = chat_offline_reply("3*(4+5)", out, sizeof(out));
    CHECK(n > 0);
    CHECK(strstr(out, "= 27") != NULL);

    n = chat_offline_reply("1+2*3", out, sizeof(out));
    CHECK(n > 0);
    CHECK(strstr(out, "= 7") != NULL);

    /* 全角括号与 × ÷ 也要能算 */
    n = chat_offline_reply("（8+2）×3", out, sizeof(out));
    CHECK(n > 0);
    CHECK(strstr(out, "= 30") != NULL);

    n = chat_offline_reply("9÷0", out, sizeof(out));
    CHECK(n > 0);
    /* 除零不应返回算式结果，而是落到普通回答 */
    CHECK(strstr(out, "= ") == NULL);
}

static void test_offline_rules(void)
{
    char out[512];

    CHECK(chat_offline_is_greeting("你好") == 1);
    CHECK(chat_offline_is_greeting("hello") == 1);
    CHECK(chat_offline_is_greeting("今天天气") == 0);

    CHECK(chat_offline_reply("你会什么", out, sizeof(out)) > 0);
    CHECK(strstr(out, "时间") != NULL || strstr(out, "四则") != NULL);

    CHECK(chat_offline_reply("输入法怎么用", out, sizeof(out)) > 0);
    CHECK(strstr(out, "候选") != NULL);

    /* 空输入给出提示而不是崩溃 */
    CHECK(chat_offline_reply("", out, sizeof(out)) > 0);
    CHECK(out[0] != '\0');
}

static void test_llm_url(void)
{
    chat_ai_config_t cfg;
    char url[768];

    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.base_url, sizeof(cfg.base_url), "http://192.168.1.10:11434/v1/");
    CHECK_EQ_INT(chat_ai_build_url(&cfg, url, sizeof(url)), 0);
    CHECK(strcmp(url, "http://192.168.1.10:11434/v1/chat/completions") == 0);

    /* https 现在支持（板端 TLS 走运行时 dlopen OpenSSL） */
    snprintf(cfg.base_url, sizeof(cfg.base_url), "https://api.deepseek.com/v1");
    CHECK_EQ_INT(chat_ai_build_url(&cfg, url, sizeof(url)), 0);
    CHECK(strcmp(url, "https://api.deepseek.com/v1/chat/completions") == 0);

    /* 其它协议仍然拒绝 */
    snprintf(cfg.base_url, sizeof(cfg.base_url), "ftp://x");
    CHECK(chat_ai_build_url(&cfg, url, sizeof(url)) != 0);
}

static void test_llm_request(void)
{
    chat_ai_config_t cfg;
    chat_message_t history[2];
    char *json = NULL;

    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.model, sizeof(cfg.model), "qwen2.5:3b");
    snprintf(cfg.system_prompt, sizeof(cfg.system_prompt), "你是助手");

    history[0].role = CHAT_ROLE_USER;
    history[0].text = strdup("你好");
    history[1].role = CHAT_ROLE_ASSISTANT;
    history[1].text = strdup("你好呀");

    CHECK_EQ_INT(chat_ai_build_request(&cfg, history, 2, "今天几号", &json), 0);
    CHECK(json != NULL);
    if (json != NULL) {
        CHECK(strstr(json, "\"model\"") != NULL);
        CHECK(strstr(json, "qwen2.5:3b") != NULL);
        CHECK(strstr(json, "\"stream\":false") != NULL || strstr(json, "\"stream\":0") != NULL);
        /* 历史 + 本次输入共 3 条消息 */
        CHECK(strstr(json, "今天几号") != NULL);
        free(json);
    }
    free(history[0].text);
    free(history[1].text);
}

static void test_llm_parse(void)
{
    const char *openai =
        "{\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\","
        "\"content\":\"你好，我是模型\"}}]}";
    const char *ollama = "{\"message\":{\"role\":\"assistant\",\"content\":\"本地回复\"}}";
    const char *err = "{\"error\":{\"message\":\"invalid api key\"}}";
    char *reply = NULL;
    char msg[160];

    CHECK_EQ_INT(chat_ai_parse_response(openai, strlen(openai), &reply, msg,
                                        sizeof(msg)), 0);
    CHECK(reply != NULL && strcmp(reply, "你好，我是模型") == 0);
    free(reply);
    reply = NULL;

    CHECK_EQ_INT(chat_ai_parse_response(ollama, strlen(ollama), &reply, msg,
                                        sizeof(msg)), 0);
    CHECK(reply != NULL && strcmp(reply, "本地回复") == 0);
    free(reply);
    reply = NULL;

    CHECK(chat_ai_parse_response(err, strlen(err), &reply, msg, sizeof(msg)) != 0);
    CHECK(reply == NULL);
    CHECK(strstr(msg, "invalid api key") != NULL);

    CHECK(chat_ai_parse_response("not json", 8, &reply, msg, sizeof(msg)) != 0);
}

void test_chat(void)
{
    test_offline_calc();
    test_offline_rules();
    test_llm_url();
    test_llm_request();
    test_llm_parse();
}
