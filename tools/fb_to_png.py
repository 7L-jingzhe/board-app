"""Convert a raw /dev/fb0 dump (BGRA, 1024x600) to PNG.

Works without Pillow: falls back to a small zlib-based PNG writer.
usage: fb_to_png.py <raw> <png> [width] [height]
"""
import struct
import sys
import zlib

W, H = 1024, 600


def write_png(path: str, rgb_rows, width: int, height: int) -> None:
    raw = b"".join(b"\x00" + row for row in rgb_rows)
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
           chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def main() -> int:
    src, dst = sys.argv[1], sys.argv[2]
    width = int(sys.argv[3]) if len(sys.argv) > 3 else W
    height = int(sys.argv[4]) if len(sys.argv) > 4 else H
    raw = open(src, "rb").read()
    need = width * height * 4
    if len(raw) < need:
        raw += b"\x00" * (need - len(raw))
    rows = []
    for y in range(height):
        base = y * width * 4
        row = raw[base:base + width * 4]
        # BGRA -> RGB
        rgb = bytearray(width * 3)
        rgb[0::3] = row[2::4]
        rgb[1::3] = row[1::4]
        rgb[2::3] = row[0::4]
        rows.append(bytes(rgb))
    write_png(dst, rows, width, height)
    print("wrote", dst, width, "x", height)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
