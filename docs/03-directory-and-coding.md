# 目录结构与编码规范

> 文档编号：03
> 状态：与实现同步（2026-11-27）
> 目标：任何人第一次打开仓库，能在 10 分钟内找到“界面在哪里、逻辑在哪里、资源在哪里、怎么编译”。

## 1. 完整目录树（当前）

```text
lv_port_linux-release-v9.2/
├── CMakeLists.txt                  # 交叉编译、lvgl、board_app、main
├── main.c                          # lv_init / fbdev / evdev / board_app_init / 主循环
├── lv_conf.h                       # LVGL 配置（字体、内存、驱动、TinyTTF、拼音输入法）
├── lvgl/                           # LVGL 9.2.3（不修改）
├── ui/ src/ freetype-2.13.3/       # 旧工程（legacy，不参与构建）
├── bin/                            # 构建产物（main / tls_check / ai_check / ai_key）
├── server/                         # ★ 命令行服务器（不链接 LVGL，可单独构建）
│   ├── server.c                    # TCP/UDP 主循环 + 控制台命令
│   ├── store.c/.h                  # 账号/好友/离线消息持久化
│   ├── game.c/.h                   # 围棋对局：邀请、权威判定、广播
│   ├── sha256.c/.h                 # PBKDF2-HMAC-SHA256 口令散列
│   ├── Makefile                    # 本机 / aarch64 交叉编译
│   └── tools/                      # test_client.c、smoke_test.sh、weiqi_smoke.sh
└── board_app/                     # ★ 客户端项目根目录
    ├── CMakeLists.txt              # board_core / board_ui / board_app + 自检工具 target
    ├── board_app.c/.h              # 启动引导（路径/存储/主题/会话/自动登录）
    ├── main_page/home_ui.c         # 首页（5 张卡片 + 顶栏账号胶囊）
    ├── core/                       # 跨应用核心（无 LVGL）
    │   ├── include/board_core/     # paths/time/log/error/storage/async/http_client/tls_client
    │   ├── src/                    # 对应实现 + stb_impl.c
    │   └── third_party/{cjson,stb}/
    ├── ui/                         # UI 框架（LVGL）
    │   ├── include/board_ui/       # theme/widgets/icons/app_manager/async/ime/comm_session
    │   └── src/                    # 对应实现（含 ui_ime.c、ui_comm_session.c）
    ├── app/
    │   ├── chat/{core,ui}/         # AI 聊天（LLM 请求/解析/离线助手 + 页面）
    │   ├── comm/{core,ui}/         # 聊天室（协议/socket/客户端/聊天记录 + 页面）
    │   ├── account/ui/             # 系统账户（登录/注册/好友管理）
    │   ├── weather/{core,ui}/      # 天气
    │   ├── gallery/{core,ui}/      # 相册
    │   └── games/
    │       ├── core/ ui/           # 游戏中心
    │       ├── g2048/ snake/ breakout/   # 三款单机游戏
    │       └── weiqi/{core,ui}/    # 联网围棋（规则核心 cn 移植自 weiqi.js）
    ├── assets/                     # 只读资源
    │   ├── fonts/NotoSansSC-Board.ttf + OFL.txt
    │   ├── ime/pinyin_dict.txt     # 拼音词库（401 音节 / 6763 字）
    │   ├── photos/                 # 示例照片（CC0）
    │   └── config/
    ├── data/                       # 运行时生成
    │   ├── cache/                  # 天气缓存
    │   ├── comm_history/           # ★ 聊天记录（<会话>.jsonl，收到即落盘）
    │   ├── edited/                 # 旧版保存目录（兼容保留）
    │   └── settings.conf           # 设置 + 记住的账号 + 保存的 AI key
    ├── third_party/
    │   ├── littleworld_arcade/     # 游戏规则核心（MIT）
    │   └── weiqi-js/               # 围棋规则上游原文 + ISC 许可
    ├── tests/
    │   ├── host/                   # 主机单测（含 test_weiqi.c）
    │   └── preview/                # 主机 headless 预览（导出截图）
    ├── tools/                      # 构建/部署/优化/自检脚本与工具
    │   ├── deploy.sh               # 一键部署（同步→测试→编译→上传→重启）
    │   ├── ai_key.c  ai_check.c  tls_check.c
    │   ├── gen_pinyin_dict.py  gen_font_subset.py  gen_chars.py
    │   ├── check_core_purity.py  check_font_coverage.py
    │   ├── board_shot.py  fb_to_png.py  dec_shot.sh
    │   └── wsl_*.sh                # 各专项脚本（构建/预览/部署/性能/联调）
    ├── docs/                       # 文档 00~14 + design/ + screenshots/
    ├── README.md
    ├── LICENSE
    └── THIRD_PARTY_NOTICES.md
```

> 示例照片与设计图已入库；`data/` 下的运行时内容不入库。`bin/`、`build*/` 不入库。

## 2. 命名规范

| 类别 | 规则 | 示例 |
|---|---|---|
| 目录 | 小写 snake_case，英文 | `main_page/`、`g2048/`、`third_party/` |
| C 文件 | snake_case，模块前缀 | `weather_parse.c`、`ui_theme.c` |
| 头文件 | 与源文件同名；公共头放 `include/<module>/` | `include/weather/weather_types.h` |
| 类型 | snake_case + `_t` | `weather_data_t`、`image_t` |
| 函数 | `<module>_<verb>_<noun>` | `weather_parse_json`、`image_rotate90` |
| 宏/常量 | 大写 + 下划线；带模块前缀 | `WEATHER_MAX_DAYS`、`UI_COLOR_PRIMARY` |
| 枚举 | 类型 `_t`，成员大写 + 前缀 | `WEATHER_OK`、`GAME_STATE_PAUSED` |
| 全局变量 | 避免；必须时 `static`，或 `g_` 前缀仅限单文件 | `static weather_service_t *s_service` |
| UI 文案 | 中文，集中定义或就近 `#define` | `"未来 7 天"` |
| 资源文件 | 小写，短横线/下划线 | `NotoSansSC-Board.ttf`、`sample-01.png` |

## 3. 文件头与注释

每个手写 `.c/.h` 文件顶部：

```c
/**
 * @file weather_parse.c
 * @brief 解析天气接口 JSON，填充 weather_data_t。
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Board App contributors
 */
```

- 公共 API 用 Doxygen 风格注释：`@param`、`@return`、`@note`（所有权/线程安全）。
- **注释语言统一用中文**：文件头 `@brief`、行内 `/* ... */`、`//` 全部写中文；
  `SPDX-License-Identifier`、`Copyright`、`@file` 文件名保持英文原样。
- 第三方/上游代码（`third_party/`、`core/third_party/`、`app/games/core/include/arcade/`）
  的版权头与上游注释保持原样，不翻译，避免与上游 diff 冲突。
- 复杂算法（固定点、裁剪坐标、字节序转换）必须有“为什么”注释，而不是重复代码。
- TODO 格式：`/* TODO(name): ... */`，并在 issue/文档中登记；不允许无主 TODO 进入交付。
- 不保留大段注释掉的旧代码；用 git 历史。

## 4. 代码风格

- C99；4 空格缩进；行宽 ≤ 100；左花括号不换行（与 LVGL 风格接近）。
- 指针星号靠变量：`char *p`。
- 单行 `if` 必须带花括号；禁止裸 `else if` 链过深（> 3 层抽函数）。
- 比较常量在左：`if (0 == ret)`（可选，团队统一即可）。
- 不使用 GCC 扩展（除 `typeof` 禁止）；`-std=gnu99` 由顶层设置，但代码保持 C99。
- 禁止在头文件定义可变全局变量；常量用 `extern const`。
- 所有 `switch` 覆盖枚举默认分支并 `default:` 处理异常；编译器 `-Wswitch` 打开。
- 浮点仅用于图像亮度/对比度/缩放；游戏核心使用整数/定点（上游已如此）。
- 不在 UI 回调里做 > 16 ms 的同步工作；重活放 worker 或分帧。

### 4.1 核心纯度规则（硬性）

`core/`、`app/*/core/`、`app/games/*/core/` 中：

- 禁止 `#include "lvgl.h"`、`#include "lv_*.h"`。
- 禁止调用 `lv_*`、`LV_*` 宏（第三方头文件里的注释除外）。
- 禁止 `#include "../ui/..."`。
- 允许：C 标准库、POSIX（`pthread`、`socket`、`dirent`、`unistd`）、第三方纯 C 库（cJSON/stb/arcade_core）。
- CI/本地检查：`python3 tools/check_core_purity.py`，发现即失败。

`ui/` 中：

- 可以调用 core API，但不得直接解析 JSON、拼 HTTP、做像素运算、实现游戏规则。
- 不得直接 `fopen` 业务文件（通过 storage/image/gallery core）。
- 不得把业务状态复制一份到 UI 里“顺手改”；UI 只持有渲染所需快照。

### 4.2 错误与日志

- 核心函数返回 `int`：`0` 成功，`-EINVAL`/`-ENOMEM`/`-EIO`/`-ETIMEDOUT` 等负值；需要返回数据的用输出参数。
- UI 回调不静默吞错：至少 `BOARD_LOG_W`，用户可见操作要有 Toast/错误态。
- 日志格式：`[时间] [级别] [模块] 消息`；不要在日志里打印用户图片内容/完整 URL 查询参数（如未来有 Key）。
- 断言只用于“不可能发生”的内部不变量；不要用断言处理用户输入/网络错误。

## 5. 前后端接口纪律

1. 每个应用的核心头文件只暴露“业务需要的类型 + 函数”，不暴露 LVGL 类型。
2. UI 不直接访问核心结构体内部字段做渲染以外的修改；修改必须走函数。
3. 跨线程数据只用值/深拷贝；不共享可变缓冲。
4. 所有返回堆指针的函数在头文件写明所有权；调用方必须按文档释放。
5. 新增字段时先改 `types.h` 与文档，再改解析/UI/测试；禁止“先用上再说”。

## 6. Git 与提交规范

- 分支：`main` 保持可编译；功能分支 `feat/weather`、`fix/gallery-crop`。
- 提交信息（Conventional Commits 风格）：
  - `feat(weather): add city switch and cache fallback`
  - `fix(gallery): clamp crop rect to image bounds`
  - `docs(arch): clarify worker cancellation`
  - `chore(build): sync board_app into top-level cmake`
- 一个提交只做一件事；格式化/重命名单独提交。
- 禁止提交：`build*/`、`bin/`、`data/cache/`、`data/edited/`、大图原图、密钥、日志。
- 第三方代码单独提交，提交信息注明来源与 commit；不与业务修改混在一起。

## 7. CMake 约定（自动收集新应用）

- 顶层使用现有 `aarch64-linux-gnu-gcc`；不新增 `pkg-config` 依赖。
- `board_app/CMakeLists.txt` 只定义库与子目录，不定义 `main`。
- **源文件自动收集**，新增应用不需要改源列表：

```cmake
# 核心：公共 core + 各 app core + 游戏共享/各游戏 core
file(GLOB_RECURSE BOARD_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/core/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/app/*/core/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/app/games/*/core/src/*.c")

# UI：框架 + 首页 + 各 app ui + 游戏 ui
file(GLOB_RECURSE BOARD_UI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_page/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/app/*/ui/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/app/games/*/ui/*.c")
```

- 头文件目录同样自动收集：
  - `core/include`、各 `app/*/core/include`、`app/games/*/core/include`；
  - `ui/include`，各 app ui 目录作为 `PRIVATE` 包含。
- `board_core` 不链接 LVGL；`board_ui` 链接 `board_core` + `lvgl`。
- 应用注册不靠 CMake 列表，而靠 `BOARD_APP_REGISTER` 链接段（见 02/05）。因此**新增应用 = 新增文件夹 + 写一个描述符**；CMake 的 `CONFIGURE_DEPENDS` 在下次构建时自动发现新 `.c`。
- 若构建系统不支持 `CONFIGURE_DEPENDS`（CMake < 3.12），新增文件后手动重跑一次 `cmake -S . -B build`；不需要改代码。
- 所有目标设置 `C_STANDARD 99`、`C_STANDARD_REQUIRED ON`；核心库加 `-Wall -Wextra`（稳定后 `-Werror`）。
- 主机测试单独 `tests/host/CMakeLists.txt`，不引用顶层 LVGL 目标。
- 不在源码树内生成构建产物；WSL 构建目录 `build_board_app/`，主机测试 `build_host_tests/`。

### 7.1 新增应用的最小步骤（无中心改动）

```text
1. 新建 app/clock/core/{src,include}（可空）
2. 新建 app/clock/ui/clock_ui.c
3. 在其中定义 static const board_app_desc_t s_clock = {...};
4. 文件末尾 BOARD_APP_REGISTER(s_clock);
5. cmake -S . -B build_board_app && cmake --build build_board_app -j8
```

首页卡片、游戏中心子卡片、导航、CMake 源列表都不需要改。自定义图标/强调色通过描述符字段提供；没有专属图标时用 `UI_ICON_GENERIC`。

## 8. 资源与路径约定

- 只读资源：`<root>/assets/...`；运行时根目录由 `board_paths_init()` 解析：
  1. 环境变量 `BOARD_APP_ROOT`；
  2. 当前工作目录（若存在 `assets/`）；
  3. 可执行文件所在目录；
  4. 可执行文件目录的上一级。
- 可写数据：`<root>/data/`；启动时确保 `cache/`、`edited/` 存在。
- 图片目录默认 `<root>/assets/photos/`，可用 `BOARD_APP_PHOTO_DIR` 覆盖。
- 字体默认 `<root>/assets/fonts/NotoSansSC-Board.ttf`，缺失时回退 `lv_font_simsun_16_cjk`。
- 不允许在代码里写 `D:\...`、`/mnt/d/...` 等开发机路径。

## 9. 代码评审清单

**分层约束**

- [ ] `core/`、`app/*/core/`、`server/` 里没有 `lv_` / `lvgl.h`（`tools/check_core_purity.py`）。
- [ ] `server/` 不链接 LVGL，也不 include 任何 `app/*/ui/` 头文件。
- [ ] `ui/` 只提供通用组件；`ui_comm_session` 依赖 core 与 `app/*/core`，不依赖具体应用 UI。

- [ ] 改动是否遵守 core/ui 边界？`check_core_purity.py` 通过？
- [ ] 新增/修改的公共 API 是否更新了 05 文档与头文件注释？
- [ ] 资源释放路径是否覆盖失败分支（early return / error label）？
- [ ] 定时器/线程是否在 `destroy` 中停止并 join/delete？
- [ ] 异步回调是否有 generation/ alive 检查？
- [ ] 触摸目标是否 ≥ 56×56？文字是否溢出/截断？
- [ ] 1024×600 下是否有遮挡、滚动冲突、返回按钮不可点？
- [ ] 是否新增了第三方代码？许可证与 UPSTREAM.md 是否齐全？
- [ ] 主机测试是否新增/更新？交叉编译是否通过？
- [ ] 文档是否同步（至少 00 索引/相关章节）？
