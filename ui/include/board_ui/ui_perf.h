/**
 * @file ui_perf.h
 * @brief 轻量性能探针：帧率、单帧耗时、LVGL 堆使用。
 *
 * 默认关闭，只有设置 `BOARD_APP_PERF=1` 时才统计并打印，正常运行零开销。
 * 用于在板上定位“卡顿是渲染太重、内存不足还是字形缓存未命中”。
 *
 * 用法（main.c）：
 *   ui_perf_init();
 *   while (1) { uint32_t t = ui_perf_begin(); lv_timer_handler(); ui_perf_end(t); }
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef UI_PERF_H
#define UI_PERF_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 读取 BOARD_APP_PERF；开启时创建 2 秒统计定时器。 */
void ui_perf_init(void);

bool ui_perf_enabled(void);

/** 一帧开始，返回单调时钟微秒数（未启用时返回 0）。 */
uint32_t ui_perf_begin(void);

/** 一帧结束，记录耗时。 */
void ui_perf_end(uint32_t start_us);

/**
 * 阶段计时打点：打印“距上一个 mark 的毫秒数”。
 * 用于定位页面创建时哪一段阻塞最久。未启用时为空操作。
 */
void ui_perf_mark(const char *name);

/**
 * 字形度量探针：对常用汉字分别测“首轮（建缓存）”和“次轮（命中缓存）”的
 * 单字耗时，用来判断卡顿是否来自字体层。未启用时为空操作。
 */
void ui_perf_font_probe(void);

/**
 * 对象创建探针：在同一父对象上创建/删除若干空对象与带文字的 label，
 * 用来区分“LVGL 对象本身慢”还是“我们的样式/布局写法慢”。
 */
void ui_perf_object_probe(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif

#endif /* UI_PERF_H */
