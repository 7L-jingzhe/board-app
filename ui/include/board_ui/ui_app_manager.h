/**
 * @file ui_app_manager.h
 * @brief 应用描述符、链接器段注册与页面导航。
 *
 * 新增应用的方法：创建 app/<name>/ui/<name>_ui.c，定义一个
 * board_app_desc_t 并调用 BOARD_APP_REGISTER(desc)。无需修改任何中心文件：
 * CMake 会通配源文件，管理器会读取 board_apps 段。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#ifndef UI_APP_MANAGER_H
#define UI_APP_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board_ui/ui_icons.h"
#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *id;              /* 唯一标识，例如 "weather" */
    const char *title;           /* 顶栏 / 首页卡片标题 */
    const char *subtitle;        /* 首页卡片副标题，可为 NULL */
    const char *parent_id;       /* 顶层为 NULL，游戏条目为 "games" */
    const char *best_score_key;  /* 可选的高分存储键 */
    ui_icon_id_t icon;           /* 图元图标 id，使用 UI_ICON_GENERIC */
    uint32_t accent;             /* 0xRRGGBB，0 表示主色 */
    int order;                   /* 升序；相同则按 id 排序 */
    bool show_on_home;           /* 仅顶层应用有效 */
    lv_obj_t *(*create)(lv_obj_t *parent);
    void (*destroy)(void);
} board_app_desc_t;

#define BOARD_APP_REG_CAT_(a, b) a##b
#define BOARD_APP_REG_CAT(a, b) BOARD_APP_REG_CAT_(a, b)
#define BOARD_APP_REGISTER(desc)                                              \
    static const board_app_desc_t *const BOARD_APP_REG_CAT(board_app_reg_,    \
                                                            __LINE__)         \
        __attribute__((used, section("board_apps"))) = &(desc)

int ui_app_manager_init(lv_obj_t *screen);
int ui_app_manager_register(const board_app_desc_t *app);
size_t ui_app_manager_count(void);
const board_app_desc_t *ui_app_manager_at(size_t index);
const board_app_desc_t *ui_app_manager_find(const char *id);

int ui_app_manager_open(const char *id);
int ui_app_manager_open_root(const char *id);
int ui_app_manager_back(void);
const char *ui_app_manager_current(void);
void ui_app_manager_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_APP_MANAGER_H */
