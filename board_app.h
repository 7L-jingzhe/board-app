/**
 * @file board_app.h
 * @brief Board App 入口与共享运行时上下文。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef BOARD_APP_H
#define BOARD_APP_H

#include "board_core/board_paths.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化路径/存储/主题/导航，并打开首页。 */
int board_app_init(const char *argv0);
void board_app_deinit(void);

/** 借用的运行时路径；在 board_app_init() 之后有效。 */
const board_paths_t *board_app_paths(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_APP_H */
