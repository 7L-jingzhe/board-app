/**
 * @file test_common.h
 * @brief 主机端测试用的小型断言辅助宏（不依赖外部框架）。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <stdio.h>

extern int g_failures;

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            g_failures++;                                                     \
        }                                                                     \
    } while (0)

#define CHECK_EQ_INT(actual, expected)                                        \
    do {                                                                      \
        long _a = (long)(actual);                                             \
        long _e = (long)(expected);                                           \
        if (_a != _e) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s=%ld, expected %ld\n", __FILE__,   \
                    __LINE__, #actual, _a, _e);                               \
            g_failures++;                                                     \
        }                                                                     \
    } while (0)

#define CHECK_NEAR(actual, expected, eps)                                     \
    do {                                                                      \
        double _a = (double)(actual);                                         \
        double _e = (double)(expected);                                       \
        double _d = _a > _e ? _a - _e : _e - _a;                              \
        if (_d > (eps)) {                                                     \
            fprintf(stderr, "FAIL %s:%d: %s=%f, expected %f\n", __FILE__,     \
                    __LINE__, #actual, _a, _e);                               \
            g_failures++;                                                     \
        }                                                                     \
    } while (0)

#endif /* TEST_COMMON_H */
