/**
 * @file chat_offline.c
 * @brief 离线助手：关键词规则、简单算术、时间与回显。
 *
 * 这是一个完全离线的兜底实现：当没有配置大模型，或网络/接口失败时，
 * 用户仍然能收到有意义的回复，也方便在无公网的开发板上验收 UI。
 * 只依赖 C 标准库，不依赖 LVGL（符合 core 层约束）。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "chat/chat_offline.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_core/board_log.h"
#include "board_core/board_time.h"

#define CHAT_OFFLINE_MAX_INPUT 1024

typedef struct {
    const char *keyword;   /* UTF-8 子串，NULL 表示默认分支 */
    const char *reply;
} offline_rule_t;

/* 顺序即优先级：越具体越靠前。 */
static const offline_rule_t RULES[] = {
    { "你好", "你好！我是这块开发板上的 AI 助手，可以问我时间、使用说明，"
              "或者随便聊两句。" },
    { "您好", "您好！有什么可以帮你的吗？" },
    { "hello", "Hello! I am the on-device assistant. 也可以直接说中文。" },
    { "hi", "Hi！试试说“现在几点”或“你会什么”。" },
    { "你是谁", "我是智慧生活开发板上的聊天助手，界面用 LVGL 绘制，"
                "中文输入由 LVGL 自带的开源拼音输入法完成。" },
    { "叫什么", "我是一个运行在开发板上的离线小助手，你可以叫我小智。" },
    { "你会什么", "离线状态下我能：报时间、做简单四则运算、介绍输入法，"
                  "以及回应问好。配置大模型后可以自由对话。" },
    { "会什么", "我能报时间、算四则运算、介绍输入法与开发板信息；"
                "配置大模型后可以自由对话。" },
    { "时间", "" },   /* 动态回复，见 chat_offline_reply */
    { "几点", "" },
    { "输入法", "虚拟键盘右下角是 LVGL 内置的开源拼音输入法：直接敲拼音（如 "
                "ni hao），上方会出现候选字，点一下就能上屏。" },
    { "拼音", "敲字母就会显示拼音候选，点候选词即可输入中文；"
              "左下角 abc 可以退回纯英文。" },
    { "键盘", "右侧就是虚拟键盘：ABC 是大写英文，abc 是小写英文，1# 是符号与数字。" },
    { "开发板", "这是一块 RK356X 开发板（aarch64、1024×600 触摸屏），"
                "运行 LVGL 9.2 界面。" },
    { "天气", "首页有独立的天气应用，会显示实时天气和 7 天预报。" },
    { "谢谢", "不客气！" },
    { "感谢", "很高兴能帮到你。" },
    { "再见", "再见，随时回来找我聊天。" },
    { "笑话", "程序员最怕的两件事：一是断电，二是没电。"
              "（还有第三件：显示器没信号。）" },
    { "offline", "当前是离线模式：没有配置大模型，或网络不可用。" },
    { NULL, "我在听懂的部分会认真回答。试试问“现在几点”“你会什么”，"
            "或配置大模型后自由对话。" },
};

static int contains_ascii(const char *haystack, const char *needle)
{
    size_t i;
    for (i = 0; haystack[i] != '\0'; i++) {
        size_t j = 0;
        while (needle[j] != '\0' && tolower((unsigned char)haystack[i + j]) ==
                                       tolower((unsigned char)needle[j])) {
            j++;
        }
        if (needle[j] == '\0') {
            return 1;
        }
    }
    return 0;
}

/* ---------------- 四则运算 ---------------- */

typedef struct {
    const char *expr;
    int pos;
    double value;
    int ok;
} calc_t;

static void calc_skip_space(calc_t *c)
{
    while (c->expr[c->pos] == ' ' || c->expr[c->pos] == '\t') {
        c->pos++;
    }
}

static double calc_sum(calc_t *c);

static double calc_atom(calc_t *c)
{
    calc_skip_space(c);
    if (c->expr[c->pos] == '(') {
        double v;
        c->pos++;
        v = calc_sum(c);
        calc_skip_space(c);
        if (c->expr[c->pos] != ')') {
            c->ok = 0;
            return 0;
        }
        c->pos++;
        return v;
    }
    {
        char *end = NULL;
        double v = strtod(c->expr + c->pos, &end);
        if (end == c->expr + c->pos) {
            c->ok = 0;
            return 0;
        }
        c->pos = (int)(end - c->expr);
        return v;
    }
}

static double calc_product(calc_t *c)
{
    double v = calc_atom(c);
    for (;;) {
        calc_skip_space(c);
        if (c->expr[c->pos] == '*') {
            c->pos++;
            v *= calc_atom(c);
        } else if (c->expr[c->pos] == '/') {
            double d;
            c->pos++;
            d = calc_atom(c);
            if (d == 0) {
                c->ok = 0;
                return 0;
            }
            v /= d;
        } else if (c->expr[c->pos] == '%') {
            double d;
            c->pos++;
            d = calc_atom(c);
            if (d == 0) {
                c->ok = 0;
                return 0;
            }
            v = fmod(v, d);
        } else {
            return v;
        }
        if (!c->ok) {
            return 0;
        }
    }
}

static double calc_sum(calc_t *c)
{
    double v = calc_product(c);
    for (;;) {
        calc_skip_space(c);
        if (c->expr[c->pos] == '+') {
            c->pos++;
            v += calc_product(c);
        } else if (c->expr[c->pos] == '-') {
            c->pos++;
            v -= calc_product(c);
        } else {
            return v;
        }
        if (!c->ok) {
            return 0;
        }
    }
}

/* 只接受由数字、运算符、括号、空格组成的“纯算式”，
 * 避免把普通句子里的数字误算。 */
static int calc_try(const char *input, double *result)
{
    char expr[CHAT_OFFLINE_MAX_INPUT];
    size_t i, n = 0;
    int digit_seen = 0;
    int op_seen = 0;
    calc_t c;

    for (i = 0; input[i] != '\0' && n + 1 < sizeof(expr); i++) {
        unsigned char ch = (unsigned char)input[i];
        unsigned char c1 = (unsigned char)input[i + 1];
        unsigned char c2 = (unsigned char)input[i + 2];
        if (ch == 0xEF && c1 == 0xBC && c2 == 0x88) { /* （ */
            expr[n++] = '(';
            i += 2;
            continue;
        }
        if (ch == 0xEF && c1 == 0xBC && c2 == 0x89) { /* ） */
            expr[n++] = ')';
            i += 2;
            continue;
        }
        if (ch == 0xC3 && c1 == 0x97) { /* × */
            expr[n++] = '*';
            i += 1;
            continue;
        }
        if (ch == 0xC3 && c1 == 0xB7) { /* ÷ */
            expr[n++] = '/';
            i += 1;
            continue;
        }
        if (isdigit(ch)) {
            digit_seen = 1;
        } else if (strchr("+-*/%()", ch) != NULL) {
            op_seen = 1;
        } else if (ch == '.' || ch == ' ') {
            /* 正常字符 */
        } else if (ch == '=' || ch == '?') {
            /* 结尾的 '=' '？'（U+FF1F 的首字节也是 0xEF）忽略 */
            if (ch == 0xEF) {
                i += 1;   /* 跳过后续两字节，避免误判为非法字符 */
            }
            continue;
        } else {
            return 0;
        }
        expr[n++] = (char)ch;
    }
    expr[n] = '\0';
    if (!digit_seen || !op_seen) {
        return 0;
    }
    memset(&c, 0, sizeof(c));
    c.expr = expr;
    c.ok = 1;
    *result = calc_sum(&c);
    calc_skip_space(&c);
    if (!c.ok || expr[c.pos] != '\0') {
        return 0;
    }
    return 1;
}

int chat_offline_is_greeting(const char *input)
{
    if (input == NULL) {
        return 0;
    }
    return contains_ascii(input, "你好") || contains_ascii(input, "hi") ||
           contains_ascii(input, "hello");
}

size_t chat_offline_reply(const char *input, char *out, size_t cap)
{
    double result = 0.0;
    char text[CHAT_OFFLINE_MAX_INPUT];
    size_t i;

    if (out == NULL || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (input == NULL) {
        return 0;
    }
    /* 去掉首尾空白，便于匹配与回显。 */
    snprintf(text, sizeof(text), "%s", input);
    {
        char *p = text;
        size_t len;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
        }
        len = strlen(p);
        while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\t' ||
                           p[len - 1] == '\r' || p[len - 1] == '\n')) {
            p[--len] = '\0';
        }
        memmove(text, p, len + 1);
    }
    if (text[0] == '\0') {
        return (size_t)snprintf(out, cap, "想聊点什么？");
    }

    /* 1) 算式优先，便于演示"能算数"。 */
    if (calc_try(text, &result)) {
        char value[64];
        if (fabs(result - floor(result)) < 1e-9 && fabs(result) < 1e15) {
            snprintf(value, sizeof(value), "%.0f", result);
        } else {
            snprintf(value, sizeof(value), "%.6g", result);
        }
        if (strcmp(value, "-0") == 0) {
            snprintf(value, sizeof(value), "0");
        }
        BOARD_LOGD("chat", "offline calc: %s = %s", text, value);
        return (size_t)snprintf(out, cap, "%s = %s", text, value);
    }

    /* 2) 关键词规则。 */
    for (i = 0; i < sizeof(RULES) / sizeof(RULES[0]); i++) {
        if (RULES[i].keyword == NULL) {
            continue;
        }
        if (contains_ascii(text, RULES[i].keyword)) {
            if (strcmp(RULES[i].keyword, "时间") == 0 ||
                strcmp(RULES[i].keyword, "几点") == 0) {
                char hhmm[16];
                char date[48];
                const char *t = "未知";
                const char *d = "";
                if (board_time_format_hhmm(hhmm, sizeof(hhmm)) == 0) {
                    t = hhmm;
                }
                if (board_time_format_date_cn(date, sizeof(date)) == 0) {
                    d = date;
                }
                return (size_t)snprintf(out, cap, "现在是 %s。%s", t, d);
            }
            return (size_t)snprintf(out, cap, "%s", RULES[i].reply);
        }
    }

    /* 3) 默认：回显 + 提示。 */
    return (size_t)snprintf(out, cap,
                            "你说的是“%s”。离线模式下我只能回应有限的话题；"
                            "配置大模型后可以自由对话。",
                            text);
}
