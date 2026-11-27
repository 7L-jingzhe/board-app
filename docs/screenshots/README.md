# 截图索引

两类截图，都是**真实渲染**（不是设计效果图）：

- `preview-*.png`：`board_app/tests/preview/` 在 x86_64 主机上用真实 LVGL 9.2 渲染，
  用于没有开发板时检查布局、字体和交互回归。
- `board-*-live.png`：直接从开发板 `/dev/fb0`（1024×600，32bpp）读出并转 PNG。

重新生成见 [06 构建部署](../06-build-deploy.md) 的「运行与验证」一节。

## 主机预览（headless）

| 文件 | 场景 |
|---|---|
| `preview-01-home.png` | 首页（5 张卡片 + 账号胶囊） |
| `preview-02-weather.png` | 天气（在线数据） |
| `preview-02b-city.png` | 城市选择弹窗 |
| `preview-02c-weather-selected.png` | 切换到其他城市后 |
| `preview-03-gallery.png` | 相册网格 |
| `preview-03b-editor.png` | 图片编辑器 |
| `preview-03c-edited.png` | 旋转/灰度/撤销后 |
| `preview-03d-adjust.png` | 亮度/对比度弹窗 |
| `preview-03e-crop.png` | 裁剪弹窗 |
| `preview-03f-cropped.png` | 1:1 裁剪结果 |
| `preview-04-games.png` | 游戏中心 |
| `preview-05-2048.png` / `05b-pause` / `05c-move` | 2048 初始/暂停/移动 |
| `preview-06-snake.png` | 贪吃蛇 |
| `preview-07-breakout.png` / `07b-playing` | 打砖块 READY / 发球后 |
| `preview-08-chat.png` | AI 聊天（左对话 + 右键盘） |
| `preview-08b-chat-pinyin.png` | AI 聊天：拼音候选条 |
| `preview-09-comm-login.png` | 聊天室未登录提示（登录已移到账户页） |
| `preview-09b-comm-chat.png` | 聊天室会话页 |
| `preview-10-games-with-weiqi.png` | 游戏中心 4 张卡片（含联网围棋） |
| `preview-11-weiqi-lobby.png` | 围棋大厅（路数选择 + 好友） |
| `preview-11b-weiqi-game.png` | 围棋对局（13 路棋盘 + 信息面板） |
| `preview-11c-weiqi-size9.png` | 点「9 路」后的高亮切换 |
| `preview-12-account.png` | 系统账户页（已登录 + 好友列表） |

## 开发板实机

| 文件 | 场景 |
|---|---|
| `board-home-live.png` | 首页（5 张卡片） |
| `board-weather-live.png` | 天气（在线 7 天） |
| `board-gallery-live.png` | 相册网格 |
| `board-games-live.png` | 游戏中心（3 款时期） |
| `board-games4-live.png` | 游戏中心 4 款（含联网围棋，一行放得下） |
| `board-2048-live.png` | 2048 |
| `board-chat-live.png` | AI 聊天页 |
| `board-ai-deepseek-live.png` | **DeepSeek 实际对话**（“在线大模型”，回复「收到」） |
| `board-ai-keyfile-live.png` | 用本地配置文件里的 key 对话（不带环境变量） |
| `board-comm-live.png` | 聊天室（好友列表 + 键盘） |
| `board-comm-message-live.png` | 第二个客户端发来的消息实时显示 |
| `board-chat-history-live.png` | 重启后从本地聊天记录恢复 |
| `board-offline-resend-live.png` | 离线补发的两条消息（含入库、队列清空） |
| `board-history-header-live.png` | 停在首页时收的消息，进聊天室后读出 |
| `board-weiqi-live.png` | 联网围棋对局（服务器权威判定） |
| `board-account-live.png` | 系统账户页 |
| `board-account-nohost.png` | 登录页：服务器地址不预置默认值 |
| `board-autologin-live.png` | 重启后自动登录（顶栏显示用户名 + 绿点） |

板端环境：RK356X / Buildroot / glibc 2.29；`main` 用 glibc 2.27 sysroot 动态构建，
LVGL 静态链入。触摸手势长跑与开机自启仍需现场人工点检。

> 截图中的天气数据来自网络接口，可能随时间变化；示例照片/字体为项目资源。
> 早期截图（`board-games-live.png`）保留作为历史对照，不代表当前布局。
