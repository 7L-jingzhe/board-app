# 第三方开源与许可证

> 文档编号：09
> 状态：与实现同步（2026-11-27）
> 原则：只引入与项目许可兼容的代码；保留版权声明；能少拷贝就少拷贝；所有第三方代码可追溯。

## 1. 许可证总览

| 组件 | 版本 / 来源 | 许可证 | 使用方式 | 是否修改 |
|---|---|---|---|---|
| LVGL | 9.2.3（现有 `lvgl/`） | MIT | 图形库，链接 | 否（仅改 `lv_conf.h`） |
| Little World Arcade `arcade_core` | commit `106fb20f042729a17712ee0be186b8f2524228f5` | MIT | 2048/贪吃蛇/打砖块规则核心 | 否（仅复制；适配在 UI 层） |
| weiqi.js | `master`（2026-11 取用） | **ISC** | 联网围棋规则核心（移植为 C） | 是（语言移植，语义保持一致） |
| cJSON | 1.7.18 | MIT | 天气/聊天/通信 JSON | 否 |
| stb_image | 2.30（或 vendored 版本） | Public Domain / MIT 双许可 | PNG/JPEG/BMP 解码 | 否 |
| stb_image_write | 1.16（或 vendored 版本） | Public Domain / MIT 双许可 | PNG 编码保存 | 否 |
| Noto Sans SC | Google Fonts / noto-cjk | SIL Open Font License 1.1 | 中文 UI 字体（子集） | 子集化（允许） |
| 示例照片 | 本项目生成 | CC0 1.0 | 相册演示 | — |
| 设计图 | 本项目生成 | CC0 1.0 | 文档 | — |

> 版本号以实现阶段 vendored 文件为准；`UPSTREAM.md` 记录实际 commit/版本与校验和。上表在 vendoring 后更新。

## 2. 组件详情

### 2.1 LVGL 9.2.3

- 仓库：<https://github.com/lvgl/lvgl>
- 许可证：MIT
- 位置：`lv_port_linux-release-v9.2/lvgl/`（现有子模块/源码）
- 版权：Copyright (c) 2021 LVGL Kft.
- 说明：本项目不修改 LVGL 业务源码；只通过 `lv_conf.h` 启用 fbdev、TinyTTF、字体、内存等配置。为了让 WSL 在没有 aarch64 C++ 编译器时完成 C-only 构建，`lvgl/CMakeLists.txt` 的 `project(...)` 语言列表从 `C CXX ASM` 改为 `C ASM`（并在顶层关闭 ThorVG/Lottie/Vector Graphic）。这是一处构建适配，不改变 LVGL 的 C 源码。LVGL 自身捆绑的第三方组件（lodepng、thorvg、stb_truetype 等）按其目录内许可证执行，随 LVGL 分发。
- 合规：保留 `lvgl/LICENCE.txt`；不在项目文档中移除 LVGL 版权。

### 2.1.1 glibc 2.27 构建 sysroot（仅构建期）

- 来源：Ubuntu 18.04 `libc6-dev-arm64-cross` / `libc6-arm64-cross` / `linux-libc-dev-arm64-cross`。
- 位置：`$HOME/aarch64-bionic`（由 `tools/setup_bionic_sysroot.sh` 下载解压，**不随项目分发**）。
- 许可证：glibc 为 LGPL-2.1+；sysroot 只用于编译/链接，不进入发布包。板端运行的是
  Buildroot 自带的 glibc 2.29，属于动态链接的系统库。
- 目的：避免 WSL gcc 的 glibc 2.38 符号导致板上 `GLIBC_2.33 not found`。

### 2.2 Little World Arcade（arcade_core）

- 仓库：<https://github.com/Start66613/littleworld-arcade>
- commit：`106fb20f042729a17712ee0be186b8f2524228f5`（2026-10-24）
- 许可证：MIT
- 版权：Copyright (c) 2026 Little World Arcade contributors
- 复制范围（仅核心，不含 ESP-IDF/LVGL UI）：
  - `firmware/components/arcade_core/include/arcade/arcade_common.h`
  - `.../arcade_2048.h`、`arcade_snake.h`、`arcade_breakout.h`
  - `.../src/arcade_2048.c`、`arcade_snake.c`、`arcade_breakout.c`、`arcade_rng.c`
- 不复制：`arcade_save.c/h`（依赖 ESP-IDF NVS）、`arcade_ui/`、字体、固件工程。
- 修改：**无算法修改**。共享的 `arcade_rng.c` 与四个头文件放 `app/games/core/`（只编译一次）；三个游戏规则分别放 `app/games/{g2048,snake,breakout}/core/src/`；UI 适配在 `app/games/*/ui/`。`third_party/littleworld_arcade/` 保留 LICENSE/UPSTREAM 以便追溯。
- 为什么选它：
  - 纯 C、无 LVGL/ESP-IDF 依赖，天然符合前后端分离；
  - 三个游戏统一 API/状态机，降低维护成本；
  - 上游有 1393 项主机测试（见其 README/STATUS）；
  - MIT，适合课程/商业项目二次开发；
  - 目标屏幕同为 1024×600（其固件用于 7 英寸 1024×600 板）。
- 风险：上游为个人实验项目（0 star），但代码结构清晰、可测试；本项目会保留完整测试与来源，必要时可替换同接口实现。

### 2.2.1 weiqi.js（联网围棋规则核心）

- 仓库：<https://github.com/cjlarose/weiqi.js>
- 版本：`master` 分支（2026-11 取用），上游原文保留在 `third_party/weiqi-js/`
- 许可证：**ISC**（Copyright (c) 2014-2015, Christopher La Rose）
- 位置：
  - 上游原文与许可：`board_app/third_party/weiqi-js/{board.js,game.js,LICENSE-ISC.txt,UPSTREAM.md}`
  - C 移植实现：`board_app/app/games/weiqi/core/`（客户端显示 + 服务器权威判定共用）
- 修改说明：把 JavaScript 的 immutable 结构改为 C 可变结构 + 固定数组，
  用 64 位 FNV 哈希集合实现位置超劫，异常改为负错误码；
  额外补了贴目/认输/棋盘序列化（属于本项目适配层）。
  逐函数映射与改动见 `third_party/weiqi-js/UPSTREAM.md`。
- ISC 合规：许可允许修改与再分发，**必须保留版权声明与许可文本**；
  已在 `weiqi.h`/`weiqi.c` 文件头注明出处，并保留 `LICENSE-ISC.txt`。

### 2.3 cJSON 1.7.18

- 仓库：<https://github.com/DaveGamble/cJSON>
- 许可证：MIT
- 版权：Copyright (c) 2009-2017 Dave Gamble and cJSON contributors
- 位置：`core/third_party/cjson/`
- 使用：解析天气接口 JSON；只使用 `cJSON_Parse`、对象/数组遍历、`cJSON_GetObjectItemCaseSensitive`、`cJSON_Delete`。
- 修改：无；保留 `LICENSE` 与源码头。
- 备注：cJSON 解析失败时返回 NULL；天气解析器必须检查每个关键节点类型。

### 2.4 stb_image / stb_image_write

- 仓库：<https://github.com/nothings/stb>
- 许可证：Public Domain（或 MIT，二选一）；版权 Sean Barrett 及贡献者
- 位置：`core/third_party/stb/`
- 使用：
  - `stb_image.h`：解码 PNG/JPEG/BMP 为 RGBA8；
  - `stb_image_write.h`：编码 PNG。
- 配置：
  ```c
  #define STB_IMAGE_IMPLEMENTATION
  #define STB_IMAGE_WRITE_IMPLEMENTATION
  ```
  只在**一个** `.c`（如 `core/src/stb_impl.c`）中定义实现，避免重复符号。
- 修改：无；保留 `LICENSE`/`UPSTREAM.md`。

### 2.5 Noto Sans SC

- 来源：<https://github.com/notofonts/noto-cjk> / Google Fonts
- 许可证：SIL Open Font License 1.1（OFL-1.1）
- 位置：`assets/fonts/NotoSansSC-Board.ttf`（子集）、`assets/fonts/OFL.txt`
- 修改：使用 `fonttools` 子集化（保留字形轮廓），属于 OFL 允许的修改；文件名加 `-Board` 以示区分。
- 义务：
  - 随软件分发 `OFL.txt`；
  - 不单独售卖字体；
  - 若修改字体，保留版权声明，不改变保留字体名称（Reserved Font Name）规则；本项目不改名销售。
- 回退：LVGL 内置 `lv_font_simsun_16_cjk`（LVGL 分发，按其许可）。

### 2.6 示例照片

- 由项目用 Pillow 以几何图形/渐变生成（一次性生成，PNG 已入库），不含第三方照片/肖像/商标。
- 许可：CC0 1.0（公共领域贡献）；可自由替换为用户自己的照片。
- 位置：`assets/photos/sample-*.png`。
- 注意：用户放入的照片版权归用户；项目不附带任何未授权图片。

### 2.7 设计图

- `docs/design/mockup-*.png` 由项目用 Pillow 绘制，使用本机中文字体渲染，仅作为设计示意。
- 许可：CC0 1.0。
- 不分发设计图使用的 Windows 系统字体（msyh/simhei）。

## 3. 许可证兼容性分析

| 方向 | 结论 |
|---|---|
| MIT 代码进入本项目（MIT） | 兼容；保留版权与许可声明 |
| Public Domain/MIT 双许可（stb） | 兼容；选择 MIT 或 PD 均可 |
| OFL 字体随软件分发 | 兼容；保留 OFL、不单独售卖 |
| GPL/LGPL 代码 | **不引入**；避免传染性/动态链接争议 |
| Apache-2.0 代码 | 兼容（需保留 NOTICE）；本期未引入 |
| 未知许可证代码 | **不引入**，除非获得书面许可并记录 |

本项目自身许可：**MIT**（见根目录 `LICENSE`）。第三方组件仍按其各自许可证；MIT 不覆盖第三方代码。

## 4. 分发清单

发布包（tar/镜像）必须包含：

```text
board_app/                       # 板端运行目录
├── main
├── chat_server                  # 可选：聊天/围棋服务器（含 game.c 的围棋规则）
├── chat_test_client             # 可选：命令行测试客户端
├── ai_key / ai_check / tls_check # 可选：自检与配置工具
├── assets/
│   ├── fonts/NotoSansSC-Board.ttf
│   ├── fonts/OFL.txt
│   ├── ime/pinyin_dict.txt      # 由 Unihan 派生（Unicode License）
│   └── photos/sample-*.png
├── data/                        # 可为空，运行时创建
├── LICENSE                      # 本项目 MIT
├── THIRD_PARTY_NOTICES.md       # 本文件的精简版
└── README.md
```

源码仓库还要保留 `third_party/weiqi-js/{LICENSE-ISC.txt,UPSTREAM.md}`（含上游原文）。

- `THIRD_PARTY_NOTICES.md` 至少列出：LVGL、Little World Arcade、**weiqi.js（ISC）**、cJSON、stb、Noto Sans SC、Unihan，以及各自许可证与链接。
- 源码仓库保留完整 `LICENSE`/`OFL.txt`/`UPSTREAM.md`。
- 不在发布包中包含 Windows 字体、设计源文件、构建中间产物。

## 5. 合规检查清单

- [ ] `third_party/littleworld_arcade/LICENSE` 存在且为 MIT 原文。
- [ ] `third_party/littleworld_arcade/UPSTREAM.md` 记录仓库/commit/复制文件/修改说明。
- [ ] `third_party/weiqi-js/LICENSE-ISC.txt` 存在且为 ISC 原文；`UPSTREAM.md` 记录仓库/版本/移植映射。
- [ ] 移植后的 `weiqi.h`/`weiqi.c` 文件头保留原作者版权与 ISC 声明。
- [ ] `core/third_party/cjson/LICENSE` 存在。
- [ ] `core/third_party/stb/LICENSE` 存在。
- [ ] `assets/fonts/OFL.txt` 存在；字体来源/子集命令记录。
- [ ] 源码中保留第三方版权头（`SPDX-License-Identifier: MIT` 等）。
- [ ] 发布包包含 `THIRD_PARTY_NOTICES.md`。
- [ ] 无 GPL/AGPL/未知许可证代码。
- [ ] 示例图片/设计图无第三方版权素材。
- [ ] 未提交 Windows 系统字体/商业字体。

## 6. 参考链接

- LVGL：<https://github.com/lvgl/lvgl>
- LVGL 许可证：<https://github.com/lvgl/lvgl/blob/master/LICENCE.txt>
- Little World Arcade：<https://github.com/Start66613/littleworld-arcade>
- cJSON：<https://github.com/DaveGamble/cJSON>
- stb：<https://github.com/nothings/stb>
- Noto CJK：<https://github.com/notofonts/noto-cjk>
- SIL OFL 1.1：<https://scripts.sil.org/OFL>
- CC0 1.0：<https://creativecommons.org/publicdomain/zero/1.0/>

> 本文不构成法律意见；如用于商业发布，请由项目所有者复核许可证与分发方式。
