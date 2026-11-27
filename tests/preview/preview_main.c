/**
 * @file preview_main.c
 * @brief 仅主机端的无头预览：把 UI 渲染到内存帧缓冲。
 *
 * 构建与运行（在 WSL 中，从 lv_port_linux 工程根目录执行）：
 *   cmake -S board_app/tests/preview -B build_preview
 *   cmake --build build_preview -j8
 *   BOARD_APP_ROOT=$PWD/board_app ./build_preview/board_preview
 *
 * 截图写入 $PREVIEW_OUT（默认 /tmp/board_preview）。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "board_app/board_app.h"
#include "board_ui/ui_app_manager.h"
#include "lvgl/lvgl.h"
#include "stb_image_write.h"

#define W 1024
#define H 600

static uint8_t g_fb[W * H * 4] __attribute__((aligned(64)));
static lv_display_t *g_disp;
static const char *g_out = "/tmp/board_preview";

static uint32_t tick_cb(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)area;
    (void)px_map;
    /* 直接渲染模式：g_fb 中已经包含像素。 */
    lv_display_flush_ready(disp);
}

static void pump(int ms)
{
    int64_t end = (int64_t)time(NULL) * 1000 + ms;
    while ((int64_t)time(NULL) * 1000 < end) {
        lv_timer_handler();
        usleep(5000);
    }
}

static void snapshot(const char *name)
{
    lv_refr_now(g_disp);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.png", g_out, name);
    uint8_t *rgba = malloc((size_t)W * H * 4);
    if (rgba == NULL) {
        return;
    }
    for (int i = 0; i < W * H; i++) {
        /* LVGL ARGB8888 的内存顺序为 B,G,R,A。 */
        rgba[i * 4 + 0] = g_fb[i * 4 + 2];
        rgba[i * 4 + 1] = g_fb[i * 4 + 1];
        rgba[i * 4 + 2] = g_fb[i * 4 + 0];
        rgba[i * 4 + 3] = 255;
    }
    if (stbi_write_png(path, W, H, 4, rgba, W * 4) == 0) {
        fprintf(stderr, "failed to write %s\n", path);
    } else {
        printf("saved %s\n", path);
    }
    free(rgba);
}

static lv_obj_t *current_page(void)
{
    return lv_obj_get_child(lv_screen_active(), 0);
}

/* 在 buttonmatrix / keyboard 的某个按钮上模拟一次点击（发 VALUE_CHANGED）。
 * 必须按 button id 查找（内部会跳过 "\n" 换行项），直接用 map 数组下标会错位。 */
static int press_text(lv_obj_t *matrix, const char *text)
{
    if (matrix == NULL || text == NULL) {
        return -1;
    }
    for (uint32_t id = 0; id < 256; id++) {
        const char *t = lv_buttonmatrix_get_button_text(matrix, id);
        if (t == NULL) {
            break;
        }
        if (strcmp(t, text) == 0) {
            lv_buttonmatrix_set_selected_button(matrix, id);
            lv_obj_send_event(matrix, LV_EVENT_VALUE_CHANGED, NULL);
            return 0;
        }
    }
    return -1;
}

/* 在 page 的子树里递归找第一个 lv_keyboard 对象。 */
static lv_obj_t *find_keyboard(lv_obj_t *root, int depth)
{
    if (root == NULL || depth > 6) {
        return NULL;
    }
    if (lv_obj_has_class(root, &lv_keyboard_class)) {
        return root;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
        lv_obj_t *found = find_keyboard(lv_obj_get_child(root, i), depth + 1);
        if (found != NULL) {
            return found;
        }
    }
    return NULL;
}

/* 在聊天页右侧键盘上敲 "ni"，触发拼音候选条。 */
/** 在子树里找“文本等于 text 的按钮”，并模拟点击（用于验证按钮状态切换）。 */
static bool click_button_by_text(lv_obj_t *root, const char *text)
{
    uint32_t i;
    uint32_t n;

    if (root == NULL || text == NULL) {
        return false;
    }
    n = lv_obj_get_child_count(root);
    for (i = 0; i < n; i++) {
        lv_obj_t *child = lv_obj_get_child(root, i);
        uint32_t j;
        uint32_t cn;

        /* 一个“按钮”在这里的特征：自身有 label 子对象，且文本匹配 */
        cn = lv_obj_get_child_count(child);
        for (j = 0; j < cn; j++) {
            lv_obj_t *lbl = lv_obj_get_child(child, j);
            if (lv_obj_check_type(lbl, &lv_label_class)) {
                const char *t = lv_label_get_text(lbl);
                if (t != NULL && strcmp(t, text) == 0) {
                    lv_obj_send_event(child, LV_EVENT_CLICKED, NULL);
                    return true;
                }
            }
        }
        if (click_button_by_text(child, text)) {
            return true;
        }
    }
    return false;
}
static void type_pinyin(lv_obj_t *page, const char *keys)
{
    lv_obj_t *kb;
    if (page == NULL) {
        return;
    }
    kb = find_keyboard(page, 0);
    if (kb == NULL) {
        fprintf(stderr, "DBG keyboard not found on chat page\n");
        return;
    }
    for (const char *p = keys; *p != '\0'; p++) {
        char key[2] = { *p, '\0' };
        if (press_text(kb, key) != 0) {
            fprintf(stderr, "DBG key not found: '%c'\n", *p);
        }
    }
}

static void click_child(lv_obj_t *parent, int index)
{
    if (parent == NULL) {
        return;
    }
    lv_obj_t *child = lv_obj_get_child(parent, index);
    if (child != NULL) {
        lv_obj_send_event(child, LV_EVENT_CLICKED, NULL);
    }
}

int main(void)
{
    const char *env = getenv("PREVIEW_OUT");
    if (env != NULL && env[0] != '\0') {
        g_out = env;
    }
    mkdir(g_out, 0755);

    lv_init();
    lv_tick_set_cb(tick_cb);
    g_disp = lv_display_create(W, H);
    lv_display_set_color_format(g_disp, LV_COLOR_FORMAT_ARGB8888);
    lv_display_set_buffers(g_disp, g_fb, NULL, sizeof(g_fb),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(g_disp, flush_cb);
    lv_display_set_default(g_disp);

    if (board_app_init("board_preview") != 0) {
        fprintf(stderr, "board_app_init failed\n");
        return 1;
    }
    pump(600);
    snapshot("preview-01-home");

    /* 导航器在转场后的 ~220ms 内拒绝再次切页；先跑一会儿再打开聊天页。 */
    pump(600);
    {
        int rc = -EAGAIN;
        for (int attempt = 0; attempt < 4 && rc != 0; attempt++) {
            rc = ui_app_manager_open("chat");
            if (rc != 0) {
                pump(400);
            }
        }
    }
    if (ui_app_manager_current() != NULL &&
        strcmp(ui_app_manager_current(), "chat") == 0) {
        pump(400);
        snapshot("preview-08-chat");
        /* 在虚拟键盘上敲 "ni"，展示拼音候选条。 */
        type_pinyin(current_page(), "ni");
        pump(400);
        snapshot("preview-08b-chat-pinyin");
    }

    /* 聊天室：需要本地服务器；BOARD_APP_COMM_AUTOLOGIN 会自动登录并进会话页。 */
    {
        int rc = -EAGAIN;
        for (int attempt = 0; attempt < 4 && rc != 0; attempt++) {
            rc = ui_app_manager_open("comm");
            if (rc != 0) {
                pump(400);
            }
        }
        if (rc == 0) {
            pump(1200);
            snapshot("preview-09-comm-login");
            if (getenv("BOARD_APP_COMM_AUTOLOGIN") != NULL) {
                pump(2200);
                snapshot("preview-09b-comm-chat");
            }
        }
    }

    /* 系统账户页（登录后：账号 + 好友管理） */
    {
        int rc = -EAGAIN;
        for (int attempt = 0; attempt < 4 && rc != 0; attempt++) {
            rc = ui_app_manager_open("account");
            if (rc != 0) {
                pump(400);
            }
        }
        if (rc == 0) {
            pump(1200);
            snapshot("preview-12-account");
        }
    }

    /* 小游戏列表（围棋挂在这里） */
    {
        int rc = -EAGAIN;
        for (int attempt = 0; attempt < 4 && rc != 0; attempt++) {
            rc = ui_app_manager_open("games");
            if (rc != 0) {
                pump(400);
            }
        }
        if (rc == 0) {
            pump(600);
            snapshot("preview-10-games-with-weiqi");
        }
    }

    /* 联网围棋：登录态来自 comm_session（上面的聊天室已自动登录） */
    {
        int rc = -EAGAIN;
        for (int attempt = 0; attempt < 4 && rc != 0; attempt++) {
            rc = ui_app_manager_open("weiqi");
            if (rc != 0) {
                pump(400);
            }
        }
        if (rc == 0) {
            pump(900);
            snapshot("preview-11-weiqi-lobby");
            /* 验证路数按钮能切换高亮：点 9 路 */
            if (click_button_by_text(current_page(), "9 路")) {
                pump(400);
                snapshot("preview-11c-weiqi-size9");
                click_button_by_text(current_page(), "13 路");
                pump(300);
            }
            pump(3000);
            snapshot("preview-11b-weiqi-game");
        }
    }

    if (ui_app_manager_open("weather") == 0) {
        pump(4000);
        snapshot("preview-02-weather");
        /* 打开城市选择器并选择第一个城市。 */
        lv_obj_t *page = current_page();
        lv_obj_t *topbar = page != NULL ? lv_obj_get_child(page, 0) : NULL;
        lv_obj_t *actions = topbar != NULL ? lv_obj_get_child(topbar, 3) : NULL;
        click_child(actions, 0);
        pump(300);
        snapshot("preview-02b-city");
        lv_obj_t *overlay = page != NULL ? lv_obj_get_child(page, lv_obj_get_child_count(page) - 1) : NULL;
        lv_obj_t *card = overlay != NULL ? lv_obj_get_child(overlay, 0) : NULL;
        lv_obj_t *content = card != NULL ? lv_obj_get_child(card, 1) : NULL;
        click_child(content, 1); /* 上海，另一个城市 */
        pump(2500);
        snapshot("preview-02c-weather-selected");
    }
    if (ui_app_manager_open("gallery") == 0) {
        pump(4000);
        snapshot("preview-03-gallery");
        /* 以编程方式打开第一个缩略图，以渲染编辑器。 */
        lv_obj_t *page = current_page();
        lv_obj_t *content = page != NULL ? lv_obj_get_child(page, 1) : NULL;
        lv_obj_t *grid = content != NULL ? lv_obj_get_child(content, 0) : NULL;
        lv_obj_t *card = NULL;
        for (uint32_t c = 0; grid != NULL && c < lv_obj_get_child_count(grid); c++) {
            lv_obj_t *child = lv_obj_get_child(grid, c);
            if (child != NULL && lv_obj_get_child_count(child) >= 2) {
                card = child;
                break;
            }
        }
        if (card != NULL) {
            lv_obj_send_event(card, LV_EVENT_CLICKED, NULL);
            pump(1500);
            snapshot("preview-03b-editor");
            /* 渲染完整一帧后再截图，确保图片/工具按钮已经绘制。 */
            lv_refr_now(g_disp);
            fprintf(stderr, "DBG editor children=%d\n",
                    (int)lv_obj_get_child_count(lv_obj_get_child(page, 1)));
            snapshot("preview-03b2-editor-rendered");
            /* 右旋、灰度、撤销，然后保存。 */
            lv_obj_t *editor = page != NULL ? lv_obj_get_child(page, 1) : NULL;
            lv_obj_t *tools = editor != NULL ? lv_obj_get_child(editor, 1) : NULL;
            click_child(tools, 2);
            pump(600);
            click_child(tools, 4);
            pump(600);
            click_child(tools, 0);
            pump(600);
            snapshot("preview-03c-edited");

            /* 走一遍调整与裁剪弹窗（先应用再关闭）。 */
            click_child(tools, 5);
            pump(600);
            snapshot("preview-03d-adjust");
            {
                lv_obj_t *overlay = lv_obj_get_child(page, lv_obj_get_child_count(page) - 1);
                lv_obj_t *dlg = overlay != NULL ? lv_obj_get_child(overlay, 0) : NULL;
                lv_obj_t *body = dlg != NULL ? lv_obj_get_child(dlg, 1) : NULL;
                lv_obj_t *row = body != NULL ? lv_obj_get_child(body, 4) : NULL;
                click_child(row, 1); /* 应用 */
            }
            pump(900);
            tools = editor != NULL ? lv_obj_get_child(editor, 1) : NULL;
            click_child(tools, 6);
            pump(600);
            snapshot("preview-03e-crop");
            {
                lv_obj_t *overlay = lv_obj_get_child(page, lv_obj_get_child_count(page) - 1);
                lv_obj_t *dlg = overlay != NULL ? lv_obj_get_child(overlay, 0) : NULL;
                lv_obj_t *body = dlg != NULL ? lv_obj_get_child(dlg, 1) : NULL;
                lv_obj_t *row = body != NULL ? lv_obj_get_child(body, 2) : NULL;
                click_child(row, 1); /* 裁剪 */
            }
            pump(900);
            snapshot("preview-03f-cropped");

            lv_obj_t *bar = page != NULL ? lv_obj_get_child(page, 0) : NULL;
            lv_obj_t *acts = bar != NULL ? lv_obj_get_child(bar, 3) : NULL;
            click_child(acts, 1); /* 保存 */
            pump(500);
        }
    }
    if (ui_app_manager_open("games") == 0) {
        pump(600);
        snapshot("preview-04-games");
    }
    if (ui_app_manager_open("g2048") == 0) {
        pump(600);
        snapshot("preview-05-2048");
        /* 用屏幕方向键向上移动，然后暂停。 */
        lv_obj_t *page = current_page();
        lv_obj_t *left = page != NULL ? lv_obj_get_child(page, 1) : NULL;
        lv_obj_t *pad = left != NULL ? lv_obj_get_child(left, 2) : NULL;
        click_child(pad, 0);
        pump(400);
        snapshot("preview-05c-2048-move");
        lv_obj_t *bar = page != NULL ? lv_obj_get_child(page, 0) : NULL;
        lv_obj_t *acts = bar != NULL ? lv_obj_get_child(bar, 3) : NULL;
        click_child(acts, 0); /* 暂停 */
        pump(300);
        snapshot("preview-05b-2048-pause");
    }
    if (ui_app_manager_open("snake") == 0) {
        pump(600);
        snapshot("preview-06-snake");
    }
    if (ui_app_manager_open("breakout") == 0) {
        pump(900);
        snapshot("preview-07-breakout");
        /* 发射小球并渲染一帧运行画面。 */
        click_child(current_page(), 4);
        pump(1200);
        snapshot("preview-07b-breakout-playing");
    }

    board_app_deinit();
    return 0;
}
