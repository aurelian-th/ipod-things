#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Render icon.png, the 55 x 55 icon Extras lists for Dice: a white die with
black pips standing on a soft contact shadow, lit like the stock Extras
pictures, on the app's table green (the packer has no transparency). The scene
is a signed-distance model marched and shaded with numpy; no image assets go in.

    python3 icon.py
    python3 icon.py --check
"""

import argparse
import math
import struct
import sys
import zlib
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
OUT = HERE / "icon.png"
WIDTH, HEIGHT, SUPERSAMPLE = 55, 55, 4

# The die, in a frame where y is up and it stands on y = 0.
HALF, EDGE = 1.0, 0.14  # half side, edge radius
PIP_RADIUS, PIP_DEPTH, PIP_SPACING = 0.19, 0.05, 0.5
YAW = 32  # degrees the die turns from square-on, showing its right face
# Visible faces: five on top, three in front, one on the right; opposite faces sum to seven.
FACES = {"top": 5, "bottom": 2, "front": 3, "back": 4, "right": 1, "left": 6}
CAMERA, LOOK, FOV = (0.06, 3.6, 5.9), (0.06, 0.72, 0.0), 30
KEY_LIGHT, KEY_POWER = (-1.4, 4.0, 2.2), 4.0
SHADOW = 0.75
BACKDROP = np.array([0x10, 0x59, 0x31]) / 255  # sRGB, the table green of the app


def length(v):
    return np.linalg.norm(v, axis=-1)


def unit(v):
    return v / np.maximum(length(v)[..., None], 1e-9)


def rotation(yaw):
    """Rotation about y by `yaw` degrees."""
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


def linear(hex_color):
    """A #rrggbb sRGB colour as linear light."""
    c = np.array([int(hex_color[i : i + 2], 16) / 255 for i in (0, 2, 4)])
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


# ---- distance fields -------------------------------------------------------


def sd_box(q, half, radius):
    d = np.abs(q) - (np.array(half) - radius)
    return length(np.maximum(d, 0)) + np.minimum(d.max(-1), 0) - radius


class Material:
    def __init__(self, color, rough=0.05, coat=1.0):
        self.color, self.rough, self.coat = linear(color), rough, coat


class Shape:
    """A distance function in its own frame, placed in the world."""

    def __init__(self, fn, material, pos=(0, 0, 0), yaw=0.0):
        self.fn, self.material = fn, material
        self.pos, self.rot = np.array(pos, float), rotation(yaw)

    def local(self, p):
        return (p - self.pos) @ self.rot

    def sdf(self, p):
        return self.fn(self.local(p))


IVORY = Material("f6f3ea", rough=0.14, coat=0.5)
INK = Material("141416", rough=0.12, coat=0.8)

# Pip cells of each face value on a 3 x 3 grid, row by row.
PIPS = {
    1: [(1, 1)],
    2: [(0, 0), (2, 2)],
    3: [(0, 0), (1, 1), (2, 2)],
    4: [(0, 0), (0, 2), (2, 0), (2, 2)],
    5: [(0, 0), (0, 2), (1, 1), (2, 0), (2, 2)],
    6: [(0, 0), (0, 1), (0, 2), (2, 0), (2, 1), (2, 2)],
}
# Face normal and the two in-face axes a pip grid runs along.
AXES = {
    "top": ((0, 1, 0), (1, 0, 0), (0, 0, 1)),
    "bottom": ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
    "front": ((0, 0, 1), (1, 0, 0), (0, 1, 0)),
    "back": ((0, 0, -1), (1, 0, 0), (0, 1, 0)),
    "right": ((1, 0, 0), (0, 0, 1), (0, 1, 0)),
    "left": ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
}


def pip_centres():
    """Sphere centres whose caps dent the faces, PIP_DEPTH deep."""
    centres = []
    for face, value in FACES.items():
        n, u, v = (np.array(a, float) for a in AXES[face])
        for row, col in PIPS[value]:
            offset = (col - 1) * PIP_SPACING * u + (row - 1) * PIP_SPACING * v
            centres.append(n * (HALF + PIP_RADIUS - PIP_DEPTH) + offset)
    return np.array(centres)


def die(yaw=YAW):
    """The rounded cube less the pip spheres, and the sphere caps left in it."""
    centres = pip_centres()

    def box(q):
        return sd_box(q, (HALF, HALF, HALF), EDGE)

    def spheres(q):
        return length(q[..., None, :] - centres).min(-1) - PIP_RADIUS

    body = Shape(lambda q: np.maximum(box(q), -spheres(q)), IVORY, (0, HALF, 0), yaw)
    pips = Shape(lambda q: np.maximum(spheres(q), box(q)), INK, (0, HALF, 0), yaw)
    return [body, pips]


# ---- marching and shading --------------------------------------------------


def scene_sdf(shapes, p):
    ds = np.stack([s.sdf(p) for s in shapes], -1)
    return ds.min(-1), ds.argmin(-1)


def march(shapes, ro, rd, tmax=20.0, steps=128):
    t = np.zeros(ro.shape[:-1])
    hit = np.zeros(t.shape, bool)
    idx = np.zeros(t.shape, int)
    for _ in range(steps):
        d, i = scene_sdf(shapes, ro + rd * t[..., None])
        new = (d < 7e-4) & ~hit
        hit |= new
        idx = np.where(new, i, idx)
        t = np.where(hit, t, np.minimum(t + np.maximum(d, 7e-4) * 0.8, tmax))
    return t, hit, idx


def normal(shapes, p, e=6e-4):
    return unit(
        np.stack(
            [
                scene_sdf(shapes, p + np.eye(3)[k] * e)[0]
                - scene_sdf(shapes, p - np.eye(3)[k] * e)[0]
                for k in range(3)
            ],
            -1,
        )
    )


def soft_shadow(shapes, ro, rd, k=6.0, tmin=0.02, tmax=8.0, steps=48):
    res = np.ones(ro.shape[:-1])
    t = np.full(res.shape, tmin)
    for _ in range(steps):
        d, _ = scene_sdf(shapes, ro + rd * t[..., None])
        res = np.minimum(res, np.clip(k * d / t, 0, 1))
        t = np.minimum(t + np.clip(d, 0.015, 0.25), tmax)
    return res


def ambient_occlusion(shapes, p, n):
    occ, scale = 0.0, 1.0
    for i in range(1, 6):
        h = 0.03 + 0.14 * i
        d, _ = scene_sdf(shapes, p + n * h)
        occ += (h - d) * scale
        scale *= 0.75
    return np.clip(1 - 2.2 * occ, 0, 1)


# A studio in linear light: a graded sky over a dark floor, and softbox strips
# (direction, long axis, width tightness, length tightness, power).
SKY_TOP, SKY_HORIZON, SKY_FLOOR = (
    np.array([0.8, 0.82, 0.88]),
    np.array([0.2, 0.21, 0.24]),
    np.array([0.01, 0.01, 0.012]),
)
STRIPS = (
    ((-0.35, 0.75, 0.55), (1, 0, -0.4), 60.0, 4.0, 6.0),  # key, upper left front
    ((-0.25, 0.85, -0.45), (1, 0, 0.5), 18.0, 2.5, 5.0),  # top back, seen in the lid
    ((0.75, 0.45, 0.25), (0, 0.3, -1), 90.0, 6.0, 2.5),  # right rim
    ((0.0, 0.2, 1.0), (1, 0, 0), 25.0, 3.0, 0.8),  # soft front fill
)


def environment(d, blur=0.0):
    y = np.clip(d[..., 1], -1, 1)
    up, down = np.clip(y, 0, 1)[..., None], np.clip(-y, 0, 1)[..., None]
    out = (
        SKY_HORIZON
        + (SKY_TOP - SKY_HORIZON) * up**0.7
        + (SKY_FLOOR - SKY_HORIZON) * down**0.5
    )
    sharp = 1 - 0.85 * min(blur, 1.0)
    for direction, axis, width, span, power in STRIPS:
        c = unit(np.array(direction, float))
        a = unit(np.array(axis, float) - np.array(axis, float) @ c * c)
        along, across = (d * a).sum(-1), (d * np.cross(c, a)).sum(-1)
        facing = np.clip((d * c).sum(-1), 0, 1)
        glow = (
            power
            * facing**2
            * np.exp(-(across**2) * width * sharp - along**2 * span * sharp)
        )
        out = out + glow[..., None]
    return out


def shade(shapes):
    """Linear RGB and alpha of the scene at SUPERSAMPLE times the output size."""
    w, h = WIDTH * SUPERSAMPLE, HEIGHT * SUPERSAMPLE
    ys, xs = np.mgrid[0:h, 0:w]
    uv = np.stack([(xs + 0.5 - w / 2) / h, -(ys + 0.5 - h / 2) / h], -1)
    cam, look = np.array(CAMERA, float), np.array(LOOK, float)
    fwd = unit(look - cam)
    right = unit(np.cross(fwd, [0, 1, 0]))
    up = np.cross(right, fwd)
    focal = 1 / math.tan(math.radians(FOV) / 2) / 2
    rd = unit(uv[..., 0:1] * right + uv[..., 1:2] * up + focal * fwd)
    ro = np.broadcast_to(cam, rd.shape)

    t, hit, idx = march(shapes, ro, rd)
    p = ro + rd * t[..., None]
    n = normal(shapes, p)
    key = np.array(KEY_LIGHT, float)
    ldir = unit(key - p)
    v = -rd
    half = unit(ldir + v)
    ndl = np.clip((n * ldir).sum(-1), 0, 1)
    ndh = np.clip((n * half).sum(-1), 0, 1)
    ndv = np.clip((n * v).sum(-1), 1e-3, 1)
    falloff = 12.0 / np.maximum(length(key - p) ** 2, 1.0)
    shadow = soft_shadow(shapes, p + n * 0.02, ldir)
    occlusion = ambient_occlusion(shapes, p, n)
    reflected = unit(rd - 2 * (n * rd).sum(-1)[..., None] * n)
    wrap = np.clip(((n * ldir).sum(-1) + 0.3) / 1.3, 0, 1) ** 1.5
    hemisphere = environment(n, blur=1.0) * 0.35

    rgb = np.zeros(p.shape)
    for k, shape in enumerate(shapes):
        mask = idx == k
        if not mask.any():
            continue
        m = shape.material
        base = np.broadcast_to(m.color, p.shape)
        f0 = np.full_like(base, 0.04)
        fresnel = f0 + (1 - f0) * ((1 - ndv) ** 5)[..., None]
        a2 = max(m.rough, 0.03) ** 2
        dn = ndh * ndh * (a2 - 1) + 1
        ggx = a2 / (math.pi * dn * dn)
        specular = (ggx * ndl * shadow * falloff)[..., None] * fresnel * 0.9
        mirror = (
            environment(reflected, blur=m.rough * 1.5) * fresnel * occlusion[..., None]
        )
        diffuse = base * (
            (wrap * shadow * KEY_POWER * falloff)[..., None]
            * np.array([1.0, 0.96, 0.9])
            / math.pi
            + hemisphere * (occlusion * occlusion)[..., None]
        )
        color = diffuse * (1 - fresnel) + mirror * m.coat + specular * KEY_POWER / 3
        rgb = np.where(mask[..., None], color, rgb)

    # The contact shadow on the invisible floor.
    gt = -ro[..., 1] / np.where(np.abs(rd[..., 1]) < 1e-6, 1e-6, rd[..., 1])
    gp = ro + rd * gt[..., None]
    ground_shadow = soft_shadow(
        shapes, gp + np.array([0, 0.02, 0]), unit(key - gp), k=3.0
    )
    gd, _ = scene_sdf(shapes, gp)
    contact = np.clip(1 - gd / 0.4, 0, 1) ** 2
    floor = (
        np.clip(0.5 * (1 - ground_shadow) * contact**0.5 + 0.6 * contact**1.5, 0, 1)
        * SHADOW
    )
    alpha = np.where(hit, 1.0, np.where(gt > 0, floor, 0))
    return rgb, hit, alpha


def to_png(rgb, hit, alpha):
    """Tone-map, encode as sRGB, box-filter down to the output size, lay the
    result over the backdrop and pack a PNG."""
    x = np.clip(rgb, 0, None)
    x = (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14)
    x = np.clip(x, 0, 1)
    srgb = np.where(x <= 0.0031308, x * 12.92, 1.055 * x ** (1 / 2.4) - 0.055)
    color = np.where(hit[..., None], srgb, 0.0)
    premultiplied = np.concatenate([color * alpha[..., None], alpha[..., None]], -1)
    small = premultiplied.reshape(HEIGHT, SUPERSAMPLE, WIDTH, SUPERSAMPLE, 4).mean(
        (1, 3)
    )
    a = small[..., 3:4]
    flat = small[..., :3] + BACKDROP * (1 - a)
    rgb8 = (np.clip(flat, 0, 1) * 255 + 0.5).astype(np.uint8)
    raw = b"".join(b"\x00" + rgb8[y].tobytes() for y in range(HEIGHT))

    def chunk(kind, data):
        return (
            struct.pack(">I", len(data))
            + kind
            + data
            + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
        )

    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )


def render():
    return to_png(*shade(die()))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument(
        "--check",
        action="store_true",
        help="render in memory and compare with the committed file",
    )
    args = parser.parse_args()
    png = render()
    if not args.check:
        OUT.write_bytes(png)
        return
    if OUT.read_bytes() != png:
        sys.exit(f"icon.py: {OUT.name} differs from a fresh render")
    print(f"{OUT.name} matches")


if __name__ == "__main__":
    main()
