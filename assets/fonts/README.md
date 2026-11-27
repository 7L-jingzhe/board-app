# 中文字体

- 文件：`NotoSansSC-Board.ttf`（Noto Sans SC 子集，约 **2.2 MB**，**6899 个字形**）
- 许可：SIL Open Font License 1.1，完整文本见 `OFL.txt`
- 字符表：`chars.txt`（收集源码里出现的所有中文字符，空格分隔）
- 用途：`ui_theme` 启动时**整块读进内存**，再用 `lv_tiny_ttf_create_data_ex()`
  按 18/22/26/34/48 px 创建 5 个字体实例（共用同一份字节）。

## 两个必须知道的点

1. **不要用 `lv_tiny_ttf_create_file()` 流式读取。**
   TinyTTF 每取一个字形都要按偏移读字体表，而 `LV_FS_STDIO_CACHE_SIZE` 必须保持 0
   （LVGL 9.2 的 `lv_fs_read_cached()` 在短读时会算错缓存区间，TinyTTF 随后死循环）。
   没有文件缓存时，2.2 MB 字体让"打开一页"需要 **44 秒**；整块读进内存后同一页 **0.65 秒**。

2. **必须用 `LV_FONT_KERNING_NONE`。**
   默认的 kerning 会对每对相邻字符查 kern 表，而 LVGL 每次改字体/宽度/换行都会重排整段文本
   → 单个 label 的 `set_font` 从 9.93 ms 降到 0.48 ms，`set_width` 从 9.92 ms 降到 0.01 ms。
   中文排版本来不用字距对，没有视觉损失。细节见 [11 章](../docs/11-chat-and-ime.md)。

## 重新生成

1. 把新增文案里的中文字加入 `chars.txt`（`tools/gen_chars.py` 可扫描源码自动收集）：

```bash
python3 board_app/tools/gen_chars.py --src board_app --output board_app/assets/fonts/chars.txt
```

2. 安装 fonttools 并下载 Noto Sans SC 可变字体（OFL）：

```bash
python3 -m pip install --user --break-system-packages fonttools
curl -L -o /tmp/NotoSansSC-VF.ttf \
  https://raw.githubusercontent.com/notofonts/noto-cjk/main/Sans/Variable/TTF/Subset/NotoSansSC-VF.ttf
```

3. 子集化：

```bash
python3 board_app/tools/gen_font_subset.py \
  --input /tmp/NotoSansSC-VF.ttf \
  --chars board_app/assets/fonts/chars.txt \
  --output board_app/assets/fonts/NotoSansSC-Board.ttf
```

4. 校验覆盖（缺字会回退到 SimSun，日志里有 `glyph-missing`）：

```bash
python3 board_app/tools/check_font_coverage.py \
  --font board_app/assets/fonts/NotoSansSC-Board.ttf \
  --src board_app
```

替换字体文件后**重启应用**即可，不需要重编（`ui_theme` 运行时读取）。
如果字体文件缺失，`ui_theme` 回退到 LVGL 内置 SimSun 16（只有少量常用字）。

> 不要把 Windows 系统字体（`msyh.ttc` / `simhei.ttf`）提交到仓库；版权不允许。
