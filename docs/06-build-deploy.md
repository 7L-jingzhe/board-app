# 构建、部署与运行

> 文档编号：06
> 状态：与实现同步（2026-11-27）
> 目标：从一台 Windows 开发机 + WSL，稳定产出可放到 aarch64 开发板运行的 `main`。

## 1. 构建环境（已探测）

| 项 | 实测值 | 说明 |
|---|---|---|
| WSL 发行版 | `Ubuntu-26.04` | `wsl -d Ubuntu-26.04` |
| WSL 用户 | `jingzhe` | 目标目录 `/home/jingzhe/lvgl/lv_port_linux-release-v9.2` |
| 交叉编译器 | `aarch64-linux-gnu-gcc` 15.2.0 | 顶层 CMake 已指定 |
| CMake | 4.2.3 | 支持 `-S/-B`、`cmake --build` |
| CPU/内存 | 24 核 / 7.6 GB | 建议 `-j8`，避免 LVGL 编译 OOM |
| pkg-config | **未安装** | 本工程不依赖 pkg-config |
| SDL2 / libcurl / libpng | **未安装** | 本工程不使用；图片用 stb，HTTP 用 socket |
| FreeType 静态库 | **不存在** | 本工程关闭 `LV_USE_FREETYPE`，不链接 |
| 目标目录 | 已存在旧工程 | 保留旧 `ui/`、`src/`；新工程独立 `board_app/` |

> 不要直接在 `/mnt/d/...` 上编译。9p 文件系统会让 LVGL 这种大量小文件的构建慢数倍。正确做法：源码在 D: 编辑，`rsync` 到 WSL 的 ext4 目录，在 ext4 构建。

## 2. 宿主机目录与 WSL 目录

```text
Windows:
  D:\data\code-files\c\embedded-systems-study\lvgl\lv_port_linux-release-v9.2\
    board_app\                 ← 本项目源码/文档
    CMakeLists.txt             ← 需要接入 board_app
    main.c                     ← 改为调用 board_app_init
    lv_conf.h                  ← 字体/内存/驱动配置

WSL:
  /home/jingzhe/lvgl/lv_port_linux-release-v9.2/
    lvgl/                      ← 已有，不覆盖
    freetype-2.13.3/           ← 已有，不使用
    board_app/                 ← 从 D: 同步
    CMakeLists.txt             ← 从 D: 同步（先备份）
    main.c                     ← 从 D: 同步（先备份）
    lv_conf.h                  ← 从 D: 同步（先备份）
    build_board_app/           ← 新建，构建产物
```

### 2.1 首次同步

在 WSL 中执行（把 D: 路径按实际修改）：

```bash
set -euo pipefail
SRC=/mnt/d/data/code-files/c/embedded-systems-study/lvgl/lv_port_linux-release-v9.2
DST=/home/jingzhe/lvgl/lv_port_linux-release-v9.2

# 1) 备份将被覆盖的顶层文件（只备份一次，带时间戳）
ts=$(date +%Y%m%d_%H%M%S)
for f in CMakeLists.txt main.c lv_conf.h; do
  [ -f "$DST/$f" ] && cp -a "$DST/$f" "$DST/$f.bak_$ts"
done

# 2) 同步新项目（--delete 仅作用于 board_app 子目录，风险可控）
rsync -a --delete "$SRC/board_app/" "$DST/board_app/"

# 3) 同步顶层接入文件
rsync -a "$SRC/CMakeLists.txt" "$SRC/main.c" "$SRC/lv_conf.h" "$DST/"

echo "sync done: $DST"
```

说明：

- `--delete` 只用于 `board_app/`，不会删除 WSL 里的 `lvgl/`、旧 `ui/`、`freetype/`。
- 首次同步前确认 WSL 的 `board_app/` 不存在或为空；如果存在旧版本，先自行备份。
- 如果 `rsync` 未安装：`sudo apt install rsync`，或用 `cp -a` 逐项复制。

### 2.2 后续同步（改代码后）

```bash
SRC=/mnt/d/data/code-files/c/embedded-systems-study/lvgl/lv_port_linux-release-v9.2
DST=/home/jingzhe/lvgl/lv_port_linux-release-v9.2
rsync -a --delete "$SRC/board_app/" "$DST/board_app/"
# 只有改顶层文件时才需要：
# rsync -a "$SRC/CMakeLists.txt" "$SRC/main.c" "$SRC/lv_conf.h" "$DST/"
```

`tools/sync_to_wsl.sh` 会封装上述命令（实现阶段提供），默认只同步 `board_app/`，顶层文件需显式 `--with-top`。

## 3. 顶层接入（实现阶段会完成）

### 3.1 顶层 `CMakeLists.txt` 的目标结构

```cmake
cmake_minimum_required(VERSION 3.16)
project(lvgl C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

# 交叉编译（与现有工程一致）
set(CMAKE_C_COMPILER "aarch64-linux-gnu-gcc")

add_subdirectory(lvgl)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)   # self-contained main
set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)

add_subdirectory(lvgl)
target_include_directories(lvgl PUBLIC ${PROJECT_SOURCE_DIR})
# 不再 add_subdirectory(ui) / src；旧目录保留但不参与构建

# board_app 内部用 CONFIGURE_DEPENDS glob 自动收集 app/*/core/src 与 app/*/ui。
# 新增应用文件夹后建议重跑一次 cmake -S . -B build_board_app 再构建。
add_subdirectory(board_app)

add_executable(main main.c mouse_cursor_icon.c)
# Auto-registration uses a linker section; whole-archive keeps every app object.
target_link_libraries(main PRIVATE
  "-Wl,--whole-archive" board_ui board_app "-Wl,--no-whole-archive"
  lvgl m pthread)
```

- `board_ui` 使用链接段 `board_apps` 做自动注册，因此必须以 `--whole-archive` 链入；否则未被引用的应用对象会被静态库丢弃。
- 不链接 `lvgl::demos`、`lvgl::thorvg`、`freetype`；本工程关闭 ThorVG/Lottie/Vector Graphic。
- `lvgl/CMakeLists.txt` 只做一处构建适配：`project(lvgl LANGUAGES C ASM ...)`（上游默认 `C CXX ASM`）。WSL 没有 aarch64 C++ 编译器，且本工程不使用 ThorVG 的 C++ 源码；LVGL 业务源码不受影响。升级 LVGL 时需重新应用这一行。

### 3.2 `lv_conf.h` 必要修改

| 配置 | 改为 | 原因 |
|---|---|---|
| `LV_MEM_SIZE` | `(4 * 1024 * 1024)` | 1024×600 多控件/图片描述符更稳 |
| `LV_USE_FREETYPE` | `0` | 不构建 FreeType 静态库 |
| `LV_TINY_TTF` | `1`（已有） | 运行时加载 TTF |
| `LV_TINY_TTF_FILE_SUPPORT` | `1` | 允许从文件创建字体（**本项目不用**：改成整块读进内存，见 4.2 第 4 步） |
| `LV_TINY_TTF_CACHE_GLYPH_CNT` | `512` | 中文页面字形较多 |
| `LV_USE_FS_STDIO` | `1`（已有） | TinyTTF 通过 `A:` 读取字体 |
| `LV_FS_STDIO_LETTER` | `'A'`（已有） | 字体路径 `A:assets/fonts/...` |
| `LV_COLOR_DEPTH` | `32`（已有） | fbdev 32bpp |
| `LV_USE_LINUX_FBDEV` | `1`（已有） | 板端显示 |
| `LV_USE_SDL` | `0`（已有） | 交叉编译不需要 SDL |
| `LV_USE_LOTTIE` | `0` | 依赖 ThorVG；本工程不用动画资源 |
| `LV_USE_VECTOR_GRAPHIC` | `0` | 不引入 ThorVG 的 C++ 依赖 |
| `LV_USE_THORVG_INTERNAL` | `0` | 同上；同时让顶层 CMake 保持 C-only |
| `LV_USE_ASSERT_STYLE` | `0` | 发布版关闭；样式检查非常慢 |
| `LV_USE_ASSERT_MEM_INTEGRITY` | `0` | 发布版关闭；每次分配全堆校验 |
| `LV_USE_ASSERT_OBJ` | `0` | 发布版关闭；每次对象访问校验 |
| `LV_USE_DEMO_WIDGETS` | `0` | 减少编译体积/时间；不影响功能 |

> 修改 `lv_conf.h` 后必须全量重编 `lvgl`，不能只增量链接。

### 3.3 `main.c` 结构

```c
#include "lvgl/lvgl.h"
#include "board_app/board_app.h"

static void display_init(void) { /* lv_linux_fbdev_create + set_file */ }
static void input_init(void)   { /* lv_evdev_create + set_disp；节点支持环境变量 */ }

int main(int argc, char **argv)
{
    lv_init();
    display_init();
    input_init();

    if (board_app_init(argv[0]) != 0) {
        fprintf(stderr, "board_app_init failed\n");
        return 1;
    }

    while (1) {
        lv_timer_handler();
        usleep(5000);
    }
}
```

- 保留原工程的 `getenv_default()`、fbdev/evdev 条件编译。
- 触摸节点改为 `getenv("LV_LINUX_INPUT_DEVICE") ?: "/dev/input/event6"`，便于不同板子。
- 不再调用 `test_main()`、`ui_init()`、`lv_demo_*`。

## 4. 字体准备

项目使用 **Noto Sans SC 子集 TTF + LVGL TinyTTF**，避免 FreeType 依赖。字体文件放 `assets/fonts/NotoSansSC-Board.ttf`，许可证 `OFL.txt` 同目录。

### 4.1 为什么需要子集

完整 Noto Sans SC 约 8–10 MB；子集只保留源码里出现的字符（含 ASCII + UI 中文 + 城市名），当前约 2.2 MB / 6899 字形。**字体整块读进内存**（不用流式：`LV_FS_STDIO_CACHE_SIZE` 必须为 0，否则 TinyTTF 会死循环，实测流式打开一页要 44 秒）。

### 4.2 生成步骤（WSL）

```bash
# 1) 安装 fonttools（只需一次；WSL 可能没有 pip，见下）
python3 -m pip install --user --break-system-packages fonttools

# 若提示 No module named pip：
curl -fsSL -o /tmp/get-pip.py https://bootstrap.pypa.io/get-pip.py
python3 /tmp/get-pip.py --user --break-system-packages
python3 -m pip install --user --break-system-packages fonttools

# 2) 下载一个开源中文字体（任选其一；必须是 TrueType 轮廓）
#    Noto Sans SC 可变字体（noto-cjk）
curl -L -o /tmp/NotoSansSC-VF.ttf \
  https://raw.githubusercontent.com/notofonts/noto-cjk/main/Sans/Variable/TTF/Subset/NotoSansSC-VF.ttf

# 3) 扫描当前源码，更新字符表
python3 board_app/tools/gen_chars.py

# 4) 生成子集
python3 board_app/tools/gen_font_subset.py \
  --input /tmp/NotoSansSC-VF.ttf \
  --chars board_app/assets/fonts/chars.txt \
  --output board_app/assets/fonts/NotoSansSC-Board.ttf
```

- `gen_chars.py` 扫描 `main_page/`、`app/`、`ui/` 中的中文字符，并补充 ASCII、常用标点、天气现象、城市名；输出 `chars.txt`。
- `gen_font_subset.py` 实例化可变字体（wght=400）→ `pyftsubset` → 输出 TTF；保持 OFL 许可。
- 若下载源不可用：使用任意含 CJK 的 OFL 字体（如 Source Han Sans、思源黑体），脚本只要求 TTF/OTF + fonttools 可读。
- **不要**把 Windows 系统字体（微软雅黑/黑体）提交到仓库；版权不允许。

### 4.3 没有字体时

`ui_theme_init(font_path)` 会检测字体文件：

1. 存在且 TinyTTF 创建成功 → 使用 `NotoSansSC-Board.ttf`（18/22/26/34/48）。
2. 失败 → 回退 `lv_font_simsun_16_cjk`（LVGL 内置，需 `LV_FONT_SIMSUN_16_CJK 1`）。
3. 再失败 → `lv_font_montserrat_14`（仅英文，UI 会出现方框，必须修复）。

回退时日志打印 `WARN`，不崩溃；交付前必须确认主字体加载成功。

## 5. 构建（WSL，ext4）

### 5.0 目标 ABI 与 sysroot

实测开发板：**RK356X，Buildroot 2018.02，glibc 2.29，aarch64，1024×600**。WSL 的
`aarch64-linux-gnu-gcc` 自带 glibc 2.38，直接编出的动态程序在板上会报：

```text
/lib/libc.so.6: version `GLIBC_2.33' not found
```

因此工程优先链接一个 **glibc 2.27（Ubuntu 18.04 bionic）aarch64 sysroot**，生成普通的
动态 `main`；2.27 的符号在板上 2.29 上可用，`getaddrinfo` 也走板端 NSS，天气正常。

```bash
# 一次性准备 sysroot（不需要 root；默认 $HOME/aarch64-bionic）
bash board_app/tools/setup_bionic_sysroot.sh

# 之后 CMake 自动检测该目录；产物 NEEDED 为 libc/libm/libpthread
rm -rf build_board_app
cmake -S . -B build_board_app -DCMAKE_BUILD_TYPE=Release
cmake --build build_board_app -j8
```

如果 sysroot 不存在，CMake 会回退到 `-static`。静态程序能启动，但 static glibc 的
`getaddrinfo` 在板上不可靠（会提示需要运行时的 glibc 共享库），天气可能一直停在初始状态；
所以**板端天气验收必须用 sysroot 动态构建**。

新机器/新板子如果 glibc 版本不同，改 `-DBOARD_APP_SYSROOT=/path/to/sysroot` 指向对应
sysroot；不要用板子不支持的 glibc 头文件/库。

### 5.1 构建命令

```bash
cd /home/jingzhe/lvgl/lv_port_linux-release-v9.2

# 干净构建（首次或改了 lv_conf.h / CMake 后）
rm -rf build_board_app
cmake -S . -B build_board_app -DCMAKE_BUILD_TYPE=Release
cmake --build build_board_app -j8
```

预期：

- 产物：`bin/main`（aarch64 ELF）。
- 顶层设置 `EXECUTABLE_OUTPUT_PATH=${PROJECT_SOURCE_DIR}/bin`，因此产物固定为 `bin/main`；当前构建已确认。
- 顶层 `BUILD_SHARED_LIBS=OFF`：LVGL/board_core/board_ui 都是静态库；`main` 仅动态依赖板端 libc/libm/libpthread。

验证：

```bash
file bin/main
# 期望：ELF 64-bit LSB pie executable, ARM aarch64 ...
readelf -h bin/main | grep Machine
# 期望：AArch64
readelf -d bin/main | grep NEEDED
# sysroot 构建期望：libpthread.so.0 / libm.so.6 / libc.so.6 / ld-linux-aarch64.so.1
```

### 5.2 构建选项

| 选项 | 默认 | 说明 |
|---|---|---|
| `BOARD_APP_SYSROOT` | `$HOME/aarch64-bionic` | 旧 glibc aarch64 sysroot；存在则动态链接 |
| `BOARD_APP_STATIC` | sysroot 存在时 OFF | 无 sysroot 时自动 ON，回退静态链接 |
| `CMAKE_BUILD_TYPE` | Release | `-O2 -g`；调试用 Debug |

### 5.3 编译速度

- LVGL 文件多，首次全量约几分钟；后续只编 `board_app` 很快。
- `-j8` 在 7.6 GB 内存上较稳；`-j24` 可能因单文件内存峰值 OOM。
- 不要在 `/mnt/d` 构建；不要在 WSL 里挂载 Windows 构建目录。

## 6. 主机测试（x86_64，不需要板子）

```bash
cd /home/jingzhe/lvgl/lv_port_linux-release-v9.2
cmake -S board_app/tests/host -B build_host_tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build_host_tests -j8
ctest --test-dir build_host_tests --output-on-failure
```

- 主机测试只链接 `board_core`（+ 测试框架），不链接 LVGL。
- 覆盖：2048/贪吃蛇/打砖块规则、天气 JSON 解析、HTTP 响应解析（本地内存样本）、图片旋转/翻转/灰度/裁剪/保存、storage 读写。
- 建议加 ASan/UBSan：`-DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"`。

### 6.1 主机 UI 预览（无板子时）

项目提供一个 headless LVGL 预览，把真实 UI 渲染到内存 framebuffer 并导出 PNG。它不依赖 SDL，也不会链接 fbdev/evdev：

```bash
cd /home/jingzhe/lvgl/lv_port_linux-release-v9.2
cmake -S board_app/tests/preview -B build_preview -DCMAKE_BUILD_TYPE=Debug
cmake --build build_preview -j8
BOARD_APP_ROOT=$PWD/board_app \
PREVIEW_OUT=$PWD/board_app/docs/screenshots \
./build_preview/board_preview
```

- 输出：主页、天气、城市弹窗、相册网格、编辑器、调整/裁剪弹窗、游戏中心、4 款游戏、聊天室/账户/围棋、暂停/发球等截图。
- 预览会真实调用 UI 回调（切城市、旋转/灰度/撤销/裁剪/保存、2048 方向键、打砖块发球），用于快速回归。
- 预览不是目标构建的一部分；发布包不包含 `build_preview/` 与 `tests/preview/` 的产物。

## 7. 部署到开发板

### 7.0 一句话版本

```bash
# 在 WSL 里（需要 aarch64 交叉编译器 + cmake + ssh）
bash board_app/tools/deploy.sh --with-server
```

脚本做 5 件事：同步源码到 WSL → 主机单测 → aarch64 交叉编译 → 上传 → 重启自检。
参数都能用环境变量覆盖：

```bash
BOARD_IP=192.168.64.216 BOARD_PASS=1 BOARD_DIR=/root/lvgl/board_apps \
    bash board_app/tools/deploy.sh [--with-server] [--skip-test] [--skip-build]
```

### 7.1 板上目录布局

```text
/root/lvgl/board_apps/
├── main                     # LVGL 客户端（aarch64，约 2.0 MB）
├── chat_server              # 聊天/围棋服务器（aarch64 静态链接，--with-server 才有）
├── chat_test_client         # 命令行测试客户端（同上）
├── assets/
│   ├── fonts/NotoSansSC-Board.ttf   # 中文字体子集（2.2 MB，整块读进内存）
│   ├── ime/pinyin_dict.txt          # 拼音词库（401 音节 / 6763 字，22 KB）
│   └── photos/                      # 相册示例图
└── data/                    # 运行时数据（自动创建）
    ├── settings.conf        # 设置 + 记住的账号（comm.user/pass/host）
    ├── cache/ edited/       # 天气缓存、相册编辑结果
    └── accounts.txt friends.txt offline/   # 服务器数据（聊天/围棋）
```

`main` 把 LVGL 静态链进来，板上**不需要** `liblvgl.so`；仍动态依赖 Buildroot 自带的
`libc.so.6` / `libm.so.6` / `libpthread.so.0`（`readelf -d bin/main | grep NEEDED` 确认）。

### 7.2 分步执行（脚本内部做的就是这些）

```bash
# ① 同步到 WSL 的 ext4 目录（比在 /mnt/d 上编译快很多）
rsync -a --delete board_app/ ~/lvgl/lv_port_linux-release-v9.2/board_app/
rsync -a --delete server/    ~/lvgl/lv_port_linux-release-v9.2/server/
rsync -a main.c lv_conf.h    ~/lvgl/lv_port_linux-release-v9.2/

# ② 主机单测（不碰板子，先保证逻辑没坏）
cmake -S board_app/tests/host -B build_host_tests && cmake --build build_host_tests -j8
(cd build_host_tests && ctest --output-on-failure)

# ③ 交叉编译（顶层 CMake，产物在 bin/main）
cmake -S . -B build_board_app && cmake --build build_board_app -j8
file bin/main       # => ELF 64-bit ... ARM aarch64

# ④ 服务器（独立程序，静态链接，可选）
(cd server && make CROSS=aarch64-linux-gnu- \
    CFLAGS="-std=gnu11 -O2 -static -Wall -Wextra -Wno-unused-parameter" -j8)

# ⑤ 上传（板上没有 scp/rsync 服务端也能用：gzip+base64 走 ssh 管道）
gzip -9c bin/main | base64 -w0 | ssh root@$BOARD_IP \
  "base64 -d | gzip -dc > /root/lvgl/board_apps/main.new && \
   mv -f /root/lvgl/board_apps/main.new /root/lvgl/board_apps/main && \
   chmod 755 /root/lvgl/board_apps/main"

# 资源目录：板上是 BusyBox tar（不支持 -z）
tar -czf - -C board_app assets | ssh root@$BOARD_IP "gzip -dc | tar xf - -C /root/lvgl/board_apps"

# ⑥ 重启
ssh root@$BOARD_IP "killall -9 main; cd /root/lvgl/board_apps && nohup ./main > /tmp/board_app.log 2>&1 &"
```

> **板端 DNS**：`/etc/resolv.conf` 可能被 dhcpcd 留空（表现为 `wget: bad address`、
> AI/天气都连不上）。修：
>
> ```bash
> echo 'nameserver 192.168.64.254' > /etc/resolv.conf
> echo 'nameserver 8.8.8.8' >> /etc/resolv.conf
> ```
>
> 要持久化就写进 `/etc/dhcpcd.conf` 的 `static domain_name_servers=`。

> **SSH 密码**：板端是 dropbear，非交互登录要 `SSH_ASKPASS` + `setsid`：
>
> ```bash
> printf '#!/bin/sh\necho 1\n' > /tmp/askpass && chmod +x /tmp/askpass
> export SSH_ASKPASS=/tmp/askpass SSH_ASKPASS_REQUIRE=force DISPLAY=:0
> setsid -w ssh -o PreferredAuthentications=password root@$BOARD_IP "..."
> ```

### 7.3 运行与验证

```bash
cd /root/lvgl/board_apps
export LV_LINUX_FBDEV_DEVICE=/dev/fb0
export LV_LINUX_INPUT_DEVICE=/dev/input/event6
./main                       # 或 nohup ./main > /tmp/board_app.log 2>&1 &
```

常用环境变量：

| 变量 | 作用 |
|---|---|
| `BOARD_APP_START=weiqi` | 直接打开某个页面（`home`/`weather`/`gallery`/`games`/`g2048`/`snake`/`breakout`/`chat`/`comm`/`weiqi`/`account`） |
| `BOARD_APP_COMM_AUTOLOGIN="host:port,user,pass"` | 自动登录（优先级高于本地记住的账号） |
| `BOARD_APP_PERF=1` | 每 2 秒打印 fps / 帧耗时 / LVGL 堆 |
| `BOARD_APP_LOG_LEVEL=debug` | 日志级别 |
| `BOARD_APP_ROOT=/path` | 指定 assets 根目录（默认 exe 所在目录） |
| `BOARD_APP_CHAT_AUTOSEND="你好"` | 自检：聊天页 2.5 秒后自动发一条（无触摸时验证 AI） |

随主程序一起部署的三个小工具：

| 工具 | 用途 |
|---|---|
| `./ai_key sk-xxx` | 把 API key 写进 `data/settings.conf`（`--clear` 清除；不带参数查看） |
| `./ai_check` | 走与聊天页相同的路径发一次真实请求，打印状态码/响应/解析结果 |
| `./tls_check` | 只验证 TLS + HTTP 通路（假 key，返回 401 也算成功），不消耗额度 |

验证手段：

- **看日志**：`grep -E "registered|font cached" /tmp/board_app.log`
- **抓屏**：`dd if=/dev/fb0 bs=2400000 count=1 | gzip -c | base64 -w0`，本地用
  `board_app/tools/board_shot.py` 转 PNG
- **点几下**：确认触摸正常（`/dev/input/event6`）

### 7.4 服务器（聊天室 / 联网围棋）

```bash
cd /root/lvgl/board_apps
./chat_server --port 20000 --data /root/lvgl/board_apps/data   # 前台
nohup ./chat_server > /tmp/chat_server.log 2>&1 &              # 后台
killall chat_server                                            # 停止
```

- 客户端登录页填 `板子IP:20000`（地址不写死，首次手填，之后记住）；
- 板上自测：`./chat_test_client 127.0.0.1 20000`；
- 端口被占用时服务器会提示 `pgrep -l chat_server` / `killall chat_server`。

### 7.5 开机自启（可选，非本期交付）

- systemd service 或 `/etc/init.d/S99board_app`，`After=multi-user.target`、`Restart=on-failure`。
- 自启前确保网络就绪（天气有离线兜底；聊天/围棋会自动重试登录）。
- 不建议在未确认显示/触摸设备权限前启用自启，否则黑屏难排查。

### 7.6 动态 IP 与 DHCP 固定租约（推荐）

板子用 `dhcpcd` 获取地址，重启后可能换 IP；**不需要在板子上写静态 IP**。推荐在
DHCP 服务器/路由器上做“地址保留/固定租约”，板子仍然走 DHCP，只是每次拿到同一个地址。

当前板端信息（以实际为准）：

| 项 | 值 |
|---|---|
| hostname | `RK356X` |
| eth0 MAC | `46:76:b5:3e:f0:fd` |
| 网关/DHCP 管理页 | `192.168.64.254`（HTTPS 管理页开放） |
| 最近地址 | `192.168.64.200/24` |

步骤：

1. 浏览器打开 `https://192.168.64.254`，登录路由器/DHCP 管理页；
2. 找到 DHCP 客户端列表 / Address Reservation / Static Lease；
3. 用 MAC `46:76:b5:3e:f0:fd` 绑定一个空闲地址（例如当前 `192.168.64.200`）；
4. 保存，重启板子，`ip -4 addr show eth0` 应仍得到保留地址。

> 这是 DHCP 固定租约，不是板端静态 IP；换网段/换 DHCP 服务器时自动失效，不会把板子锁死。

如果无法登录 DHCP 服务器，用主机侧扫描脚本按 MAC 找当前地址：

```powershell
pwsh -File board_app/tools/find_board.ps1
# 输出：BOARD_IP=192.168.64.200
```

脚本只做 ICMP 扫描 + Windows 邻居表查询，不修改板子网络。**不要**同时在
`/etc/network/interfaces` / `dhcpcd.conf` 里再写静态地址，否则会和 DHCP 租约冲突。

## 8. 运行配置

| 变量 | 示例 | 作用 |
|---|---|---|
| `BOARD_APP_ROOT` | `/root/board_app` | 资源/数据根 |
| `BOARD_APP_DATA_DIR` | `/userdata/board_app` | 可写数据（rootfs 只读时用） |
| `BOARD_APP_PHOTO_DIR` | `/userdata/photos` | 相册目录 |
| `BOARD_APP_WEATHER_URL` | `http://192.168.1.10:8080/api/weather/city/` | 内网镜像/测试 |
| `BOARD_APP_LOG_LEVEL` | `debug` | 日志级别 |
| `BOARD_APP_START` | `home` | 启动直接进入的应用 id（如 `weather`） |
| `LV_LINUX_FBDEV_DEVICE` | `/dev/fb0` | framebuffer |
| `LV_LINUX_INPUT_DEVICE` | `/dev/input/event6` | 触摸事件节点 |
| `BOARD_APP_PERF` | `1` | 每 2 秒打印 fps/帧耗时/LVGL 堆 |
| `BOARD_APP_AI_KEY` | `sk-…` | API key（优先于 `data/settings.conf` 的 `ai.key`） |
| `BOARD_APP_AI_URL` / `_MODEL` | `https://api.deepseek.com/v1` / `deepseek-flash` | 大模型接口与模型名 |
| `BOARD_APP_CHAT_AUTOSEND` | `你好` | 自检：聊天页 2.5 秒后自动发送 |
| `BOARD_APP_COMM_AUTOLOGIN` | `127.0.0.1:20000,alice,alice123` | 自动登录聊天/围棋账号 |

> 完整变量表（含默认值）见 [05 模块接口](05-module-api.md) 第 8 节。

## 9. 常见问题排查

| 现象 | 可能原因 | 处理 |
|---|---|---|
| CMake 报找不到 `ui/ui.h` | 顶层仍 `add_subdirectory(ui)` | 改为只构建 `board_app`；旧 `ui/` 不参与 |
| 链接报 `FreeType` 未定义 | `LV_USE_FREETYPE=1` 但无 `libfreetype.a` | 设 `LV_USE_FREETYPE 0`，移除 freetype 链接 |
| 链接报 `lv_font_simsun_16_cjk` 未定义 | 回退字体未启用 | `LV_FONT_SIMSUN_16_CJK 1` |
| 启动黑屏 | fbdev 设备/权限/分辨率不对 | `fbset`/`cat /dev/fb0`；检查 `LV_LINUX_FBDEV_DEVICE` |
| 触摸无反应 | 事件节点不对/权限 | `evtest /dev/input/eventX`；改 `LV_LINUX_INPUT_DEVICE` |
| 中文显示方框 | 字体缺失/子集缺字/TinyTTF 未启用 | 检查字体路径、`LV_TINY_TTF_FILE_SUPPORT`、`chars.txt` |
| 天气一直离线/403 | 板子无网络/DNS/接口被限流 | 板端 `wget -O- "http://wttr.in/Guangzhou?format=j1"`；403 带 `40310003 too many requests` 时是 CDN 限流 |
| 天气解析失败 | 接口字段变化 | 打开 debug 日志；用 05 的字段表比对；更新 parser + 测试样本 |
| 相册打不开图 | 路径/权限/格式不支持 | 确认 `BOARD_APP_PHOTO_DIR`；支持 png/jpg/jpeg/bmp；看日志 |
| 保存失败 | 原图目录不可写/磁盘满 | 检查原图目录权限与空间；`assets/photos` 需可写 |
| 界面卡顿 | 发布版仍开了 LVGL 断言/阴影过重 | 关闭 `LV_USE_ASSERT_*`；减少卡片阴影；先确认是全量 Release |
| 编辑大图卡顿/OOM | 原图过大 | 调低编辑上限（1280×800）；换小图；检查内存 |
| 运行报 `error while loading shared libraries` | 板端缺库/架构不对 | `file bin/main`、`ldd`；用交叉编译器产物 |
| 编译极慢 | 在 `/mnt/d` 构建 | 同步到 WSL ext4 再编 |
| 修改 `lv_conf.h` 不生效 | 增量构建未重编 LVGL | 删 `build_board_app` 全量重编 |
| 上传后板端没变化 | 上传校验失败/旧进程占用 | `sha256sum` 对比；`killall main` 后重起 |
| 服务器启动报端口占用 | 旧 `chat_server` 还在跑 | `pgrep -l chat_server` → `killall chat_server`，或 `--port 20100` |
| 板端 `bad address`（AI/天气都连不上） | DHCP 没写 DNS | 写 `/etc/resolv.conf`（见 7.2 的说明） |
| AI 提示“网络不可用” | 分层排查 | `./tls_check` → `./ai_check`（见 08 第 7.5 节） |
| 聊天补发收不到 | 历史文件为空 | 确认启动日志有 `chat history dir:`；session 必须在登录前初始化 |

## 10. 可复现构建清单

交付前按顺序执行并记录结果：

1. `rsync` 同步到 WSL ext4。
2. `rm -rf build_board_app && cmake -S . -B build_board_app -DCMAKE_BUILD_TYPE=Release`。
3. `cmake --build build_board_app -j8` 成功。
4. `file bin/main` 显示 aarch64。
5. `cmake -S board_app/tests/host -B build_host_tests && cmake --build ... && ctest ...` 全绿。
6. 字体文件存在且 `fc-scan`/`fonttools` 可读；UI 中文无方框（板上点检）。
7. 部署到板子，按 07 的清单点检。
