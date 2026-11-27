#!/usr/bin/env python3
"""Collect the Chinese characters used by the UI into assets/fonts/chars.txt.

The font subset generator reads this file. Re-run after adding Chinese UI text,
weather conditions, city names, or after regenerating the pinyin dictionary
(assets/ime/pinyin_dict.txt) - every hanzi the IME can produce must have a
glyph, otherwise candidates show up as empty boxes.

Usage:
  python3 tools/gen_chars.py [project-root]

SPDX-License-Identifier: MIT
Copyright (c) 2026 Board App contributors
"""
import os
import sys

EXTRA = (
    "晴多云阴小雨中大雨暴雨雷阵雪雾霾沙尘浮扬冰雹冻阵夹风东南西北级"
    "优良好轻度重污染空气质量湿度最高低今明周星期三四五六日一二"
    "北京上海广州深圳成都杭州武汉西安"
    "预报当前温度时间更新离线演示数据在线刷新城市选择取消确定"
    "相册照片编辑保存旋转翻转灰度调整裁剪撤销重置亮度对比度"
    "小游戏贪吃蛇打砖块得分最高长度生命暂停继续重新开始返回"
    "主界面首页应用中心点击卡片进入查看打开暂无加载中失败"
    "拖动挡板发球清屏游戏结束达成挑战步数分数方向键滑动"
    "聊天打开输入法键盘中文英文数字候选拼音助手清空发送"
)
PUNCT = "°℃%·—…“”‘’《》「」【】、，。；：！？（）×÷≈±→←↑↓♥●○■□▸◂‹›"


def dictionary_chars(root: str) -> str:
    """Every hanzi reachable through the IME candidate dictionary."""
    path = os.path.join(root, "assets", "ime", "pinyin_dict.txt")
    if not os.path.isfile(path):
        print("warn: %s missing; run tools/gen_pinyin_dict.py first" % path,
              file=sys.stderr)
        return ""
    chars = set()
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.startswith("#") or "\t" not in line:
                continue
            chars.update(line.split("\t", 1)[1].strip())
    return "".join(chars)


def main() -> int:
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..")
    root = os.path.abspath(root)
    chars = set()
    for dirpath, dirnames, filenames in os.walk(root):
        parts = set(dirpath.split(os.sep))
        if parts & {"docs", "tests", "third_party", "build", "bin"}:
            continue
        for name in filenames:
            if not name.endswith((".c", ".h")):
                continue
            try:
                text = open(os.path.join(dirpath, name), encoding="utf-8").read()
            except OSError:
                continue
            chars.update(ch for ch in text if "\u4e00" <= ch <= "\u9fff")
    chars.update(dictionary_chars(root))
    chars.update(chr(i) for i in range(32, 127))
    chars.update(PUNCT)
    chars.update(EXTRA)

    out = os.path.join(root, "assets", "fonts", "chars.txt")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", encoding="utf-8") as f:
        f.write("".join(sorted(chars)))
    print("chars", len(chars), "->", out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
