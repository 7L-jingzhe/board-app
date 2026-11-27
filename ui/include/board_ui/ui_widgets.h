/**
 * @file ui_widgets.h
 * @brief Board App 设计语言的可复用 LVGL 控件。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef UI_WIDGETS_H
#define UI_WIDGETS_H

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *ui_topbar_create(lv_obj_t *parent, const char *title,
                           lv_event_cb_t back_cb, void *user_data);
lv_obj_t *ui_topbar_add_action(lv_obj_t *topbar, const char *text,
                               lv_event_cb_t cb, void *user_data);

lv_obj_t *ui_card_create(lv_obj_t *parent, int w, int h);
lv_obj_t *ui_button_primary_create(lv_obj_t *parent, const char *text,
                                   lv_event_cb_t cb, void *user_data);
lv_obj_t *ui_button_secondary_create(lv_obj_t *parent, const char *text,
                                     lv_event_cb_t cb, void *user_data);
lv_obj_t *ui_icon_button_create(lv_obj_t *parent, const char *symbol,
                                lv_event_cb_t cb, void *user_data);
lv_obj_t *ui_pill_create(lv_obj_t *parent, const char *text, lv_color_t fg,
                         lv_color_t bg);

/** 返回内容容器；overlay/card 是其祖先对象。 */
lv_obj_t *ui_modal_create(lv_obj_t *parent, const char *title, int width);
void ui_modal_close(lv_obj_t *content);

void ui_toast_show(const char *text);
lv_obj_t *ui_loading_create(lv_obj_t *parent, const char *text);
void ui_empty_state_show(lv_obj_t *parent, const char *title, const char *detail);

#ifdef __cplusplus
}
#endif

#endif /* UI_WIDGETS_H */
