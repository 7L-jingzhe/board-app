/**
 * @file ui_ime.h
 * @brief 可复用的中文输入法组件：虚拟键盘 + LVGL 开源拼音输入法。
 *
 * 这个组件把“键盘 + 拼音候选”封装成一个整体，任何页面只要有一个
 * lv_textarea（单行或多行都可以）就能获得中文输入能力：
 *
 *   lv_obj_t *ta = lv_textarea_create(page);
 *   ui_ime_t *ime = ui_ime_create(page, ta);   // 键盘自动放在 page 底部
 *   ui_ime_set_ready_cb(ime, on_send, page);   // 点“送出”/回车时回调
 *
 * 布局：ui_ime_create() 把键盘放在 parent 底部（100% 宽 × UI_IME_HEIGHT），
 * 页面需要把可交互区域留出这么大的底部空间，否则键盘会盖住内容。
 *
 * 输入法引擎是 LVGL 自带的开源拼音输入法（lvgl/src/others/ime，
 * MIT 许可，源自 lvgl/lv_ime_pinyin）。候选字库默认使用
 * <BOARD_APP_ROOT>/assets/ime/pinyin_dict.txt（由
 * board_app/tools/gen_pinyin_dict.py 生成），缺失时回退到引擎内置词典。
 *
 * 按键说明（26 键）：
 *   - 字母键：拼音候选；点上方候选字上屏
 *   - < >  ：候选翻页
 *   - ABC  ：大写英文；1#：符号；abc：小写英文
 *   - 底部工具栏：中 / 英 切换输入模式，数值键可改为快捷词
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef UI_IME_H
#define UI_IME_H

#include <stdbool.h>

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 键盘默认高度（含候选条与底部工具栏）。 */
#define UI_IME_HEIGHT 330

typedef struct ui_ime ui_ime_t;

/** 点击“送出”或键盘回车时触发，在 UI 线程执行。 */
typedef void (*ui_ime_ready_cb_t)(void *user_data);

/**
 * 创建输入法并绑定到目标 textarea。
 * @param parent 页面根容器；键盘会作为其子对象放在底部。
 * @param ta     键盘写入的 textarea（不能为 NULL）。
 * @return 句柄；失败返回 NULL。
 */
ui_ime_t *ui_ime_create(lv_obj_t *parent, lv_obj_t *ta);

/**
 * 重新摆放输入法：分屏布局时把键盘放到右半屏（整列），
 * 全屏输入时保持默认的底部全宽。
 * @param align 相对 parent 的对齐方式，例如 LV_ALIGN_TOP_RIGHT
 * @param width 宽度（<=0 表示保持 parent 全宽）
 * @param height 高度（<=0 表示 UI_IME_HEIGHT）
 */
void ui_ime_set_geometry(ui_ime_t *ime, lv_align_t align, int32_t x_ofs,
                         int32_t y_ofs, int32_t width, int32_t height);

/** 释放句柄。必须在 textarea 和 parent 销毁之前调用（页面 destroy 中调用）。 */
void ui_ime_destroy(ui_ime_t *ime);

/** 设置“送出”回调（回车键 / 工具栏送出按钮 / OK 键）。 */
void ui_ime_set_ready_cb(ui_ime_t *ime, ui_ime_ready_cb_t cb, void *user_data);

/**
 * 设置底部工具栏的 4 个快捷句式按钮文字。
 * @param keys 长度 >= 4 的字符串数组（UTF-8，NULL 项跳过）。
 */
void ui_ime_set_quick_keys(ui_ime_t *ime, const char *const keys[4]);

/**
 * 从 <BOARD_APP_ROOT>/data/ime/<app_id>.conf 读取快捷词（每行一个，最多 4 个，
 * # 开头为注释）。应用自己维护常用句，输入法组件保持通用。
 */
void ui_ime_load_quick_keys(ui_ime_t *ime, const char *app_id);

/** 在目标 textarea 当前光标处插入文本。 */
void ui_ime_insert(ui_ime_t *ime, const char *text);

/** 清空目标 textarea。 */
void ui_ime_clear_input(ui_ime_t *ime);

/** 显示/隐藏整个输入法。 */
void ui_ime_set_visible(ui_ime_t *ime, bool visible);

/** 中文模式（true，默认）或纯英文模式（false）。 */
void ui_ime_set_chinese(ui_ime_t *ime, bool chinese);
bool ui_ime_is_chinese(const ui_ime_t *ime);

/** 拿到内部键盘对象（需要额外挂事件时使用）。 */
lv_obj_t *ui_ime_get_keyboard(const ui_ime_t *ime);

/** 拿到绑定的 textarea。 */
lv_obj_t *ui_ime_get_textarea(const ui_ime_t *ime);

/** 候选词典是否加载成功（失败时为内置小词典）。 */
bool ui_ime_dictionary_loaded(const ui_ime_t *ime);

/**
 * 在后台线程预读并解析拼音词库（assets/ime/pinyin_dict.txt）。
 *
 * 建议在应用启动时调用一次（board_app_init 里已调用）：读文件 + 解析纯文本
 * 与 UI 线程并行，等真正打开聊天页/输入法时词库已就绪，省掉一次同步 I/O。
 * 线程内不做任何 lv_* 调用；若线程未就绪，ui_ime_create() 会同步兜底。
 */
void ui_ime_preload(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_IME_H */
