# 接入原始 lv_port_linux（integration）

本目录放的是**本仓库之外**需要替换/新增的文件。`board-app` 是独立仓库，
但真正的程序要构建在 LVGL 官方的 [lv_port_linux](https://github.com/lvgl/lv_port_linux)
工程里（它有 `lvgl/` 子模块、fbdev/evdev 驱动和顶层构建）。

按下面三步，任何人在一份干净的 `lv_port_linux` 上都能复现本项目。

## 0. 前提

```bash
git clone https://github.com/lvgl/lv_port_linux.git
cd lv_port_linux
git submodule update --init --recursive      # 拉 lvgl/（9.2.x）
```

确认 `lvgl/` 版本是 **9.2.x**（本项目在 9.2.3 上开发；`lv_conf.h` 用了
`lv_tiny_ttf_create_data_ex`、`lv_ime_pinyin` 等 9.2 API）。

## 1. 放入 board_app

把本仓库（`board-app`）整个目录复制到 `lv_port_linux/` 下，目录名保持 `board_app`：

```text
lv_port_linux/
├── lvgl/                  ← 官方子模块，不动
├── board_app/             ← 本仓库内容
├── main.c                 ← 用本目录的 main.c 覆盖
├── CMakeLists.txt         ← 用本目录的 CMakeLists.txt 覆盖
└── lv_conf.h              ← 用本目录的 lv_conf.h 覆盖
```

```bash
cp board_app/integration/main.c        ./main.c
cp board_app/integration/CMakeLists.txt ./CMakeLists.txt
cp board_app/integration/lv_conf.h     ./lv_conf.h
```

> 这三个文件是**必须的**，缺一个都构建不出来：
> - `main.c`：原来是纯 LVGL demo 入口，现在改为 `lv_init` → fbdev/evdev →
>   `board_app_init(argv[0])` → 主循环里跑 `lv_timer_handler()`；
> - `CMakeLists.txt`：加 `add_subdirectory(board_app)`、`--whole-archive` 链接
>   board_ui/board_app（应用靠链接段自动注册）、链接 `dl`（`tls_client` 用 dlopen）、
>   以及可选的 glibc 2.27 sysroot 逻辑；
> - `lv_conf.h`：开 TinyTTF（`LV_USE_TINY_TTF`）、拼音输入法（`LV_USE_IME_PINYIN`）、
>   4 MB 内存池、`LV_FS_STDIO_CACHE_SIZE 0`（必须为 0）、关 DRM/SDL、只留 fbdev。

如果板子用 aarch64 交叉编译，再准备 sysroot（板端 glibc 2.29，用 2.27 编）：

```bash
bash board_app/tools/setup_bionic_sysroot.sh    # 默认装到 $HOME/aarch64-bionic
```

## 2. 构建

```bash
cmake -S . -B build_board_app
cmake --build build_board_app -j8
file bin/main        # => ELF 64-bit ... ARM aarch64
```

本机（x86_64）调试：去掉交叉编译器前缀直接 `cmake -S . -B build && cmake --build build`，
或直接用 `board_app/tests/preview` 的 headless 预览。

## 3. 运行

把下面这些拷到板子上（`/root/lvgl/board_apps/` 只是习惯路径，任意可写目录均可）：

```text
main
assets/          字体、拼音词库、示例照片
```

```bash
cd /root/lvgl/board_apps
export LV_LINUX_FBDEV_DEVICE=/dev/fb0
export LV_LINUX_INPUT_DEVICE=/dev/input/event6
./main
```

或用一键脚本：`bash board_app/tools/deploy.sh`（见 `board_app/docs/06-build-deploy.md`）。

## 这些文件是怎么改的（便于评审 diff）

| 文件 | 相对上游的改动 |
|---|---|
| `main.c` | 只加了 `#include "board_app/board_app.h"`、`board_app_init(argv[0])`、`board_app_deinit()`、以及 perf 探针调用；framebuffer/evdev 初始化与主循环结构保持不变 |
| `CMakeLists.txt` | 保留上游的 lvgl 构建与 `main` 目标；新增 `add_subdirectory(board_app)`、`--whole-archive` 链接、`-ldl`、可选 sysroot、`BUILD_SHARED_LIBS=OFF` 静态链 LVGL |
| `lv_conf.h` | 上游基础上改：`LV_COLOR_DEPTH 32`、`LV_MEM_SIZE 4MB`、`LV_USE_OS LV_OS_PTHREAD`、`LV_USE_LINUX_FBDEV 1`（DRM/SDL 关）、`LV_USE_TINY_TTF 1` + `LV_TINY_TTF_FILE_SUPPORT 1`、`LV_USE_IME_PINYIN 1` + `LV_IME_PINYIN_USE_K9_MODE 0`、`LV_FS_STDIO_CACHE_SIZE 0`、字体相关开关 |

> 想让别人看到完整 diff：在 `lv_port_linux` 里先 `git stash` 或对比上游 tag，
> 再 `git diff -- main.c CMakeLists.txt lv_conf.h`。本目录的三个文件就是"改完之后"的版本。

## 还需要注意

- **`lvgl/` 里的 CMakeLists 有一处改动**：把 `project(... LANGUAGES C CXX ASM)`
  改成 `C ASM`（板端没有 C++ 交叉编译器，ThorVG 也用不到）。这属于上游文件，
  这里只做说明，不复制——升级 LVGL 后需要重新改一次：
  `sed -i 's/LANGUAGES C CXX ASM/LANGUAGES C ASM/' lvgl/CMakeLists.txt`
- 顶层 `lv_conf.h` 的路径必须能被 `lvgl/` 找到（上游默认在工程根）；
  本项目把它放在根目录，`lvgl/CMakeLists.txt` 里通过 `LV_CONF_PATH` 或默认查找生效。
- 如果不想覆盖上游 `main.c`，也可以只把 `board_app_init` 调用加进你已有的入口，
  但 fbdev/evdev 的初始化顺序要一致（先 `lv_init` 再注册显示/输入，最后 `board_app_init`）。
