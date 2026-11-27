/**
 * @file chat_ui.c
 * @brief AI 聊天页：左半屏聊天记录 + 输入框，右半屏虚拟键盘（输入法）。
 *
 * 布局（1024×600）：
 *   ┌──────────────────────────────────────────┬───────────────┐
 *   │ 顶栏（返回 / 标题 / 清空）                 │  键盘         │
 *   ├──────────────────────────────────────────┤  ① 候选条     │
 *   │ 消息气泡（自动滚动，新消息时贴底）          │  ② 26 键      │
 *   │ …                                        │  ③ 中/英 + 送出│
 *   ├──────────────────────────────────────────┤               │
 *   │ 输入框 + 发送                              │               │
 *   └──────────────────────────────────────────┴───────────────┘
 *
 * 虚拟键盘与拼音输入法来自通用组件 board_ui/ui_ime.h（LVGL 开源
 * lv_ime_pinyin + 生成的自定义候选词库），本文件只关心聊天交互。
 *
 * 回复来源：配置了 BOARD_APP_AI_URL 时调用 OpenAI 兼容接口（worker 线程），
 * 否则/失败时使用离线助手（app/chat/core/chat_offline.c）。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "board_ui/ui_app_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_core/board_log.h"
#include "board_ui/ui_async.h"
#include "board_ui/ui_icons.h"
#include "board_ui/ui_ime.h"
#include "board_ui/ui_perf.h"
#include "board_ui/ui_theme.h"
#include "board_ui/ui_widgets.h"
#include "chat/chat_service.h"
#include "chat/chat_types.h"

/* 分屏布局：左边聊天面板，右边整列是虚拟键盘。
 * 键盘 12 列按键，列宽 512 → 每键约 40px，在 1024×600 上不拥挤。 */
#define CHAT_IME_W 512
#define CHAT_IME_RIGHT_MARGIN 8
#define CHAT_PANEL_X 16
#define CHAT_PANEL_Y 80
#define CHAT_PANEL_W (1024 - CHAT_IME_W - CHAT_IME_RIGHT_MARGIN - CHAT_PANEL_X * 2)
#define CHAT_PANEL_H 504
#define CHAT_LOG_H (CHAT_PANEL_H - 100)
#define CHAT_REPLY_MAX (CHAT_MSG_MAX * 2)

typedef struct {
    lv_obj_t *page;
    lv_obj_t *topbar;
    lv_obj_t *log;            /* 消息列表容器（可滚动） */
    lv_obj_t *ta;             /* 输入框 */
    lv_obj_t *send_btn;
    lv_obj_t *typing;         /* “正在输入…”气泡 */
    lv_obj_t *status_label;   /* 顶栏右侧：在线/离线 */
    ui_ime_t *ime;

    board_async_queue_t *queue;
    ui_async_pump_t *pump;
    chat_service_t *service;
    uint32_t seq;

    chat_message_t history[CHAT_HISTORY_MAX];
    size_t history_count;

    lv_timer_t *ellipsis_timer;
    int ellipsis_dots;
    lv_obj_t *typing_dots;

    bool busy;
} chat_app_t;

static chat_app_t *s_chat;

static void chat_send(chat_app_t *app);

/* -------------------------------------------------------------------------- */
/* 消息列表                                                                    */
/* -------------------------------------------------------------------------- */

static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                            const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text != NULL ? text : "");
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
}

static void log_scroll_to_bottom(chat_app_t *app)
{
    if (app->log == NULL) {
        return;
    }
    /* 布局在下一帧才更新高度，延迟一拍再滚动到底。 */
    lv_obj_update_layout(app->log);
    lv_obj_scroll_to_y(app->log, lv_obj_get_scroll_bottom(app->log), LV_ANIM_OFF);
}

/* 添加一条消息气泡。user=true 为用户（右对齐、主色）。 */
static lv_obj_t *add_bubble(chat_app_t *app, const char *text, bool user)
{
    lv_obj_t *row = lv_obj_create(app->log);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, user ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    if (!user) {
        /* 助手头像：小圆点，和首页强调色一致。 */
        lv_obj_t *avatar = lv_obj_create(row);
        lv_obj_remove_style_all(avatar);
        lv_obj_set_size(avatar, 34, 34);
        lv_obj_set_style_radius(avatar, 17, 0);
        lv_obj_set_style_bg_color(avatar, UI_COLOR_PRIMARY_SOFT, 0);
        lv_obj_set_style_bg_opa(avatar, LV_OPA_COVER, 0);
        lv_obj_remove_flag(avatar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *dot = lv_label_create(avatar);
        lv_label_set_text(dot, "AI");
        lv_obj_set_style_text_font(dot, ui_font_caption(), 0);
        lv_obj_set_style_text_color(dot, UI_COLOR_PRIMARY, 0);
        lv_obj_center(dot);
    }

    lv_obj_t *bubble = lv_obj_create(row);
    lv_obj_remove_style_all(bubble);
    lv_obj_set_width(bubble, LV_PCT(84));
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(bubble, 16, 0);
    lv_obj_set_style_pad_hor(bubble, 16, 0);
    lv_obj_set_style_pad_ver(bubble, 12, 0);
    lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bubble, user ? UI_COLOR_PRIMARY_SOFT : UI_COLOR_SURFACE_ALT, 0);
    lv_obj_set_style_border_width(bubble, 1, 0);
    lv_obj_set_style_border_color(bubble,
                                  user ? lv_color_mix(UI_COLOR_PRIMARY, UI_COLOR_SURFACE, 40)
                                       : UI_COLOR_LINE,
                                  0);
    lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(bubble, 6, 0);
    lv_obj_remove_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = make_label(bubble, text, ui_font_small(),
                                 user ? UI_COLOR_TEXT : UI_COLOR_TEXT);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_user_data(bubble, label);
    lv_obj_set_style_pad_bottom(bubble, 0, 0);
    return bubble;
}

static void clear_log(chat_app_t *app)
{
    if (app->log != NULL) {
        lv_obj_clean(app->log);
    }
    app->typing = NULL;
    app->typing_dots = NULL;
}

static void show_typing(chat_app_t *app, bool show)
{
    if (show) {
        if (app->typing != NULL) {
            return;
        }
        app->typing = add_bubble(app, "正在思考", false);
        if (app->typing != NULL) {
            lv_obj_t *label = lv_obj_get_user_data(app->typing);
            app->typing_dots = label;
            if (app->ellipsis_timer != NULL) {
                lv_timer_resume(app->ellipsis_timer);
            }
        }
    } else {
        if (app->typing != NULL) {
            lv_obj_delete(app->typing);
            app->typing = NULL;
            app->typing_dots = NULL;
        }
        if (app->ellipsis_timer != NULL) {
            lv_timer_pause(app->ellipsis_timer);
        }
    }
    log_scroll_to_bottom(app);
}

static void ellipsis_timer_cb(lv_timer_t *timer)
{
    chat_app_t *app = lv_timer_get_user_data(timer);
    static const char *const DOTS[] = { "正在思考", "正在思考.", "正在思考..", "正在思考..." };
    if (app == NULL || app->typing_dots == NULL) {
        return;
    }
    app->ellipsis_dots = (app->ellipsis_dots + 1) % 4;
    lv_label_set_text(app->typing_dots, DOTS[app->ellipsis_dots]);
}

/* -------------------------------------------------------------------------- */
/* 历史记录                                                                    */
/* -------------------------------------------------------------------------- */

static void history_append(chat_app_t *app, chat_role_t role, const char *text)
{
    chat_message_t *msg;
    if (text == NULL || text[0] == '\0') {
        return;
    }
    if (app->history_count >= CHAT_HISTORY_MAX) {
        /* 丢弃最早的一条，整体前移。 */
        free(app->history[0].text);
        memmove(&app->history[0], &app->history[1],
                (CHAT_HISTORY_MAX - 1) * sizeof(app->history[0]));
        app->history_count = CHAT_HISTORY_MAX - 1;
    }
    msg = &app->history[app->history_count];
    msg->role = role;
    msg->text = strdup(text);
    if (msg->text == NULL) {
        return;
    }
    app->history_count++;
}

static void history_clear(chat_app_t *app)
{
    for (size_t i = 0; i < app->history_count; i++) {
        free(app->history[i].text);
        app->history[i].text = NULL;
    }
    app->history_count = 0;
}

/* -------------------------------------------------------------------------- */
/* 请求 / 响应                                                                 */
/* -------------------------------------------------------------------------- */

static void update_status(chat_app_t *app)

{
    chat_ai_config_t cfg;
    if (app->status_label == NULL) {
        return;
    }
    chat_service_load_config(&cfg);
    if (cfg.enabled) {
        lv_label_set_text(app->status_label, "在线大模型");
        lv_obj_set_style_text_color(app->status_label, UI_COLOR_SUCCESS, 0);
    } else {
        lv_label_set_text(app->status_label, "离线助手");
        lv_obj_set_style_text_color(app->status_label, UI_COLOR_TEXT_MUTED, 0);
    }
}

static void set_busy(chat_app_t *app, bool busy)
{
    app->busy = busy;
    if (app->send_btn != NULL) {
        if (busy) {
            lv_obj_add_state(app->send_btn, LV_STATE_DISABLED);
            lv_obj_set_style_bg_opa(app->send_btn, 120, 0);
        } else {
            lv_obj_remove_state(app->send_btn, LV_STATE_DISABLED);
            lv_obj_set_style_bg_opa(app->send_btn, LV_OPA_COVER, 0);
        }
    }
}

static void deliver_reply(chat_app_t *app, const char *text, bool from_llm,
                          const char *note)
{
    lv_obj_t *bubble;
    if (app == NULL || text == NULL) {
        return;
    }
    bubble = add_bubble(app, text, false);
    if (bubble != NULL && note != NULL && note[0] != '\0') {
        /* 兜底说明用小字附在气泡下方，避免把技术细节混进回复正文。 */
        lv_obj_t *tip = make_label(bubble, note, ui_font_caption(), UI_COLOR_WARNING);
        lv_obj_set_width(tip, LV_PCT(100));
    }
    history_append(app, CHAT_ROLE_ASSISTANT, text);
    if (!from_llm) {
        BOARD_LOGI("chat", "reply from offline assistant");
    }
    log_scroll_to_bottom(app);
}

static void reply_cb(void *item, void *user_data)
{
    chat_app_t *app = user_data;
    chat_reply_t *res = item;
    if (res == NULL) {
        return;
    }
    if (app == NULL) {
        free(res->reply);
        free(res);
        return;
    }
    if (res->seq == app->seq) {
        show_typing(app, false);
        set_busy(app, false);
        deliver_reply(app, res->reply, res->source == CHAT_AI_SOURCE_LLM, res->note);
    }
    free(res->reply);
    free(res);
}

static void chat_send(chat_app_t *app)
{
    const char *text;
    char *copy;
    chat_ai_config_t cfg;
    int ret;

    if (app == NULL || app->busy) {
        return;
    }
    text = lv_textarea_get_text(app->ta);
    if (text == NULL) {
        return;
    }
    while (*text == ' ' || *text == '\n' || *text == '\r') {
        text++;
    }
    if (*text == '\0') {
        ui_toast_show("先输入内容");
        return;
    }
    copy = strdup(text);
    if (copy == NULL) {
        return;
    }
    lv_textarea_set_text(app->ta, "");

    add_bubble(app, copy, true);
    log_scroll_to_bottom(app);
    /* 当前输入先不入历史：服务端把最后一条作为本次 user 消息。 */
    history_append(app, CHAT_ROLE_USER, copy);

    chat_service_load_config(&cfg);
    app->seq++;
    ret = chat_service_ask(app->service, &cfg, app->history,
                           app->history_count - 1, copy, app->seq);
    free(copy);
    if (ret != 0) {
        /* 上一次请求还没回来；把刚插入的用户消息回退，避免历史错位。 */
        if (app->history_count > 0) {
            free(app->history[--app->history_count].text);
        }
        lv_obj_t *last = NULL;
        if (app->log != NULL && lv_obj_get_child_count(app->log) > 0) {
            last = lv_obj_get_child(app->log, lv_obj_get_child_count(app->log) - 1);
        }
        if (last != NULL) {
            lv_obj_delete(last);
        }
        ui_toast_show("上一条还在回复中");
        return;
    }
    set_busy(app, true);
    show_typing(app, true);
}

/* -------------------------------------------------------------------------- */
/* 事件                                                                        */
/* -------------------------------------------------------------------------- */

/**
 * 自检钩子：BOARD_APP_CHAT_AUTOSEND="你好" 时，页面创建 2.5 秒后自动发一条。
 * 用于没有触摸的环境（SSH）验证 AI 链路；日志会打印 http_post 结果。
 */
static void autosend_timer_cb(lv_timer_t *timer)
{
    static bool autosent;
    chat_app_t *app = lv_timer_get_user_data(timer);
    const char *text = getenv("BOARD_APP_CHAT_AUTOSEND");

    lv_timer_delete(timer);
    if (autosent || app == NULL || text == NULL || text[0] == 0 ||
        app->ta == NULL) {
        return;
    }
    autosent = true;
    lv_textarea_set_text(app->ta, text);
    BOARD_LOGI("chat", "autosend: %s", text);
    chat_send(app);
}

static void send_clicked_cb(lv_event_t *e)
{
    chat_send(lv_event_get_user_data(e));
}

static void ta_ready_cb(lv_event_t *e)
{
    chat_send(lv_event_get_user_data(e));
}

static void ime_ready_cb(void *user_data)
{
    chat_send(user_data);
}

static void clear_clicked_cb(lv_event_t *e)
{
    chat_app_t *app = lv_event_get_user_data(e);
    if (app == NULL) {
        return;
    }
    /* 让旧的回复结果失效，避免清空后又冒出一条消息。 */
    app->seq++;
    history_clear(app);
    clear_log(app);
    add_bubble(app, "对话已清空，可以重新提问。", false);
    ui_ime_clear_input(app->ime);
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    (void)ui_app_manager_back();
}

/* -------------------------------------------------------------------------- */
/* 页面构建                                                                    */
/* -------------------------------------------------------------------------- */

static lv_obj_t *chat_create(lv_obj_t *parent)
{
    chat_app_t *app = calloc(1, sizeof(*app));
    lv_obj_t *panel;
    lv_obj_t *input_card;
    chat_ai_config_t cfg;

    if (app == NULL) {
        return NULL;
    }
    s_chat = app;
    app->page = parent;
    lv_obj_set_style_bg_color(parent, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    ui_perf_mark("chat_start");

    app->topbar = ui_topbar_create(parent, "AI 聊天", back_cb, app);
    lv_obj_t *clear_btn = ui_topbar_add_action(app->topbar, "清空", clear_clicked_cb, app);
    lv_obj_set_style_bg_color(clear_btn, UI_COLOR_SURFACE_ALT, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(clear_btn, 0), UI_COLOR_TEXT_SECONDARY, 0);
    ui_perf_mark("topbar");

    /* 标题旁的在线状态。 */
    app->status_label = lv_label_create(app->topbar);
    lv_obj_set_style_text_font(app->status_label, ui_font_caption(), 0);
    lv_label_set_text(app->status_label, "离线助手");
    lv_obj_set_style_text_color(app->status_label, UI_COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_pad_left(app->status_label, 4, 0);

    /* ---------- 左侧聊天面板 ---------- */
    panel = ui_card_create(parent, CHAT_PANEL_W, CHAT_PANEL_H);
    lv_obj_set_pos(panel, CHAT_PANEL_X, CHAT_PANEL_Y);
    /* 卡片默认有内边距与阴影，会吃掉 1px 边框内侧的空间；这里清零后
     * 直接在里面按坐标放“消息区”和“输入区”两块。 */
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_style_shadow_width(panel, 0, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    app->log = lv_obj_create(panel);
    lv_obj_remove_style_all(app->log);
    lv_obj_set_size(app->log, CHAT_PANEL_W - 2, CHAT_LOG_H);
    lv_obj_set_pos(app->log, 1, 1);
    lv_obj_set_style_pad_hor(app->log, 16, 0);
    lv_obj_set_style_pad_ver(app->log, 14, 0);
    lv_obj_set_style_pad_row(app->log, 12, 0);
    lv_obj_set_style_bg_opa(app->log, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(app->log, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(app->log, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(app->log, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(app->log, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_width(app->log, 4, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(app->log, UI_COLOR_LINE, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(app->log, 2, LV_PART_SCROLLBAR);

    /* 输入区：输入框 + 发送按钮。 */
    input_card = lv_obj_create(panel);
    lv_obj_remove_style_all(input_card);
    lv_obj_set_size(input_card, CHAT_PANEL_W - 2, CHAT_PANEL_H - CHAT_LOG_H - 2);
    lv_obj_set_pos(input_card, 1, CHAT_LOG_H + 1);
    lv_obj_set_style_bg_color(input_card, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(input_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(input_card, 18, 0);
    lv_obj_set_style_border_side(input_card, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(input_card, 1, 0);
    lv_obj_set_style_border_color(input_card, UI_COLOR_LINE, 0);
    lv_obj_set_style_pad_all(input_card, 14, 0);
    lv_obj_set_flex_flow(input_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(input_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(input_card, 12, 0);
    lv_obj_remove_flag(input_card, LV_OBJ_FLAG_SCROLLABLE);

    app->ta = lv_textarea_create(input_card);
    lv_obj_set_size(app->ta, CHAT_PANEL_W - 160, 92);    lv_textarea_set_one_line(app->ta, true);
    lv_textarea_set_placeholder_text(app->ta, "说点什么…（右侧键盘可输入中文）");
    lv_textarea_set_max_length(app->ta, 200);
    lv_obj_set_style_text_font(app->ta, ui_font_small(), 0);
    lv_obj_set_style_text_color(app->ta, UI_COLOR_TEXT, 0);
    lv_obj_set_style_bg_color(app->ta, UI_COLOR_SURFACE_ALT, 0);
    lv_obj_set_style_bg_opa(app->ta, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(app->ta, 12, 0);
    lv_obj_set_style_pad_all(app->ta, 12, 0);
    lv_obj_set_style_border_width(app->ta, 1, 0);
    lv_obj_set_style_border_color(app->ta, UI_COLOR_LINE, 0);
    lv_obj_set_style_border_color(app->ta, UI_COLOR_PRIMARY, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(app->ta, UI_COLOR_SURFACE_ALT, LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(app->ta, UI_COLOR_TEXT_MUTED,
                                LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_text_font(app->ta, ui_font_small(), LV_PART_TEXTAREA_PLACEHOLDER);

    app->send_btn = ui_button_primary_create(input_card, "发送", send_clicked_cb, app);
    lv_obj_set_size(app->send_btn, 96, 58);
    lv_obj_add_event_cb(app->ta, ta_ready_cb, LV_EVENT_READY, app);
    ui_perf_mark("panel");

    /* 左侧面板下方的一行提示：说明输入法用法。 */
    lv_obj_t *hint = lv_obj_create(parent);
    lv_obj_remove_style_all(hint);
    lv_obj_set_size(hint, CHAT_PANEL_W, 32);
    lv_obj_set_pos(hint, CHAT_PANEL_X, CHAT_PANEL_Y + CHAT_PANEL_H + 8);
    lv_obj_set_style_bg_opa(hint, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(hint, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *hint_label = lv_label_create(hint);
    lv_label_set_text(hint_label, "敲拼音出候选字 · 点候选上屏 · 中/英可切换");
    lv_obj_set_style_text_font(hint_label, ui_font_caption(), 0);
    lv_obj_set_style_text_color(hint_label, UI_COLOR_TEXT_MUTED, 0);
    lv_obj_align(hint_label, LV_ALIGN_LEFT_MID, 4, 0);

    /* ---------- 右侧虚拟键盘（通用输入法组件，占满右列） ---------- */
    app->ime = ui_ime_create(parent, app->ta);
    if (app->ime != NULL) {
        ui_ime_set_geometry(app->ime, LV_ALIGN_TOP_RIGHT, -CHAT_IME_RIGHT_MARGIN,
                            CHAT_PANEL_Y, CHAT_IME_W, CHAT_PANEL_H);
    }
    if (app->ime == NULL) {
        ui_toast_show("键盘初始化失败");
    } else {
        ui_ime_set_ready_cb(app->ime, ime_ready_cb, app);
        ui_ime_load_quick_keys(app->ime, "chat");
    }
    ui_perf_mark("keyboard");

    /* ---------- 服务与欢迎语 ---------- */
    app->queue = board_async_queue_create(8);
    app->pump = app->queue != NULL
                    ? ui_async_pump_create(app->queue, reply_cb, app, 40)
                    : NULL;
    app->service = app->queue != NULL ? chat_service_create(app->queue) : NULL;
    if (app->service == NULL) {
        ui_toast_show("聊天服务初始化失败");
    }
    app->ellipsis_timer = lv_timer_create(ellipsis_timer_cb, 400, app);
    if (app->ellipsis_timer != NULL) {
        lv_timer_pause(app->ellipsis_timer);
    }
    ui_perf_mark("service");

    chat_service_load_config(&cfg);
    update_status(app);
    add_bubble(app, "你好！我是这块开发板的 AI 助手。右侧是虚拟键盘："
                    "直接敲拼音就会出现候选字，也可以点击下方常用语。", false);
    if (cfg.enabled) {
        add_bubble(app, "已连接大模型，直接问我就好。", false);
    } else {
        add_bubble(app, "现在没有配置大模型（离线助手模式）：可以问时间、"
                        "算数、问使用说明，试试“3*(4+5)”。", false);
    }
    lv_textarea_set_cursor_pos(app->ta, LV_TEXTAREA_CURSOR_LAST);
    ui_perf_mark("welcome");
    /* 自检用：BOARD_APP_CHAT_AUTOSEND 指定的文本 2.5s 后自动发送 */
    {
        lv_timer_t *t = lv_timer_create(autosend_timer_cb, 2500, app);
        if (t != NULL) {
            lv_timer_set_repeat_count(t, 1);
        }
    }
    return parent;
}

static void chat_destroy(void)
{
    chat_app_t *app = s_chat;
    if (app == NULL) {
        return;
    }
    /* 先停 worker，再拆 UI 相关资源。 */
    if (app->service != NULL) {
        chat_service_cancel(app->service);
        chat_service_destroy(app->service);
        app->service = NULL;
    }
    if (app->queue != NULL) {
        void *item;
        while ((item = board_async_queue_pop(app->queue)) != NULL) {
            chat_reply_t *res = item;
            free(res->reply);
            free(res);
        }
    }
    if (app->pump != NULL) {
        ui_async_pump_destroy(app->pump);
        app->pump = NULL;
    }
    if (app->queue != NULL) {
        board_async_queue_destroy(app->queue);
        app->queue = NULL;
    }
    if (app->ellipsis_timer != NULL) {
        lv_timer_delete(app->ellipsis_timer);
        app->ellipsis_timer = NULL;
    }
    ui_ime_destroy(app->ime);
    app->ime = NULL;
    history_clear(app);
    free(app);
    s_chat = NULL;
}

static const board_app_desc_t s_chat_app = {
    .id = "chat",
    .title = "AI 聊天",
    .subtitle = "键盘输入 · AI 对话",
    .parent_id = NULL,
    .best_score_key = NULL,
    .icon = UI_ICON_CHAT,
    .accent = 0x2EC4AE,
    .order = 5,
    .show_on_home = true,
    .create = chat_create,
    .destroy = chat_destroy,
};

BOARD_APP_REGISTER(s_chat_app);
