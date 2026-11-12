"""Convert native LVGL RGB captures into reviewable PNGs using only the standard library."""
from pathlib import Path
import struct
import sys
import zlib

folder = Path(sys.argv[1])

def write_png(path, width, height, rgb):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    rows = b"".join(b"\0" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))

for source in folder.glob("*.ppm"):
    magic, dimensions, maximum, rgb = source.read_bytes().split(b"\n", 3)
    width, height = map(int, dimensions.split())
    assert magic == b"P6" and maximum == b"255" and len(rgb) == width * height * 3
    write_png(source.with_suffix(".png"), width, height, rgb)

names = ["clock", "codex", "muyu", "dice", "dice-rolling", "2048"]
frames = [(folder / (name + ".ppm")).read_bytes().split(b"\n", 3)[3] for name in names]
rows = []
for row in range(2):
    for y in range(240):
        rows.append(b"".join(frames[row * 3 + col][y * 720:(y + 1) * 720] for col in range(3)))
write_png(folder / "factory-pages.png", 720, 480, b"".join(rows))
print("PNG previews ready")
