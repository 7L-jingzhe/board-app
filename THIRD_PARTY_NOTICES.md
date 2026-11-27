# Third-Party Notices

Board App includes the following third-party components. The full license
texts are kept next to the code; this file is the short distribution notice.

| Component | Version / commit | License | Location |
|---|---|---|---|
| LVGL | 9.2.3 | MIT | `lvgl/` (project root) |
| LVGL Pinyin IME (`lv_ime_pinyin`) | part of LVGL 9.2.3 | MIT | `lvgl/src/others/ime/` |
| Little World Arcade `arcade_core` | `106fb20f042729a17712ee0be186b8f2524228f5` | MIT | `app/games/*/core/`, `third_party/littleworld_arcade/` |
| cJSON | 1.7.18 | MIT | `core/third_party/cjson/` |
| weiqi.js（围棋规则核心） | `master`（2026-11 取用） | ISC | `app/games/weiqi/core/`（移植）、`third_party/weiqi-js/`（上游原文） |
| stb_image / stb_image_write | 2.30 / 1.16 | Public Domain / MIT | `core/third_party/stb/` |
| Noto Sans SC (subset) | noto-cjk `main` | SIL OFL 1.1 | `assets/fonts/` |
| Unicode Han Database (Unihan) | UCD `latest` | Unicode License v3 | `assets/ime/pinyin_dict.txt` (derived data) |
| Sample photos / design mockups | project-generated | CC0 1.0 | `assets/photos/`, `docs/design/` |

- Project code: MIT, Copyright (c) 2026 7L-jingzhe, see `LICENSE`.
- `assets/fonts/OFL.txt` is the SIL OFL text for Noto Sans SC.
- `assets/ime/pinyin_dict.txt` is derived from Unihan `kMandarin`
  (<https://www.unicode.org/Public/UCD/latest/ucd/Unihan.zip>) restricted to the
  GB 2312 hanzi set; the Unicode License text is at
  <https://www.unicode.org/license.txt>. Regenerate with
  `board_app/tools/gen_pinyin_dict.py`.
- `third_party/littleworld_arcade/LICENSE` and `UPSTREAM.md` record the
  upstream source and copied files.
- `third_party/weiqi-js/LICENSE-ISC.txt` and `UPSTREAM.md` record the upstream
  source (<https://github.com/cjlarose/weiqi.js>) and the porting mapping; the
  ported C core keeps the copyright notice in `weiqi.c` / `weiqi.h`.
- No GPL/AGPL code is intentionally included in `board_app/`.
