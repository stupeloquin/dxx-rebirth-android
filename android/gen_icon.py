#!/usr/bin/env python3
"""Generate app icons by extracting the Pyro-GX 3D model from Descent 1 PIG file.

Uses BSP-ordered traversal with back-face culling for correct rendering.
Pass --d2 to generate the Descent II variant with a "II" overlay.
"""

import struct
import math
import os
import argparse
from PIL import Image, ImageDraw

# === Descent 1 constants ===
MAX_TEXTURES = 800
MAX_SOUNDS = 250
MAX_VCLIPS = 70
MAX_EFFECTS = 60
MAX_WALL_ANIMS = 30
MAX_ROBOT_TYPES = 30
MAX_ROBOT_JOINTS = 600
MAX_WEAPON_TYPES = 30
MAX_POWERUP_TYPES = 29
MAX_GAUGE_BMS = 80
MAX_POLYGON_MODELS = 85
MAX_OBJ_BITMAPS = 210
MAX_SUBMODELS = 10
MAX_GUNS = 8
NDL = 5
N_ANIM_STATES = 5

TMAP_INFO_SIZE = 26
VCLIP_SIZE = 82
ECLIP_SIZE = 130
D1_WCLIP_SIZE = 66
WEAPON_INFO_D1_SIZE = 115
POWERUP_TYPE_INFO_SIZE = 16
POLYMODEL_HEADER_SIZE = 734
JOINTPOS_SIZE = 8


class PigReader:
    def __init__(self, data, offset=0):
        self.data = data
        self.pos = offset

    def tell(self):
        return self.pos

    def skip(self, n):
        self.pos += n

    def read_bytes(self, n):
        result = self.data[self.pos:self.pos + n]
        self.pos += n
        return result

    def read_int32(self):
        v = struct.unpack_from('<i', self.data, self.pos)[0]
        self.pos += 4
        return v

    def read_uint32(self):
        v = struct.unpack_from('<I', self.data, self.pos)[0]
        self.pos += 4
        return v

    def read_int16(self):
        v = struct.unpack_from('<h', self.data, self.pos)[0]
        self.pos += 2
        return v

    def read_uint16(self):
        v = struct.unpack_from('<H', self.data, self.pos)[0]
        self.pos += 2
        return v

    def read_byte(self):
        v = self.data[self.pos]
        self.pos += 1
        return v

    def read_fix(self):
        return self.read_int32()

    def read_vector(self):
        return (self.read_fix(), self.read_fix(), self.read_fix())

    def read_bitmap_index(self):
        return self.read_uint16()


def read_robot_info_d1(r):
    r.read_int32(); r.read_int32()  # model_num, n_guns
    for _ in range(MAX_GUNS): r.read_vector()
    for _ in range(MAX_GUNS): r.read_byte()
    for _ in range(4): r.read_int16()
    r.read_int16()
    for _ in range(4): r.read_byte()
    r.read_int32()
    for _ in range(4): r.read_fix()
    for _ in range(NDL * 3): r.read_fix()
    r.skip(4 * NDL * 2)
    for _ in range(NDL * 2): r.read_fix()
    for _ in range(NDL * 2): r.read_byte()
    for _ in range(6): r.read_byte()  # cloak, attack, boss, see/attack/claw sounds
    for _ in range((MAX_GUNS + 1) * N_ANIM_STATES):
        r.read_int16(); r.read_int16()
    return r.read_int32()


def read_polymodel_header(r):
    start = r.tell()
    n_models = r.read_byte()
    r.skip(3)
    model_data_size = r.read_uint32()
    r.skip(4)
    submodel_ptrs = [r.read_int32() for _ in range(MAX_SUBMODELS)]
    submodel_offsets = [r.read_vector() for _ in range(MAX_SUBMODELS)]
    for _ in range(MAX_SUBMODELS): r.read_vector()
    for _ in range(MAX_SUBMODELS): r.read_vector()
    for _ in range(MAX_SUBMODELS): r.read_fix()
    submodel_parents = [r.read_byte() for _ in range(MAX_SUBMODELS)]
    for _ in range(MAX_SUBMODELS): r.read_vector()
    for _ in range(MAX_SUBMODELS): r.read_vector()
    mins = r.read_vector()
    maxs = r.read_vector()
    rad = r.read_fix()
    first_texture = r.read_uint16()
    n_textures = r.read_byte()
    r.read_byte()
    assert r.tell() - start == POLYMODEL_HEADER_SIZE
    return {
        'n_models': n_models,
        'model_data_size': model_data_size,
        'submodel_ptrs': submodel_ptrs,
        'submodel_offsets': submodel_offsets,
        'submodel_parents': submodel_parents,
        'rad': rad, 'mins': mins, 'maxs': maxs,
        'n_textures': n_textures, 'first_texture': first_texture,
    }


def read_player_ship(r):
    model_num = r.read_int32()
    r.read_int32()
    for _ in range(7): r.read_fix()
    for _ in range(8): r.read_vector()
    return model_num


def parse_pig_gamedata(pig_data):
    r = PigReader(pig_data, 4)
    r.read_int32()
    for _ in range(MAX_TEXTURES): r.read_bitmap_index()
    for _ in range(MAX_TEXTURES): r.skip(TMAP_INFO_SIZE)
    r.skip(MAX_SOUNDS * 2)
    r.read_int32()
    for _ in range(MAX_VCLIPS): r.skip(VCLIP_SIZE)
    r.read_int32()
    for _ in range(MAX_EFFECTS): r.skip(ECLIP_SIZE)
    r.read_int32()
    for _ in range(MAX_WALL_ANIMS): r.skip(D1_WCLIP_SIZE)
    n_robot_types = r.read_int32()
    for i in range(MAX_ROBOT_TYPES):
        magic = read_robot_info_d1(r)
        if i < n_robot_types:
            assert magic == 0x0000abcd, f"Robot {i} magic: 0x{magic:08x}"
    r.read_int32()
    for _ in range(MAX_ROBOT_JOINTS): r.skip(JOINTPOS_SIZE)
    r.read_int32()
    for _ in range(MAX_WEAPON_TYPES): r.skip(WEAPON_INFO_D1_SIZE)
    r.read_int32()
    for _ in range(MAX_POWERUP_TYPES): r.skip(POWERUP_TYPE_INFO_SIZE)
    n_polygon_models = r.read_int32()
    print(f"N_polygon_models: {n_polygon_models}")
    polymodels = []
    for i in range(n_polygon_models):
        polymodels.append(read_polymodel_header(r))
    for i in range(n_polygon_models):
        polymodels[i]['model_data'] = r.read_bytes(polymodels[i]['model_data_size'])
    for _ in range(MAX_GAUGE_BMS): r.read_bitmap_index()
    for _ in range(MAX_POLYGON_MODELS * 2): r.read_int32()
    for _ in range(MAX_OBJ_BITMAPS): r.read_bitmap_index()
    for _ in range(MAX_OBJ_BITMAPS): r.read_int16()
    player_model_num = read_player_ship(r)
    print(f"Player ship model_num: {player_model_num}")
    return polymodels, player_model_num


def render_polymodel_bsp(model_data, submodel_ptrs, n_models,
                         angle_x, angle_y, angle_z, size):
    """Render polymodel with BSP-ordered traversal, back-face culling, and lighting.

    The polymodel interpreter data is a BSP tree. SORTNORM nodes split space by a
    plane; we evaluate which side the viewer is on and traverse back-to-front.
    FLATPOLY/TMAPPOLY leaves are back-face culled using their stored normal.
    """
    cx, sx = math.cos(angle_x), math.sin(angle_x)
    cy, sy = math.cos(angle_y), math.sin(angle_y)
    cz, sz = math.cos(angle_z), math.sin(angle_z)

    # View direction in model space: R^T * (0, 0, 1)
    view_dir = (-sy * cx, sx, cy * cx)

    def rotate(x, y, z):
        x1 = x * cy + z * sy
        z1 = -x * sy + z * cy
        y1 = y * cx - z1 * sx
        z2 = y * sx + z1 * cx
        x2 = x1 * cz - y1 * sz
        y2 = x1 * sz + y1 * cz
        return (x2, y2, z2)

    def read_u16(off):
        return struct.unpack_from('<H', model_data, off)[0]

    def read_vec(off):
        x, y, z = struct.unpack_from('<iii', model_data, off)
        return (x / 65536.0, y / 65536.0, z / 65536.0)

    def dot3(a, b):
        return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]

    points = []
    draw_list = []

    def traverse(p):
        """Walk the BSP tree, collecting visible polygons in back-to-front order."""
        while p < len(model_data) - 1:
            op = read_u16(p)

            if op == 0:  # EOF
                break

            elif op == 1:  # DEFPOINTS
                n = read_u16(p + 2)
                points.clear()
                for i in range(n):
                    points.append(read_vec(p + 4 + i * 12))
                p += 4 + n * 12

            elif op == 7:  # DEFP_START
                n = read_u16(p + 2)
                start = read_u16(p + 4)
                for i in range(n):
                    v = read_vec(p + 8 + i * 12)
                    while len(points) <= start + i:
                        points.append((0, 0, 0))
                    points[start + i] = v
                p += 8 + n * 12

            elif op == 2 or op == 3:  # FLATPOLY or TMAPPOLY
                n = read_u16(p + 2)
                normal = read_vec(p + 16)
                facing = dot3(view_dir, normal)

                verts = []
                for i in range(n):
                    idx = read_u16(p + 30 + i * 2)
                    if idx < len(points):
                        verts.append(points[idx])
                if len(verts) >= 3 and facing > 0:
                    proj = [rotate(*v) for v in verts]
                    light = 0.75 + 0.25 * facing
                    draw_list.append((proj, light))

                if op == 2:  # FLATPOLY
                    p += 30 + ((n & ~1) + 1) * 2
                else:  # TMAPPOLY
                    p += 30 + ((n & ~1) + 1) * 2 + n * 12

            elif op == 4:  # SORTNORM
                plane_normal = read_vec(p + 4)
                front_off = read_u16(p + 28)
                back_off = read_u16(p + 30)

                if dot3(view_dir, plane_normal) > 0:
                    traverse(p + back_off)
                    traverse(p + front_off)
                else:
                    traverse(p + front_off)
                    traverse(p + back_off)
                p += 32

            elif op == 5:  # RODBM
                p += 36

            elif op == 6:  # SUBCALL
                sub_off = read_u16(p + 16)
                traverse(p + sub_off)
                p += 20

            elif op == 8:  # GLOW
                p += 4

            else:
                break

    if n_models > 0 and submodel_ptrs[0] < len(model_data):
        traverse(submodel_ptrs[0])

    if not draw_list:
        return Image.new('RGBA', (size, size), (0, 0, 0, 0))

    all_pts = [pt for proj, _ in draw_list for pt in proj]
    min_x = min(pt[0] for pt in all_pts)
    max_x = max(pt[0] for pt in all_pts)
    min_y = min(pt[1] for pt in all_pts)
    max_y = max(pt[1] for pt in all_pts)

    range_x = max_x - min_x or 1
    range_y = max_y - min_y or 1
    margin = size * 0.08
    usable = size - 2 * margin
    scale = min(usable / range_x, usable / range_y)
    off_x = size / 2 - (min_x + max_x) / 2 * scale
    off_y = size / 2 - (min_y + max_y) / 2 * scale

    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    for proj, light in draw_list:
        screen_pts = [(pt[0] * scale + off_x, pt[1] * scale + off_y) for pt in proj]
        v = max(0, min(255, int(255 * light)))
        draw.polygon(screen_pts, fill=(v, v, v, 255))

    img = img.transpose(Image.FLIP_TOP_BOTTOM)
    return img


def make_ship_icon(ship_render, size, output_path):
    """Compose white ship silhouette on plain black background."""
    img = Image.new('RGBA', (size, size), (0, 0, 0, 255))

    ship_size = int(size * 0.84)
    ship_resized = ship_render.resize((ship_size, ship_size), Image.LANCZOS)
    offset = (size - ship_size) // 2

    ship_layer = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    ship_layer.paste(ship_resized, (offset, offset), ship_resized)
    img = Image.alpha_composite(img, ship_layer)

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    img.save(output_path, 'PNG')
    print(f"  Generated {output_path} ({size}x{size})")


def draw_roman_two(draw, size):
    """Draw bold geometric "II" in the lower-right of the icon.

    Uses two tall white rectangles with serifs, plus a dark outline for
    contrast against the white ship. Font-independent (no system fonts needed).
    """
    # "II" occupies roughly the lower-right quadrant
    # Each numeral I is a tall rectangle with small serifs top and bottom
    h = int(size * 0.38)          # height of each "I"
    w = max(2, int(size * 0.07))  # stroke width of each "I"
    serif = max(1, int(w * 0.6))  # serif overhang on each side
    serif_h = max(1, int(h * 0.08))  # serif thickness
    gap = max(2, int(size * 0.08))   # space between the two "I"s

    # Total width of "II" block
    total_w = 2 * (w + 2 * serif) + gap
    # Position: lower-right area, with some padding from edge
    pad_x = int(size * 0.10)
    pad_y = int(size * 0.08)
    base_x = size - pad_x - total_w
    base_y = size - pad_y - h

    outline = max(1, int(size * 0.015))  # dark outline thickness

    for i_num in range(2):
        cx = base_x + i_num * (w + 2 * serif + gap) + serif + w // 2
        # Draw dark outline first (slightly larger shapes behind white)
        _draw_serif_I(draw, cx, base_y, w, h, serif, serif_h, outline, (0, 0, 0, 220))
        # Draw white "I" on top
        _draw_serif_I(draw, cx, base_y, w, h, serif, serif_h, 0, (255, 255, 255, 255))


def _draw_serif_I(draw, cx, top_y, w, h, serif, serif_h, expand, color):
    """Draw a single serifed 'I' character centered at cx."""
    e = expand  # outline expansion
    half_w = w // 2

    # Vertical stroke
    draw.rectangle([cx - half_w - e, top_y - e,
                     cx + half_w + e, top_y + h + e], fill=color)
    # Top serif
    draw.rectangle([cx - half_w - serif - e, top_y - e,
                     cx + half_w + serif + e, top_y + serif_h + e], fill=color)
    # Bottom serif
    draw.rectangle([cx - half_w - serif - e, top_y + h - serif_h - e,
                     cx + half_w + serif + e, top_y + h + e], fill=color)


def make_d2_icon(ship_render, size, output_path):
    """Compose ship icon with "II" overlay for Descent II."""
    img = Image.new('RGBA', (size, size), (0, 0, 0, 255))

    ship_size = int(size * 0.84)
    ship_resized = ship_render.resize((ship_size, ship_size), Image.LANCZOS)
    offset = (size - ship_size) // 2

    ship_layer = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    ship_layer.paste(ship_resized, (offset, offset), ship_resized)
    img = Image.alpha_composite(img, ship_layer)

    # Draw "II" overlay
    overlay = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    overlay_draw = ImageDraw.Draw(overlay)
    draw_roman_two(overlay_draw, size)
    img = Image.alpha_composite(img, overlay)

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    img.save(output_path, 'PNG')
    print(f"  Generated {output_path} ({size}x{size})")


def main():
    parser = argparse.ArgumentParser(description='Generate DXX-Rebirth app icons from Descent PIG data')
    parser.add_argument('--d2', action='store_true', help='Generate Descent II variant with "II" overlay')
    parser.add_argument('--preview', type=str, default=None, help='Path for 512px preview image')
    args = parser.parse_args()

    pig_path = '/build/descent/data/descent.pig'
    if not os.path.exists(pig_path):
        pig_path = os.path.expanduser('~/descent/descent/data/descent.pig')

    print(f"Reading {pig_path}...")
    with open(pig_path, 'rb') as f:
        pig_data = f.read()

    polymodels, player_model_num = parse_pig_gamedata(pig_data)
    pm = polymodels[player_model_num]
    print(f"Ship model: {pm['n_models']} submodels, {pm['model_data_size']} bytes")

    # Ship facing right, viewed from slightly above
    render_size = 512
    angle_x = 0.3
    angle_y = 0.5
    angle_z = 0.0

    ship_img = render_polymodel_bsp(
        pm['model_data'], pm['submodel_ptrs'], pm['n_models'],
        angle_x, angle_y, angle_z, render_size
    )

    # Determine output base
    base = '/build/dxx-android/app/src/main/res'
    if not os.path.exists('/build/dxx-android'):
        base = os.path.expanduser('~/descent/dxx-android/app/src/main/res')

    densities = {
        'mipmap-mdpi': 48,
        'mipmap-hdpi': 72,
        'mipmap-xhdpi': 96,
        'mipmap-xxhdpi': 144,
        'mipmap-xxxhdpi': 192,
    }

    if args.d2:
        # Save preview
        preview_path = args.preview or '/tmp/ship_icon_d2_preview.png'
        preview = Image.new('RGBA', (512, 512), (0, 0, 0, 255))
        preview = Image.alpha_composite(preview, ship_img)
        overlay = Image.new('RGBA', (512, 512), (0, 0, 0, 0))
        draw_roman_two(ImageDraw.Draw(overlay), 512)
        preview = Image.alpha_composite(preview, overlay)
        preview.save(preview_path)
        print(f"D2 icon preview: {preview_path}")

        print("\nGenerating Descent II app icons...")
        for folder, icon_size in densities.items():
            path = f"{base}/{folder}/ic_launcher_d2.png"
            make_d2_icon(ship_img, icon_size, path)
        make_d2_icon(ship_img, 512, f"{base}/../ic_launcher_d2_512.png")
    else:
        # Save preview
        preview_path = args.preview or '/tmp/ship_icon_preview.png'
        preview = Image.new('RGBA', (512, 512), (0, 0, 0, 255))
        preview = Image.alpha_composite(preview, ship_img)
        preview.save(preview_path)
        print(f"Icon preview: {preview_path}")

        print("\nGenerating app icons...")
        for folder, icon_size in densities.items():
            path = f"{base}/{folder}/ic_launcher.png"
            make_ship_icon(ship_img, icon_size, path)
        make_ship_icon(ship_img, 512, f"{base}/../ic_launcher_512.png")

    print("Done!")


if __name__ == '__main__':
    main()
