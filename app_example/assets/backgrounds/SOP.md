# Background Image SOP

## Overview

Convert a PNG to an LVGL 9.3 C array for use as a dashboard background.

Two output formats are supported:

| Format     | Alpha | Size       | Use case                       |
|------------|-------|------------|--------------------------------|
| RGB565     | No    | 32 KB      | Solid/full-frame images        |
| RGB565A8   | Yes   | 48 KB      | Cut-out / transparent images   |

The script (`convert_to_lvgl.py`) detects the PNG's mode automatically:
- **RGBA** (with transparency) → RGB565A8  
- **RGB** (no transparency) → RGB565

---

## Step-by-step

### 1. Prepare your image

- Size: **128 × 128** pixels for generated tiled backgrounds; the centered silicon watermark is **200 × 200** pixels
- Format: **PNG**
- No alpha (solid fill): save as RGB PNG
- With alpha (cut-out): save as RGBA PNG, transparent areas will be truly invisible

### 2. Place the PNG

Put your file in this directory:

```
pc_dashboard_demo/app_example/assets/backgrounds/your_image.png
```

### 3. Register in FILE_MAP

Open `convert_to_lvgl.py` and add a line to `FILE_MAP`:

```python
FILE_MAP = {
    "bg_cobalt_128.png":             ("bg_cobalt",   "bg_cobalt_bitmap"),
    "bg_inferno_128.png":            ("bg_inferno",  "bg_inferno_bitmap"),
    "mcu.png":                       ("bg_silicon", "bg_silicon_bitmap"),
    "your_image.png":               ("your_struct", "your_bitmap"),   # <-- add here
}
```

The mapping is: `"PNG filename": ("struct_name", "bitmap_array_name")`

### 4. Run converter

```bash
python convert_to_lvgl.py
```

This generates `your_struct.c` in the same directory.

### 5. Declare in bg.h

Open `bg.h` and add:

```c
extern const lv_image_dsc_t your_struct;
```

### 6. Link in CMakeLists.txt

Open `app_example/CMakeLists.txt` and add to the source list:

```cmake
assets/backgrounds/your_struct.c
```

### 7. Use in theme.c

In `pc_dashboard_theme.c`, assign `bg_image` to your desired theme:

```c
/* THEME_SILICON */
{
    ...
    .bg_image = &your_struct,
},
```

Then adjust the display style in `theme_watermark_update()`:

```c
if (g_theme_id == THEME_SILICON)
{
    /* centred, non-tiled */
    lv_obj_set_size(g_bg_watermark, 200, 200);
    lv_obj_set_pos(g_bg_watermark,
                   (SCREEN_WIDTH - 200) / 2,
                   (SCREEN_HEIGHT - 200) / 2);
    lv_obj_set_style_bg_image_tiled(g_bg_watermark, false, 0);
}
else
{
    /* full-screen tiled */
    lv_obj_set_size(g_bg_watermark, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(g_bg_watermark, 0, 0);
    lv_obj_set_style_bg_image_tiled(g_bg_watermark, true, 0);
}
```

---

## Tips

- **Cut-out (alpha) images** use RGB565A8 = 48 KB each. This is fine for one or two extras, but don't add too many on a 512 KB SRAM MCU.
- **Solid images** use RGB565 = 32 KB each. The cobalt and inferno defaults use this format.
- The centered silicon watermark uses RGB565A8 because it contains an alpha channel.
- The `generate_bg.py` helper regenerates the cobalt and inferno tiled patterns; `mcu.png` is maintained as the silicon watermark source.
- The watermark container uses `LV_OPA_10` (10 % opacity). With alpha images, transparent areas will be fully invisible, only the subject shows faintly.

## Quick reference

```
Add PNG → register FILE_MAP → run convert_to_lvgl.py
       → declare bg.h → add CMakeLists.txt → assign theme.c
```

All four files to edit:

| File                                      | What to add               |
|-------------------------------------------|---------------------------|
| `assets/backgrounds/convert_to_lvgl.py`   | FILE_MAP entry            |
| `assets/backgrounds/bg.h`                 | `extern const` declaration |
| `app_example/CMakeLists.txt`              | source file               |
| `app_example/pc_dashboard_theme.c`        | `bg_image = &xxx,`        |
