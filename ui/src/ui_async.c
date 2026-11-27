/**
 * @file ui_async.c
 * @brief 通过 LVGL 定时器轮询工作线程队列（始终在 UI 线程执行）。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "board_ui/ui_async.h"

#include <stdlib.h>

struct ui_async_pump {
    board_async_queue_t *queue;
    ui_async_handler_t handler;
    void *user_data;
    lv_timer_t *timer;
};

static void pump_timer_cb(lv_timer_t *timer)
{
    ui_async_pump_t *pump = lv_timer_get_user_data(timer);
    void *item;
    if (pump == NULL || pump->handler == NULL) {
        return;
    }
    while ((item = board_async_queue_pop(pump->queue)) != NULL) {
        pump->handler(item, pump->user_data);
    }
}

ui_async_pump_t *ui_async_pump_create(board_async_queue_t *queue,
                                      ui_async_handler_t handler,
                                      void *user_data,
                                      uint32_t period_ms)
{
    ui_async_pump_t *pump;
    if (queue == NULL || handler == NULL) {
        return NULL;
    }
    pump = calloc(1, sizeof(*pump));
    if (pump == NULL) {
        return NULL;
    }
    pump->queue = queue;
    pump->handler = handler;
    pump->user_data = user_data;
    pump->timer = lv_timer_create(pump_timer_cb, period_ms > 0 ? period_ms : 30, pump);
    if (pump->timer == NULL) {
        free(pump);
        return NULL;
    }
    return pump;
}

void ui_async_pump_destroy(ui_async_pump_t *pump)
{
    if (pump == NULL) {
        return;
    }
    if (pump->timer != NULL) {
        lv_timer_delete(pump->timer);
    }
    free(pump);
}
