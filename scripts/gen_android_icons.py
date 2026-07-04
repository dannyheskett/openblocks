#!/usr/bin/env python3
"""Generate adaptive-icon foreground layers for the Android app.

Android 8+ (API 26) uses adaptive icons: a separate background + foreground
drawable that the launcher masks to its own shape (circle, squircle, ...). We
supply a solid background color (res/values/colors.xml) and these transparent
foreground PNGs holding the S-tetromino mark, sized so it sits inside the
adaptive-icon "safe zone" (the centre ~66 of 108 dp that every mask keeps).

Reuses the mark/palette from gen_play_assets.py so all icons stay identical.
Legacy square PNGs (mipmap-*/ic_launcher.png) are left as-is for API < 26.
"""
import os
import sys
from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
import gen_play_assets as g  # noqa: E402  (palette + draw_tetromino)

# Foreground canvas is 108 dp; density buckets scale from mdpi (1x).
DENSITIES = {
    "mdpi": 108,
    "hdpi": 162,
    "xhdpi": 216,
    "xxhdpi": 324,
    "xxxhdpi": 432,
}

RES = os.path.join(os.path.dirname(__file__), "..", "android", "res")


def gen_foreground(size):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    # Fit the 3-wide S-piece across ~58% of the canvas so it stays within the
    # 66/108 safe zone with margin. width = 3*cell + 2*gap, gap = 0.13*cell.
    cell = 0.58 * size / (3 + 2 * 0.13)
    gap = 0.13 * cell
    g.draw_tetromino(img, size / 2, size / 2, cell, gap, corner=cell * 0.16)
    return img


def main():
    for name, size in DENSITIES.items():
        d = os.path.join(RES, "mipmap-%s" % name)
        os.makedirs(d, exist_ok=True)
        path = os.path.join(d, "ic_launcher_foreground.png")
        gen_foreground(size).save(path)
        print("wrote", os.path.relpath(path))


if __name__ == "__main__":
    main()
