# 维护与扩展指南

> 文档编号：08
> 状态：与实现同步（2026-11-27）
> 目标：让后来者在不读完所有源码的情况下，安全地新增功能、替换数据源、升级依赖。

## 1. 日常开发流程

```text
改 D: 源码/文档
  → 本地静态检查（core 纯度/格式）
  → 同步到 WSL ext4（tools/sync_to_wsl.sh）
  → 主机测试（board_app/tests/host）
  → 交叉编译（build_board_app）
  → 部署到板子
  → 按 07 点检
  → 更新文档/提交
```

**不要**在 `/mnt/d` 直接构建；不要把 `build_board_app/`、`bin/`、`data/` 提交到 git。

## 2. 新增一个应用（模板）

以“时钟”应用为例，目标：主界面多一个卡片，点击进入，返回正常。

### 2.1 目录

```text
board_app/app/clock/
├── core/
│   ├── include/clock/clock.h
│   └── src/clock.c
└── ui/
    ├── clock_ui.h
    └── clock_ui.c
```

- 核心逻辑（时间计算/时区/闹钟）放 `core/`，不含 LVGL。
- UI 只做显示与输入。

### 2.2 核心头文件

```c
/* app/clock/core/include/clock/clock.h */
#pragma once
#include <stdbool.h>
typedef struct { int hour, minute, second; } clock_time_t;
int clock_now(clock_time_t *out);
```

### 2.3 UI 描述符与自动注册

```c
/* app/clock/ui/clock_ui.c */
#include "board_ui/ui_app_manager.h"
static lv_obj_t *clock_create(lv_obj_t *parent) { /* build UI */ }
static void clock_destroy(void) { /* timers/images/threads */ }

static const board_app_desc_t s_clock = {
    .id = "clock",
    .title = "时钟",
    .subtitle = "本地时间与日期",
    .parent_id = NULL,          /* 顶层应用，出现在首页 */
    .best_score_key = NULL,
    .icon = UI_ICON_GENERIC,    /* 没有专属图标时用通用图标 */
    .accent = 0x2F6BFF,
    .order = 40,
    .show_on_home = true,
    .create = clock_create,
    .destroy = clock_destroy,
};
BOARD_APP_REGISTER(s_clock);
```

### 2.4 不需要改中心文件

- 首页：自动遍历 `parent_id == NULL && show_on_home`，生成卡片。
- 导航：自动按 `id` 查找，返回栈由管理器维护。
- CMake：`CONFIGURE_DEPENDS` glob 自动收集 `app/clock/ui/*.c` 与 `app/clock/core/src/*.c`。
- 游戏中心：只有 `parent_id == "games"` 的条目才会出现在游戏中心；普通应用不写该字段。

### 2.5 构建

```bash
# 新增文件夹后重新配置一次，让 CMake 重新 glob；之后正常构建
cmake -S . -B build_board_app
cmake --build build_board_app -j8
```

`CONFIGURE_DEPENDS` 在支持的生成器上会自动触发重新 glob；显式重跑一次最稳妥。两种方式都不需要改源列表。

### 2.6 检查

- `check_core_purity.py` 通过。
- 新增主机测试（如 `test_clock.c`）。
- 首页自动出现卡片；1024×600 下不溢出；4+ 应用时滚动正常。
- 点击卡片进入、返回主界面正常；连续进出 10 次无泄漏。
- 更新 05 接口文档与 04 UI 文档（仅当新增了公共字段/组件）。

### 2.7 需要登录 / 需要输入法的应用

- **登录态**：不要自己 new `comm_client`。统一用 `board_ui/ui_comm_session`：

```c
comm_session_init(NULL);                     /* board_app_init 已经初始化过，这里幂等 */
comm_session_subscribe(on_session_event, app);/* 页面销毁时 unsubscribe */
app->client = comm_session_client();         /* 发请求用 */
if (!comm_session_logged_in()) { /* 显示"去账户登录" */ }
```

  理由：同名账号第二次连接会被服务器踢下线，一个进程只能有一条登录连接。
  另外 `comm_session` 在登录前就建好了聊天记录目录，消息到达即落盘。

- **输入法**：不要自己拼键盘。用 `ui_ime_create(parent, ta)` 挂到任意 textarea 上，
  `ui_ime_set_geometry()` 摆位置，`ui_ime_set_ready_cb()` 接「送出」。
  参考 `app/comm/ui/comm_ui.c` 与 `app/chat/ui/chat_ui.c`。

- **异步结果**：worker 线程只 `board_async_queue_push`，UI 用 `ui_async_pump` 消费；
  带 `seq` 丢弃过期结果（参考 `chat_service`）。

## 3. 新增一个游戏（使用开源核心）

### 3.0 三种游戏形态，先选对路

| 形态 | 例子 | 规则跑在哪 | 注意 |
|---|---|---|---|
| 纯单机 | 2048 / 贪吃蛇 / 打砖块 | 客户端（`arcade_core`） | 最高分写 `settings.conf` |
| 单机 + 第三方规则核心 | 同上 | 同上 | 保留 LICENSE / UPSTREAM |
| **联机、需要裁判** | 联网围棋 | **服务器**（`server/game.c`，与客户端共用 `weiqi` 核心） | 客户端只发“意图”，收全量状态重绘 |

联机游戏的关键约定：

1. 规则核心放 `app/<game>/core/`，**纯 C、不依赖 LVGL**，服务器与客户端链接同一份；
2. 客户端上传的是**操作**（`{"x":3,"y":5}`），不是棋盘；服务器校验后广播**完整状态**；
3. 客户端不做合法性判断（本地只做轻提示），避免两端不一致；
4. 断线/离开要通知对手（`GAME_PEER_LEFT`），服务器清理房间；
5. 协议消息加在 `comm/comm_proto.h`，事件加在 `comm_client.h` 的 `comm_evt_kind_t`。

### 3.1 选型标准

1. **许可证**：MIT/BSD/Apache-2.0/Public Domain；禁止 GPL/AGPL（除非整个项目 compatible，且评审明确同意）。
2. **核心与 UI 分离**：最好有纯逻辑文件；至少能抽出规则，不依赖 LVGL/OS。
3. **必须是纯 C**：板端交叉工具链没有 C++ 编译器；C99 优先。JS/Go/Rust 项目只能“移植成 C”，并保留上游许可与移植说明（参考 `third_party/weiqi-js/UPSTREAM.md`）。
4. **可测试**：有稳定 API、无全局状态、随机数可注入。
5. **体量**：单游戏核心 < 3000 行；无网络/无动态资源。
6. **维护性**：最近有提交/文档/测试；记录 commit。

### 3.2 引入步骤

1. 在 `board_app/third_party/<项目名>/` 放：
   - `LICENSE`（原文）
   - `UPSTREAM.md`（仓库 URL、commit、复制路径、是否修改、修改说明）
   - 只复制需要的核心文件，不整仓拷贝。
2. 游戏目录 `app/games/<name>/core/src/` 放**该游戏**的规则源码；共享的 `arcade_rng.c`/头文件放 `app/games/core/`，避免重复符号。
3. 写 UI 并定义描述符：

```c
static lv_obj_t *mygame_create(lv_obj_t *parent);
static void mygame_destroy(void);

static const board_app_desc_t s_mygame = {
    .id = "mygame",
    .title = "我的游戏",
    .subtitle = "一句话玩法",
    .parent_id = "games",              /* 自动出现在游戏中心 */
    .best_score_key = "game.mygame.best",
    .icon = UI_ICON_GENERIC,
    .accent = 0x2FBF71,
    .order = 40,
    .show_on_home = false,
    .create = mygame_create,
    .destroy = mygame_destroy,
};
BOARD_APP_REGISTER(s_mygame);
```

4. 不需要改游戏中心/首页/CMake 源列表；构建时自动发现。
5. 写主机测试：初始化、关键规则、边界、随机种子确定性。
6. 在 09 许可证文档登记。
7. 板上点检：暂停/继续/重开/退出/最高分/触摸。

### 3.3 游戏 UI 适配原则

- UI 不复制规则；只读取核心状态渲染。
- 定时器周期从核心 API 获取（如 `arcade_snake_step_ms`）或固定 tick（打砖块 16ms）。
- 退出时 `lv_timer_delete`；暂停时 `lv_timer_pause`。
- 最高分键统一 `game.<id>.best`；只在超过时写。
- 结算弹窗只显示，不修改核心状态；重开调用 `*_init`。

## 4. 替换天气数据源

### 4.1 当前 provider

- 默认：`weather_provider.c`，URL 前缀 `http://wttr.in/`，内置城市代码映射成英文城市名后拼接。
- 解析：`weather_parse_json` 自动识别 wttr.in（worldweatheronline）与 itboy 两种格式。
- 板端线路被 CDN 限流（403 `40310003 too many requests`）时，可换其他数据源或稍后重试。
- 兜底：`weather_cache_load` + `weather_mock_fill`。

### 4.2 新增 provider

1. 在 `weather_types.h` 定义统一模型（已有）。
2. 新增 `weather_provider_xxx.c`，实现：
   ```c
   int weather_provider_fetch(const char *city_code, char **out_json, size_t *out_len);
   ```
   或直接返回 `weather_data_t`（推荐：provider 只负责 HTTP + 原始 body，解析分离）。
3. 在 `weather_service.c` 用编译期/运行期选择 provider：
   - 编译期：`-DBOARD_APP_WEATHER_PROVIDER_XXX=1`；
   - 运行期：`BOARD_APP_WEATHER_PROVIDER=xxx`。
4. 新增解析器或复用 `weather_parse_json`（字段映射不同则新建）。
5. 增加 `tests/host/data/weather_xxx.json` 样本与解析测试。
6. 更新 05 字段表、06 环境变量、09 许可证（若引入新库）。

### 4.3 HTTPS 需求

当前 `http_client` 只支持 HTTP。若必须 HTTPS：

- 方案 A：引入 libcurl（需要交叉编译 libcurl + OpenSSL/mbedTLS），体积/依赖大；
- 方案 B：引入 mbedTLS/wolfSSL + 自行封装，工作量中等；
- 方案 C：板端跑一个本地 HTTP 代理（如 tinyproxy）转发 HTTPS，应用仍用 HTTP。
- 选择后在 02/05/06 记录；不要把 TLS 逻辑塞进 `weather_parse`。

### 4.4 接口变更排查

1. 板端 `wget -O- <url>` 保存 JSON。
2. 用 `tests/host/data/` 样本替换，跑解析测试。
3. 看 `weather_parse.c` 字段路径（`data.forecast[].high` 等）。
4. 修改解析器 + 测试 + 文档。
5. 不要在生产代码里打印完整 JSON（可能含位置信息）。

## 5. 字体与中文显示

### 5.1 什么时候需要重新生成

- 新增/修改中文 UI 文案；
- 天气接口出现新天气现象字；
- 新增城市名；
- 更换字体/字重。

### 5.2 步骤

```bash
# 1) 扫描源码，更新 chars.txt（ASCII/标点/天气/城市也会自动补入）
python3 board_app/tools/gen_chars.py

# 2) 生成子集
python3 board_app/tools/gen_font_subset.py \
  --input /path/to/NotoSansSC.ttf \
  --chars board_app/assets/fonts/chars.txt \
  --output board_app/assets/fonts/NotoSansSC-Board.ttf
```

- `chars.txt` 应包含：ASCII 32–126、常用标点、所有 UI 中文、天气现象、城市名。
- 生成后检查文件大小（100–500 KB）与 `fonttools` 可读。
- 部署时替换 `assets/fonts/NotoSansSC-Board.ttf`；无需重编（TinyTTF 运行时读取），但需重启应用。
- 若出现方框：
  1. 确认字体路径/`BOARD_APP_ROOT`；
  2. 确认 `LV_TINY_TTF_FILE_SUPPORT=1`；
  3. 用 `fonttools ttLib` 检查字形是否存在；
  4. 加大 `chars.txt` 重新生成；
  5. 临时回退 `lv_font_simsun_16_cjk`（检查 `LV_FONT_SIMSUN_16_CJK=1`）。

### 5.3 字体许可证

- Noto Sans SC 使用 SIL OFL 1.1；必须随包分发 `OFL.txt`，不得单独售卖字体。
- 不得把 Windows 系统字体（msyh.ttc/simhei.ttf）提交/分发；设计图可以使用本机字体，但仓库只放 OFL 字体。

## 6. 修改主题/布局

- 颜色/字号/圆角集中在 `ui/include/board_ui/ui_theme.h` 与 `ui_theme.c`；禁止在业务代码散落 `lv_color_hex(0x...)`。
- 修改色板后检查对比度（正文 ≥ 4.5:1）。
- 布局用 `lv_obj_set_flex_*` / `lv_obj_set_grid_*` / 百分比；固定坐标只用于 1024×600 的棋盘/图片区，并集中在常量。
- 修改首页卡片数量时同步调整宽度/间距，确保 3 列或 2×2 不溢出。
- 修改后跑 04 的“设计验收清单”。

## 7. 调试手段

### 7.1 日志

```bash
BOARD_APP_LOG_LEVEL=debug ./main 2>&1 | tee /tmp/board_app.log
```

- 关注初始化顺序、字体加载、路径探测、HTTP 状态、解析失败字段、图片解码失败。
- 不要把日志级别默认设为 debug（性能/刷屏）。

### 7.2 显示/输入

- `fbset -i` 查看分辨率/bpp。
- `cat /dev/fb0 > /tmp/fb.raw` 后用 Python/Pillow 转 PNG（BGRA，1024×600）检查是否花屏。
- `evtest /dev/input/eventX` 确认触摸坐标/事件。
- 触摸坐标不准：检查 evdev 的 ABS 范围与屏幕分辨率映射；必要时在 `main.c` 设置 `lv_evdev_set_calibration`（如 LVGL 版本支持）。

### 7.3 性能

- `top -H -p $(pidof main)` 看 UI 线程/worker CPU。
- 游戏暂停后 CPU 应明显下降（定时器暂停）。
- 页面切换卡顿：检查是否在 `create` 中同步解码大图/发网络请求。
- 内存增长：进出应用 20 次，比较 `/proc/<pid>/status` 的 `VmRSS`；用 LVGL 对象计数日志辅助。

### 7.4 崩溃

- 编译 Debug + `-g`，板端 `gdb ./main core`（若板端有 gdb）。
- 优先检查：空指针（字体/图片/队列）、释放后使用（异步回调）、重复删除定时器、栈溢出（大局部数组）。
- 打开 ASan 的主机测试复现 core 问题；UI 问题用日志缩小到页面/回调。

### 7.5 联机与 AI 排查

| 现象 | 先看 | 处理 |
|---|---|---|
| 聊天室连不上 | `pgrep -l chat_server`、`ss -lnp \| grep 20000` | 端口被占：`killall chat_server`；否则起服务器 |
| 登录提示“密码错误/账号不存在” | 服务器日志 | 客户端没有注册入口时会提示先注册；账号在 `data/accounts.txt`（散列） |
| 补发消息收不到 | `cat data/comm_history/<user>.jsonl` | 若为空：确认 session 在**登录前**初始化（`board_app_init` 里 `comm_session_init(&s_paths)`） |
| 聊天记录没落盘 | 启动日志 `chat history dir:` | 没这行说明 `comm_session_init` 没拿到 paths |
| AI 提示“网络不可用” | `./tls_check` → `./ai_check` | 分层判断：TLS/HTTP vs key vs 解析 |
| AI 返回 401 | `./ai_key` 看 key 是否在 | key 没发：检查 `http_post_headers` 的 `Authorization` 头 |
| AI 返回 400 | 看 `ai_check` 打印的 body | 模型名/参数错；官方模型现在是 `deepseek-flash` / `deepseek-v4-pro` |
| DNS 解析失败（`bad address`） | `cat /etc/resolv.conf` | 板端 dhcpcd 可能没写：写入 `nameserver 192.168.64.254` |

### 7.6 板端 glibc 版本错误

```text
/lib/libc.so.6: version `GLIBC_2.33' not found
```

板子是 Buildroot glibc 2.29，WSL 默认交叉 gcc 生成 glibc 2.38 符号。处理：

```bash
bash board_app/tools/setup_bionic_sysroot.sh   # glibc 2.27 sysroot
rm -rf build_board_app
cmake -S . -B build_board_app -DCMAKE_BUILD_TYPE=Release
cmake --build build_board_app -j8
readelf -d bin/main | grep NEEDED
```

确认 `NEEDED` 为 `libpthread.so.0`/`libm.so.6`/`libc.so.6`/`ld-linux-aarch64.so.1`。
不要用 `-static` 作为板端最终方案：静态 glibc 的 `getaddrinfo` 依赖运行时的 NSS 共享库，
天气在线会失败；静态只作为没有 sysroot 时的临时回退。

## 8. 升级 LVGL

1. 备份 `lv_conf.h`；记录当前版本（9.2.3）。
2. 替换 `lvgl/` 为新版本；先不改业务代码，编译空壳 `main` 验证 API 兼容。
3. 重点检查：`lv_image_dsc_t`/draw buf、`lv_tiny_ttf_*`、`lv_evdev_*`、`lv_fs`、`lv_timer` API。
4. 逐步编译 `board_core`（不依赖 LVGL）→ `board_ui` → `main`。
5. 跑主机测试 + 板上点检。
6. 更新 02/05/06/09 中的版本号与差异说明。

## 9. 升级第三方游戏核心

1. 在 `third_party/littleworld_arcade/UPSTREAM.md` 记录当前 commit。
2. 拉取新版本，只复制 `arcade_core` 的 `include/arcade/*.h` 与 `src/arcade_*.c`（不含 `arcade_save` 的 IDF 实现）。
3. 对比 API/结构体变化；更新 UI 适配与测试。
4. 保留 LICENSE/版权头；不要“顺手重构”上游代码。
5. 跑 `tests/host/test_game.c`，再跑板上点检。

## 10. 旧工程清理（legacy）

现状：

- `lv_port_linux-release-v9.2/ui/` 是旧 SquareLine 工程（WSL 中存在，D: 副本缺失）。
- `src/` 是旧测试代码（`test_main` 等）。
- 顶层原 CMake 同时 `add_subdirectory(ui)` 和 `add_subdirectory(src)`，并调用 `ui_init()/test_main()`。

处理原则：

- **不删除**用户旧文件；只把它们从构建中移除。
- 顶层 `main.c` 改为 `board_app_init()`；不再 include `ui/ui.h`、`head.h`。
- 顶层 CMake 不再 `add_subdirectory(ui)`/`src`，不再链接 `src_app`。
- 在 README/06 中标注“legacy，不参与构建”；如确认不再需要，再由项目所有者决定删除。

## 11. 发布检查清单

- [ ] 版本号更新（`BOARD_APP_VERSION`）。
- [ ] 主机测试全绿（含 ASan）。
- [ ] 交叉编译干净通过。
- [ ] 字体/示例图/许可证齐全。
- [ ] `data/` 不被提交；`.gitignore` 正确。
- [ ] 文档同步：00 状态、05 接口、06 构建、07 验收、09 许可证。
- [ ] 生成发布包：`main` + `assets/` + `README` + `LICENSES`。
- [ ] 在板子上按 07 完整点检并记录。

## 12. 维护者备忘

- 先保证“能编译、能返回、不崩溃”，再追求视觉细节。
- 任何“临时写死”的路径/尺寸/颜色，必须加 `TODO` 并在发版前清理。
- 任何新增第三方代码，先问许可证，再问能不能少拷贝。
- 文档是交付物；代码改了文档没改，视为未完成。
