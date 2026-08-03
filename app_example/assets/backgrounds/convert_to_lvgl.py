"""
Convert 128x128 PNG backgrounds to LVGL 9.3 C arrays.

Supports two output formats:
  - RGB565       (no alpha, smaller): for solid / full-frame images
  - RGB565A8  (with alpha, larger):   for cut-out / transparent images
                                       color_data(w*h*2) + alpha_data(w*h)

Add your PNG to FILE_MAP (see SOP.md), then run:
    python convert_to_lvgl.py
"""
import os
from PIL import Image

INPUT_DIR = os.path.dirname(os.path.abspath(__file__))

# FILE_MAP: "your_image.png" -> ("struct_name", "bitmap_array_name")
# The script auto-detects alpha: RGBA PNG -> RGB565A8, RGB PNG -> RGB565.
FILE_MAP = {
    "bg_cobalt_intel_128.png":  ("bg_cobalt",   "bg_cobalt_bitmap"),
    "bg_inferno_amd_128.png":   ("bg_inferno",  "bg_inferno_bitmap"),
    "bg_silicon_apple_205.png": ("bg_silicon",  "bg_silicon_bitmap"),
    "clock432.png":             ("clock432",    "clock432_bitmap"),
}


def to_rgb565_bytes(r, g, b):
    """Convert R,G,B (0-255) to two bytes in little-endian RGB565 format."""
    rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    lo = rgb565 & 0xFF
    hi = (rgb565 >> 8) & 0xFF
    return lo, hi


def format_hex_block(data, width=16):
    """Format a list of bytes into C hex initialiser lines."""
    lines = []
    for i in range(0, len(data), width):
        chunk = data[i: i + width]
        hex_str = ", ".join(f"0x{b:02X}" for b in chunk)
        lines.append(f"    {hex_str},")
    return "\n".join(lines)


def convert_png_to_c(src_name, struct_name, bitmap_name):
    src_path = os.path.join(INPUT_DIR, src_name)
    img = Image.open(src_path)
    w, h = img.size

    has_alpha = img.mode in ("RGBA", "LA", "PA") or (img.mode == "P" and "transparency" in img.info)

    if has_alpha:
        # ---- RGB565A8: colour + separate alpha channel ----
        rgba = img.convert("RGBA")
        color_bytes = []
        alpha_bytes = []
        for r, g, b, a in rgba.getdata():
            lo, hi = to_rgb565_bytes(r, g, b)
            color_bytes.append(lo)
            color_bytes.append(hi)
            alpha_bytes.append(a)

        raw_data = color_bytes + alpha_bytes         # colour first, alpha appended
        data_size = len(raw_data)                    # w*h*2 + w*h  =  w*h*3
        cf_name = "LV_COLOR_FORMAT_RGB565A8"
        stride = w * 2                               # stride is colour-row bytes

        print(f"  [{src_name}] RGBA detected -> RGB565A8  ({data_size} bytes)")
    else:
        # ---- RGB565: colour only ----
        rgb = img.convert("RGB")
        color_bytes = []
        for r, g, b in rgb.getdata():
            lo, hi = to_rgb565_bytes(r, g, b)
            color_bytes.append(lo)
            color_bytes.append(hi)

        raw_data = color_bytes
        data_size = len(raw_data)                    # w*h*2
        cf_name = "LV_COLOR_FORMAT_RGB565"
        stride = w * 2

        print(f"  [{src_name}] RGB detected -> RGB565  ({data_size} bytes)")

    # ------------------------------------------------------------------
    # Write C file
    # ------------------------------------------------------------------
    c_name = struct_name + ".c"
    c_path = os.path.join(INPUT_DIR, c_name)

    bitmap_body = format_hex_block(raw_data)

    c_code = f"""\
#include "lvgl.h"

LV_ATTRIBUTE_LARGE_CONST static const uint8_t {bitmap_name}[] = {{
{bitmap_body}
}};

const lv_image_dsc_t {struct_name} = {{
    .header = {{
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = {cf_name},
        .flags = 0,
        .w = {w},
        .h = {h},
        .stride = {stride},
        .reserved_2 = 0,
    }},
    .data_size = {data_size},
    .data = {bitmap_name},
}};
"""
    with open(c_path, "w", encoding="utf-8") as f:
        f.write(c_code)

    print(f"  -> {c_path}")
    return struct_name


if __name__ == "__main__":
    print("--- convert_to_lvgl.py ---")
    names = []
    for src, (struct_name, bitmap_name) in sorted(FILE_MAP.items()):
        print(f"Processing: {src}")
        name = convert_png_to_c(src, struct_name, bitmap_name)
        names.append(name)
    print(f"\nDone: {', '.join(names)}")
