#!/usr/bin/env python3
"""Generate Google Play store-listing image assets for openblocks.

Reproduces the app's S-tetromino mark and palette (sampled from the shipped
launcher icon) at the sizes the Play Console requires:

  - icon-512.png              512x512  (hi-res app icon; required)
  - feature-graphic-1024x500.png       (feature graphic; required)

Deterministic and dependency-light (Pillow only) so the assets can be
regenerated on demand. Outputs land in android/play-assets/.
"""
import os
from PIL import Image, ImageDraw, ImageFont

# Palette sampled from android/res/mipmap-xxxhdpi/ic_launcher.png.
BG     = (18, 20, 30, 255)      # near-black navy
CYAN   = (0, 200, 240, 255)     # S-piece top pair
BLUE   = (80, 80, 240, 255)     # S-piece bottom pair
WHITE  = (238, 240, 248, 255)
MUTED  = (120, 128, 150, 255)

OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "android", "play-assets")
MONO_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"


def _rounded(draw, box, radius, fill):
    draw.rounded_rectangle(box, radius=radius, fill=fill)


def draw_tetromino(img, cx, cy, cell, gap, corner):
    """Draw the S-piece centered on (cx, cy). Grid (col, row), origin top-left:
       row 0 (top):    cols 1,2  -> cyan
       row 1 (bottom): cols 0,1  -> blue
    """
    d = ImageDraw.Draw(img)
    step = cell + gap
    # Center the 3-wide x 2-tall bounding grid on (cx, cy).
    x0 = cx - (3 * step - gap) / 2
    y0 = cy - (2 * step - gap) / 2
    cells = [((1, 0), CYAN), ((2, 0), CYAN), ((0, 1), BLUE), ((1, 1), BLUE)]
    for (col, row), color in cells:
        bx = x0 + col * step
        by = y0 + row * step
        _rounded(d, [bx, by, bx + cell, by + cell], corner, color)


def gen_icon(size=512):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # Rounded-square background (~22% corner radius, matches the shipped icon).
    _rounded(d, [0, 0, size - 1, size - 1], int(size * 0.22), BG)
    cell = int(size * 0.26)
    gap = int(size * 0.035)
    draw_tetromino(img, size / 2, size / 2, cell, gap, corner=int(cell * 0.16))
    return img


def gen_feature(w=1024, h=500):
    img = Image.new("RGBA", (w, h), BG)
    d = ImageDraw.Draw(img)
    # Subtle diagonal lightening from bottom-left for a little depth.
    glow = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gd.ellipse([-w * 0.3, h * 0.4, w * 0.7, h * 1.6], fill=(30, 40, 70, 90))
    img = Image.alpha_composite(img, glow)
    d = ImageDraw.Draw(img)

    # Tetromino mark on the left third.
    draw_tetromino(img, w * 0.22, h * 0.5, cell=96, gap=14, corner=15)

    # Wordmark + tagline on the right.
    title = ImageFont.truetype(MONO_BOLD, 96)
    tag = ImageFont.truetype(MONO_BOLD, 34)
    tx = int(w * 0.40)
    d.text((tx, h * 0.30), "openblocks", font=title, fill=WHITE)
    d.text((tx + 4, h * 0.30 + 108), "a falling-block puzzle", font=tag, fill=MUTED)
    return img.convert("RGB")


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    icon = gen_icon(512)
    icon_path = os.path.join(OUT_DIR, "icon-512.png")
    icon.save(icon_path)
    print("wrote", os.path.relpath(icon_path))

    feat = gen_feature()
    feat_path = os.path.join(OUT_DIR, "feature-graphic-1024x500.png")
    feat.save(feat_path)
    print("wrote", os.path.relpath(feat_path))


if __name__ == "__main__":
    main()
