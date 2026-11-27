# 联网围棋（小游戏子模块）

> 文档编号：13
> 状态：与实现同步（2026-11-27），双客户端真机对弈通过
> 入口：首页 → 小游戏 → 联网围棋（`app_id = "weiqi"`，`parent_id = "games"`）

## 1. 组成

```text
board_app/app/games/weiqi/
├── core/include/weiqi/weiqi.h     规则核心 API（纯 C，无 LVGL）
├── core/src/weiqi.c               规则实现（移植自 weiqi.js，ISC）
└── ui/weiqi_ui.c                  LVGL 界面：未登录提示 / 大厅 / 对局

board_app/third_party/weiqi-js/    上游原文 + LICENSE-ISC.txt + UPSTREAM.md

board_app/tests/host/test_weiqi.c  规则单测（提子/自杀/超劫/停手/数子/序列化）

server/game.c / game.h             对局房间 + 权威规则判定 + 邀请队列
```

注册信息：`id = "weiqi"`、`parent_id = "games"`、`show_on_home = false`、
`order = 4`（游戏中心第 4 张卡片）、图标 `UI_ICON_WEIQI`。

规则核**移植自开源项目 [cjlarose/weiqi.js](https://github.com/cjlarose/weiqi.js)**
（ISC 许可），上游原文与许可文本保留在 `third_party/weiqi-js/`，移植映射与改动
说明见该目录 `UPSTREAM.md`。

## 2. 规则范围

| 规则 | 说明 |
|---|---|
| 棋盘 | 9 / 13 / 19 路，大厅可选，默认 13 |
| 提子 | 落子后先提掉无气的敌方块（上游 `placeStone`） |
| 禁止自杀 | 落子后自己无气则非法（上游默认 `allowSuicide=false`） |
| 劫争 | **位置超劫**（positional superko）：回到历史局面即非法（上游 `history` + “Violation of Ko”） |
| 停一手 | 连续两次 pass 终局（上游 `isOver`） |
| 数子 | 中国规则：子 + 单方围空（上游 `areaScore`） |
| 贴目 | 黑贴 3.5 子（7 目半），本项目适配层 |
| 认输 | 本项目适配层（上游无此概念） |

移植实现逐函数对应上游（`groupAtPosition` → `group_at`、`placeStone` →
`place_stone`、`areaScore` → `weiqi_area_score` 等），单测覆盖提子/自杀/超劫/
停手/数子/序列化。

## 3. 服务器是规则权威

客户端**不自己判断合法性**：

```text
玩家点击棋盘 -> C→S GAME_MOVE {"x","y"}   （只是“我想下这里”）
             -> 服务器 weiqi_play() 判定
             -> S→C GAME_STATE {"board","turn","bc","wc","last_x","last_y","over","result"}
             -> 双方按同一份棋盘重绘
```

好处：两边不可能不一致，也不用信任客户端。棋盘用 361 字节文本下发
（`'B'` 黑 / `'W'` 白 / `'.'` 空，按行拼接），一次全量同步，简单且够快。

### 3.1 协议消息

| 值 | 方向 | 说明 | JSON |
|---|---|---|---|
| 16 | C→S | 邀请对局 | `user` `size` |
| 17 | C→S | 接受邀请 | `from` |
| 18 | C→S | 落子 | `x` `y` |
| 19 | C→S | 停一手 | — |
| 20 | C→S | 认输 | — |
| 21 | C→S | 离开对局 | — |
| 22 | C→S | 撤回邀请 | `user` |
| 48 | S→C | 收到邀请 | `from` `size` |
| 49 | S→C | 开局 | `opponent` `size` `color` |
| 50 | S→C | 权威局面 | `board` `turn` `bc` `wc` `last_x` `last_y` `over` `result` |
| 51 | S→C | 邀请被拒/失效 | `user` `msg` |
| 52 | S→C | 对局结束 | `result` `reason` |
| 53 | S→C | 对手离开 | `user` |
| 54 | S→C | 邀请被撤回 | `user` |

邀请方执黑先行；只有好友之间能邀请；同一人同时只能在一盘棋里。

## 4. 与聊天室共用登录态

两个应用都需要账号。如果各开一条连接，同名账号会被服务器“后登录踢先登录”。
所以连接/登录/好友列表/事件流统一放在 `board_ui/ui_comm_session`：

- 会话单例：一个 `comm_client` + 一个事件队列 + 事件多播（最多 4 个订阅者）
- `comm_session_init()` 幂等，任何页面首次进入时调用
- 页面销毁只 `unsubscribe`，**不断开连接**
- 好友列表在 session 里缓存，围棋大厅直接用
- 页面没打开时收到的对局邀请会暂存一次，等页面订阅后补处理
  （`comm_session_take_pending_invite`）
- `BOARD_APP_COMM_AUTOLOGIN="host:port,user,pass"` 统一在 session 层实现，
  聊天页和围棋页都支持（看板/演示模式）

## 5. 界面

- **未登录**：提示 + 「去聊天室登录」按钮（跳转 `comm`）
- **大厅**：9/13/19 路选择、好友列表（在线可邀请，离线置灰）、「邀请」
- **对局**：左侧棋盘（网格 + 棋子 + 最后一手高亮），右侧信息面板
  （对手、路数、执子颜色、双方提子数、轮次、结果）+「停一手」「认输」「退出对局」

棋盘用 `lv_line` 画网格、预建 361 个圆形 `lv_obj` 当棋子（按局面显隐），
点击用坐标反算交叉点，不依赖额外资源。

> 坑：路数按钮（9/13/19 路）放在一个 `row` 容器里，最初切换高亮时遍历的是
> 大厅容器的**直接子对象**，所以一个按钮都匹配不到——提示文字变了、高亮不变。
> 现在把三个按钮指针存在 `size_btns[]` 里按指针更新颜色。

## 6. 验证

规则单测（`board_app/tests/host/test_weiqi.c`）：提子、自杀、位置超劫、
连续 pass 终局、数子与贴目、棋盘序列化往返、服务器状态复刻 —— 全部通过。

服务器端到端（`server/tools/weiqi_smoke.sh`）：

```text
[alice] [game] 邀请来自 bob（9 路）
[alice] [game] 开局 vs bob，9 路，我执白
[bob]   [board] 轮到黑 ...   ......X..
[alice] [board] 轮到白 ...   ..O......
[alice] [error] 还没轮到你          <- 抢下被服务器拒绝
[alice] [game] 结束：白胜（对方认输）
```

板上真机：`BOARD_APP_START=weiqi BOARD_APP_COMM_AUTOLOGIN="127.0.0.1:20000,alice,alice123"`，
第二个客户端（bob）发邀请后自动开局，落子实时同步到棋盘。

截图（`docs/screenshots/`）：

- `preview-11-weiqi-lobby.png`：大厅（路数选择 + 好友）
- `preview-11b-weiqi-game.png`：对局（13 路棋盘 + 信息面板）
- `preview-11c-weiqi-size9.png`：点「9 路」后高亮切换正常
- `board-weiqi-live.png`：板上与第二个客户端对局进行中
- `preview-10-games-with-weiqi.png`：小游戏中心 4 张卡片

## 7. 已知限制

- 没有计时/读秒、没有让子、没有悔棋、没有 SGF 复盘。
- 数子未做“死子提清”交互，需要双方用停一手把局面走完再终局。
- 断线即判对手离开（不做断线重连恢复对局）。
- 邀请自动接受（客户端假设一人一号一设备）；未做接受/拒绝弹窗。
- 棋盘上限 19 路，历史局面保存 512 手（够下满一盘）。
