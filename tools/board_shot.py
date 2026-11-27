#!/usr/bin/env python3
"""Decode the gzip+base64 framebuffer payload pulled from the board into PNG.

usage: board_shot.py <payload.b64> <out.png>
The payload file may contain other SSH output lines; the longest base64 line wins.
"""
import base64
import gzip
import sys

payload, out_png = sys.argv[1], sys.argv[2]
data = open(payload, "rb").read()
lines = [l for l in data.split(b"\n") if len(l) > 1000]
if not lines:
    print("no base64 payload in", payload)
    raise SystemExit(1)
raw = gzip.decompress(base64.b64decode(lines[-1]))
raw_path = out_png.rsplit(".", 1)[0] + ".raw"
open(raw_path, "wb").write(raw)
print("framebuffer bytes:", len(raw), "->", raw_path)

W, H = 1024, 600
rows = []
need = W * H * 4
if len(raw) < need:
    raw += b"\x00" * (need - len(raw))
for y in range(H):
    base = y * W * 4
    row = raw[base:base + W * 4]
    rgb = bytearray(W * 3)
    rgb[0::3] = row[2::4]
    rgb[1::3] = row[1::4]
    rgb[2::3] = row[0::4]
    rows.append(bytes(rgb))
png = sys.modules.get("board_png_util")
import struct
import zlib
raw_rows = b"".join(b"\x00" + r for r in rows)
def chunk(tag, body):
    return (struct.pack(">I", len(body)) + tag + body +
            struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF))
ihdr = struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)
out = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
       chunk(b"IDAT", zlib.compress(raw_rows, 6)) + chunk(b"IEND", b""))
open(out_png, "wb").write(out)
print("wrote", out_png)
