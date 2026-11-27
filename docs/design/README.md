# 设计图说明

本目录是 **UI 评审基线**（早期设计稿），不是运行时资源。图片由项目用 Pillow 按
`docs/04-ui-design.md` 的布局/色板绘制，用于在写代码前确认信息层级、1024×600 下是否
遮挡/溢出、整体风格是否统一。

| 文件 | 内容 | 状态 |
|---|---|---|
| `mockup-home.png` | 主界面：时间、标题、应用卡片 | 早期（3 卡片；现为 5 卡片） |
| `mockup-weather.png` | 天气：当前天气 + 7 天预报 + 城市/刷新 | 现行 |
| `mockup-gallery.png` | 相册：3 列缩略图网格 | 现行 |
| `mockup-editor.png` | 图片编辑：大图 + 工具按钮 + 保存 | 现行 |
| `mockup-games.png` | 游戏中心：2048 / 贪吃蛇 / 打砖块 | 早期（现为 4 款，含联网围棋） |

## 后加页面没有设计图

聊天室、AI 聊天、系统账户、联网围棋是后续加入的，**没有单独画 mockup**，
以真实渲染截图为准：

- 预览：`docs/screenshots/preview-08*`（AI 聊天）、`preview-09*`（聊天室）、
  `preview-10/11*`（围棋）、`preview-12`（账户）
- 板端：`docs/screenshots/board-comm-live.png`、`board-chat-live.png`、
  `board-weiqi-live.png`、`board-account-live.png`

设计规格都写在 `docs/04-ui-design.md`（第 7 章小游戏、10.1 围棋、10.2 聊天室、10.3 账户）。

## 注意

- 设计图使用本机中文字体渲染，仅用于示意；仓库运行字体为 Noto Sans SC 子集（OFL）。
- 实现以 `docs/04-ui-design.md` 的尺寸、颜色、字号令牌为准；若图与文档冲突，
  **以文档为准**并更新图。
- 设计图许可：CC0 1.0（可自由使用/修改，不含第三方素材）。
- 生成脚本未入库（依赖本机字体路径）；需要复现时在 `tools/` 增加一个只接受 OFL 字体的生成脚本。
