#!/bin/bash
# Decode the last base64 payload in <in> to PNG <out>. usage: dec_shot.sh <in> <out>
set -uo pipefail
IN="$1"
OUT="$2"
python3 - "$IN" "$OUT" <<'PY'
import base64, gzip, struct, sys, zlib

src, out_png = sys.argv[1], sys.argv[2]
data = open(src, "rb").read()
lines = [l for l in data.split(b"\n") if len(l) > 1000]
if not lines:
    print("no payload")
    raise SystemExit(1)
raw = gzip.decompress(base64.b64decode(lines[-1]))
W, H = 1024, 600
need = W * H * 4
if len(raw) < need:
    raw += b"\x00" * (need - len(raw))
rows = []
for y in range(H):
    base = y * W * 4
    row = raw[base:base + W * 4]
    rgb = bytearray(W * 3)
    rgb[0::3] = row[2::4]
    rgb[1::3] = row[1::4]
    rgb[2::3] = row[0::4]
    rows.append(b"\x00" + bytes(rgb))

def chunk(tag, body):
    return (struct.pack(">I", len(body)) + tag + body +
            struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF))

png = (b"\x89PNG\r\n\x1a\n" +
       chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)) +
       chunk(b"IDAT", zlib.compress(b"".join(rows), 6)) +
       chunk(b"IEND", b""))
open(out_png, "wb").write(png)
print("wrote", out_png, len(png), "bytes")
PY
