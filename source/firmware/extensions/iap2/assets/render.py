#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Render settings-car.png, the Settings pane icon for Car Audio: an
Apple-style front-view car silhouette on a 78 x 121 gray coverage mask, lit
from the top and mirrored in a fading reflection below. Uses Pillow.

    python3 assets/render.py
    python3 assets/render.py --check
"""

import argparse
import math
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw

HERE = Path(__file__).resolve().parent
OUT = HERE / "settings-car.png"
WIDTH, HEIGHT = 78, 121
SCALE = 8

# Art box vertical bounds matching stock Settings icons
ART_TOP, ART_BOTTOM = 16.0, 60.0
LIGHT_TOP, LIGHT_BOTTOM = 228.0, 153.0

# Floor reflection line and linear fade
MIRROR = 77.5
REFLECTION = (0.40, 0.02)


def _dist_sq_point_to_seg(px: float, py: float, ax: float, ay: float, bx: float, by: float) -> float:
    dx = bx - ax
    dy = by - ay
    denom = dx * dx + dy * dy
    if denom <= 0.0:
        return (px - ax) ** 2 + (py - ay) ** 2
    t = ((px - ax) * dx + (py - ay) * dy) / denom
    if t < 0.0:
        t = 0.0
    elif t > 1.0:
        t = 1.0
    qx = ax + t * dx
    qy = ay + t * dy
    return (px - qx) ** 2 + (py - qy) ** 2


def draw_rounded_trapezoid(
    img: Image.Image,
    top_y: float,
    bot_y: float,
    top_half_w: float,
    bot_half_w: float,
    radius: float,
    fill: int,
    cx: float = 39.0,
) -> None:
    """Draw a horizontally centered rounded trapezoid via exact Euclidean offset."""
    s = SCALE
    r = radius * s
    r2 = r * r
    ty = top_y * s
    by = bot_y * s
    tw = top_half_w * s
    bw = bot_half_w * s
    xc = cx * s

    # Inset the outer trapezoid edges inward by r so dilating by r gives exact rounded corners
    dx = bw - tw
    dy = by - ty
    hypot = math.hypot(dx, dy)
    # Side unit outward normal is (dy/hypot, -dx/hypot).
    # Horizontal offset at fixed y to shift line inward by r is r * hypot / dy.
    x_inset = r * hypot / dy
    # At y = ty + r, outer x is tw + dx * (r / dy); subtract x_inset.
    # At y = by - r, outer x is bw - dx * (r / dy); subtract x_inset.
    in_ty = ty + r
    in_by = by - r
    in_tw = max(0.0, tw + dx * (r / dy) - x_inset)
    in_bw = max(0.0, bw - dx * (r / dy) - x_inset)

    tl = (xc - in_tw, in_ty)
    tr = (xc + in_tw, in_ty)
    br = (xc + in_bw, in_by)
    bl = (xc - in_bw, in_by)
    segs = ((tl, tr), (tr, br), (br, bl), (bl, tl))

    x0 = max(0, int(math.floor(xc - bw - 2)))
    x1 = min(img.width - 1, int(math.ceil(xc + bw + 2)))
    y0 = max(0, int(math.floor(ty - 2)))
    y1 = min(img.height - 1, int(math.ceil(by + 2)))

    px = img.load()
    in_h = in_by - in_ty
    for y in range(y0, y1 + 1):
        fy = y + 0.5
        inside_y = in_ty <= fy <= in_by
        half_w_at_y = (in_tw + (in_bw - in_tw) * ((fy - in_ty) / in_h)) if inside_y else -1.0
        for x in range(x0, x1 + 1):
            fx = x + 0.5
            if inside_y and abs(fx - xc) <= half_w_at_y:
                px[x, y] = fill
                continue
            min_d2 = r2 + 1.0
            for (ax, ay), (bx, by_) in segs:
                d2 = _dist_sq_point_to_seg(fx, fy, ax, ay, bx, by_)
                if d2 < min_d2:
                    min_d2 = d2
            if min_d2 <= r2:
                px[x, y] = fill


def render_mask() -> Image.Image:
    w_ss, h_ss = WIDTH * SCALE, HEIGHT * SCALE
    mask = Image.new("L", (w_ss, h_ss), 0)
    d = ImageDraw.Draw(mask)
    s = SCALE
    cx = 39.0

    # 1. Tires (protruding below lower body)
    for tx0, tx1 in ((13.0, 22.8), (55.2, 65.0)):
        d.rounded_rectangle((tx0 * s, 47.0 * s, tx1 * s, 60.0 * s), radius=2.8 * s, fill=255)

    # 2. Cabin roof + A-pillars
    draw_rounded_trapezoid(
        mask,
        top_y=16.5,
        bot_y=36.8,
        top_half_w=17.5,
        bot_half_w=23.2,
        radius=4.8,
        fill=255,
        cx=cx,
    )

    # 3. Side mirrors
    d.rounded_rectangle((11.2 * s, 30.2 * s, 17.6 * s, 34.8 * s), radius=2.0 * s, fill=255)
    d.rounded_rectangle((60.4 * s, 30.2 * s, 66.8 * s, 34.8 * s), radius=2.0 * s, fill=255)

    # 4. Main car body / hood / bumper
    d.rounded_rectangle((11.5 * s, 33.5 * s, 66.5 * s, 53.5 * s), radius=6.0 * s, fill=255)

    # 5. Smooth windshield cutout
    draw_rounded_trapezoid(
        mask,
        top_y=20.4,
        bot_y=33.8,
        top_half_w=13.4,
        bot_half_w=18.2,
        radius=2.4,
        fill=0,
        cx=cx,
    )

    # 6. Headlight cutouts (two crisp circular headlights)
    for hx0, hx1 in ((15.8, 23.4), (54.6, 62.2)):
        d.ellipse((hx0 * s, 39.6 * s, hx1 * s, 47.2 * s), fill=0)

    # 7. Subtle center grille accent cutout
    d.rounded_rectangle(
        (28.5 * s, 42.8 * s, 49.5 * s, 46.2 * s),
        radius=1.7 * s,
        fill=0,
    )

    return mask.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS)


def apply_lighting_and_reflection(shape: Image.Image) -> Image.Image:
    # Vertical lighting gradient across the icon box
    slope = (LIGHT_TOP - LIGHT_BOTTOM) / (ART_BOTTOM - ART_TOP)
    top_val = LIGHT_TOP + slope * ART_TOP

    art = Image.new("L", (WIDTH, HEIGHT), 0)
    src_px = shape.load()
    art_px = art.load()
    for y in range(HEIGHT):
        light = max(0.0, min(255.0, top_val - slope * y)) / 255.0
        for x in range(WIDTH):
            art_px[x, y] = round(src_px[x, y] * light)

    # Reflection mirrored across MIRROR (77.5) and faded linearly
    start = round(MIRROR + 0.5 + (MIRROR - ART_BOTTOM))
    refl = Image.new("L", (WIDTH, HEIGHT), 0)
    refl_px = refl.load()
    for y in range(start, HEIGHT):
        src_y = round(2.0 * MIRROR - y)
        if 0 <= src_y < HEIGHT:
            t = (y - start) / max(1, (HEIGHT - 1 - start))
            fade = REFLECTION[0] + t * (REFLECTION[1] - REFLECTION[0])
            for x in range(WIDTH):
                refl_px[x, y] = round(art_px[x, src_y] * fade)

    return ImageChops.lighter(art, refl)


def render(out_path: Path) -> None:
    shape = render_mask()
    final = apply_lighting_and_reflection(shape)
    final.save(out_path, format="PNG")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument(
        "--check",
        action="store_true",
        help="render into a temporary file and compare with the committed PNG",
    )
    args = parser.parse_args()
    if not args.check:
        render(OUT)
        return
    with tempfile.TemporaryDirectory(prefix="settings-car-") as name:
        fresh = Path(name) / OUT.name
        render(fresh)
        if fresh.read_bytes() != OUT.read_bytes():
            sys.exit(f"render.py: {OUT.name} differs from a fresh render")
        print(f"{OUT.name} matches")


if __name__ == "__main__":
    main()
