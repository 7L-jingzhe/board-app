/**
 * @file ui_ime.c
 * @brief 可复用输入法组件：LVGL 键盘 + 开源拼音输入法 + 候选词库加载。
 *
 * 输入法引擎来自 LVGL 自身的 lv_ime_pinyin（MIT），本文件只负责：
 *   1. 创建键盘并绑定 textarea；
 *   2. 从 assets/ime/pinyin_dict.txt 加载候选词库并交给引擎；
 *   3. 对齐到项目主题（深色 + 青绿）的样式；
 *   4. 翻页键、中/英切换、底部工具栏与“送出”回调。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "board_ui/ui_ime.h"

#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "board_app/board_app.h"
#include "board_core/board_log.h"
#include "board_ui/ui_theme.h"

#define IME_DICT_MAX_BYTES (512 * 1024)
#define IME_DICT_MAX_ENTRIES 2048
#define IME_LINE_MAX 2048
#define UI_IME_TOOLBAR_H 48
#define UI_IME_QUICK_H 44
#define UI_IME_CANDIDATE_H 44

struct ui_ime {
    lv_obj_t *root;          /* 输入法根容器（parent 的子对象） */
    lv_obj_t *board;         /* 键盘 + 候选条区域（不含底部工具栏） */
    lv_obj_t *hint;          /* 窄布局时顶部的模式提示 */
    lv_obj_t *quick_row;     /* 快捷词行（无内容时整行隐藏） */
    lv_obj_t *kb;            /* lv_keyboard */
    lv_obj_t *ime;           /* lv_ime_pinyin */
    lv_obj_t *cand_panel;    /* 候选条（由 lv_ime_pinyin 创建） */
    lv_obj_t *mode_btn;      /* 底部工具栏的中/英切换按钮 */
    lv_obj_t *quick_btns[4];
    lv_obj_t *ta;
    ui_ime_ready_cb_t ready_cb;
    void *ready_user_data;
    bool chinese;
    /* 当前几何：优先显示尺寸变化时（快捷词行列显隐）重新套用 */
    lv_align_t align;
    int32_t x_ofs;
    int32_t y_ofs;
    int32_t width;
    int32_t height;
};

/* -------------------------------------------------------------------------- */
/* 词库加载                                                                    */
/* -------------------------------------------------------------------------- */

/* pinyin 部分只允许 a-z，长度 1..8；汉字部分必须非空。 */
static bool dict_line_ok(const char *py)
{
    size_t i;
    if (py == NULL || py[0] == '\0') {
        return false;
    }
    for (i = 0; py[i] != '\0'; i++) {
        if (py[i] < 'a' || py[i] > 'z' || i >= 8) {
            return false;
        }
    }
    return true;
}

/**
 * 解析 "pinyin<TAB>汉字" 行。整个文件读进 buffer 后原地用 '\0' 切分，
 * 再把每行的 py / py_mb 指针填进 dict（lv_pinyin_dict_t 的成员是
 * const 指针，只能初始化，不能二次赋值）。
 * @param buffer 读入的文件内容，归调用者所有（会被就地修改）。
 * @return 0 成功；失败时 *dict_out 为 NULL。
 */
static int parse_dictionary(char *buffer, lv_pinyin_dict_t **dict_out,
                            size_t *count_out, size_t max_entries)
{
    char *line;
    size_t entries = 0;
    char **py_ptr;
    char **mb_ptr;

    *dict_out = NULL;
    *count_out = 0;

    /* 第一遍：切分并统计。记录每个字段的指针，第二遍直接构造常量表。 */
    py_ptr = calloc(max_entries, sizeof(*py_ptr));
    mb_ptr = calloc(max_entries, sizeof(*mb_ptr));
    if (py_ptr == NULL || mb_ptr == NULL) {
        free(py_ptr);
        free(mb_ptr);
        return -1;
    }
    line = buffer;
    while (line != NULL && *line != '\0' && entries < max_entries) {
        char *next = strchr(line, '\n');
        char *tab;
        if (next != NULL) {
            *next = '\0';
            next++;
        }
        if (*line != '\0' && *line != '#') {
            tab = strchr(line, '\t');
            if (tab != NULL) {
                *tab = '\0';
                if (*(tab + 1) != '\0' && dict_line_ok(line)) {
                    py_ptr[entries] = line;
                    mb_ptr[entries] = tab + 1;
                    entries++;
                }
            }
        }
        line = next;
    }

    if (entries > 0) {
        /* lv_pinyin_dict_t 的成员是 `const char * const`，只能在初始化时赋值，
         * 所以直接分配一块"与结构体布局相同"的内存并用指针写入：
         * 约定 C 的 struct 首成员偏移为 0、指针与 void* 尺寸一致。 */
        struct dict_slot {
            const char *py;
            const char *py_mb;
        };
        struct dict_slot *slots;
        if (sizeof(struct dict_slot) != sizeof(lv_pinyin_dict_t)) {
            free(py_ptr);
            free(mb_ptr);
            return -1;
        }
        slots = calloc(entries + 1, sizeof(*slots));
        if (slots != NULL) {
            for (size_t i = 0; i < entries; i++) {
                slots[i].py = py_ptr[i];
                slots[i].py_mb = mb_ptr[i];
            }
            slots[entries].py = NULL;
            slots[entries].py_mb = NULL;
            *dict_out = (lv_pinyin_dict_t *)slots;
            *count_out = entries;
        }
    }
    free(py_ptr);
    free(mb_ptr);
    return *dict_out != NULL ? 0 : -1;
}

/* 词库是进程级资源：多个页面/应用共用同一份文件，只加载一次，
 * 并且在所有使用它的页面销毁之前一直有效（引擎只保存指针）。 */
/* 词库是进程级共享资源：多个页面/应用共用同一份词库。
 *
 * 这里的文件读取与解析放在**后台线程**里（ui_ime_preload()），和 UI 线程的
 * 页面构建并行；UI 线程第一次需要词库时若线程还没读完，就在这里等它
 * （22 KB 的文件，通常已经完成，等待时间为 0）。
 * 注意：只有“读文件 + 解析纯文本”在线程里做，任何 lv_* 调用仍然只在 UI 线程。 */
static pthread_mutex_t s_dict_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_dict_cond;
static int s_dict_cond_ready;
static int s_dict_state;      /* 0=未开始 1=加载中 2=完成 */
static lv_pinyin_dict_t *s_dict;
static char *s_dict_buf;
static size_t s_dict_count;


static void dict_load_locked(const char *who)
{
    const board_paths_t *paths = board_app_paths();
    char path[PATH_MAX + 64];
    FILE *f;
    long total;

    s_dict_state = 1;
    if (paths == NULL) {
        s_dict_state = 2;
        return;
    }
    snprintf(path, sizeof(path), "%s/ime/pinyin_dict.txt", paths->assets);
    f = fopen(path, "rb");
    if (f == NULL) {
        BOARD_LOGW("ime", "dictionary not found: %s (using built-in)", path);
        s_dict_state = 2;
        return;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (total = ftell(f)) <= 0 ||
        total > IME_DICT_MAX_BYTES) {
        BOARD_LOGW("ime", "dictionary size rejected: %s", path);
        fclose(f);
        s_dict_state = 2;
        return;
    }
    rewind(f);
    s_dict_buf = malloc((size_t)total + 1);
    if (s_dict_buf == NULL || fread(s_dict_buf, 1, (size_t)total, f) != (size_t)total) {
        BOARD_LOGW("ime", "cannot read dictionary: %s", path);
        free(s_dict_buf);
        s_dict_buf = NULL;
        fclose(f);
        s_dict_state = 2;
        return;
    }
    fclose(f);
    s_dict_buf[total] = '\0';
    if (parse_dictionary(s_dict_buf, &s_dict, &s_dict_count, IME_DICT_MAX_ENTRIES) != 0) {
        BOARD_LOGW("ime", "dictionary has no usable entries: %s", path);
        free(s_dict_buf);
        s_dict_buf = NULL;
        s_dict = NULL;
        s_dict_state = 2;
        return;
    }
    BOARD_LOGI("ime", "loaded %u pinyin entries from %s (by %s)",
               (unsigned)s_dict_count, path, who);
    s_dict_state = 2;
}

static void *dict_thread(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&s_dict_lock);
    /* 只由“尚未完成”这个条件决定是否加载；状态 1 只表示线程已启动。 */
    if (s_dict_state != 2) {
        dict_load_locked("worker");
    }
    if (s_dict_cond_ready) {
        pthread_cond_broadcast(&s_dict_cond);
    }
    pthread_mutex_unlock(&s_dict_lock);
    return NULL;
}

void ui_ime_preload(void)
{
    pthread_condattr_t attr;
    pthread_t th;

    pthread_mutex_lock(&s_dict_lock);
    if (s_dict_state != 0) {
        pthread_mutex_unlock(&s_dict_lock);
        return;
    }
    if (!s_dict_cond_ready) {
        pthread_condattr_init(&attr);
        pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
        pthread_cond_init(&s_dict_cond, &attr);
        pthread_condattr_destroy(&attr);
        s_dict_cond_ready = 1;
    }
    s_dict_state = 1;      /* 1 = 线程已启动 */
    pthread_mutex_unlock(&s_dict_lock);

    if (pthread_create(&th, NULL, dict_thread, NULL) == 0) {
        pthread_detach(th);
    } else {
        /* 起不了线程就同步加载，保证功能不受影响。 */
        pthread_mutex_lock(&s_dict_lock);
        if (s_dict_state != 2) {
            dict_load_locked("ui-fallback");
        }
        pthread_cond_broadcast(&s_dict_cond);
        pthread_mutex_unlock(&s_dict_lock);
    }
}

static const lv_pinyin_dict_t *shared_dictionary(void)
{
    pthread_mutex_lock(&s_dict_lock);
    if (s_dict_state != 2) {
        if (s_dict_cond_ready) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            ts.tv_nsec += 500 * 1000 * 1000;
            if (ts.tv_nsec >= 1000000000L) {
                ts.tv_sec += 1;
                ts.tv_nsec -= 1000000000L;
            }
            /* 等后台线程；超时（比如文件在慢速存储上）就自己加载。 */
            (void)pthread_cond_timedwait(&s_dict_cond, &s_dict_lock, &ts);
        }
        if (s_dict_state != 2) {
            dict_load_locked("ui-sync");
        }
    }
    pthread_mutex_unlock(&s_dict_lock);
    return s_dict;
}

/* -------------------------------------------------------------------------- */
/* 样式                                                                        */
/* -------------------------------------------------------------------------- */

static void style_keyboard(ui_ime_t *ime)
{
    lv_obj_t *kb = ime->kb;

    lv_obj_remove_flag(kb, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_style_bg_color(kb, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(kb, 0, 0);
    lv_obj_set_style_radius(kb, 0, 0);
    lv_obj_set_style_pad_all(kb, 8, 0);
    lv_obj_set_style_pad_row(kb, 6, 0);
    lv_obj_set_style_pad_column(kb, 6, 0);

    /* 普通按键 */
    lv_obj_set_style_bg_color(kb, UI_COLOR_SURFACE_ALT, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 10, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, UI_COLOR_TEXT, LV_PART_ITEMS);
    /* 按键文字是 ASCII 与 LVGL 内置符号（LV_SYMBOL_BACKSPACE 等私用区码点），
     * 键盘上没有任何中文。用内置 Montserrat 而不是 TinyTTF：
     *  1) 符号本来就不在 Noto Sans SC 里，走 TTF 每次都是“未命中 → 查回退字体”，
     *     每次布局量测 46 个按键就是 46 次无谓查找；
     *  2) TinyTTF 对每个字符都要走字形缓存与 stb 解析，内置位图字体是纯查表。
     * 板上实测：这一项让“整树重排”从 1.6 s 降到毫秒级。 */
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_20, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);

    /* 功能键（退格/回车/切换大小写/符号） */
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x3C4647), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(kb, lv_color_mix(UI_COLOR_PRIMARY, UI_COLOR_SURFACE_ALT, 40),
                              LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(kb, UI_COLOR_TEXT_SECONDARY, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(kb, UI_COLOR_TEXT, LV_PART_ITEMS | LV_STATE_CHECKED);

    /* 焦点态不额外描边，触摸屏上只靠按下态反馈。 */
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(kb, LV_OPA_TRANSP, LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(kb, 0, LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(kb, UI_COLOR_PRIMARY_SOFT, LV_PART_ITEMS | LV_STATE_FOCUSED);
}

static void style_candidate_panel(ui_ime_t *ime)
{
    lv_obj_t *panel = ime->cand_panel;
    if (panel == NULL) {
        return;
    }
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x1B2021), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, UI_COLOR_LINE, 0);
    lv_obj_set_style_pad_all(panel, 6, 0);

    lv_obj_set_style_bg_color(panel, UI_COLOR_SURFACE_ALT, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(panel, UI_COLOR_PRIMARY_SOFT, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(panel, UI_COLOR_PRIMARY_SOFT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_radius(panel, 8, LV_PART_ITEMS);
    lv_obj_set_style_text_color(panel, UI_COLOR_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_text_color(panel, UI_COLOR_TEXT_MUTED, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_font(panel, ui_font_small(), LV_PART_ITEMS);
    lv_obj_set_style_border_width(panel, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(panel, 0, LV_PART_ITEMS);
}

/* -------------------------------------------------------------------------- */
/* 事件                                                                        */
/* -------------------------------------------------------------------------- */

static void fire_ready(ui_ime_t *ime)
{
    if (ime->ready_cb != NULL) {
        ime->ready_cb(ime->ready_user_data);
    }
}

static void kb_value_changed_cb(lv_event_t *e)
{
    ui_ime_t *ime = lv_event_get_user_data(e);
    lv_obj_t *kb = lv_event_get_current_target(e);
    uint32_t id = lv_buttonmatrix_get_selected_button(kb);
    const char *txt;
    if (ime == NULL || id == LV_BUTTONMATRIX_BUTTON_NONE) {
        return;
    }
    txt = lv_buttonmatrix_get_button_text(kb, id);
    if (txt == NULL) {
        return;
    }
    if (strcmp(txt, LV_SYMBOL_OK) == 0) {
        fire_ready(ime);
    } else if (strcmp(txt, LV_SYMBOL_LEFT) == 0 || strcmp(txt, LV_SYMBOL_RIGHT) == 0) {
        BOARD_LOGD("ime", "page key: %s", txt);
    }
    if (ime->cand_panel != NULL) {
        BOARD_LOGD("ime", "key '%s' ta='%s' cand_hidden=%d", txt,
                   ime->ta != NULL ? lv_textarea_get_text(ime->ta) : "-",
                   (int)lv_obj_has_flag(ime->cand_panel, LV_OBJ_FLAG_HIDDEN));
    }
}

static void quick_clicked_cb(lv_event_t *e)
{
    ui_ime_t *ime = lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_current_target(e);
    lv_obj_t *label;
    if (ime == NULL) {
        return;
    }
    label = lv_obj_get_child(btn, 0);
    if (label != NULL) {
        ui_ime_insert(ime, lv_label_get_text(label));
    }
}

static void mode_clicked_cb(lv_event_t *e)
{
    ui_ime_t *ime = lv_event_get_user_data(e);
    ui_ime_set_chinese(ime, !ime->chinese);
}

static void send_clicked_cb(lv_event_t *e)
{
    ui_ime_t *ime = lv_event_get_user_data(e);
    fire_ready(ime);
}

/* -------------------------------------------------------------------------- */
/* 底部工具栏：中/英切换 + 快捷词 + 送出                                         */
/* -------------------------------------------------------------------------- */

static lv_obj_t *tool_button(lv_obj_t *bar, const char *text, int width,
                             lv_color_t bg, lv_color_t fg)
{
    lv_obj_t *btn = lv_button_create(bar);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, width, LV_PCT(100));
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, lv_color_mix(UI_COLOR_PRIMARY, bg, 45), LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale(btn, 240, LV_STATE_PRESSED);
    {
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, text != NULL ? text : "");
        lv_obj_set_style_text_font(label, ui_font_small(), 0);
        lv_obj_set_style_text_color(label, fg, 0);
        lv_obj_center(label);
    }
    return btn;
}

static void build_toolbar(ui_ime_t *ime)
{
    /* 快捷词单独一行（等宽 4 格，无内容时整行隐藏）。 */
    lv_obj_t *quick = lv_obj_create(ime->root);
    lv_obj_remove_style_all(quick);
    lv_obj_set_size(quick, LV_PCT(100), UI_IME_QUICK_H);
    lv_obj_align(quick, LV_ALIGN_BOTTOM_MID, 0, -UI_IME_TOOLBAR_H);
    lv_obj_set_style_bg_color(quick, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(quick, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(quick, 8, 0);
    lv_obj_set_style_pad_ver(quick, 4, 0);
    lv_obj_set_style_pad_column(quick, 6, 0);
    lv_obj_set_flex_flow(quick, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(quick, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(quick, LV_OBJ_FLAG_SCROLLABLE);
    ime->quick_row = quick;

    for (int i = 0; i < 4; i++) {
        ime->quick_btns[i] = tool_button(quick, "", 0, UI_COLOR_SURFACE_ALT,
                                         UI_COLOR_TEXT_SECONDARY);
        lv_obj_set_flex_grow(ime->quick_btns[i], 1);
        lv_obj_set_height(ime->quick_btns[i], UI_IME_QUICK_H - 8);
        lv_obj_add_flag(ime->quick_btns[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(ime->quick_btns[i], quick_clicked_cb, LV_EVENT_CLICKED, ime);
    }

    lv_obj_t *bar = lv_obj_create(ime->root);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), UI_IME_TOOLBAR_H);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(bar, 8, 0);
    lv_obj_set_style_pad_column(bar, 6, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    ime->mode_btn = tool_button(bar, "中", 56, UI_COLOR_PRIMARY_SOFT, UI_COLOR_PRIMARY);
    lv_obj_add_event_cb(ime->mode_btn, mode_clicked_cb, LV_EVENT_CLICKED, ime);

    lv_obj_t *spacer = lv_obj_create(bar);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_height(spacer, 1);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);

    lv_obj_t *send = tool_button(bar, "送出", 88, UI_COLOR_PRIMARY, UI_COLOR_WHITE);
    lv_obj_add_event_cb(send, send_clicked_cb, LV_EVENT_CLICKED, ime);
}

/* -------------------------------------------------------------------------- */
/* 公开 API                                                                    */
/* -------------------------------------------------------------------------- */

ui_ime_t *ui_ime_create(lv_obj_t *parent, lv_obj_t *ta)
{
    ui_ime_t *ime;
    const lv_pinyin_dict_t *dict;

    if (parent == NULL || ta == NULL) {
        return NULL;
    }
    ime = calloc(1, sizeof(*ime));
    if (ime == NULL) {
        return NULL;
    }
    ime->ta = ta;
    ime->chinese = true;
    ime->align = LV_ALIGN_BOTTOM_MID;
    ime->width = 0;             /* 0 = 占满父容器宽度 */
    ime->height = UI_IME_HEIGHT;

    ime->root = lv_obj_create(parent);
    lv_obj_remove_style_all(ime->root);
    lv_obj_set_size(ime->root, LV_PCT(100), UI_IME_HEIGHT);
    lv_obj_align(ime->root, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(ime->root, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(ime->root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ime->root, 1, 0);
    lv_obj_set_style_border_side(ime->root, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(ime->root, UI_COLOR_LINE, 0);
    lv_obj_remove_flag(ime->root, LV_OBJ_FLAG_SCROLLABLE);

    /* 独立覆盖层：候选条是它的子对象（按钮矩阵），拼音引擎按“父容器 + 对齐”
     * 放置候选条；lv_keyboard 自己带一行空白标签行，若把候选条放进去会被挤扁。 */
    ime->board = lv_obj_create(ime->root);
    lv_obj_remove_style_all(ime->board);
    lv_obj_set_size(ime->board, LV_PCT(100), UI_IME_HEIGHT - UI_IME_TOOLBAR_H);
    lv_obj_align(ime->board, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(ime->board, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(ime->board, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(ime->board, LV_OBJ_FLAG_CLICKABLE);

    ime->kb = lv_keyboard_create(ime->board);
    lv_obj_set_width(ime->kb, LV_PCT(100));
    /* 键盘占 board 区域的 80%，上方留给候选条；高度按容器算，
     * 这样分屏（高 520）和底部（高 330）两种用法都能放下。 */
    lv_obj_set_height(ime->kb, LV_PCT(80));
    lv_obj_align(ime->kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    /* 先绑定 textarea：lv_keyboard_set_mode() 在未绑定时会断言失败。 */
    lv_keyboard_set_textarea(ime->kb, ta);
    build_toolbar(ime);

    ime->ime = lv_ime_pinyin_create(ime->board);
    dict = shared_dictionary();
    if (dict != NULL) {
        lv_ime_pinyin_set_dict(ime->ime, (lv_pinyin_dict_t *)dict);
    }
    lv_ime_pinyin_set_keyboard(ime->ime, ime->kb);
    ime->cand_panel = lv_ime_pinyin_get_cand_panel(ime->ime);
    if (ime->cand_panel != NULL) {
        lv_obj_set_size(ime->cand_panel, LV_PCT(100), UI_IME_CANDIDATE_H);
        lv_obj_align_to(ime->cand_panel, ime->kb, LV_ALIGN_OUT_TOP_MID, 0, 0);
    }

    /* 候选条占用了键盘上方空间，不再额外放提示标签。 */
    ime->hint = NULL;

    style_keyboard(ime);
    style_candidate_panel(ime);

    lv_keyboard_set_mode(ime->kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_ime_pinyin_set_mode(ime->ime, LV_IME_PINYIN_MODE_K26);
    ui_ime_set_chinese(ime, true);

    lv_obj_add_event_cb(ime->kb, kb_value_changed_cb, LV_EVENT_VALUE_CHANGED, ime);
    return ime;
}

static void apply_geometry(ui_ime_t *ime)
{
    int32_t w = ime->width > 0 ? ime->width : lv_pct(100);
    int32_t h = ime->height > 0 ? ime->height : UI_IME_HEIGHT;
    int32_t reserved = UI_IME_TOOLBAR_H;

    lv_obj_set_size(ime->root, w, h);
    lv_obj_align(ime->root, ime->align, ime->x_ofs, ime->y_ofs);
    if (ime->quick_row != NULL &&
        !lv_obj_has_flag(ime->quick_row, LV_OBJ_FLAG_HIDDEN)) {
        reserved += UI_IME_QUICK_H;
    }
    if (ime->board != NULL) {
        lv_obj_set_size(ime->board, LV_PCT(100), h - reserved);
        lv_obj_align(ime->board, LV_ALIGN_TOP_MID, 0, 0);
    }
    /* 只有分屏布局（root 高于默认高度）才显示顶部提示。 */
    if (ime->hint != NULL) {
        if (h > UI_IME_HEIGHT) {
            lv_obj_remove_flag(ime->hint, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ime->hint, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void ui_ime_set_geometry(ui_ime_t *ime, lv_align_t align, int32_t x_ofs,
                         int32_t y_ofs, int32_t width, int32_t height)
{
    if (ime == NULL || ime->root == NULL) {
        return;
    }
    ime->align = align;
    ime->x_ofs = x_ofs;
    ime->y_ofs = y_ofs;
    if (width > 0) {
        ime->width = width;
    }
    if (height > 0) {
        ime->height = height;
    }
    apply_geometry(ime);
}

void ui_ime_destroy(ui_ime_t *ime)
{
    if (ime == NULL) {
        return;
    }
    /* 句柄只持有内核对象的引用；词库由共享缓存管理，不在这里释放。 */
    ime->kb = NULL;
    ime->ime = NULL;
    ime->cand_panel = NULL;
    ime->root = NULL;
    ime->ta = NULL;
    free(ime);
}

void ui_ime_set_ready_cb(ui_ime_t *ime, ui_ime_ready_cb_t cb, void *user_data)
{
    if (ime == NULL) {
        return;
    }
    ime->ready_cb = cb;
    ime->ready_user_data = user_data;
}

void ui_ime_set_quick_keys(ui_ime_t *ime, const char *const keys[4])
{
    int shown = 0;

    if (ime == NULL || keys == NULL || ime->quick_row == NULL) {
        return;
    }
    /* 等宽四格，最多显示 4 个；没有内容时整行隐藏，把空间让给键盘 */
    for (int i = 0; i < 4; i++) {
        lv_obj_t *btn = ime->quick_btns[i];
        lv_obj_t *label = btn != NULL ? lv_obj_get_child(btn, 0) : NULL;
        if (label == NULL) {
            continue;
        }
        if (keys[i] == NULL || keys[i][0] == '\0') {
            lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_label_set_text(label, keys[i]);
        lv_obj_remove_flag(btn, LV_OBJ_FLAG_HIDDEN);
        shown++;
    }
    if (shown == 0) {
        lv_obj_add_flag(ime->quick_row, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(ime->quick_row, LV_OBJ_FLAG_HIDDEN);
    }
    /* 快捷词行的显隐会改变键盘可用高度，重新套用一次几何。 */
    apply_geometry(ime);
}

/**
 * 读取应用自带的快捷词配置：<BOARD_APP_ROOT>/data/ime/<应用 id>.conf，
 * 每行一个 UTF-8 词（最多 4 行，# 开头为注释）。同一份输入法组件
 * 可以被多个应用复用，各应用只声明自己的常用词。
 */
void ui_ime_load_quick_keys(ui_ime_t *ime, const char *app_id)
{
    const board_paths_t *paths;
    char path[PATH_MAX + 64];
    FILE *f;
    char line_buf[256];
    char keys_buf[4][96];
    const char *keys[4] = { NULL, NULL, NULL, NULL };
    int n = 0;

    if (ime == NULL || app_id == NULL) {
        return;
    }
    paths = board_app_paths();
    if (paths == NULL) {
        return;
    }
    snprintf(path, sizeof(path), "%s/ime/%s.conf", paths->data, app_id);
    f = fopen(path, "r");
    if (f == NULL) {
        return;
    }
    while (n < 4 && fgets(line_buf, sizeof(line_buf), f) != NULL) {
        char *p = line_buf;
        size_t len;
        /* 超长行只取前一段，剩下的丢到行尾——否则注释的后半截会被当成词。 */
        if (strchr(line_buf, '\n') == NULL) {
            int ch;
            while ((ch = fgetc(f)) != EOF && ch != '\n') {
                /* drain */
            }
        }
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '#' || *p == '\0' || *p == '\r' || *p == '\n') {
            continue;
        }
        len = strlen(p);
        while (len > 0 && (p[len - 1] == '\n' || p[len - 1] == '\r' ||
                           p[len - 1] == ' ')) {
            p[--len] = '\0';
        }
        if (p[0] != '\0') {
            snprintf(keys_buf[n], sizeof(keys_buf[n]), "%s", p);
            keys[n] = keys_buf[n];
            n++;
        }
    }
    fclose(f);
    if (n > 0) {
        ui_ime_set_quick_keys(ime, keys);
        BOARD_LOGI("ime", "loaded %d quick keys for %s: [%s][%s][%s][%s]", n, app_id,
                   keys[0] ? keys[0] : "", keys[1] ? keys[1] : "",
                   keys[2] ? keys[2] : "", keys[3] ? keys[3] : "");
    }
}

void ui_ime_insert(ui_ime_t *ime, const char *text)
{
    if (ime == NULL || ime->ta == NULL || text == NULL || text[0] == '\0') {
        return;
    }
    /* 保证光标在末尾，快捷词才追加在已输入内容之后。 */
    lv_textarea_set_cursor_pos(ime->ta, LV_TEXTAREA_CURSOR_LAST);
    lv_textarea_add_text(ime->ta, text);
}

void ui_ime_clear_input(ui_ime_t *ime)
{
    if (ime == NULL || ime->ta == NULL) {
        return;
    }
    lv_textarea_set_text(ime->ta, "");
}

void ui_ime_set_visible(ui_ime_t *ime, bool visible)
{
    if (ime == NULL || ime->root == NULL) {
        return;
    }
    if (visible) {
        lv_obj_remove_flag(ime->root, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ime->root, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_ime_set_chinese(ui_ime_t *ime, bool chinese)
{
    if (ime == NULL || ime->kb == NULL || ime->ime == NULL) {
        return;
    }
    ime->chinese = chinese;
    /* lv_ime_pinyin 在 K26 模式下才拦截字母键做候选；英文模式下键盘行为
     * 与普通 lv_keyboard 一致（字母直接上屏）。两种模式的按键表相同，
     * 因此不需要换模式，只需告诉引擎是否参与。 */
    lv_keyboard_set_mode(ime->kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_ime_pinyin_set_mode(ime->ime, LV_IME_PINYIN_MODE_K26);
    if (ime->mode_btn != NULL) {
        lv_obj_t *label = lv_obj_get_child(ime->mode_btn, 0);
        if (label != NULL) {
            lv_label_set_text(label, chinese ? "中" : "英");
            lv_obj_set_style_text_color(label,
                                        chinese ? UI_COLOR_PRIMARY : UI_COLOR_TEXT_SECONDARY, 0);
        }
        lv_obj_set_style_bg_color(ime->mode_btn,
                                  chinese ? UI_COLOR_PRIMARY_SOFT : UI_COLOR_SURFACE_ALT, 0);
    }
}

bool ui_ime_is_chinese(const ui_ime_t *ime)
{
    return ime != NULL && ime->chinese;
}

lv_obj_t *ui_ime_get_keyboard(const ui_ime_t *ime)
{
    return ime != NULL ? ime->kb : NULL;
}

lv_obj_t *ui_ime_get_textarea(const ui_ime_t *ime)
{
    return ime != NULL ? ime->ta : NULL;
}

bool ui_ime_dictionary_loaded(const ui_ime_t *ime)
{
    (void)ime;
    return shared_dictionary() != NULL;
}
