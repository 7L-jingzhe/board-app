# 开发板应用项目 · 文档索引

> 项目代号：**Board App**（智能生活面板）
> 目标平台：aarch64 Linux 开发板，1024×600 触摸屏，LVGL 9.2，fbdev + evdev
> 文档状态：与实现同步（2026-11-27）

## 1. 这个项目是什么

在 `lv_port_linux-release-v9.2` 中新增一个可独立维护的应用项目 `board_app/`。它运行在开发板 Linux 上，提供：

1. **主界面**：时间、日期、五个应用入口（AI 聊天 / 聊天室 / 天气 / 相册 / 小游戏）。
2. **AI 聊天**：左半屏对话、右半屏虚拟键盘；键盘内置 **LVGL 自带的开源拼音输入法**，输入法封装成可复用组件 `board_ui/ui_ime`，任何页面挂一个 textarea 即可使用。
3. **聊天室（通信模块）**：独立命令行服务器（TCP 私聊 + UDP 广播 + 账号/好友/离线消息持久化，口令加盐散列），客户端在 LVGL 里登录/加好友/聊天，协议全部为 JSON。
3. **天气查询**：当前天气、湿度、风力、空气质量、未来 7 天预报；联网获取，失败时读缓存或演示数据。
4. **相册**：浏览本地图片；旋转、翻转、灰度、亮度/对比度、裁剪；保存为新 PNG。
5. **小游戏中心**：2048、贪吃蛇、打砖块、**联网围棋**。**游戏规则核心来自 GitHub 开源项目**（Little World Arcade / weiqi.js），本项目只写 LVGL 界面和适配层。

设计上强制 **UI 与核心代码分离**：`core/` 不包含任何 LVGL 头文件或 `lv_*` 调用；`ui/` 只负责渲染、输入和动画。这样核心逻辑可以在 PC 上单元测试，也方便换 UI 或移植到其他屏幕。

## 2. 先读哪几篇

| 角色 | 建议阅读顺序 |
|---|---|
| 第一次接触项目 | 00 索引 → 01 需求 → 02 架构 → 06 构建运行 |
| 负责 UI / 交互 | 04 UI 设计 → 05 模块接口 → 03 编码规范 |
| 负责核心逻辑 | 01 需求 → 02 架构 → 05 模块接口 → 07 测试 |
| 负责构建 / 部署 | 06 构建部署 → 07 测试验收 → 08 维护 |
| 评审 / 验收 | 01 需求 → 04 UI → 07 测试验收 → 09 许可证 |

## 3. 文档清单

| 编号 | 文档 | 内容 |
|---|---|---|
| 00 | 本文档 | 索引、阅读路线、关键决策速览 |
| 01 | [需求规格说明书](01-requirements.md) | 目标、范围、功能需求、非功能需求、验收标准 |
| 02 | [总体架构设计](02-architecture.md) | 分层、模块、生命周期、线程模型、数据流 |
| 03 | [目录结构与编码规范](03-directory-and-coding.md) | 完整目录树、命名、注释、提交规范、前后端边界检查 |
| 04 | [UI 与交互设计](04-ui-design.md) | 设计语言、色板、字号、逐屏布局、状态与动效 |
| 05 | [模块接口与数据契约](05-module-api.md) | 头文件级 API、所有权、线程安全、文件格式 |
| 06 | [构建、部署与运行](06-build-deploy.md) | WSL 编译、开发板部署、字体生成、故障排查 |
| 07 | [测试与验收](07-test-acceptance.md) | 主机测试、板上点检、性能与回归清单 |
| 08 | [维护与扩展指南](08-maintenance.md) | 新增应用/游戏/天气源、改主题、调试、升级 LVGL |
| 09 | [第三方开源与许可证](09-third-party-licenses.md) | 组件、版本/commit、许可证、合规清单 |
| 10 | [开发计划与里程碑](10-roadmap.md) | 分阶段任务、交付物、退出条件 |
| 11 | [AI 聊天与可复用输入法](11-chat-and-ime.md) | 聊天页、`ui_ime` 组件 API、拼音词库生成、字体与性能、许可证 |
| 12 | [通信模块：聊天室](12-comm-module.md) | JSON 协议（TCP 可靠 / UDP 广播）、服务器与本地存储、口令散列、LVGL 客户端 |
| 13 | [联网围棋](13-weiqi-online.md) | 移植自 weiqi.js 的规则核心（ISC）、服务器权威判定、共享登录态、对局消息 |
| 14 | [系统账户](14-account-system.md) | 全系统统一登录、会话单例、记住账号与开机自动登录 |

其他入口文档：

- `board_app/README.md`：项目总览、功能表、快速开始
- `board_app/assets/fonts/README.md`：字体子集与 `LV_FONT_KERNING_NONE` 的坑
- `board_app/docs/screenshots/README.md`：所有预览/板端截图索引
- `board_app/docs/design/README.md`：早期设计图说明（哪些页面没有 mockup）
- `server/README.md`：命令行服务器（编译/运行/命令/协议/测试）
- `board_app/THIRD_PARTY_NOTICES.md`：发布用第三方声明

设计图在 `docs/design/`：

- `mockup-home.png`：主界面
- `mockup-weather.png`：天气
- `mockup-gallery.png`：相册网格
- `mockup-editor.png`：图片编辑
- `mockup-games.png`：小游戏中心

真实运行截图（主机 headless 预览）在 `docs/screenshots/`，索引见该目录 README。

## 4. 关键决策速览

| 决策 | 选择 | 理由 |
|---|---|---|
| 语言 / 图形库 | C99 + LVGL 9.2.3 | 与现有 `lv_port_linux` 工程一致，资源占用低 |
| 屏幕 | 1024×600，单点触摸 | 现有工程 `screen*.png` 与 `main.c` 输入配置 |
| 目标构建 | `aarch64-linux-gnu-gcc` 交叉编译 | 现有顶层 CMake 已指定 |
| 项目目录名 | `board_app/`（英文） | 避免中文路径在 CMake、交叉编译、脚本中的编码问题；文档与 UI 保持中文 |
| UI/核心分离 | 独立 CMake 静态库 `board_core` / `board_ui` | 链接层面保证 `core` 不能偷偷调用 LVGL |
| 天气数据源 | `wttr.in` HTTP JSON（无需 Key），可替换 | 开发板实测可访问（部分 CDN 接口对板端线路返回 403）；提供 Mock/缓存兜底 |
| 图片解码/编码 | `stb_image` + `stb_image_write` | 单头文件、无额外系统库、支持 PNG/JPEG/BMP |
| 中文字体 | Noto Sans SC 子集 + TinyTTF，SimSun 16 兜底 | 开源 OFL、按需字形、避免 FreeType 构建依赖 |
| 小游戏核心 | [Little World Arcade](https://github.com/Start66613/littleworld-arcade) `arcade_core`（MIT，commit `106fb20`） | 纯 C、无 LVGL/IDF 依赖、含 2048/贪吃蛇/打砖块与单元测试 |
| 工程集成 | 新建 `board_app`，修改顶层 `main.c`/`CMakeLists.txt`/`lv_conf.h` | 入口仍由现有 fbdev/evdev 初始化，应用逻辑全部进新目录 |
| 输入法 | LVGL 自带 `lv_ime_pinyin`（MIT）封装成 `board_ui/ui_ime` | 开源、无额外依赖；词库由 Unihan 生成（401 音节 / 6763 字） |
| 联机通信 | 独立命令行服务器 + JSON 协议（TCP 可靠 / UDP 广播） | 服务器不链接 LVGL；口令 PBKDF2 散列存储 |
| 系统账户 | 会话单例 `board_ui/ui_comm_session`，聊天室与围棋共用 | 一个账号一条连接，避免同名互踢；本地记住账号可开机自动登录 |
| 围棋规则 | 移植 [cjlarose/weiqi.js](https://github.com/cjlarose/weiqi.js)（ISC）为纯 C | 板端只有 C 编译器；服务器用它做权威判定 |
| HTTPS | 运行时 `dlopen` 板端 OpenSSL 1.1（`core/src/tls_client.c`） | 交叉编译 sysroot 没有 openssl 头文件，但板端运行时有库 |
| 聊天记录 | `data/comm_history/<会话>.jsonl`，收到即落盘 | 收消息与 UI 解耦，页面没打开也不丢；界面只读历史 |

## 5. 当前状态

| 阶段 | 状态 | 说明 |
|---|---|---|
| Phase 0 文档与设计 | ✅ 完成 | 本文档集 + 5 张设计图 |
| Phase 1 骨架与构建 | ✅ 完成 | CMake、WSL 交叉编译通过；板端已运行 |
| Phase 2 主界面与主题 | ✅ 完成 | 首页、导航、状态栏、主机预览、板端截图 |
| Phase 3 天气 | ✅ 板端验证 | 广州在线 17℃/7 天；1 小时缓存过期重取已实测 |
| Phase 4 相册 | ✅ 板端网格验证 | 编辑/裁剪/保存已实现并在主机预览验证 |
| Phase 5 小游戏 | ✅ 完成 | 2048/贪吃蛇/打砖块（Little World Arcade）+ **联网围棋**（weiqi.js 移植），4 卡片自适应一行 |
| Phase 6 输入法与 AI 聊天 | ✅ 完成 | 可复用 `ui_ime`；DeepSeek 等 OpenAI 兼容接口，HTTPS 直连，Key 可存本地 |
| Phase 7 联机通信 | ✅ 完成 | 聊天室（账号/好友/私聊/群聊/离线补发/聊天记录）+ 联网围棋 |
| Phase 8 系统账户 | ✅ 完成 | 全系统统一登录、记住账号、开机自动登录 |
| Phase 9 集成与验收 | ◐ 部分完成 | 板端显示/网络/联机/AI 均已验证；**触摸手势长跑、开机自启待现场** |
| Phase 10 交付文档 | ✅ 完成 | 15 篇文档 + README + 板端/预览截图 |

> 说明：文档中的路径、接口、颜色值在实现阶段如有调整，必须同步更新对应文档；文档与代码不一致视为缺陷。

## 6. 目录命名约定

用户给出的结构使用中文描述（主页面代码、天气查询、核心代码等）。为了工具链稳定，代码目录使用英文，含义一一对应：

| 用户描述 | 实际目录 |
|---|---|
| 项目/ | `board_app/` |
| 主页面代码 | `board_app/main_page/` |
| app/天气查询/ui | `board_app/app/weather/ui/` |
| app/天气查询/核心代码 | `board_app/app/weather/core/` |
| app/相册/ui | `board_app/app/gallery/ui/` |
| app/相册/核心代码 | `board_app/app/gallery/core/` |
| app/小游戏/小游戏1/ui | `board_app/app/games/g2048/ui/` |
| app/小游戏/小游戏1/核心代码 | `board_app/app/games/g2048/core/`（第三方核心） |
| app/聊天室 | `board_app/app/comm/` |
| app/系统账户 | `board_app/app/account/` |
| app/小游戏/联网围棋 | `board_app/app/games/weiqi/` |
| 服务器（命令行） | `server/`（与 `board_app/` 同级，独立构建） |

完整目录树见 [03 目录结构与编码规范](03-directory-and-coding.md)。
