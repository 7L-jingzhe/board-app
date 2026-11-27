/**
 * @file gallery_ui.c
 * @brief 相册宫格、查看器与编辑器 UI。
 *
 * 核心的图像解码/编辑/保存位于 app/gallery/core。本文件负责
 * LVGL 对象、触摸事件、撤销/重做按钮与预览转换。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "board_ui/ui_app_manager.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_app/board_app.h"
#include "board_core/board_log.h"
#include "board_core/board_time.h"
#include "board_ui/ui_icons.h"
#include "board_ui/ui_theme.h"
#include "board_ui/ui_widgets.h"
#include "gallery/gallery_store.h"
#include "gallery/image.h"
#include "gallery/image_history.h"
#include "gallery/image_ops.h"
#include "lvgl/lvgl.h"

#define EDIT_MAX_W 1280
#define EDIT_MAX_H 800
#define PREVIEW_MAX_W 976
#define PREVIEW_MAX_H 400

typedef enum {
    GALLERY_GRID = 0,
    GALLERY_EDITOR = 1,
} gallery_mode_t;

typedef struct {
    lv_obj_t *page;
    lv_obj_t *topbar;
    lv_obj_t *title_label;
    lv_obj_t *content;
    lv_obj_t *refresh_btn;
    lv_obj_t *save_btn;
    lv_obj_t *delete_btn;
    gallery_mode_t mode;
    bool current_is_edited;
    bool current_can_delete;

    gallery_item_t items[GALLERY_MAX_ITEMS];
    int item_count;
    int current_index;

    uint8_t *thumb_data[GALLERY_MAX_ITEMS];
    lv_image_dsc_t thumb_dsc[GALLERY_MAX_ITEMS];
    lv_obj_t *thumbs[GALLERY_MAX_ITEMS];

    image_t *current;
    image_t *original;
    char current_path[PATH_MAX];
    image_history_t *history;
    uint8_t *preview_buf;
    size_t preview_cap;
    lv_image_dsc_t preview_dsc;
    lv_obj_t *preview_img;
    lv_obj_t *undo_btn;

    int crop_aspect;  /* 0=1:1，1=4:3，2=16:9，3=自由 */
    int crop_anchor;  /* 0..8，4=居中 */
    lv_obj_t *adjust_brightness;
    lv_obj_t *adjust_contrast;
    lv_obj_t *adjust_modal;
    lv_obj_t *crop_modal;
    lv_obj_t *confirm_modal;
    lv_obj_t *save_modal;
} gallery_app_t;

static gallery_app_t *s_gallery;

static void show_grid(gallery_app_t *app);
static void update_preview(gallery_app_t *app);
static void update_toolbar(gallery_app_t *app);
static void rotate_left_cb(lv_event_t *e);
static void rotate_right_cb(lv_event_t *e);
static void flip_cb(lv_event_t *e);
static void gray_cb(lv_event_t *e);
static void adjust_cb(lv_event_t *e);
static void crop_cb(lv_event_t *e);
static void undo_cb(lv_event_t *e);
static void reset_cb(lv_event_t *e);

/* ------------------------------------------------------------------ 辅助函数 */

static void free_thumbs(gallery_app_t *app)
{
    /* 缩略图像素由本模块 malloc，卡片对象由 LVGL 释放（lv_obj_clean）。
     * 顺序必须严格是：解除图像引用 → 删除卡片对象 → 释放像素。
     * 这里只做第一步，并把卡片指针清空；第 2、3 步在 show_grid / destroy 里。 */
    for (int i = 0; i < GALLERY_MAX_ITEMS; i++) {
        lv_obj_t *card = app->thumbs[i];
        if (card != NULL && lv_obj_is_valid(card)) {
            lv_obj_t *img = lv_obj_get_child(card, 0);
            /* 只处理确实是图像对象的第一个子对象。 */
            if (img != NULL && lv_obj_check_type(img, &lv_image_class)) {
                lv_image_set_src(img, NULL);
            }
        }
        app->thumbs[i] = NULL;
    }
}

/* 释放缩略图像素缓冲（必须在相关卡片对象被删除之后调用）。 */
static void free_thumb_buffers(gallery_app_t *app)
{
    for (int i = 0; i < GALLERY_MAX_ITEMS; i++) {
        free(app->thumb_data[i]);
        app->thumb_data[i] = NULL;
        memset(&app->thumb_dsc[i], 0, sizeof(app->thumb_dsc[i]));
    }
}

static void clear_editor(gallery_app_t *app)
{
    /* 预览图挂的是 app->preview_buf（本模块 malloc），对象由 LVGL 释放。
     * 必须在释放像素之前删掉图像对象，否则重绘/删对象时会访问已释放内存。 */
    if (app->preview_img != NULL) {
        lv_obj_delete_async(app->preview_img);
        app->preview_img = NULL;
    }
    image_free(app->current);
    image_free(app->original);
    image_history_destroy(app->history);
    app->current = NULL;
    app->original = NULL;
    app->history = NULL;
    free(app->preview_buf);
    app->preview_buf = NULL;
    app->preview_cap = 0;
    app->undo_btn = NULL;
    app->current_path[0] = '\0';
    app->current_is_edited = false;
    app->current_can_delete = false;
}

static lv_obj_t *tool_button(lv_obj_t *parent, const char *text,
                             lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 104, 72);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_bg_color(btn, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, UI_COLOR_LINE, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_bg_color(btn, UI_COLOR_PRIMARY_SOFT, LV_STATE_PRESSED);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, ui_font_caption(), 0);
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_center(label);
    if (cb != NULL) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    }
    return btn;
}

static void set_dsc(lv_image_dsc_t *dsc, uint8_t *data, int w, int h)
{
    memset(dsc, 0, sizeof(*dsc));
    dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf = LV_COLOR_FORMAT_ARGB8888;
    dsc->header.w = (uint32_t)w;
    dsc->header.h = (uint32_t)h;
    dsc->header.stride = (uint32_t)w * 4u;
    dsc->data_size = (uint32_t)w * (uint32_t)h * 4u;
    dsc->data = data;
}

/* ------------------------------------------------------------------- 宫格 */

static void thumbnail_clicked_cb(lv_event_t *e)
{
    gallery_app_t *app = lv_event_get_user_data(e);
    lv_obj_t *card = lv_event_get_current_target(e);
    int index = (int)(intptr_t)lv_obj_get_user_data(card);
    if (app == NULL || index < 0 || index >= app->item_count) {
        return;
    }
    image_t *full = image_load(app->items[index].path);
    if (full == NULL) {
        ui_toast_show("图片解码失败");
        return;
    }
    /* 按原始分辨率编辑可能耗尽板子的 RAM。保留一个质量高但大小
     * 有界的编辑缓冲；原始文件不受影响。 */
    image_t *edit = image_resize_fit(full, EDIT_MAX_W, EDIT_MAX_H);
    image_free(full);
    if (edit == NULL) {
        ui_toast_show("图片过大，无法编辑");
        return;
    }
    clear_editor(app);
    app->current = edit;
    app->original = image_clone(edit);
    app->history = image_history_create(edit);
    app->current_index = index;
    snprintf(app->current_path, sizeof(app->current_path), "%s",
             app->items[index].path);
    app->current_is_edited = strstr(app->items[index].name, "_edited_") != NULL;
    app->current_can_delete = gallery_store_can_delete(app->current_path);
    app->mode = GALLERY_EDITOR;
    lv_obj_clean(app->content);
    lv_obj_add_flag(app->refresh_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(app->save_btn, LV_OBJ_FLAG_HIDDEN);
    if (app->current_can_delete) {
        lv_obj_remove_flag(app->delete_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(app->delete_btn, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(app->title_label, app->items[index].name);

    lv_obj_t *canvas = lv_obj_create(app->content);
    lv_obj_remove_style_all(canvas);
    lv_obj_set_pos(canvas, 24, 84);
    lv_obj_set_size(canvas, PREVIEW_MAX_W, PREVIEW_MAX_H);
    lv_obj_set_style_bg_color(canvas, lv_color_hex(0x1A2020), 0);
    lv_obj_set_style_bg_opa(canvas, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(canvas, 18, 0);
    lv_obj_remove_flag(canvas, LV_OBJ_FLAG_SCROLLABLE);

    app->preview_img = lv_image_create(canvas);
    lv_obj_center(app->preview_img);

    lv_obj_t *tools = lv_obj_create(app->content);
    lv_obj_remove_style_all(tools);
    lv_obj_set_pos(tools, 52, 496);
    lv_obj_set_size(tools, 920, 72);
    lv_obj_set_style_bg_opa(tools, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(tools, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(tools, 10, 0);
    lv_obj_remove_flag(tools, LV_OBJ_FLAG_SCROLLABLE);

    app->undo_btn = tool_button(tools, "撤销", undo_cb, app);
    tool_button(tools, "左旋", rotate_left_cb, app);
    tool_button(tools, "右旋", rotate_right_cb, app);
    tool_button(tools, "翻转", flip_cb, app);
    tool_button(tools, "灰度", gray_cb, app);
    tool_button(tools, "调整", adjust_cb, app);
    tool_button(tools, "裁剪", crop_cb, app);
    tool_button(tools, "重置", reset_cb, app);

    update_preview(app);
    update_toolbar(app);
}

static void build_grid(gallery_app_t *app)
{
    int64_t t0 = board_time_now_ms();
    app->item_count = gallery_store_list(app->items, GALLERY_MAX_ITEMS);
    if (app->item_count < 0) {
        app->item_count = 0;
    }
    char title[64];
    snprintf(title, sizeof(title), "相册 · %d 张", app->item_count);
    lv_label_set_text(app->title_label, title);

    lv_obj_t *grid = lv_obj_create(app->content);
    lv_obj_remove_style_all(grid);
    lv_obj_set_pos(grid, 32, 96);
    lv_obj_set_size(grid, 960, 472);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(grid, 24, 0);
    lv_obj_set_style_pad_row(grid, 24, 0);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_AUTO);

    if (app->item_count == 0) {
        ui_empty_state_show(app->content, "还没有照片",
                            "把图片放到 assets/photos/ 后点击刷新");
        return;
    }

    for (int i = 0; i < app->item_count; i++) {
        image_t *full = image_load(app->items[i].path);
        image_t *thumb = full != NULL ? image_resize_fit(full, 280, 160) : NULL;
        image_free(full);
        if (thumb == NULL) {
            BOARD_LOGW("gallery", "thumbnail failed: %s", app->items[i].name);
            continue;
        }
        size_t bytes = (size_t)thumb->w * (size_t)thumb->h * 4u;
        uint8_t *data = malloc(bytes);
        if (data == NULL) {
            BOARD_LOGW("gallery", "thumbnail oom: %s", app->items[i].name);
            image_free(thumb);
            continue;
        }
        if (image_copy_to_bgra(thumb, data, bytes) != 0) {
            BOARD_LOGW("gallery", "thumbnail convert failed: %s",
                       app->items[i].name);
            free(data);
            image_free(thumb);
            continue;
        }
        set_dsc(&app->thumb_dsc[i], data, thumb->w, thumb->h);
        app->thumb_data[i] = data;
        image_free(thumb);

        lv_obj_t *card = ui_card_create(grid, 304, 220);
        /* 在板子的软件渲染器上，10 张卡片带 24 px 阴影开销很大；
         * 边框/圆角已经足以区分卡片。 */
        lv_obj_set_style_shadow_width(card, 0, 0);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(card, (void *)(intptr_t)i);
        lv_obj_add_event_cb(card, thumbnail_clicked_cb, LV_EVENT_CLICKED, app);
        /* 记录卡片对象：重建宫格时本模块释放缩略图内存，必须先把仍挂在卡片
         * 上的图像引用解除，避免 LVGL 之后访问已释放的像素。 */
        app->thumbs[i] = card;

        lv_obj_t *img = lv_image_create(card);
        lv_image_set_src(img, &app->thumb_dsc[i]);
        lv_obj_set_size(img, app->thumb_dsc[i].header.w, app->thumb_dsc[i].header.h);
        lv_obj_set_pos(img, 12, 12);
        lv_obj_set_style_radius(img, 14, 0);
        lv_obj_set_style_clip_corner(img, true, 0);
        lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *name = lv_label_create(card);
        lv_label_set_text(name, app->items[i].name);
        lv_obj_set_style_text_font(name, ui_font_caption(), 0);
        lv_obj_set_style_text_color(name, UI_COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_pos(name, 16, 186);
        lv_obj_set_width(name, 220);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    }
    BOARD_LOGD("gallery", "grid %d items in %lld ms", app->item_count,
               (long long)(board_time_now_ms() - t0));
}

static void show_grid(gallery_app_t *app)
{
    clear_editor(app);
    app->mode = GALLERY_GRID;
    /* 先解除卡片对缩略图像素的引用（对象仍有效），再删对象，最后释放像素。 */
    free_thumbs(app);
    lv_obj_clean(app->content);
    free_thumb_buffers(app);
    lv_obj_add_flag(app->save_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(app->delete_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(app->refresh_btn, LV_OBJ_FLAG_HIDDEN);
    build_grid(app);
}

/* ------------------------------------------------------------------ 编辑器 */

static void update_toolbar(gallery_app_t *app)
{
    if (app->undo_btn == NULL) {
        return;
    }
    if (image_history_can_undo(app->history)) {
        lv_obj_remove_state(app->undo_btn, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(app->undo_btn, LV_STATE_DISABLED);
    }
}

static void update_preview(gallery_app_t *app)
{
    if (app->current == NULL || app->preview_img == NULL) {
        return;
    }
    /* 核心层存储的是 RGBA；LVGL ARGB8888 在内存中为 B,G,R,A。转换的是
     * 降采样后的副本而非编辑缓冲，以降低重绘开销。 */
    image_t *scaled = image_resize_fit(app->current, PREVIEW_MAX_W, PREVIEW_MAX_H);
    if (scaled == NULL) {
        return;
    }
    size_t bytes = (size_t)scaled->w * (size_t)scaled->h * 4u;
    if (bytes > app->preview_cap) {
        /* realloc 会搬移内存，旧地址上仍是上一帧的像素，先清空引用再换缓冲。 */
        lv_image_set_src(app->preview_img, NULL);
        uint8_t *p = realloc(app->preview_buf, bytes);
        if (p == NULL) {
            image_free(scaled);
            return;
        }
        app->preview_buf = p;
        app->preview_cap = bytes;
    }
    if (image_copy_to_bgra(scaled, app->preview_buf, bytes) != 0) {
        image_free(scaled);
        return;
    }
    set_dsc(&app->preview_dsc, app->preview_buf, scaled->w, scaled->h);
    lv_image_set_src(app->preview_img, &app->preview_dsc);
    lv_obj_set_size(app->preview_img, scaled->w, scaled->h);
    lv_obj_center(app->preview_img);
    lv_obj_invalidate(app->preview_img);
    image_free(scaled);
    update_toolbar(app);
}

static void after_edit(gallery_app_t *app)
{
    /* 历史保存的是操作后的状态。撤销会丢弃当前栈顶并返回
     * 上一状态的克隆，因此结果由调用方负责释放。 */
    if (app->history != NULL) {
        (void)image_history_push(app->history, app->current);
    }
    update_preview(app);
}

static void rotate_left_cb(lv_event_t *e)
{
    gallery_app_t *app = lv_event_get_user_data(e);
    if (image_rotate90(&app->current, false) == 0) {
        after_edit(app);
    }
}

static void rotate_right_cb(lv_event_t *e)
{
    gallery_app_t *app = lv_event_get_user_data(e);
    if (image_rotate90(&app->current, true) == 0) {
        after_edit(app);
    }
}

static void flip_cb(lv_event_t *e)
{
    gallery_app_t *app = lv_event_get_user_data(e);
    if (image_flip(app->current, true) == 0) {
        after_edit(app);
    }
}

static void gray_cb(lv_event_t *e)
{
    gallery_app_t *app = lv_event_get_user_data(e);
    if (image_grayscale(app->current) == 0) {
        after_edit(app);
    }
}

static void undo_cb(lv_event_t *e)
{
    gallery_app_t *app = lv_event_get_user_data(e);
    image_t *prev = image_history_undo(app->history);
    if (prev != NULL) {
        image_free(app->current);
        app->current = prev;
        update_preview(app);
    }
}

static void reset_cb(lv_event_t *e)
{
    gallery_app_t *app = lv_event_get_user_data(e);
    if (app->original == NULL) {
        return;
    }
    image_t *fresh = image_clone(app->original);
    if (fresh == NULL) {
        return;
    }
    image_free(app->current);
    app->current = fresh;
    image_history_reset(app->history, fresh);
    update_preview(app);
}

/* 弹窗对象被删除时的清理回调。
 * destroy() 里已经 free(app)，但页面对象之后才被 lv_obj_delete，
 * 届时仍会派发 LV_EVENT_DELETE，因此这里只能通过 s_gallery 访问，
 * 并在其为空时直接返回，避免 use-after-free。 */
static void save_modal_deleted_cb(lv_event_t *e)
{
    gallery_app_t *app = s_gallery;
    (void)e;
    if (app != NULL) {
        app->save_modal = NULL;
    }
}

static void save_copy_cb(lv_event_t *e)
{
    gallery_app_t *app = s_gallery;
    char path[PATH_MAX];
    if (app == NULL || app->current == NULL) {
        return;
    }
    int rc = gallery_store_save_near(app->current, app->current_path, path,
                                     sizeof(path));
    lv_obj_t *content = app->save_modal;
    app->save_modal = NULL;
    ui_modal_close(content);
    if (rc != 0) {
        BOARD_LOGE("gallery", "save-as failed rc=%d", rc);
        ui_toast_show("另存为失败");
        return;
    }
    snprintf(app->current_path, sizeof(app->current_path), "%s", path);
    app->current_is_edited = true;
    app->current_can_delete = true;
    lv_obj_remove_flag(app->delete_btn, LV_OBJ_FLAG_HIDDEN);
    const char *name = strrchr(path, '/');
    lv_label_set_text(app->title_label, name != NULL ? name + 1 : path);
    ui_toast_show(name != NULL ? name + 1 : path);
}

static void save_overwrite_cb(lv_event_t *e)
{
    gallery_app_t *app = s_gallery;
    char path[PATH_MAX];
    int rc;
    if (app == NULL || app->current == NULL) {
        return;
    }
    if (app->current_is_edited) {
        rc = gallery_store_overwrite_edited(app->current, app->current_path);
        snprintf(path, sizeof(path), "%s", app->current_path);
    } else {
        /* 原图是只读资源；保持其完整，并在其旁边写入新副本，
         * 而不是直接报错。 */
        rc = gallery_store_save_near(app->current, app->current_path, path,
                                     sizeof(path));
        if (rc == 0) {
            snprintf(app->current_path, sizeof(app->current_path), "%s", path);
            app->current_is_edited = true;
            app->current_can_delete = true;
            lv_obj_remove_flag(app->delete_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
    lv_obj_t *content = app->save_modal;
    app->save_modal = NULL;
    ui_modal_close(content);
    if (rc != 0) {
        BOARD_LOGE("gallery", "save failed rc=%d", rc);
        ui_toast_show("保存失败，请检查 data/edited 权限");
        return;
    }
    const char *name = strrchr(path, '/');
    lv_label_set_text(app->title_label, name != NULL ? name + 1 : path);
    ui_toast_show(name != NULL ? name + 1 : path);
    BOARD_LOGI("gallery", "saved %s", path);
}

static void save_cb(lv_event_t *e)
{
    gallery_app_t *app = lv_event_get_user_data(e);
    if (app == NULL || app->current == NULL) {
        BOARD_LOGE("gallery", "save: no current image");
        ui_toast_show("没有可保存的图片");
        return;
    }
    lv_obj_t *content = ui_modal_create(app->page, "保存图片", 560);
    app->save_modal = content;
    lv_obj_add_event_cb(content, save_modal_deleted_cb, LV_EVENT_DELETE, app);
    lv_obj_t *detail = lv_label_create(content);
    lv_label_set_text(detail,
                      app->current_is_edited
                          ? "保存修改会覆盖当前副本；另存为会在同一目录新建一张。"
                          : "原图不会被覆盖；保存修改和另存为都会在图片同目录生成新文件。");
    lv_obj_set_style_text_font(detail, ui_font_body(), 0);
    lv_obj_set_style_text_color(detail, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(detail, lv_pct(100));
    lv_obj_t *row = lv_obj_create(content);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 56);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 12, 0);
    ui_button_secondary_create(row, "另存为", save_copy_cb, app);
    ui_button_primary_create(row, "保存修改", save_overwrite_cb, app);
}

/* ------------------------------------------------------------- 调整对话框 */

static void adjust_apply_cb(lv_event_t *e)
{
    gallery_app_t *app = s_gallery;
    lv_obj_t *btn = lv_event_get_current_target(e);
    (void)btn;
    int brightness = app != NULL && app->adjust_brightness != NULL
                         ? lv_slider_get_value(app->adjust_brightness)
                         : 0;
    int contrast = app != NULL && app->adjust_contrast != NULL
                       ? lv_slider_get_value(app->adjust_contrast)
                       : 0;
    if (app != NULL) {
        image_t *tmp = image_clone(app->current);
        if (tmp != NULL && image_adjust(tmp, brightness, contrast) == 0) {
            image_free(app->current);
            app->current = tmp;
            after_edit(app);
        } else {
            image_free(tmp);
        }
        app->adjust_brightness = NULL;
        app->adjust_contrast = NULL;
        lv_obj_t *content = app->adjust_modal;
        app->adjust_modal = NULL;
        ui_modal_close(content);
    }
}

static void adjust_cancel_cb(lv_event_t *e)
{
    gallery_app_t *app = s_gallery;
    lv_obj_t *btn = lv_event_get_current_target(e);
    (void)btn;
    if (app != NULL) {
        app->adjust_brightness = NULL;
        app->adjust_contrast = NULL;
        lv_obj_t *content = app->adjust_modal;
        app->adjust_modal = NULL;
        ui_modal_close(content);
    }
}

static void adjust_cb(lv_event_t *e)
{
    gallery_app_t *app = lv_event_get_user_data(e);
    lv_obj_t *content = ui_modal_create(app->page, "调整亮度 / 对比度", 560);
    app->adjust_modal = content;

    lv_obj_t *bl = lv_label_create(content);
    lv_label_set_text(bl, "亮度");
    lv_obj_set_style_text_font(bl, ui_font_small(), 0);
    lv_obj_set_style_text_color(bl, UI_COLOR_TEXT_SECONDARY, 0);
    app->adjust_brightness = lv_slider_create(content);
    lv_slider_set_range(app->adjust_brightness, -100, 100);
    lv_slider_set_value(app->adjust_brightness, 0, LV_ANIM_OFF);
    lv_obj_set_width(app->adjust_brightness, lv_pct(100));

    lv_obj_t *cl = lv_label_create(content);
    lv_label_set_text(cl, "对比度");
    lv_obj_set_style_text_font(cl, ui_font_small(), 0);
    lv_obj_set_style_text_color(cl, UI_COLOR_TEXT_SECONDARY, 0);
    app->adjust_contrast = lv_slider_create(content);
    lv_slider_set_range(app->adjust_contrast, -100, 100);
    lv_slider_set_value(app->adjust_contrast, 0, LV_ANIM_OFF);
    lv_obj_set_width(app->adjust_contrast, lv_pct(100));

    lv_obj_t *row = lv_obj_create(content);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 56);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_t *cancel = ui_button_secondary_create(row, "取消", adjust_cancel_cb, app);
    lv_obj_t *apply = ui_button_primary_create(row, "应用", adjust_apply_cb, app);
    (void)cancel;
    (void)apply;
}

/* --------------------------------------------------------------- 裁剪对话框 */

static void crop_aspect_cb(lv_event_t *e)
{
    gallery_app_t *app = lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_current_target(e);
    app->crop_aspect = (int)(intptr_t)lv_obj_get_user_data(btn);
}

static void crop_anchor_cb(lv_event_t *e)
{
    gallery_app_t *app = lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_current_target(e);
    app->crop_anchor = (int)(intptr_t)lv_obj_get_user_data(btn);
}

static void crop_apply_cb(lv_event_t *e)
{
    gallery_app_t *app = s_gallery;
    lv_obj_t *btn = lv_event_get_current_target(e);
    (void)btn;
    if (app == NULL || app->current == NULL) {
        return;
    }
    int iw = app->current->w;
    int ih = app->current->h;
    int cw;
    int ch;
    /* 比例预设会适配到图像内部；3x3 锚点决定裁剪窗口
     * 的位置（默认居中）。 */
    if (app->crop_aspect == 3) {
        cw = iw * 8 / 10;
        ch = ih * 8 / 10;
    } else {
        double ratio = app->crop_aspect == 1 ? 4.0 / 3.0
                       : app->crop_aspect == 2 ? 16.0 / 9.0
                                               : 1.0;
        if ((double)iw / (double)ih > ratio) {
            ch = ih;
            cw = (int)((double)ih * ratio);
        } else {
            cw = iw;
            ch = (int)((double)iw / ratio);
        }
    }
    if (cw < 64) {
        cw = 64;
    }
    if (ch < 64) {
        ch = 64;
    }
    if (cw > iw) {
        cw = iw;
    }
    if (ch > ih) {
        ch = ih;
    }
    int ax = app->crop_anchor % 3;
    int ay = app->crop_anchor / 3;
    int x = (iw - cw) * ax / 2;
    int y = (ih - ch) * ay / 2;
    if (image_crop(&app->current, x, y, cw, ch) == 0) {
        after_edit(app);
    }
    lv_obj_t *content = app->crop_modal;
    app->crop_modal = NULL;
    ui_modal_close(content);
}

static void crop_cancel_cb(lv_event_t *e)
{
    gallery_app_t *app = s_gallery;
    (void)e;
    if (app != NULL) {
        lv_obj_t *content = app->crop_modal;
        app->crop_modal = NULL;
        ui_modal_close(content);
    }
}

static void crop_cb(lv_event_t *e)
{
    gallery_app_t *app = lv_event_get_user_data(e);
    lv_obj_t *content = ui_modal_create(app->page, "裁剪", 560);
    app->crop_modal = content;
    app->crop_aspect = 0;
    app->crop_anchor = 4;

    lv_obj_t *ratio = lv_obj_create(content);
    lv_obj_remove_style_all(ratio);
    lv_obj_set_size(ratio, lv_pct(100), 56);
    lv_obj_set_style_bg_opa(ratio, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(ratio, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(ratio, 8, 0);
    static const char *labels[4] = { "1:1", "4:3", "16:9", "自由" };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = ui_button_secondary_create(ratio, labels[i], NULL, NULL);
        lv_obj_set_size(b, 112, 48);
        lv_obj_set_user_data(b, (void *)(intptr_t)i);
        lv_obj_add_event_cb(b, crop_aspect_cb, LV_EVENT_CLICKED, app);
    }

    lv_obj_t *pos = lv_obj_create(content);
    lv_obj_remove_style_all(pos);
    lv_obj_set_size(pos, 220, 168);
    lv_obj_set_style_bg_opa(pos, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(pos, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(pos, 4, 0);
    lv_obj_set_style_pad_row(pos, 4, 0);
    static const char *anchors[9] = { "左上", "上", "右上", "左", "中", "右",
                                      "左下", "下", "右下" };
    for (int i = 0; i < 9; i++) {
        lv_obj_t *b = ui_button_secondary_create(pos, anchors[i], NULL, NULL);
        lv_obj_set_size(b, 68, 48);
        lv_obj_set_user_data(b, (void *)(intptr_t)i);
        lv_obj_add_event_cb(b, crop_anchor_cb, LV_EVENT_CLICKED, app);
    }

    lv_obj_t *row = lv_obj_create(content);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 56);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 12, 0);
    ui_button_secondary_create(row, "取消", crop_cancel_cb, app);
    ui_button_primary_create(row, "裁剪", crop_apply_cb, app);
}

/* --------------------------------------------------------------- 生命周期 */

static void confirm_delete_cb(lv_event_t *e)
{
    /* 同上：只通过 s_gallery 访问，destroy() 之后该指针已被清空。 */
    gallery_app_t *app = s_gallery;
    (void)e;
    if (app != NULL) {
        app->confirm_modal = NULL;
    }
}

static void discard_confirm_cb(lv_event_t *e)
{
    gallery_app_t *app = lv_event_get_user_data(e);
    if (app == NULL) {
        return;
    }
    lv_obj_t *content = app->confirm_modal;
    app->confirm_modal = NULL;
    ui_modal_close(content);
    show_grid(app);
}

static void cancel_confirm_cb(lv_event_t *e)
{
    gallery_app_t *app = lv_event_get_user_data(e);
    if (app == NULL) {
        return;
    }
    lv_obj_t *content = app->confirm_modal;
    app->confirm_modal = NULL;
    ui_modal_close(content);
}

static void delete_confirm_cb(lv_event_t *e)
{
    gallery_app_t *app = lv_event_get_user_data(e);
    if (app == NULL) {
        return;
    }
    int rc = app->current_can_delete
                 ? gallery_store_delete_edited(app->current_path)
                 : -EACCES;
    lv_obj_t *content = app->confirm_modal;
    app->confirm_modal = NULL;
    ui_modal_close(content);
    if (rc != 0) {
        ui_toast_show("删除失败");
        return;
    }
    show_grid(app);
}

static void delete_cb(lv_event_t *e)
{
    gallery_app_t *app = lv_event_get_user_data(e);
    if (app == NULL || !app->current_can_delete) {
        return;
    }
    lv_obj_t *content = ui_modal_create(app->page, "删除图片？", 520);
    app->confirm_modal = content;
    lv_obj_add_event_cb(content, confirm_delete_cb, LV_EVENT_DELETE, app);
    lv_obj_t *detail = lv_label_create(content);
    lv_label_set_text(detail, "删除后不可恢复，确认删除当前图片？");
    lv_obj_set_style_text_font(detail, ui_font_body(), 0);
    lv_obj_set_style_text_color(detail, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(detail, lv_pct(100));
    lv_obj_t *row = lv_obj_create(content);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 56);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 12, 0);
    ui_button_secondary_create(row, "取消", cancel_confirm_cb, app);
    lv_obj_t *del = ui_button_secondary_create(row, "删除", delete_confirm_cb, app);
    lv_obj_set_style_text_color(lv_obj_get_child(del, 0), UI_COLOR_DANGER, 0);
}

static void gallery_back_cb(lv_event_t *e)
{
    gallery_app_t *app = lv_event_get_user_data(e);
    if (app != NULL && app->mode == GALLERY_EDITOR) {
        /* 仅在用户确实修改过图片时才询问。 */
        if (!image_history_can_undo(app->history)) {
            show_grid(app);
            return;
        }
        lv_obj_t *content = ui_modal_create(app->page, "放弃编辑？", 520);
        app->confirm_modal = content;
        lv_obj_add_event_cb(content, confirm_delete_cb, LV_EVENT_DELETE, app);
        lv_obj_t *detail = lv_label_create(content);
        lv_label_set_text(detail, "当前修改尚未保存，返回相册将丢弃这些修改。");
        lv_obj_set_style_text_font(detail, ui_font_body(), 0);
        lv_obj_set_style_text_color(detail, UI_COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(detail, lv_pct(100));
        lv_obj_t *row = lv_obj_create(content);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(100), 56);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 12, 0);
        ui_button_secondary_create(row, "取消", cancel_confirm_cb, app);
        ui_button_primary_create(row, "放弃并返回", discard_confirm_cb, app);
        return;
    }
    (void)ui_app_manager_back();
}

static void refresh_cb(lv_event_t *e)
{
    gallery_app_t *app = lv_event_get_user_data(e);
    show_grid(app);
}

static lv_obj_t *gallery_create(lv_obj_t *parent)
{
    gallery_app_t *app = calloc(1, sizeof(*app));
    if (app == NULL) {
        return NULL;
    }
    /* 同一时间只允许一个相册实例：管理器会先销毁旧页面再创建新页面。
     * s_gallery 让 destroy() 能释放非 LVGL 资源。 */
    s_gallery = app;
    app->page = parent;
    lv_obj_set_style_bg_color(parent, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    if (board_app_paths() == NULL || gallery_store_init(board_app_paths()) != 0) {
        ui_toast_show("相册存储初始化失败");
    }

    app->topbar = ui_topbar_create(parent, "相册", gallery_back_cb, app);
    app->title_label = lv_obj_get_child(app->topbar, 1);
    app->refresh_btn = ui_topbar_add_action(app->topbar, "刷新", refresh_cb, app);
    app->save_btn = ui_topbar_add_action(app->topbar, "保存", save_cb, app);
    app->delete_btn = ui_topbar_add_action(app->topbar, "删除", delete_cb, app);
    lv_obj_add_flag(app->save_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(app->delete_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(app->delete_btn, UI_COLOR_SURFACE_ALT, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(app->delete_btn, 0),
                                UI_COLOR_DANGER, 0);

    app->content = lv_obj_create(parent);
    lv_obj_remove_style_all(app->content);
    lv_obj_set_pos(app->content, 0, 0);
    lv_obj_set_size(app->content, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(app->content, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(app->content, LV_OBJ_FLAG_SCROLLABLE);
    /* 它只是布局容器；点击应交给其子对象或上方的
     * topbar。 */
    lv_obj_remove_flag(app->content, LV_OBJ_FLAG_CLICKABLE);

    show_grid(app);
    /* content 对象是全屏的，且在 topbar 之后创建。抬高 topbar，
     * 使其返回/刷新/保存按钮保持可点击。 */
    lv_obj_move_foreground(app->topbar);
    return parent;
}

static void gallery_destroy(void)
{
    gallery_app_t *app = s_gallery;
    if (app == NULL) {
        return;
    }
    /* destroy() 在页面对象被删除之前调用，此时卡片仍有效，
     * 可以先解除引用再释放像素。 */
    clear_editor(app);
    free_thumbs(app);
    free_thumb_buffers(app);
    free(app);
    s_gallery = NULL;
}

static const board_app_desc_t s_gallery_app = {
    .id = "gallery",
    .title = "相册",
    .subtitle = "浏览与编辑照片",
    .parent_id = NULL,
    .best_score_key = NULL,
    .icon = UI_ICON_GALLERY,
    .accent = 0x5B8C87,
    .order = 20,
    .show_on_home = true,
    .create = gallery_create,
    .destroy = gallery_destroy,
};

BOARD_APP_REGISTER(s_gallery_app);
