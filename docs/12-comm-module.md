# 通信模块：聊天室（TCP 私聊 + UDP 广播）

> 文档编号：12
> 状态：与实现同步（2026-11-27），板端双客户端联调通过
> 关联：02 架构、05 模块接口、11 聊天与输入法

## 1. 组成

```text
server/                        服务器（纯命令行，不链接 LVGL，可单独运行）
├── server.c                   TCP/UDP 主循环 + 控制台命令
├── store.c/.h                 账号/好友/离线消息持久化
├── sha256.c/.h                口令加盐散列（PBKDF2-HMAC-SHA256）
├── Makefile                   本机 / aarch64 交叉编译
└── tools/
    ├── test_client.c          命令行测试客户端（同一套协议）
    └── smoke_test.sh          一键端到端联调脚本

board_app/app/comm/            客户端
├── core/include/comm/
│   ├── comm_proto.h           帧格式、消息类型、字段名校验（纯 C）
│   ├── comm_net.h             POSIX socket 封装（TCP/UDP/整帧收发）
│   └── comm_client.h          客户端网络核心（收发线程 + 事件队列）
├── core/src/                  上述三者的实现
└── ui/comm_ui.c               LVGL 界面：登录/注册 → 好友列表 + 聊天
```

客户端界面复用第 11 篇的 `board_ui/ui_ime` 输入法组件（中文拼音候选）。

## 2. 协议：所有业务数据都是 JSON

参考代码里那种 `"目标ip:端口 消息"` 的裸文本格式**没有沿用**。现在所有报文的
业务负载都是 JSON，用项目自带的 cJSON 生成/解析。

### 2.1 TCP（可靠通道）

```text
+--------+--------+---------------------------+
| len:4  | type:1 | JSON body (len-1 字节)     |
+--------+--------+---------------------------+
  len = 1 + strlen(json)，大端；上限 65535
```

举例（登录）：

```text
len=0x00000031 type=2  {"user":"alice","pass":"alice123","udp_port":41346}
```

服务器应答：

```text
type=32 {"for":2,"msg":"登录成功"}
type=33 {"for":2,"code":13,"msg":"密码错误"}
```

私聊投递：

```text
type=35 {"from":"bob","text":"你好","ts":1795673372,"offline":false}
```

好友列表：

```text
type=34 {"friends":[{"user":"bob","online":true}]}
```

### 2.2 UDP（不可靠通道）

UDP 有数据报边界，不需要长度前缀：

```text
+--------+---------------------------+
| type:1 | JSON body                 |
+--------+---------------------------+
```

- 服务器 → 客户端 `type=128`：`{"online":["alice","bob"]}` 在线名单广播
- 客户端 → 服务器 `type=130`：`{"text":"大家好"}` 群聊上送
- 服务器 → 客户端 `type=131`：`{"from":"alice","text":"大家好","ts":...}` 群聊广播

UDP 只用于“丢了也无所谓”的数据：在线状态、群聊。**不重传、不保证顺序**。
私聊、登录、好友、离线补发全部走 TCP。

### 2.3 消息类型表

| 值 | 方向 | 说明 | JSON 字段 |
|---|---|---|---|
| 1 | C→S | 注册 | `user` `pass` |
| 2 | C→S | 登录 | `user` `pass` `udp_port` |
| 3 | C→S | 加好友 | `user` |
| 4 | C→S | 删好友 | `user` |
| 5 | C→S | 拉好友列表 | — |
| 6 | C→S | 私聊（TCP） | `to` `text` |
| 7 | C→S | 心跳 | — |
| 8 | C→S | 退出 | — |
| 32 | S→C | 成功 | `for` `msg` |
| 33 | S→C | 失败 | `for` `code` `msg` |
| 34 | S→C | 好友列表 | `friends[]` |
| 35 | S→C | 收到私聊 | `from` `text` `ts` `offline` |
| 36 | S→C | 好友上下线 | `user` `online` |
| 37 | S→C | 有人加你 | `user` `by` |
| 39 | S→C | 系统公告 | `msg` |
| 128 | S→C | 在线名单（UDP） | `online[]` |
| 130 | C→S | 群聊上送（UDP） | `text` |
| 131 | S→C | 群聊广播（UDP） | `from` `text` `ts` |

## 3. 服务器

### 3.1 编译运行

```bash
cd server
make                                  # 本机 x86_64
make CROSS=aarch64-linux-gnu- CFLAGS="-std=gnu11 -O2 -static -Wall"   # 板端
./chat_server                                   # 默认 0.0.0.0:20000，UDP 20001
./chat_server --port 20000 --bind 0.0.0.0 --data ./data
./chat_server --no-color                        # 或 NO_COLOR=1
```

服务器是纯命令行程序，后台运行时 stdin 关闭也不影响服务。

### 3.2 控制台命令

| 命令 | 说明 |
|---|---|
| `/help` | 帮助 |
| `/users` | 在线用户（名字/来源 IP/UDP 端口/空闲时间） |
| `/accounts` | 所有已注册账号 |
| `/msg <user> <text>` | 以服务器身份发消息（对方离线则暂存） |
| `/broadcast <text>` | 给所有在线用户发系统公告 |
| `/kick <user>` | 强制断开 |
| `/status` | 监听地址、运行时长、在线/注册数、TCP/UDP 计数、离线消息数 |
| `/clear` `/quit` | 清屏 / 退出 |

### 3.3 本地存储

```text
data/
├── accounts.txt          # 每行: <user> <salt_hex> <hash_hex>
├── friends.txt           # 每行: <a> <b>（双向各一行）
└── offline/<user>.jsonl  # 每行一条 {"from","text","ts"}，登录时取走并清空
```

- 口令：`PBKDF2-HMAC-SHA256(pass, salt16, 20000 轮)`，盐用 `/dev/urandom`。
  `accounts.txt` 里**没有明文口令**，实测 `grep 明文` 命中 0。
- 写文件用“临时文件 + rename”，进程被 kill 也不会留下半个文件。
- 离线消息：对方不在线时 `{from,text,ts}` 追加到 `offline/<user>.jsonl`，
  对方下次登录先按 TCP 逐条补发，再清空。

## 4. 客户端（LVGL）

- 首页新增「聊天室」卡片（`app/comm`）。
- **登录界面不在聊天室里**：账户是系统级的，登录/注册/好友管理在首页顶栏的「账户」页。
  聊天室未登录时只显示提示 + 「去登录」按钮。
- 会话页：左好友列表（含「群聊 · UDP」一项、在线圆点、未读「新」标记）、
  中间消息区 + 输入框 + 发送、右侧是 `ui_ime` 中文输入法键盘。
- 「加好友」弹窗输入用户名即可（服务器直接建立双向好友关系）。
- 顶栏显示「聊天室 · 用户名」「在线 n/m」。
- 断线自动回到登录页并提示。

网络线程（`comm_client`）只做收发，收到帧解析成 `comm_event_t` 投进
`board_async_queue`，UI 线程用 `ui_async_pump` 消费，worker 从不碰 `lv_obj_t`。

**连接与登录态由 `board_ui/ui_comm_session` 统一持有**（单例）：聊天室、联网围棋、
账户页共用同一条 TCP 连接与好友缓存。页面销毁只退订事件，不主动断开。
详见 [14 系统账户](14-account-system.md)。

### 4.1 演示/看板自动登录

```bash
BOARD_APP_START=comm \
BOARD_APP_COMM_AUTOLOGIN="127.0.0.1:20000,alice,alice123" ./main
```

## 5. 端到端验证

主机侧（一条命令跑完注册→登录→加好友→在线私聊→离线暂存→补发→UDP 群聊）：

```bash
bash server/tools/smoke_test.sh
```

关键输出示例：

```text
alice [ok] 已添加好友 (for=3)
alice [friends] bob(离线)
bob   [msg] alice: 你好，这是在线消息
alice [ok] 已发送 (for=6)          # bob 离线，服务器暂存
bob   [msg] (离线补发) alice: 这条是你离线时发的
alice [group] alice: 大家好，这是 UDP 群聊
bob   [group] alice: 大家好，这是 UDP 群聊
```

板上实测（服务器 + 客户端都在板子上）：

```text
/root/lvgl/board_apps/chat_server      # aarch64 静态链接
/root/lvgl/board_apps/main             # LVGL 客户端
BOARD_APP_COMM_AUTOLOGIN="127.0.0.1:20000,alice,alice123"
```

第二个客户端发消息后，界面上实时出现 `bob: 你好，我是 bob（板上来客）`。

## 6. 启动与进程管理

### 6.1 启动

```bash
cd /root/lvgl/board_apps

./chat_server                      # 前台：默认 0.0.0.0:20000，UDP 20001，数据 ./data
./chat_server --port 20100         # 换端口（UDP 自动用 20101）
./chat_server --data /root/lvgl/chat_data    # 指定数据目录

# 后台常驻（关掉终端也不退）
nohup ./chat_server > /tmp/chat_server.log 2>&1 &
tail -f /tmp/chat_server.log       # 看服务器日志
```

### 6.2 停止

```bash
killall chat_server     # 或前台运行时 Ctrl-C
```

### 6.3 启动失败：`Address already in use`

```
TCP 监听失败：端口 20000 已被占用。
```

说明已经有一个 `chat_server` 在跑（很可能是之前 `nohup`/后台启动的实例）。
现在服务器会直接给出处理办法：

```bash
pgrep -l chat_server      # 确认进程
killall chat_server       # 结束它
./chat_server             # 再启动
```

也可以换端口：`./chat_server --port 20100`（客户端登录页地址要一起改）。

### 6.4 板上自测账号
`/root/lvgl/board_apps/data` 里预置了演示账号（也可用客户端自己注册）：

| 账号 | 密码 |
|---|---|
| alice | alice123 |
| bob | bob123 |

两者已互为好友。从 PC 连板子上的服务器：

```bash
cd server && make && ./chat_test_client 192.168.64.216 20000
```

### 6.5 聊天记录（本地持久化）

聊天内容按会话存到本地，重启后自动恢复：

```text
<data>/comm_history/
├── bob.jsonl          # 和 bob 的私聊
└── group.jsonl        # 群聊
```

每行一条 JSON Lines：

```json
{"from":"bob","mine":0,"ts":1795692061,"text":"你好呀，最近怎么样？"}
```

- 发送成功、收到消息时各追加一条（`comm_history_append`）；
- 切换会话时 `comm_history_load` 读最近记录重建消息区（`comm_history_init` 在
  聊天页创建时建目录）；
- 每个会话只保留最近 **200** 条，超过就重写文件丢最旧的；
- 换行符会替换成空格，保证一条一行；
- 文件写失败只记日志，不影响聊天。

界面上就是重启客户端后，打开聊天室就能看到之前的对话（不需要服务器再补发）。

### 6.6 收消息的线程模型（现在的做法）

按“收消息与 UI 彻底解耦”重构：

```text
board_app_init()
  └─ comm_session_init(paths)      ← 登录之前
       ├─ comm_history_init()      建 data/comm_history
       ├─ comm_client_create()     网络线程从这里就在收包
       └─ 收到 INCOMING/GROUP ──> 立刻 comm_history_append()
                                   （不看聊天页是否打开、有没有订阅者）

聊天页（comm_ui）
  └─ 只做两件事：切会话时 load_history()、收到事件时再 load_history()
     显示的永远是历史文件里的内容，内存不另存一份
```

这样服务器补发的消息一旦到达就落盘，进不进聊天室都不会丢；UI 只是历史的
一个视图。旧实现依赖“页面订阅者存在”，页面没开时事件被丢弃，是补发收不到的根因。

### 6.7 离线补发的两个坑

1. **聊天页没打开时，事件会丢**：开机自动登录后停在首页，服务器补发的
   `INCOMING` 到了但没人订阅 → 事件被丢掉。**现在的做法不是缓存消息副本**，
   而是让网络线程收到消息后**立刻写入聊天记录文件**（session 层，早于登录就
   初始化），所以页面开不开都不影响落盘；页面只是去读历史。
2. **补投要写历史**：补投时同时 `add_log` + `save_history`，否则数据只在内存，
   切会话被 `load_history` 重建时又会丢。

## 7. 常见误解：SO_REUSEADDR 能解决什么

`comm_net.c` 的 `comm_tcp_listen()` 里一直有：

```c
int on = 1;
setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));   /* comm_net.c:74 */
```

它解决的是 **TIME_WAIT**：服务器刚退出、端口上还留着已关闭连接时，重启不用等
（否则可能 `Address already in use`）。**它不能**让两个正在运行的进程共享同一端口。

两者的区别（本机实测）：

| 场景 | 只设 `SO_REUSEADDR` | 再加 `SO_REUSEPORT` |
|---|---|---|
| 端口上有 TIME_WAIT 残留，立刻重启同一程序 | 成功 | 成功 |
| 已经有一个进程在 `listen`，再起一个 | **`Address already in use`** | 两个都能 `listen` |

所以“端口被占用”**不是**少写了 `SO_REUSEADDR`，而是确实还有另一个实例在跑。
本项目**故意不用 `SO_REUSEPORT`**：它会让内核把新连接分给不同进程，两个进程各有
自己的在线表和内存里的会话状态，会出现“A 登录了 B 看不到、私聊投递失败”。
一个端口一个实例才是正确语义；要同时跑多个服务器就换端口。

乌班图/主流 Linux 内核对监听端口 rebind 已经比较宽松（实测去掉 `SO_REUSEADDR`
也能在 TIME_WAIT 后立刻启动），保留这一行仍然有益于老内核与其它 BSD 系行为差异。

## 8. 已知限制

- 服务器单进程多线程（每连接一个线程），上限 64 连接；适合教室/小规模演示。
- 明文 TCP：口令与会话内容不加密，适合内网；要上公网请套 TLS/VPN。
- 群聊走 UDP，跨网段/丢包时可能收不到，属预期行为。
- 好友关系是“直接添加”，没有确认/拒绝流程。
- 历史消息只在内存（每个会话最近 200 条），重登录后从服务器补发的只有离线消息。
