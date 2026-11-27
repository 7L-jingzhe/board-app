/**
 * @file ui_async.h
 * @brief 在 UI 线程上排空 board_async_queue 的 LVGL 定时器。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef UI_ASYNC_H
#define UI_ASYNC_H

#include "board_core/board_async_queue.h"
#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ui_async_handler_t)(void *item, void *user_data);

typedef struct ui_async_pump ui_async_pump_t;

ui_async_pump_t *ui_async_pump_create(board_async_queue_t *queue,
                                      ui_async_handler_t handler,
                                      void *user_data,
                                      uint32_t period_ms);
void ui_async_pump_destroy(ui_async_pump_t *pump);

#ifdef __cplusplus
}
#endif

#endif /* UI_ASYNC_H */
