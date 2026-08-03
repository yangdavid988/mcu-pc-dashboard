"""
Generate 32x32 A8-format weather icon C files for LVGL 9.3.

Usage: python gen_weather_icons.py
Output: icon_sun.c, icon_cloud.c, icon_rain.c, icon_drizzle.c,
        icon_thunderstorm.c, icon_snow.c, icon_fog.c
"""

import math

W = 32
H = 32

def new_canvas():
    """32x32 zero-initialized (fully transparent)."""
    return [[0] * W for _ in range(H)]

def set_px(canvas, x, y, alpha=0xFF):
    if 0 <= x < W and 0 <= y < H:
        canvas[y][x] = max(canvas[y][x], alpha)

def fill_circle(canvas, cx, cy, r, alpha=0xFF):
    """Bresenham-style filled circle with AA-like falloff."""
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            dist = math.sqrt(dx * dx + dy * dy)
            if dist <= r - 0.5:
                set_px(canvas, cx + dx, cy + dy, alpha)
            elif dist <= r + 0.5:
                # Simple AA: fade at edge
                fade = int((r + 0.5 - dist) * alpha)
                set_px(canvas, cx + dx, cy + dy, max(0, min(alpha, fade)))

def fill_rect(canvas, x0, y0, x1, y1, alpha=0xFF):
    for y in range(max(0, y0), min(H, y1 + 1)):
        for x in range(max(0, x0), min(W, x1 + 1)):
            set_px(canvas, x, y, alpha)

def draw_line(canvas, x0, y0, x1, y1, alpha=0xFF):
    """Bresenham line."""
    dx = abs(x1 - x0)
    dy = -abs(y1 - y0)
    sx = 1 if x0 < x1 else -1
    sy = 1 if y0 < y1 else -1
    err = dx + dy
    x, y = x0, y0
    while True:
        set_px(canvas, x, y, alpha)
        if x == x1 and y == y1:
            break
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x += sx
        if e2 <= dx:
            err += dx
            y += sy

def draw_ray(canvas, cx, cy, angle_deg, inner_r, outer_r, alpha=0xDD):
    """Draw a ray from inner_r to outer_r at given angle (0=right, CCW)."""
    rad = math.radians(angle_deg)
    x0 = round(cx + inner_r * math.cos(rad))
    y0 = round(cy + inner_r * math.sin(rad))
    x1 = round(cx + outer_r * math.cos(rad))
    y1 = round(cy + outer_r * math.sin(rad))
    draw_line(canvas, x0, y0, x1, y1, alpha)

def to_c_array(canvas):
    """Convert canvas to flat byte array (row-major)."""
    result = []
    for row in canvas:
        result.extend(row)
    return bytes(result)

def write_icon_file(filename, name, bitmap_bytes):
    lines = []
    lines.append('#include "lvgl.h"')
    lines.append('')
    lines.append(f'LV_ATTRIBUTE_LARGE_CONST static const uint8_t {name}_bitmap[] = {{')
    # Format as 16 bytes per line
    for i in range(0, len(bitmap_bytes), 16):
        chunk = bitmap_bytes[i:i+16]
        hex_str = ', '.join(f'0x{b:02X}' for b in chunk)
        lines.append(f'    {hex_str},')
    lines.append('};')
    lines.append('')
    lines.append(f'const lv_image_dsc_t {name} = {{')
    lines.append('    .header = {')
    lines.append('        .magic = LV_IMAGE_HEADER_MAGIC,')
    lines.append('        .cf = LV_COLOR_FORMAT_A8,')
    lines.append('        .flags = 0,')
    lines.append('        .w = 32,')
    lines.append('        .h = 32,')
    lines.append('        .stride = 32,')
    lines.append('        .reserved_2 = 0,')
    lines.append('    },')
    lines.append('    .data_size = 1024,')
    lines.append(f'    .data = {name}_bitmap,')
    lines.append('};')
    lines.append('')

    with open(filename, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))
    print(f"  Wrote {filename}  ({len(bitmap_bytes)} bytes)")

# ============================================================
# Icon: Sun (Clear)
# ============================================================
def gen_sun():
    c = new_canvas()
    # Central circle
    fill_circle(c, 16, 16, 7, 0xFF)
    fill_circle(c, 16, 16, 5, 0xFF)
    # 8 rays
    for angle in [0, 45, 90, 135, 180, 225, 270, 315]:
        draw_ray(c, 16, 16, angle, 8, 14, 0xDD)
    return c

# ============================================================
# Icon: Cloud (Clouds)
# ============================================================
def gen_cloud():
    c = new_canvas()
    # Cloud body: overlapping circles + bottom rect
    fill_circle(c, 16, 15, 9, 0xFF)       # main body
    fill_circle(c, 9, 16, 7, 0xFF)        # left bump
    fill_circle(c, 23, 16, 7, 0xFF)       # right bump
    fill_circle(c, 12, 12, 6, 0xFF)       # top-left fill
    fill_circle(c, 20, 12, 6, 0xFF)       # top-right fill
    fill_rect(c, 6, 18, 26, 22, 0xFF)     # flat bottom
    return c

# ============================================================
# Icon: Rain
# ============================================================
def gen_rain():
    c = gen_cloud()
    # Rain drops: 3 vertical dashed lines below cloud
    for x, y0 in [(10, 22), (16, 24), (22, 22)]:
        draw_line(c, x, y0, x, y0 + 6, 0xCC)
        draw_line(c, x + 1, y0 + 3, x + 1, y0 + 8, 0x99)
    return c

# ============================================================
# Icon: Drizzle
# ============================================================
def gen_drizzle():
    c = gen_cloud()
    # Drizzle: shorter, thinner drops
    for x, y0 in [(10, 22), (14, 25), (18, 22), (22, 24)]:
        draw_line(c, x, y0, x, y0 + 4, 0xBB)
    return c

# ============================================================
# Icon: Thunderstorm
# ============================================================
def gen_thunderstorm():
    c = gen_cloud()
    # Lightning bolt (zigzag) below cloud
    bolt = [(14, 22), (12, 28), (15, 28), (13, 31), (18, 25), (15, 25), (17, 22)]
    for i in range(len(bolt) - 1):
        draw_line(c, bolt[i][0], bolt[i][1], bolt[i+1][0], bolt[i+1][1], 0xFF)
    # Thicker bolt
    for i in range(len(bolt) - 1):
        draw_line(c, bolt[i][0]+1, bolt[i][1], bolt[i+1][0]+1, bolt[i+1][1], 0x99)
    return c

# ============================================================
# Icon: Snow
# ============================================================
def gen_snow():
    c = gen_cloud()
    # Snow: small dots (asterisk-like marks) below cloud
    snowflakes = [(9, 23), (16, 25), (23, 23)]
    for sx, sy in snowflakes:
        # Small cross pattern
        draw_line(c, sx - 2, sy, sx + 2, sy, 0xDD)
        draw_line(c, sx, sy - 2, sx, sy + 2, 0xDD)
    return c

# ============================================================
# Icon: Fog
# ============================================================
def gen_fog():
    c = new_canvas()
    # 3 horizontal wavy lines
    for row_y, alpha in [(10, 0xDD), (16, 0xDD), (22, 0xDD)]:
        for x in range(4, 28):
            set_px(c, x, row_y, alpha)
            # Slight wave
            wave = int(2 * math.sin(x * 0.8))
            set_px(c, x, row_y + wave, max(0x88, alpha - 0x22))
    return c

# ============================================================
# Main
# ============================================================
if __name__ == '__main__':
    icons = [
        ('icon_sun', gen_sun()),
        ('icon_cloud', gen_cloud()),
        ('icon_rain', gen_rain()),
        ('icon_drizzle', gen_drizzle()),
        ('icon_thunderstorm', gen_thunderstorm()),
        ('icon_snow', gen_snow()),
        ('icon_fog', gen_fog()),
    ]

    import os
    script_dir = os.path.dirname(os.path.abspath(__file__))

    for name, canvas in icons:
        data = to_c_array(canvas)
        filename = os.path.join(script_dir, f'{name}.c')
        write_icon_file(filename, name, data)

    print("\nDone! 7 weather icons generated.")
    print("Now add to icons.h:")
    for name, _ in icons:
        print(f'  extern const lv_image_dsc_t {name};')
