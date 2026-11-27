/**
 * @file board_app.c
 * @brief 应用启动引导：路径、存储、主题与首页。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include "board_app/board_app.h"

#include <stdlib.h>
#include <string.h>

#include "board_core/board_log.h"
#include "board_core/board_storage.h"
#include "board_ui/ui_app_manager.h"
#include "board_ui/ui_comm_session.h"
#include "board_ui/ui_ime.h"
#include "board_ui/ui_theme.h"
#include "lvgl/lvgl.h"

static board_paths_t s_paths;
static int s_ready;

static void apply_log_level(void)
{
    const char *env = getenv("BOARD_APP_LOG_LEVEL");
    if (env == NULL) {
        return;
    }
    if (strcmp(env, "debug") == 0) {
        board_log_set_level(BOARD_LOG_DEBUG);
    } else if (strcmp(env, "info") == 0) {
        board_log_set_level(BOARD_LOG_INFO);
    } else if (strcmp(env, "warn") == 0) {
        board_log_set_level(BOARD_LOG_WARN);
    } else if (strcmp(env, "error") == 0) {
        board_log_set_level(BOARD_LOG_ERROR);
    }
}

/**
 * 开机自动登录：延迟 1.5 秒执行，避免服务器没开时连接超时拖慢启动。
 * 账号来自上次登录成功后保存的本地记录（comm_session_remember）。
 */
static void autologin_timer_cb(lv_timer_t *timer)
{
    lv_timer_delete(timer);
    comm_session_auto_login();
}

int board_app_init(const char *argv0)
{
    int ret;

    /* 必须先解析路径再初始化存储/主题：两者都要从 assets/data 读取。 */
    apply_log_level();
    if (board_paths_init(&s_paths, argv0) != 0) {
        BOARD_LOGE("app", "cannot resolve asset/data root; set BOARD_APP_ROOT");
        return -1;
    }
    if (board_paths_ensure_data_dirs(&s_paths) != 0) {
        BOARD_LOGE("app", "cannot create data directories under %s", s_paths.data);
        return -1;
    }
    BOARD_LOGI("app", "root=%s data=%s", s_paths.root, s_paths.data);

    if (board_storage_init(s_paths.settings) != 0) {
        BOARD_LOGW("app", "settings load failed; using defaults");
    }

    ui_theme_init(s_paths.font);
    /* ui_app_manager_init 会扫描由各应用填充的链接器段。 */
    ui_app_manager_init(lv_screen_active());
    /* 词库读取/解析放在后台线程，与首页构建并行；打开聊天页时通常已就绪。 */
    ui_ime_preload();
    /* 在创建首个页面前先把上下文标记为就绪：通过 BOARD_APP_START 打开的
     * 应用可能会在其 create() 中调用 board_app_paths()。 */
    s_ready = 1;
    /* 可选的 kiosk/测试入口：BOARD_APP_START=weather 会直接打开该页面。
     * 未知的 id 会回退到首页。 */
    const char *start = getenv("BOARD_APP_START");
    const char *start_id = (start != NULL && ui_app_manager_find(start) != NULL)
                               ? start
                               : "home";
    /* 先建会话（历史目录、收包线程），再打开页面 */
    if (comm_session_init(&s_paths) != 0) {
        BOARD_LOGW("app", "comm session init failed; 聊天/围棋可能不可用");
    }

    ret = ui_app_manager_open_root(start_id);
    if (ret != 0) {
        s_ready = 0;
        BOARD_LOGE("app", "cannot open home page (%d)", ret);
        return -1;
    }

    /* 1.5 秒后自动登录上次的账号（会话已在上面初始化） */
    {
        lv_timer_t *t = lv_timer_create(autologin_timer_cb, 1500, NULL);
        if (t != NULL) {
            lv_timer_set_repeat_count(t, 1);
        }
    }
    return 0;
}

void board_app_deinit(void)
{
    if (!s_ready) {
        return;
    }
    (void)board_storage_save();
    ui_app_manager_shutdown();
    board_storage_shutdown();
    ui_theme_deinit();
    s_ready = 0;
}

const board_paths_t *board_app_paths(void)
{
    return s_ready ? &s_paths : NULL;
}
