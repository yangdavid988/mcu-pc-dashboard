#!/usr/bin/env python
"""
Generate clock400.c from clock400.png — ARGB8888 with alpha-transparent corners.

The original clock432.png (400x432) was pre-compensated for DBL070's non-square
pixels (ratio 1.078).  clock400.png is a 400x400 version that is physically
circular — used by T1720A (pixel ratio 0.333) to avoid distorting the clock face.

Output path: ./app_example/assets/backgrounds/clock400.c
"""

import os
import sys
from PIL import Image

OUT_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__),
                                         '..', 'assets', 'backgrounds'))

SRC = os.path.join(OUT_DIR, 'clock400.png')
DST = os.path.join(OUT_DIR, 'clock400.c')

W, H = 400, 400

def main():
    img = Image.open(SRC).convert('RGBA')
    assert img.size == (W, H), f'Expected {W}x{H}, got {img.size}'

    pixels = list(img.getdata())
    out = []
    for y in range(H):
        for x in range(W):
            r, g, b, a = pixels[y * W + x]
            # Preserve original alpha from PNG (includes anti-aliased edges)
            out.append(f'0x{b:02X}')
            out.append(f'0x{g:02X}')
            out.append(f'0x{r:02X}')
            out.append(f'0x{a:02X}')

    # Format: 16 bytes per line
    lines = []
    for i in range(0, len(out), 16):
        chunk = out[i:i+16]
        lines.append('    ' + ', '.join(chunk) + ',')

    bitmap_body = '\n'.join(lines)
    data_size = W * H * 4  # 640000
    stride = W * 4         # 1600

    c_code = f'''#include "lvgl.h"

LV_ATTRIBUTE_LARGE_CONST static const uint8_t clock400_bitmap[] = {{
{bitmap_body}
}};

const lv_image_dsc_t clock400 = {{
    .header = {{
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_ARGB8888,
        .flags = 0,
        .w = {W},
        .h = {H},
        .stride = {stride},
        .reserved_2 = 0,
    }},
    .data_size = {data_size},
    .data = clock400_bitmap,
}};
'''

    with open(DST, 'w', encoding='utf-8', newline='\n') as f:
        f.write(c_code)

    print(f'Done: {DST} ({len(out)//4} pixels, {data_size} bytes)')

if __name__ == '__main__':
    main()
