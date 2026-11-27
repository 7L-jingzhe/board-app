/**
 * @file ui_widgets.c
 * @brief 可复用控件：顶栏、卡片、按钮、弹窗、轻提示与状态视图。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "board_ui/ui_widgets.h"

#include <stdio.h>
#include <string.h>

#include "board_ui/ui_theme.h"

static lv_obj_t *s_toast;
static lv_timer_t *s_toast_timer;

static void style_clear(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
}

static lv_obj_t *label_create(lv_obj_t *parent, const char *text,
                              const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text != NULL ? text : "");
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

lv_obj_t *ui_button_primary_create(lv_obj_t *parent, const char *text,
                                   lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_height(btn, 56);
    lv_obj_set_style_pad_hor(btn, 24, 0);
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_set_style_bg_color(btn, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, UI_COLOR_PRIMARY_DARK, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale(btn, 236, LV_STATE_PRESSED);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *label = label_create(btn, text, ui_font_body(), UI_COLOR_WHITE);
    lv_obj_center(label);
    if (cb != NULL) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    }
    return btn;
}

lv_obj_t *ui_button_secondary_create(lv_obj_t *parent, const char *text,
                                     lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_height(btn, 56);
    lv_obj_set_style_pad_hor(btn, 24, 0);
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_set_style_bg_color(btn, UI_COLOR_SURFACE_ALT, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, UI_COLOR_PRIMARY_SOFT, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale(btn, 236, LV_STATE_PRESSED);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *label = label_create(btn, text, ui_font_body(), UI_COLOR_TEXT);
    lv_obj_center(label);
    if (cb != NULL) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    }
    return btn;
}

lv_obj_t *ui_icon_button_create(lv_obj_t *parent, const char *symbol,
                                lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 56, 56);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_bg_color(btn, UI_COLOR_SURFACE_ALT, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, UI_COLOR_PRIMARY_SOFT, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, UI_COLOR_PRIMARY, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale(btn, 236, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, UI_COLOR_LINE, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *label = label_create(btn, symbol, ui_font_h2(), UI_COLOR_TEXT);
    lv_obj_center(label);
    if (cb != NULL) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    }
    return btn;
}

lv_obj_t *ui_topbar_create(lv_obj_t *parent, const char *title,
                           lv_event_cb_t back_cb, void *user_data)
{
    lv_obj_t *bar = lv_obj_create(parent);
    style_clear(bar);
    lv_obj_set_size(bar, lv_pct(100), 72);
    lv_obj_set_style_pad_left(bar, 24, 0);
    lv_obj_set_style_pad_right(bar, 24, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 16, 0);

    if (back_cb != NULL) {
        /* 触摸目标是 56x56（原来 48x48 在板上偏小、易误触）。 */
        lv_obj_t *back = ui_icon_button_create(bar, "‹", back_cb, user_data);
        lv_obj_set_size(back, 56, 56);
        lv_obj_set_style_radius(back, 18, 0);
        lv_obj_set_style_bg_color(back, UI_COLOR_SURFACE_ALT, 0);
        lv_obj_set_style_border_width(back, 1, 0);
        lv_obj_set_style_border_color(back, UI_COLOR_LINE, 0);
        lv_obj_t *arrow = lv_obj_get_child(back, 0);
        lv_obj_set_style_text_font(arrow, ui_font_display(), 0);
        lv_obj_set_style_text_color(arrow, UI_COLOR_TEXT, 0);
        lv_obj_set_style_translate_y(arrow, -2, 0);
    }
    lv_obj_t *title_label = label_create(bar, title, ui_font_h2(), UI_COLOR_TEXT);
    lv_obj_set_style_text_letter_space(title_label, 0, 0);

    lv_obj_t *spacer = lv_obj_create(bar);
    style_clear(spacer);
    lv_obj_set_height(spacer, 1);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);

    lv_obj_t *actions = lv_obj_create(bar);
    style_clear(actions);
    /* 固定大小的操作区，可防止页面有两个操作按钮（例如城市 + 刷新、
     * 暂停 + 重开）时右对齐的按钮被挤出布局。 */
    lv_obj_set_size(actions, 420, 56);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(actions, 12, 0);
    lv_obj_remove_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(bar, actions);
    return bar;
}

lv_obj_t *ui_topbar_add_action(lv_obj_t *topbar, const char *text,
                               lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *actions;
    lv_obj_t *btn;
    if (topbar == NULL) {
        return NULL;
    }
    actions = lv_obj_get_user_data(topbar);
    if (actions == NULL) {
        return NULL;
    }
    btn = ui_button_primary_create(actions, text, cb, user_data);
    lv_obj_set_height(btn, 52);
    lv_obj_set_style_pad_hor(btn, 18, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(btn, 0), ui_font_small(), 0);
    return btn;
}

lv_obj_t *ui_card_create(lv_obj_t *parent, int w, int h)
{
    lv_obj_t *card = lv_obj_create(parent);
    style_clear(card);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 24, 0);
    lv_obj_set_style_border_color(card, UI_COLOR_LINE, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_shadow_width(card, 24, 0);
    lv_obj_set_style_shadow_opa(card, 60, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_offset_y(card, 8, 0);
    return card;
}

lv_obj_t *ui_pill_create(lv_obj_t *parent, const char *text, lv_color_t fg,
                         lv_color_t bg)
{
    lv_obj_t *pill = lv_obj_create(parent);
    style_clear(pill);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, 36);
    lv_obj_set_style_pad_hor(pill, 14, 0);
    lv_obj_set_style_radius(pill, 18, 0);
    lv_obj_set_style_bg_color(pill, bg, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_t *label = label_create(pill, text, ui_font_caption(), fg);
    lv_obj_center(label);
    return pill;
}

static void overlay_clicked_cb(lv_event_t *e)
{
    lv_obj_t *overlay = lv_event_get_current_target(e);
    if (lv_event_get_target(e) == overlay) {
        lv_obj_delete(overlay);
    }
}

static void modal_close_cb(lv_event_t *e)
{
    lv_obj_t *content = lv_event_get_user_data(e);
    ui_modal_close(content);
}

lv_obj_t *ui_modal_create(lv_obj_t *parent, const char *title, int width)
{
    lv_obj_t *overlay = lv_obj_create(parent);
    style_clear(overlay);
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(overlay, UI_COLOR_BOARD_DARK, 0);
    lv_obj_set_style_bg_opa(overlay, 120, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(overlay, overlay_clicked_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *card = lv_obj_create(overlay);
    style_clear(card);
    lv_obj_set_width(card, width > 0 ? width : 560);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(card, 24, 0);
    lv_obj_set_style_radius(card, 24, 0);
    lv_obj_set_style_bg_color(card, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(card, 32, 0);
    lv_obj_set_style_shadow_opa(card, 40, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 16, 0);
    lv_obj_center(card);

    lv_obj_t *title_label = label_create(card, title, ui_font_h2(), UI_COLOR_TEXT);
    lv_obj_set_width(title_label, lv_pct(100));

    lv_obj_t *content = lv_obj_create(card);
    style_clear(content);
    lv_obj_set_width(content, lv_pct(100));
    lv_obj_set_height(content, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content, 16, 0);

    lv_obj_t *close = lv_button_create(card);
    lv_obj_remove_style_all(close);
    lv_obj_set_size(close, 40, 40);
    lv_obj_set_style_radius(close, 12, 0);
    lv_obj_set_style_bg_color(close, UI_COLOR_SURFACE_ALT, 0);
    lv_obj_set_style_bg_opa(close, LV_OPA_COVER, 0);
    lv_obj_add_flag(close, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_t *x = label_create(close, "×", ui_font_h3(), UI_COLOR_TEXT_SECONDARY);
    lv_obj_center(x);
    lv_obj_add_event_cb(close, modal_close_cb, LV_EVENT_CLICKED, content);
    return content;
}

void ui_modal_close(lv_obj_t *content)
{
    lv_obj_t *card;
    lv_obj_t *overlay;
    if (content == NULL) {
        return;
    }
    card = lv_obj_get_parent(content);
    if (card == NULL) {
        return;
    }
    overlay = lv_obj_get_parent(card);
    if (overlay != NULL) {
        lv_obj_delete(overlay);
    }
}

static void toast_timer_cb(lv_timer_t *timer)
{
    if (s_toast != NULL) {
        lv_obj_delete(s_toast);
        s_toast = NULL;
    }
    lv_timer_delete(timer);
    s_toast_timer = NULL;
}

void ui_toast_show(const char *text)
{
    lv_obj_t *layer = lv_layer_top();
    if (layer == NULL) {
        layer = lv_screen_active();
    }
    if (s_toast != NULL) {
        lv_obj_delete(s_toast);
        s_toast = NULL;
    }
    if (s_toast_timer != NULL) {
        lv_timer_delete(s_toast_timer);
        s_toast_timer = NULL;
    }
    lv_obj_t *toast = lv_obj_create(layer);
    style_clear(toast);
    lv_obj_set_size(toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(toast, 20, 0);
    lv_obj_set_style_pad_ver(toast, 12, 0);
    lv_obj_set_style_radius(toast, 14, 0);
    lv_obj_set_style_bg_color(toast, UI_COLOR_TEXT, 0);
    lv_obj_set_style_bg_opa(toast, 235, 0);
    lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -40);
    label_create(toast, text, ui_font_small(), UI_COLOR_WHITE);
    s_toast = toast;
    s_toast_timer = lv_timer_create(toast_timer_cb, 2000, NULL);
    lv_timer_set_repeat_count(s_toast_timer, 1);
}

lv_obj_t *ui_loading_create(lv_obj_t *parent, const char *text)
{
    lv_obj_t *box = lv_obj_create(parent);
    style_clear(box);
    lv_obj_set_size(box, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box, 12, 0);
    lv_obj_t *spinner = lv_spinner_create(box);
    lv_obj_set_size(spinner, 48, 48);
    lv_obj_set_style_arc_color(spinner, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_arc_width(spinner, 4, 0);
    label_create(box, text != NULL ? text : "加载中…", ui_font_small(),
                 UI_COLOR_TEXT_SECONDARY);
    return box;
}

void ui_empty_state_show(lv_obj_t *parent, const char *title, const char *detail)
{
    lv_obj_t *box = lv_obj_create(parent);
    style_clear(box);
    lv_obj_set_size(box, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box, 12, 0);

    lv_obj_t *icon = lv_obj_create(box);
    style_clear(icon);
    lv_obj_set_size(icon, 96, 96);
    lv_obj_set_style_radius(icon, 28, 0);
    lv_obj_set_style_bg_color(icon, UI_COLOR_PRIMARY_SOFT, 0);
    lv_obj_set_style_bg_opa(icon, LV_OPA_COVER, 0);
    lv_obj_t *glyph = label_create(icon, "…", ui_font_h1(), UI_COLOR_PRIMARY);
    lv_obj_center(glyph);

    label_create(box, title, ui_font_h2(), UI_COLOR_TEXT);
    lv_obj_t *d = label_create(box, detail != NULL ? detail : "", ui_font_small(),
                               UI_COLOR_TEXT_SECONDARY);
    lv_obj_set_style_text_align(d, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(d, 600);
}
