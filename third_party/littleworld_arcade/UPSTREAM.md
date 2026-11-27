# Little World Arcade · arcade_core

- Upstream: <https://github.com/Start66613/littleworld-arcade>
- Commit: `106fb20f042729a17712ee0be186b8f2524228f5` (2026-10-24)
- License: MIT (see `LICENSE`)
- Copyright: Copyright (c) 2026 Little World Arcade contributors

## Copied files

From `firmware/components/arcade_core/`:

| Upstream | Local |
|---|---|
| `include/arcade/arcade_common.h` | `app/games/core/include/arcade/arcade_common.h` |
| `include/arcade/arcade_2048.h` | `app/games/core/include/arcade/arcade_2048.h` |
| `include/arcade/arcade_snake.h` | `app/games/core/include/arcade/arcade_snake.h` |
| `include/arcade/arcade_breakout.h` | `app/games/core/include/arcade/arcade_breakout.h` |
| `src/arcade_rng.c` | `app/games/core/src/arcade_rng.c` |
| `src/arcade_2048.c` | `app/games/g2048/core/src/arcade_2048.c` |
| `src/arcade_snake.c` | `app/games/snake/core/src/arcade_snake.c` |
| `src/arcade_breakout.c` | `app/games/breakout/core/src/arcade_breakout.c` |

Not copied: `arcade_save.c/h` (ESP-IDF NVS dependency), `arcade_ui/`, fonts,
ESP-IDF firmware/project files.

## Modifications

None to the game algorithms. `arcade_rng.c` is compiled once from
`app/games/core/src` to avoid duplicate symbols; the three rule files live in
their own game core folders. UI adaptation is in `app/games/*/ui/`.

## Upgrade

1. Copy the new files above.
2. Keep the MIT header and this file updated (new commit).
3. Rebuild and run `tests/host` plus the on-board game checklist.
