# stb_image / stb_image_write

- Upstream: <https://github.com/nothings/stb>
- Version: stb_image 2.30, stb_image_write 1.16 (master snapshot, 2026-11-25)
- License: Public Domain / MIT dual license (see `LICENSE`)
- Files: `stb_image.h`, `stb_image_write.h` (unmodified)
- Implementation: `core/src/stb_impl.c` defines both implementations once.
- Used for: gallery decode (PNG/JPEG/BMP) and PNG save.

Update by replacing the headers and rebuilding; run `tests/host` image tests.
