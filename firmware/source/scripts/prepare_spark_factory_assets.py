"""Adapt official SparkBot assets to LVGL 9 without changing their artwork."""
from pathlib import Path
import re
import struct
import zlib

PROJECT = Path(__file__).resolve().parents[1]
WORKSPACE = PROJECT.parents[1]
UPSTREAM = WORKSPACE / "esp_sparkbot_official/example/factory_demo_v1"
BOARD = PROJECT / "main/boards/esp-sparkbot"

def png(path, width, height, rgb):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    rows = b"".join(b"\0" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))

def c_array(name, data):
    lines = [", ".join(f"0x{value:02x}" for value in data[i:i + 24]) for i in range(0, len(data), 24)]
    return f"static const LV_ATTRIBUTE_MEM_ALIGN uint8_t {name}[] = {{\n    " + ",\n    ".join(lines) + "\n};\n"

source = (UPSTREAM / "main/ui/images/ui_img_muyu_png.c").read_text(encoding="utf-8")
body = source.split("ui_img_muyu_png_data[] = {", 1)[1].split("};", 1)[0]
pixels = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", body))
assert len(pixels) == 256 * 256 * 3
# The factory project uses swapped RGB565 plus alpha. Composite on its black background.
rgb = bytearray()
for i in range(0, len(pixels), 3):
    color = (pixels[i] << 8) | pixels[i + 1]
    alpha = pixels[i + 2]
    rgb.extend((((color >> 11) * 255 // 31) * alpha // 255, (((color >> 5) & 63) * 255 // 63) * alpha // 255, ((color & 31) * 255 // 31) * alpha // 255))
# Pre-scale to the original displayed 200px size to avoid scaling every resting frame.
scaled = bytearray()
for y in range(200):
    sy = y * 255 / 199
    y0 = int(sy)
    y1 = min(255, y0 + 1)
    fy = sy - y0
    for x in range(200):
        sx = x * 255 / 199
        x0 = int(sx)
        x1 = min(255, x0 + 1)
        fx = sx - x0
        for channel in range(3):
            top = rgb[(y0 * 256 + x0) * 3 + channel] * (1 - fx) + rgb[(y0 * 256 + x1) * 3 + channel] * fx
            bottom = rgb[(y1 * 256 + x0) * 3 + channel] * (1 - fx) + rgb[(y1 * 256 + x1) * 3 + channel] * fx
            scaled.append(round(top * (1 - fy) + bottom * fy))
raw = bytearray()
for i in range(0, len(scaled), 3):
    r, g, b = scaled[i:i + 3]
    raw.extend(struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)))
output = ['// Official ESP-SparkBot artwork adapted by scripts/prepare_spark_factory_assets.py.', '#include "spark_factory_assets.h"', c_array("muyu_pixels", raw)]
output.append('const lv_image_dsc_t spark_muyu_image = {.header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = 200, .h = 200, .stride = 400}, .data_size = sizeof(muyu_pixels), .data = muyu_pixels};')
for number in range(1, 7):
    bmp = (UPSTREAM / f"lottie_assets/dice{number}.bmp").read_bytes()
    offset = struct.unpack_from("<I", bmp, 10)[0]
    assert struct.unpack_from("<ii", bmp, 18) == (120, 120)
    assert len(bmp[offset:]) == 120 * 120 * 3
    output.append(c_array(f"dice_{number}", bmp[offset:]))
output.append('const uint8_t* const spark_dice_textures[6] = {dice_1, dice_2, dice_3, dice_4, dice_5, dice_6};')
(BOARD / "spark_factory_assets.c").write_text("\n".join(output) + "\n", encoding="utf-8")
(PROJECT / "logs/ui-previews").mkdir(parents=True, exist_ok=True)
png(PROJECT / "logs/ui-previews/official-muyu-artwork.png", 200, 200, scaled)
for name in ["ComfortaaBold75", "OPPOSansH18", "OPPOSansH25"]:
    text = (UPSTREAM / f"main/ui/fonts/ui_font_{name}.c").read_text(encoding="utf-8")
    text = text.replace('#include "../ui.h"', '#include <lvgl.h>\nLV_FONT_DECLARE(font_puhui_20_4);')
    text = text.replace('.fallback = NULL,', '.fallback = &font_puhui_20_4,')
    (BOARD / f"spark_font_{name}.c").write_text(text, encoding="utf-8")
print("Adapted original wooden-fish artwork, six dice textures and three factory fonts.")
