#!/usr/bin/env python
"""
Convert clock432 RGB565 bitmap to ARGB8888 with alpha-transparent corners.

Reads the existing clock432.c, parses the RGB565 pixel data, sets circle
pixels to original colour + alpha=0xFF and corner pixels to alpha=0x00,
then writes clock432.c back with LV_COLOR_FORMAT_ARGB8888 and stride=1600.
"""

import re
import sys
import os

SRC = os.path.join(os.path.dirname(__file__),
                   '..', 'app_example', 'img_bg', 'clock432.c')
SRC = os.path.normpath(SRC)

W, H = 400, 432
CX, CY = W // 2, H // 2
RX, RY = CX, CY
LIMIT = RX * RX * RY * RY

def inside(x, y):
    dx = x - CX
    dy = y - CY
    return dx*dx * RY*RY + dy*dy * RX*RX <= LIMIT

def main():
    with open(SRC, 'r', encoding='utf-8') as f:
        text = f.read()

    # Parse the bitmap array
    # Find 'clock432_bitmap[]' or similar
    m = re.search(r'(static\s+const\s+uint8_t\s+clock432_bitmap\s*\[\s*\]\s*=\s*\{)(.*?)(\};)', text, re.DOTALL)
    if not m:
        # Try alternative: LV_ATTRIBUTE_LARGE_CONST
        m = re.search(r'(LV_ATTRIBUTE_LARGE_CONST\s+static\s+const\s+uint8_t\s+clock432_bitmap\s*\[\s*\]\s*=\s*\{)(.*?)(\};)', text, re.DOTALL)
    if not m:
        print("ERROR: could not find clock432_bitmap array in", SRC)
        sys.exit(1)

    prefix, body, suffix = m.group(1), m.group(2), m.group(3)

    # Extract hex bytes
    hex_bytes = re.findall(r'0x([0-9A-Fa-f]{2})', body)
    if len(hex_bytes) != 345600:
        print(f"WARNING: expected 345600 bytes, got {len(hex_bytes)}")

    # Convert RGB565 -> ARGB8888 (4 bytes per pixel)
    out = []
    for y in range(H):
        for x in range(W):
            idx = y * W + x
            if idx * 2 + 1 >= len(hex_bytes):
                break
            lo = int(hex_bytes[idx * 2], 16)
            hi = int(hex_bytes[idx * 2 + 1], 16)
            v = (hi << 8) | lo
            # RGB565: R=bits[15:11], G=bits[10:5], B=bits[4:0]
            r5 = (v >> 11) & 0x1F
            g6 = (v >> 5) & 0x3F
            b5 = v & 0x1F
            r8 = (r5 << 3) | (r5 >> 2)
            g8 = (g6 << 2) | (g6 >> 4)
            b8 = (b5 << 3) | (b5 >> 2)

            if inside(x, y):
                # Opaque
                out.append(f"0x{b8:02X}")
                out.append(f"0x{g8:02X}")
                out.append(f"0x{r8:02X}")
                out.append(f"0xFF")
            else:
                # Transparent (all-zero)
                out.extend(["0x00", "0x00", "0x00", "0x00"])

    # Format output: 16 bytes per line to keep file compact
    lines = []
    for i in range(0, len(out), 16):
        chunk = out[i:i+16]
        lines.append("    " + ", ".join(chunk) + ",")

    bitmap_body = "\n".join(lines)

    # Build new file
    new_text = text[:m.start(1)] + (
        'LV_ATTRIBUTE_LARGE_CONST static const uint8_t clock432_bitmap[] = {\n'
        + bitmap_body + '\n'
        '};\n'
    ) + text[m.end(3):]

    # Update image descriptor fields
    new_text = re.sub(
        r'(\.cf\s*=\s*)LV_COLOR_FORMAT_RGB565',
        r'\g<1>LV_COLOR_FORMAT_ARGB8888',
        new_text
    )
    new_text = re.sub(
        r'(\.stride\s*=\s*)800',
        r'\g<1>' + str(W * 4),
        new_text
    )
    new_text = re.sub(
        r'(\.data_size\s*=\s*)345600',
        r'\g<1>' + str(W * H * 4),
        new_text
    )

    # Verify data size
    expected_pixels = W * H
    actual_pixels = len(out) // 4
    print(f"Pixels: expected={expected_pixels}, actual={actual_pixels}")
    print(f"Data size: {W * H * 4} bytes ({len(out)} hex values)")
    print(f"Format: ARGB8888, stride={W * 4}")

    # Write back
    with open(SRC, 'w', encoding='utf-8') as f:
        f.write(new_text)
    print(f"Written: {SRC}")

if __name__ == '__main__':
    main()
