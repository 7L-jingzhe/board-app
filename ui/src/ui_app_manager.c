/**
 * @file ui_app_manager.c
 * @brief 发现 board_apps 段中的条目并切换页面。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "board_ui/ui_app_manager.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_core/board_log.h"
#include "board_ui/ui_theme.h"

#define APP_MAX 32
#define STACK_MAX 8

#if defined(__GNUC__)
/* GNU ld 会为该自定义段生成这两个边界符号。每个应用 UI 文件通过
 * BOARD_APP_REGISTER() 在此放入一个描述符指针。顶层 CMake 以
 * --whole-archive 链接 board_ui，因此未被引用的应用目标文件
 * （以及其中的注册条目）不会被链接器丢弃。 */
extern const board_app_desc_t *const __start_board_apps[];
extern const board_app_desc_t *const __stop_board_apps[];
#endif

static lv_obj_t *s_screen;
static lv_obj_t *s_page;
static const board_app_desc_t *s_current;
static const board_app_desc_t *s_apps[APP_MAX];
static size_t s_count;
static const board_app_desc_t *s_stack[STACK_MAX];
static int s_stack_depth;
static bool s_busy;
static lv_timer_t *s_busy_timer;

static int cmp_apps(const void *a, const void *b)
{
    const board_app_desc_t *const *pa = a;
    const board_app_desc_t *const *pb = b;
    int d = (*pa)->order - (*pb)->order;
    if (d != 0) {
        return d;
    }
    return strcmp((*pa)->id, (*pb)->id);
}

static int register_internal(const board_app_desc_t *app)
{
    if (app == NULL || app->id == NULL || app->create == NULL) {
        return -EINVAL;
    }
    if (s_count >= APP_MAX) {
        return -ENOSPC;
    }
    for (size_t i = 0; i < s_count; i++) {
        if (strcmp(s_apps[i]->id, app->id) == 0) {
            BOARD_LOGW("nav", "duplicate app id ignored: %s", app->id);
            return -EINVAL;
        }
    }
    s_apps[s_count++] = app;
    return 0;
}

static void busy_timer_cb(lv_timer_t *timer)
{
    s_busy = false;
    lv_timer_delete(timer);
    s_busy_timer = NULL;
}

static void mark_busy(void)
{
    s_busy = true;
    if (s_busy_timer != NULL) {
        lv_timer_delete(s_busy_timer);
    }
    s_busy_timer = lv_timer_create(busy_timer_cb, 220, NULL);
    lv_timer_set_repeat_count(s_busy_timer, 1);
}

static int switch_to(const board_app_desc_t *app, bool push)
{
    lv_obj_t *page;

    /* 同一时刻只存在一个页面。先构建新页面再删除旧页面，
     * 可避免切换过程中屏幕出现白闪。 */
    if (app == NULL || app->create == NULL) {
        return -EINVAL;
    }
    if (s_busy) {
        return -EAGAIN;
    }
    if (s_current == app) {
        return 0;
    }
    if (push && s_current != NULL && s_stack_depth >= STACK_MAX) {
        return -ENOSPC;
    }

    page = lv_obj_create(s_screen);
    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_style_bg_color(page, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    if (app->create(page) == NULL) {
        lv_obj_delete(page);
        return -EIO;
    }
    if (push && s_current != NULL) {
        s_stack[s_stack_depth++] = s_current;
    }

    /* 旧页面可能持有定时器/线程/图像；先让它释放这些资源。 */
    if (s_current != NULL && s_current->destroy != NULL) {
        s_current->destroy();
    }
    if (s_page != NULL) {
        lv_obj_delete(s_page);
    }
    s_page = page;
    s_current = app;
    lv_obj_fade_in(page, 180, 0);
    mark_busy();
    BOARD_LOGI("nav", "open %s", app->id);
    return 0;
}

int ui_app_manager_init(lv_obj_t *screen)
{
    s_screen = (screen != NULL) ? screen : lv_screen_active();
    s_count = 0;
    s_stack_depth = 0;
    s_current = NULL;
    s_page = NULL;
    s_busy = false;

#if defined(__GNUC__)
    for (const board_app_desc_t *const *p = __start_board_apps;
         p < __stop_board_apps; p++) {
        (void)register_internal(*p);
    }
#endif
    qsort(s_apps, s_count, sizeof(s_apps[0]), cmp_apps);
    BOARD_LOGI("nav", "registered %zu app(s)", s_count);
    return 0;
}

int ui_app_manager_register(const board_app_desc_t *app)
{
    int ret = register_internal(app);
    if (ret == 0) {
        qsort(s_apps, s_count, sizeof(s_apps[0]), cmp_apps);
    }
    return ret;
}

size_t ui_app_manager_count(void)
{
    return s_count;
}

const board_app_desc_t *ui_app_manager_at(size_t index)
{
    return index < s_count ? s_apps[index] : NULL;
}

const board_app_desc_t *ui_app_manager_find(const char *id)
{
    if (id == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < s_count; i++) {
        if (strcmp(s_apps[i]->id, id) == 0) {
            return s_apps[i];
        }
    }
    return NULL;
}

int ui_app_manager_open(const char *id)
{
    const board_app_desc_t *app = ui_app_manager_find(id);
    if (app == NULL) {
        return -ENOENT;
    }
    return switch_to(app, true);
}

int ui_app_manager_open_root(const char *id)
{
    const board_app_desc_t *app = ui_app_manager_find(id);
    if (app == NULL) {
        return -ENOENT;
    }
    s_stack_depth = 0;
    return switch_to(app, false);
}

int ui_app_manager_back(void)
{
    const board_app_desc_t *target;

    if (s_busy) {
        return -EAGAIN;
    }
    if (s_stack_depth > 0) {
        target = s_stack[--s_stack_depth];
    } else {
        target = ui_app_manager_find("home");
        if (target == NULL || target == s_current) {
            return 0;
        }
    }
    return switch_to(target, false);
}

const char *ui_app_manager_current(void)
{
    return s_current != NULL ? s_current->id : NULL;
}

void ui_app_manager_shutdown(void)
{
    if (s_current != NULL && s_current->destroy != NULL) {
        s_current->destroy();
    }
    if (s_page != NULL) {
        lv_obj_delete(s_page);
    }
    if (s_busy_timer != NULL) {
        lv_timer_delete(s_busy_timer);
    }
    s_busy_timer = NULL;
    s_page = NULL;
    s_current = NULL;
    s_count = 0;
    s_stack_depth = 0;
    s_screen = NULL;
}
