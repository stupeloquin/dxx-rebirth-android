#!/usr/bin/env python3
"""Generate touch_icons.h with embedded PNG byte arrays for DXX-Rebirth Android touch overlay.

Each icon is 128x128, white shapes on transparent RGBA background.
Run inside Docker where PIL/Pillow is available.
"""

import io
import math
from PIL import Image, ImageDraw

SIZE = 128
WHITE = (255, 255, 255, 255)
TRANSPARENT = (0, 0, 0, 0)
LINE_WIDTH = 6


def new_icon():
    return Image.new("RGBA", (SIZE, SIZE), TRANSPARENT)


def draw_crosshair(img):
    """BTN_FIRE_SECONDARY: Crosshair / target circle with cross."""
    d = ImageDraw.Draw(img)
    cx, cy = SIZE // 2, SIZE // 2
    r = 40
    # Outer circle
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=WHITE, width=LINE_WIDTH)
    # Inner dot
    d.ellipse([cx - 6, cy - 6, cx + 6, cy + 6], fill=WHITE)
    # Cross lines extending beyond circle
    gap = 12
    ext = 22
    # Top
    d.line([(cx, cy - r - ext), (cx, cy - gap)], fill=WHITE, width=LINE_WIDTH)
    # Bottom
    d.line([(cx, cy + gap), (cx, cy + r + ext)], fill=WHITE, width=LINE_WIDTH)
    # Left
    d.line([(cx - r - ext, cy), (cx - gap, cy)], fill=WHITE, width=LINE_WIDTH)
    # Right
    d.line([(cx + gap, cy), (cx + r + ext, cy)], fill=WHITE, width=LINE_WIDTH)


def draw_starburst(img):
    """BTN_FLARE: Starburst / radiating lines from center."""
    d = ImageDraw.Draw(img)
    cx, cy = SIZE // 2, SIZE // 2
    num_rays = 8
    inner_r = 14
    outer_r = 52
    for i in range(num_rays):
        angle = 2 * math.pi * i / num_rays
        x1 = cx + inner_r * math.cos(angle)
        y1 = cy + inner_r * math.sin(angle)
        x2 = cx + outer_r * math.cos(angle)
        y2 = cy + outer_r * math.sin(angle)
        d.line([(x1, y1), (x2, y2)], fill=WHITE, width=LINE_WIDTH)
    # Center dot
    d.ellipse([cx - 8, cy - 8, cx + 8, cy + 8], fill=WHITE)


def draw_bomb(img):
    """BTN_BOMB: Circle with short fuse line on top."""
    d = ImageDraw.Draw(img)
    cx, cy = SIZE // 2, SIZE // 2 + 8
    r = 36
    # Body circle
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=WHITE, width=LINE_WIDTH)
    # Fill with slight smaller circle for solid look
    d.ellipse([cx - r + 3, cy - r + 3, cx + r - 3, cy + r - 3], fill=WHITE)
    # Fuse stem
    fuse_x = cx + 6
    fuse_bottom = cy - r + 4
    fuse_top = fuse_bottom - 20
    d.line([(fuse_x, fuse_bottom), (fuse_x, fuse_top)], fill=WHITE, width=LINE_WIDTH)
    # Spark at top of fuse - small lines
    spark_cx, spark_cy = fuse_x, fuse_top
    for angle_deg in [-50, -10, 30, 70]:
        a = math.radians(angle_deg)
        d.line([
            (spark_cx, spark_cy),
            (spark_cx + 10 * math.cos(a), spark_cy + 10 * math.sin(a) - 6)
        ], fill=WHITE, width=3)


def draw_bank_left(img):
    """BTN_BANK_LEFT: Curved arrow pointing counter-clockwise."""
    d = ImageDraw.Draw(img)
    cx, cy = SIZE // 2, SIZE // 2
    r = 38
    # Draw arc (partial circle) - upper portion
    # PIL arc uses degrees, 0=3 o'clock, going clockwise
    d.arc([cx - r, cy - r, cx + r, cy + r], start=200, end=340, fill=WHITE, width=LINE_WIDTH)
    # Arrowhead at the end (340 degrees = roughly 7 o'clock position going CW, which is upper-right)
    # 340 degrees in PIL = 340 deg clockwise from 3 o'clock = about 1 o'clock position
    end_angle = math.radians(-20)  # 340 deg in standard coords
    ax = cx + r * math.cos(end_angle)
    ay = cy + r * math.sin(end_angle)
    # Arrow points in direction of arc (counter-clockwise = upward at this point)
    arrow_len = 16
    d.polygon([
        (ax, ay),
        (ax - arrow_len, ay - 4),
        (ax - 4, ay + arrow_len),
    ], fill=WHITE)


def draw_bank_right(img):
    """BTN_BANK_RIGHT: Curved arrow pointing clockwise."""
    d = ImageDraw.Draw(img)
    cx, cy = SIZE // 2, SIZE // 2
    r = 38
    # Mirror of bank_left
    d.arc([cx - r, cy - r, cx + r, cy + r], start=200, end=340, fill=WHITE, width=LINE_WIDTH)
    # Arrowhead at the start (200 degrees)
    start_angle = math.radians(-200)
    ax = cx + r * math.cos(start_angle)
    ay = cy + r * math.sin(start_angle)
    arrow_len = 16
    d.polygon([
        (ax, ay),
        (ax + arrow_len, ay - 4),
        (ax + 4, ay + arrow_len),
    ], fill=WHITE)


def draw_automap(img):
    """BTN_AUTOMAP: Grid / 3x3 squares (map)."""
    d = ImageDraw.Draw(img)
    margin = 22
    cell_size = (SIZE - 2 * margin - 2 * 6) // 3  # 6px gaps
    gap = 6
    for row in range(3):
        for col in range(3):
            x = margin + col * (cell_size + gap)
            y = margin + row * (cell_size + gap)
            d.rectangle([x, y, x + cell_size, y + cell_size], outline=WHITE, width=4)
            # Fill center cell to indicate "you are here"
            if row == 1 and col == 1:
                d.rectangle([x + 4, y + 4, x + cell_size - 4, y + cell_size - 4], fill=WHITE)


def draw_esc(img):
    """BTN_ESC: Left-pointing arrow (back)."""
    d = ImageDraw.Draw(img)
    cx, cy = SIZE // 2, SIZE // 2
    # Arrow shaft
    shaft_left = cx - 32
    shaft_right = cx + 36
    d.line([(shaft_left, cy), (shaft_right, cy)], fill=WHITE, width=LINE_WIDTH)
    # Arrowhead
    head_size = 24
    d.line([(shaft_left, cy), (shaft_left + head_size, cy - head_size)], fill=WHITE, width=LINE_WIDTH)
    d.line([(shaft_left, cy), (shaft_left + head_size, cy + head_size)], fill=WHITE, width=LINE_WIDTH)


def draw_accept(img):
    """BTN_ACCEPT: Checkmark."""
    d = ImageDraw.Draw(img)
    # Checkmark from bottom-left to middle-bottom then up to top-right
    points = [
        (24, 68),   # start (left)
        (52, 96),   # bottom of check
        (104, 32),  # top-right
    ]
    d.line(points, fill=WHITE, width=LINE_WIDTH + 2)


def png_to_bytes(img):
    """Convert PIL Image to PNG byte string."""
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    return buf.getvalue()


def bytes_to_c_array(data, name):
    """Convert bytes to C unsigned char array literal."""
    lines = []
    lines.append(f"static constexpr unsigned char {name}[] = {{")
    for i in range(0, len(data), 12):
        chunk = data[i:i+12]
        hex_vals = ",".join(f"0x{b:02x}" for b in chunk)
        lines.append(f"\t{hex_vals},")
    lines.append("};")
    lines.append(f"static constexpr std::size_t {name}_size = sizeof({name});")
    return "\n".join(lines)


def main():
    icons = [
        ("fire_secondary", draw_crosshair),
        ("flare",          draw_starburst),
        ("bomb",           draw_bomb),
        ("bank_left",      draw_bank_left),
        ("bank_right",     draw_bank_right),
        ("automap",        draw_automap),
        ("esc",            draw_esc),
        ("accept",         draw_accept),
    ]

    btn_names = [
        "BTN_FIRE_SECONDARY",
        "BTN_FLARE",
        "BTN_BOMB",
        "BTN_BANK_LEFT",
        "BTN_BANK_RIGHT",
        "BTN_AUTOMAP",
        "BTN_ESC",
        "BTN_ACCEPT",
    ]

    arrays = []
    array_names = []
    for name, draw_fn in icons:
        img = new_icon()
        draw_fn(img)
        png_data = png_to_bytes(img)
        arr_name = f"icon_{name}_png"
        arrays.append(bytes_to_c_array(png_data, arr_name))
        array_names.append(arr_name)
        print(f"  {name}: {len(png_data)} bytes")

    # Build the header file
    header = []
    header.append("/*")
    header.append(" * This file is part of the DXX-Rebirth project <https://www.dxx-rebirth.com/>.")
    header.append(" * It is copyright by its individual contributors, as recorded in the")
    header.append(" * project's Git history.  See COPYING.txt at the top level for license")
    header.append(" * terms and a link to the Git history.")
    header.append(" */")
    header.append("")
    header.append("/*")
    header.append(" * Embedded PNG byte arrays for touch overlay button icons.")
    header.append(f" * Auto-generated by gen_touch_icons.py — {SIZE}x{SIZE} white on transparent RGBA.")
    header.append(" * Do not edit by hand.")
    header.append(" */")
    header.append("")
    header.append("#pragma once")
    header.append("")
    header.append("#include <cstddef>")
    header.append("")
    header.append("namespace dcx {")
    header.append("namespace touch_icons {")
    header.append("")

    for arr in arrays:
        header.append(arr)
        header.append("")

    # Build the button_icons table
    header.append("struct icon_entry {")
    header.append("\tconst unsigned char *data;")
    header.append("\tstd::size_t size;")
    header.append("};")
    header.append("")
    header.append("// Indices match the BTN_* enum in touch.cpp")
    header.append("static constexpr icon_entry button_icons[] = {")
    for arr_name, btn_name in zip(array_names, btn_names):
        header.append(f"\t{{ {arr_name}, {arr_name}_size }}, // {btn_name}")
    header.append("};")
    header.append("")
    header.append("static constexpr std::size_t button_icon_count = sizeof(button_icons) / sizeof(button_icons[0]);")
    header.append("")
    header.append("}  // namespace touch_icons")
    header.append("}  // namespace dcx")
    header.append("")

    output_path = "common/include/touch_icons.h"
    with open(output_path, "w") as f:
        f.write("\n".join(header))
    print(f"\nWrote {output_path}")


if __name__ == "__main__":
    main()
