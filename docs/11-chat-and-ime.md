# AI 聊天与可复用输入法

> 文档编号：11
> 状态：与实现同步（2026-11-27），板端 DeepSeek 对话通过
> 关联：02 架构、04 UI、05 模块接口、09 第三方许可

## 1. 功能概述

聊天页把屏幕一分为二：

```text
┌──────────────────────────────┬──────────────────┐
│ 顶栏：返回 / AI 聊天 / 清空    │                  │
├──────────────────────────────┤  候选条（拼音）   │
│ 对话气泡（可滚动）             ├──────────────────┤
│                              │  虚拟键盘 26 键   │
├──────────────────────────────┤                  │
│ 输入框                [发送]  ├──────────────────┤
│ 提示：敲拼音出候选字…          │ 常用语 / 中英/送出│
└──────────────────────────────┴──────────────────┘
        左侧 496px                    右侧 512px
```

- 左半屏：对话记录、输入框、发送按钮。
- 右半屏：虚拟键盘 + 中文拼音候选条 + 常用语 + 中/英切换 + 送出。
- 回复来源：配置了 OpenAI 兼容接口时走在线大模型；未配置或失败时自动回退到离线助手。

## 2. 输入法：独立可复用模块

输入法不是聊天页的私有实现，而是 UI 框架层的通用组件：

| 文件 | 作用 |
|---|---|
| `ui/include/board_ui/ui_ime.h` | 公共 API |
| `ui/src/ui_ime.c` | 键位、候选条、词库加载、样式 |
| `assets/ime/pinyin_dict.txt` | 候选词库（401 音节 / 6763 字） |
| `data/ime/<app_id>.conf` | 每个应用自己的常用语（最多 4 行） |

任何页面只要有一个 `lv_textarea`，三行代码即可获得中文输入：

```c
lv_obj_t *ta = lv_textarea_create(page);
ui_ime_t *ime = ui_ime_create(page, ta);          /* 默认底部全宽 */
ui_ime_set_ready_cb(ime, on_send, page);          /* 回车/送出 */
ui_ime_load_quick_keys(ime, "chat");              /* 可选：常用语 */
```

分屏用法（聊天页就是这样做的）：

```c
ui_ime_t *ime = ui_ime_create(page, ta);
ui_ime_set_geometry(ime, LV_ALIGN_TOP_RIGHT, -8, 80, 512, 504);
```

其它 API：`ui_ime_insert()`、`ui_ime_clear_input()`、`ui_ime_set_visible()`、
`ui_ime_set_chinese()`、`ui_ime_get_keyboard()`、`ui_ime_dictionary_loaded()`。

### 2.1 输入法引擎（开源）

引擎使用 **LVGL 自带的开源拼音输入法** `lv_ime_pinyin`
（`lvgl/src/others/ime/lv_ime_pinyin.c`，MIT 许可，随 LVGL 一起分发）。
本工程只做三件事：

1. 在 `lv_conf.h` 打开 `LV_USE_IME_PINYIN`（关闭 9 键模式，省一张按键表）；
2. 用 `lv_ime_pinyin_set_dict()` 装入自定义词库；
3. 绑定到 `lv_keyboard`，并按项目主题重新着色。

因此候选字、翻页（`<` `>`）、退格、上屏都是引擎行为，不是自己写的拼音算法。

### 2.2 词库生成

引擎内置词库只有 322 个音节、几百个汉字，实际打字不够用。词库由脚本生成：

```bash
python3 board_app/tools/gen_pinyin_dict.py --unihan /tmp/Unihan.zip \
        --output board_app/assets/ime/pinyin_dict.txt
```

- 数据源：Unicode Han Database（Unihan `kMandarin`，Unicode License v3）；
- 范围：GB 2312 一、二级汉字共 6763 个（覆盖日常输入）；
- 处理：去声调、`ü → v`（与引擎的 `nv/lv` 约定一致）；
- 结果：401 个音节、6763 字，`pinyin<TAB>汉字` 每行一条，约 22 KB；
- 运行期由 `ui_ime.c` 一次性读进内存并交给 `lv_ime_pinyin_set_dict()`。

### 2.3 字体必须覆盖所有候选字

候选条直接用字体渲染汉字。词库扩到 6763 字之后，字体子集也必须同步扩大，
否则候选里会出现方框：

```bash
python3 board_app/tools/gen_chars.py board_app          # 扫描源码 + 词库字
python3 board_app/tools/gen_font_subset.py --input NotoSansSC-VF.ttf \
        --chars board_app/assets/fonts/chars.txt \
        --output board_app/assets/fonts/NotoSansSC-Board.ttf
python3 board_app/tools/check_font_coverage.py \
        board_app/assets/fonts/NotoSansSC-Board.ttf \
        board_app/assets/ime/pinyin_dict.txt           # 必须输出 missing: 0
```

当前字体：6899 个字形、约 2.2 MB。

### 2.3.5 接入 DeepSeek（HTTPS）

板端**能直接调 DeepSeek 官方 API**，不需要 PC 代理。

### key 存到文件里（推荐，一次就好）

```bash
cd /root/lvgl/board_apps
./ai_key sk-xxxxxxxx          # 保存到 data/settings.conf
./main                        # 以后直接启动，不用带环境变量
```

其他用法：

```bash
./ai_key                      # 查看当前 url/model/key（key 做掩码显示）
./ai_key --url https://api.deepseek.com/v1 --model deepseek-flash
./ai_key --clear              # 删除本地 key
```

写入的键（`data/settings.conf`，权限 0600）：

```text
ai.url=https://api.deepseek.com/v1
ai.model=deepseek-flash
ai.key=sk-xxxxxxxx
```

启动时日志会打一行确认：

```text
[chat] AI key loaded from local config (https://api.deepseek.com/v1, model=deepseek-flash)
```

> key 以**明文**保存在本地配置里（和 `comm.pass` 的弱混淆不同），
> 权限是 600、只有 root 可读。设备被别人拿到就等于交出 key，注意保管。

### 用环境变量临时覆盖

```bash
export BOARD_APP_AI_KEY=sk-xxxxxxxx     # 优先级高于本地保存的 key
export BOARD_APP_AI_URL=https://api.deepseek.com/v1
export BOARD_APP_AI_MODEL=deepseek-flash
./main
```

也可以全部显式指定：

```bash
export BOARD_APP_AI_URL=https://api.deepseek.com/v1
export BOARD_APP_AI_MODEL=deepseek-flash    # 官方模型：deepseek-flash / deepseek-v4-pro
export BOARD_APP_AI_KEY=sk-xxxxxxxx
```

实现方式（因为板子没有 openssl 开发头文件，但**运行时有库**）：

- `core/src/tls_client.c`：运行时 `dlopen` 板端的 `libssl.so.1.1` + `libcrypto.so.1.1`，
  自己声明用到的 OpenSSL 函数原型（`TLS_client_method` / `SSL_connect` /
  `SSL_ctrl` 设 SNI 等），编译期零依赖；
- `http_client.c` 支持 `https://`（默认端口 443），TLS 读写与非阻塞 socket 的
  `WANT_READ/WANT_WRITE` 重试都走 `poll`；
- 证书校验：用板端 `/etc/ssl/certs`，校验失败直接拒绝（不降级成不校验）。

**API key 走 `Authorization: Bearer <key>` 请求头**（`http_post_headers()`）。
早期版本漏了这个头，服务器一定返回 401，界面上表现为“大模型返回 HTTP 401”或
（网络层失败时）“网络不可用”。现在聊天页日志里会打印 `key=yes/no` 便于确认。

自检工具（不需要真 key，返回 401 就说明链路通）：

```bash
./tls_check
# tls_available: yes
# http_post rc=0 status=401 ...
# RESULT: TLS+HTTP OK (only key missing)
```

> **坑（已修）**：`chat_service` 里的取消标志曾写成 `bool cancel`，却按
> `volatile int *` 传给 `http_client` —— 类型双关会读到相邻内存的垃圾值，
> 请求一开始就被当成“已取消”，日志表现为
> `http_post failed rc=-111 (Connection refused)`、界面提示“网络不可用”。
> 现在改为 `volatile int cancel`。另外 `connect_host()` 失败原本返回 `-1`，
> 和 `EPERM` 撞码，现在返回 `-ECONNREFUSED`。

**前提：板端 DNS 要能解析**。板子上 `resolv.conf` 曾经是空的（dhcpcd 没写），
表现为 `wget: bad address`。修法：

```bash
echo 'nameserver 192.168.64.254' > /etc/resolv.conf
echo 'nameserver 8.8.8.8' >> /etc/resolv.conf
```

（要持久化就写进 `/etc/dhcpcd.conf` 的 `static domain_name_servers=...`。）

没有 key、或网络/DNS/证书任一步失败时，聊天页会自动回退到**离线助手**，
不会卡住界面。

### 2.4 性能：两个必须知道的坑

#### 坑 1：字体必须放进内存

`ui_theme_init()` 把 TTF 一次性读进 RAM，再为 5 个字号创建
`lv_tiny_ttf_create_data_ex()` 实例（共用同一份字节）。

原因（板上实测）：TinyTTF 每取一个字形都要通过 `lv_fs` 按偏移读字体表，而
`LV_FS_STDIO_CACHE_SIZE` 必须保持 0 —— LVGL 9.2 的 `lv_fs_read_cached()`
在短读时会算出错误的缓存区间，TinyTTF 随后陷入死循环（实测挂死）。没有文件缓存时，
2.2 MB 字体让“打开一页”需要 **44 秒**；放进内存后同一页 **0.65 秒**。

> 结论：不要开 `LV_FS_STDIO_CACHE_SIZE`；字体走内存。

#### 坑 2：kerning 让每次文本重排贵 1000 倍（真正的卡顿源）

`lv_tiny_ttf_create_data()` 默认用 `LV_FONT_KERNING_NORMAL`。字体带 kern 表时，
TinyTTF 会对**每一对相邻字符**调用 `stbtt_GetGlyphKernAdvance`（逐对 seek + 解析
kern 子表）。而 LVGL 的 label 在“改字体 / 改宽度 / 改换行模式 / 改文本”时都会
**重新量测整段文本**，于是每次样式变更都变成几毫秒到十几毫秒。

板上实测（`BOARD_APP_PERF=1` 的 label 分步探针，5 个汉字）：

| 操作 | 修前 | 修后（`LV_FONT_KERNING_NONE`） |
|---|---|---|
| `lv_obj_set_style_text_font` | **9.93 ms** | 0.48 ms |
| `lv_obj_set_width` | **9.92 ms** | 0.01 ms |
| `lv_label_set_long_mode` | **9.93 ms** | 0.01 ms |
| 一条 40 字气泡 | 139 ms | 30 ms（首次光栅化，之后为 0） |
| `lv_textarea_set_cursor_pos` | **1620 ms** | 1.19 ms |
| 键盘构建（46 键） | 336 ms | 34~47 ms |
| **聊天页整体创建** | **≈2.3 s** | **≈0.18 s** |
| 首帧最大耗时 | 758 ms | 21~32 ms |

修复只有一处：创建字体时传 `LV_FONT_KERNING_NONE`。中文排版本来不用字距对，
没有视觉损失。

#### 坑 3：键盘不要用 TTF 画符号

键盘按键是 ASCII + LVGL 内置符号（`LV_SYMBOL_BACKSPACE` 等私用区码点）。
Noto 里没有这些字形，走 TTF 就是“每次未命中 → 查一圈回退字体 → 写一条 error 日志”，
而日志是同步 `write`。实测一次页面创建产生 **210 条**这样的日志。

现在键盘的 `LV_PART_ITEMS` 字体直接指定内置 `lv_font_montserrat_20`
（纯查表、自带符号表），日志归零，键盘构建也更快。

#### 3.1 多线程边界

LVGL 的绘制、布局、字体光栅化**必须在 UI 线程**：LVGL 的堆与字形缓存不是线程安全的，
把 `lv_obj_*` 放到别的线程会直接崩。所以本项目按“能并行的才并行”来做：

| 工作 | 线程 | 说明 |
|---|---|---|
| 天气 HTTP 请求 | worker | 结果经 `board_async_queue` 回 UI 线程 |
| AI 聊天 HTTP 请求 | worker | 同上，带 `seq` 丢弃过期结果 |
| 拼音词库读取 + 解析 | worker | `ui_ime_preload()`，启动时与首页构建并行 |
| 图像解码/保存（相册） | 调用线程 | 见 05 文档 |
| 所有 `lv_*` 调用 | **仅 UI 线程** | 包括字形光栅化，不可并行 |

#### 3.2 性能探针

```bash
BOARD_APP_PERF=1 ./main        # 每 2 秒打印 fps / 帧耗时 / LVGL 堆
```

日志形如：

```text
[perf] fps=191.0 frame avg=12us max=431us slow(>33ms)=0 | heap used=96KB free=3985KB ...
[perf] mark keyboard     47.488 ms (since last)      # 页面创建分阶段耗时
[perf] label steps (ms/obj): create=0.01 set_text=0.00 set_font=0.48 ...
[perf] object probe (N=20): plain=0.01 label=0.21 flex_pct=0.01 ms/obj
```

默认关闭，`ui_perf_begin/end` 在未启用时直接返回，正常运行时零开销。

## 3. 聊天后端

| 文件 | 职责 |
|---|---|
| `app/chat/core/include/chat/chat_types.h` | 消息/配置/结果结构 |
| `app/chat/core/include/chat/chat_llm.h` + `.c` | OpenAI 兼容请求构造与响应解析 |
| `app/chat/core/include/chat/chat_offline.h` + `.c` | 离线助手（规则 + 四则运算 + 时间） |
| `app/chat/core/include/chat/chat_service.h` + `.c` | 工作线程、取消、结果投递 |
| `app/chat/ui/chat_ui.c` | 页面、气泡、发送、与我输入法联动 |

线程模型与天气一致：UI 线程发起 → worker 线程 HTTP → `board_async_queue`
→ UI 线程 `ui_async_pump` 回调里更新界面（worker 从不碰 `lv_obj_t`）。
每次请求带 `seq`，清空对话或退出页面后旧结果自动丢弃。

### 3.1 配置在线大模型

```bash
export BOARD_APP_AI_URL=http://192.168.1.10:11434/v1   # OpenAI 兼容 base
export BOARD_APP_AI_MODEL=qwen2.5:3b
export BOARD_APP_AI_KEY=sk-xxx                          # 可选
```

- 只要 `BOARD_APP_AI_URL` 非空即视为启用；请求发往 `<base>/chat/completions`。
- 只支持 `http://`（板端 HTTP 客户端不含 TLS）；本地 Ollama / vLLM / llama.cpp
  都满足。
- 超时 25 秒；失败、非 200、解析失败都会**自动回退离线助手**，并在气泡下标注原因。
- 顶栏右侧显示“在线大模型 / 离线助手”。

### 3.2 离线助手

没有网络也能演示：问候、时间、输入法/开发板说明、简单四则运算（支持括号、`×÷`）、
以及兜底回声。实现见 `chat_offline.c`。

## 4. UI 细节

- 气泡：用户靠右、主色底；助手靠左、深灰底 + `AI` 圆点头像。
- “正在思考…”带省略号动画（`lv_timer`，400 ms）。
- 发送后输入框清空、发送按钮置灰，回复到达后恢复。
- 常用语来自 `data/ime/chat.conf`：你好 / 现在几点？ / 你会什么？ / 谢谢。
- 键盘按键 `中/英` 切换写在输入法组件里，默认中文。

## 5. 已知限制

- 只支持 26 键全拼，不提供 9 键/笔画/双拼；多字词候选需要扩展词库格式（引擎按单音节匹配）。
- 词库是“音节 → 候选字”，不做整句联想（受引擎能力限制）。
- 大模型接口必须是明文 HTTP；需要 TLS 时要新增 `http_client_tls`。
- LVGL 的绘制/字体光栅化只能在 UI 线程，所以“打字时首帧”仍有一次性光栅化成本
  （约 2.5 ms/汉字，之后走字形缓存），这是 TinyTTF 的固有限制。
- `ttf_get_glyph_dsc_cb: cache not allocated` 若仍出现在日志里：说明某个字符
  既不在 Noto 子集、也不在回退字体里；正常 UI 已无此日志（键盘符号改内置字体后归零）。

## 6. 验收清单

1. 首页出现第 4 张卡片“AI 聊天”，图标为对话气泡。
2. 进入后左聊天、右键盘，键盘可点、候选条在键盘上方。
3. 敲 `ni` 出现 `妮 霓 倪 泥 尼 拟`，点候选上屏。
4. `中/英` 可切换；`1#` 进入符号；`ABC` 大写。
5. 未配置大模型时，输入“3*(4+5)”得到“3*(4+5) = 27”。
6. 配置 `BOARD_APP_AI_URL` 后能收到模型回复，顶栏显示“在线大模型”。
7. 断网/接口错误时自动回退离线助手并提示原因。
