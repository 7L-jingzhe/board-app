# weiqi.js（围棋规则核心的上游）

本项目围棋规则核心**移植自** [cjlarose/weiqi.js](https://github.com/cjlarose/weiqi.js)，
许可 **ISC**（见同目录 `LICENSE-ISC.txt`）。

## 上游信息

| 项 | 值 |
|---|---|
| 仓库 | https://github.com/cjlarose/weiqi.js |
| 引用版本 | `master` 分支（2026-11 取用） |
| 许可 | ISC，Copyright (c) 2014-2015, Christopher La Rose |
| 语言 | JavaScript（ES2015，依赖 `immutable`） |
| 上游文件 | `src/lib/board.js`、`src/lib/game.js`、`src/lib/util.js`、`src/lib/records.js` |

本目录保留了取用时的上游原文（`board.js`、`game.js`、`README.md`、
`package.json`），作为移植依据与审计对照；构建**不参与**编译。

## 移植到哪里

| 上游 | 本项目 C 实现（`board_app/app/weiqi/core/src/weiqi.c`） |
|---|---|
| `createBoard(size)` | `weiqi_init()` |
| `placeStone(board, move)` | `place_stone()`（内部函数） |
| `groupAtPosition(...)` | `group_at()`（BFS 连通块 + 气） |
| `removeGroup(...)` | `remove_stones()` |
| `areaScore(board)` | `weiqi_area_score()` → 对外 `weiqi_score()` |
| `toArray(board)` | `weiqi_board_string()` |
| `createGame(size)` | `weiqi_init()` |
| `play(game, player, pos)` | `weiqi_play()` |
| `pass(game, player)` | `weiqi_pass()` |
| `isOver(game)` | `g->over`（连续两次 pass） |
| `history` + “Violation of Ko” | `g->history[]` 哈希集合（**位置超劫** positional superko） |

## 移植时的改动（只做必要适配）

1. **语言与数据结构**：JS 的不可变（`immutable`）结构改为 C 的可变结构 +
   固定大小数组（19×19 棋盘、512 手历史），去掉 `immutable` 依赖。
2. **劫争判定**：上游用“历史局面集合”做位置超劫，C 版用 64 位 FNV 哈希
   集合代替（`history[]`），语义一致；冲突概率 2^-64 量级。
3. **自杀**：上游默认禁止（抛错），C 版返回 `-2`；未开放 `allowSuicide`。
4. **补了上游没有的部分**（属于本项目适配层，不是规则核心）：
   - 胜负判定与中国规则贴目 3.5 子（`weiqi_score` 里对白方加 `WEIQI_KOMI`）；
   - 认输（`weiqi_resign`，上游无此概念）；
   - 棋盘文本序列化/反序列化（给网络同步用，`'B'/'W'/'.'`）；
   - 连续 pass 达到 2 次时自动终局并算分。
5. 坐标沿用上游 `(i, j)` 语义，本项目命名为 `(x, y)`，左上角 `(0,0)`，
   `x` 横向、`y` 纵向。

## 许可合规

- ISC 允许修改、衍生、商用，要求**保留版权声明与许可文本**：
  已保留 `LICENSE-ISC.txt`，并在 `weiqi.c`、`weiqi.h` 文件头注明出处。
- 项目总声明见 `board_app/THIRD_PARTY_NOTICES.md`。
