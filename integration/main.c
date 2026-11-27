/**
 * @file main.c
 * @brief Linux 入口：初始化 LVGL、framebuffer、触摸，然后交给 Board App。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "board_app/board_app.h"
#include "board_ui/ui_perf.h"
#include "lvgl/lvgl.h"

/* 读取环境变量；为空或未设置时返回默认值。 */
static const char *getenv_default(const char *name, const char *dflt)
{
    const char *v = getenv(name);
    return (v != NULL && v[0] != '\0') ? v : dflt;
}

#if LV_USE_LINUX_FBDEV
/* 板端使用 /dev/fb0 直接输出，默认 1024×600/32bpp。 */
static void display_init(void)
{
    const char *device = getenv_default("LV_LINUX_FBDEV_DEVICE", "/dev/fb0");
    lv_display_t *disp = lv_linux_fbdev_create();
    if (disp == NULL) {
        fprintf(stderr, "failed to create fbdev display\n");
        return;
    }
    lv_linux_fbdev_set_file(disp, device);
}
#elif LV_USE_LINUX_DRM
/* DRM/KMS 后端：需要 /dev/dri/card0 权限。 */
static void display_init(void)
{
    const char *device = getenv_default("LV_LINUX_DRM_CARD", "/dev/dri/card0");
    lv_display_t *disp = lv_linux_drm_create();
    if (disp == NULL) {
        fprintf(stderr, "failed to create DRM display\n");
        return;
    }
    lv_linux_drm_set_file(disp, device, -1);
}
#elif LV_USE_SDL
/* SDL 后端仅用于桌面调试；板端不启用。 */
static void display_init(void)
{
    int width = atoi(getenv_default("LV_SDL_VIDEO_WIDTH", "1024"));
    int height = atoi(getenv_default("LV_SDL_VIDEO_HEIGHT", "600"));
    lv_sdl_window_create(width, height);
}
#else
#error "Unsupported LVGL display backend"
#endif

/* 打开触摸设备（evdev）。失败时只告警，仍可显示画面便于排查。 */
static void input_init(void)
{
    const char *device = getenv_default("LV_LINUX_INPUT_DEVICE", "/dev/input/event6");
    lv_indev_t *touch = lv_evdev_create(LV_INDEV_TYPE_POINTER, device);
    if (touch == NULL) {
        fprintf(stderr, "warning: failed to open touch device %s\n", device);
        return;
    }
    lv_indev_set_disp(touch, lv_disp_get_default());
}

int main(int argc, char **argv)
{
    (void)argc;

    lv_init();
    display_init();
    input_init();

    /* 性能探针要在 board_app_init 之前就绪：首页/启动直达页面的创建耗时
     * 也要被统计进去（BOARD_APP_PERF=1 才启用）。 */
    ui_perf_init();

    /* Board App 自己解析资源路径、初始化主题并打开首页。 */
    if (board_app_init(argv[0]) != 0) {
        fprintf(stderr, "board_app_init failed\n");
        return 1;
    }

    /* 主循环：LVGL 定时器 + 短暂休眠，避免空转占满 CPU。 */
    while (1) {
        uint32_t t0 = ui_perf_begin();
        lv_timer_handler();
        ui_perf_end(t0);
        usleep(5000);
    }
    return 0;
}
