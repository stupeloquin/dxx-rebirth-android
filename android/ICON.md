# App Icon Generation

`gen_icon.py` extracts the Pyro-GX 3D model from `descent.pig` and renders it as a white silhouette app icon using BSP-ordered polygon traversal with back-face culling and shading.

## Running

Inside Docker:
```bash
docker exec dxx-builder python3 /build/dxx-android/gen_icon.py
```

Locally (if Pillow is installed and game data exists at `~/descent/descent/data/descent.pig`):
```bash
python3 gen_icon.py
```

### Descent II variant

```bash
python3 gen_icon.py --d2
```

This adds a bold serifed "II" in the lower-right corner. Output files are named `ic_launcher_d2.png`.

### Custom preview path

```bash
python3 gen_icon.py --preview /tmp/my_preview.png
```

## Output

- `app/src/main/res/mipmap-mdpi/ic_launcher.png` (48px)
- `app/src/main/res/mipmap-hdpi/ic_launcher.png` (72px)
- `app/src/main/res/mipmap-xhdpi/ic_launcher.png` (96px)
- `app/src/main/res/mipmap-xxhdpi/ic_launcher.png` (144px)
- `app/src/main/res/mipmap-xxxhdpi/ic_launcher.png` (192px)
- `app/src/main/ic_launcher_512.png` (512px, for Play Store)
- Preview: `/tmp/ship_icon_preview.png` (or `/tmp/ship_icon_d2_preview.png` for D2)

## Tweaking Appearance

Key parameters in `gen_icon.py`, all in the `main()` function:

| Parameter | Current | Effect |
|-----------|---------|--------|
| `angle_x` | 0.3 | Pitch: higher = more top-down view |
| `angle_y` | 0.5 | Yaw: positive = nose points right |
| `angle_z` | 0.0 | Roll |
| `size * 0.84` | 0.84 | Ship scale within icon (in `make_ship_icon`) |
| `0.75 + 0.25 * facing` | 0.75-1.0 | Shading range: min brightness to max (in `render_polymodel_bsp`) |
| `size * 0.08` | 0.08 | Margin around ship (in `render_polymodel_bsp`) |
| Background | black | Set in `make_ship_icon`: `(0, 0, 0, 255)` |
