#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The dice as OpenGL ES arrays: every solid as indexed triangles in 16.16
fixed point with normals and texture coordinates into the label atlas,
the labels the app draws into the atlas at start, and one rest matrix per
result that turns the result towards the camera. Writes solids.c; --check
only verifies.

The atlas is ATLAS_WIDTH x ATLAS_HEIGHT, cut into cells sized to each face.
Every face maps onto its own cell, except faces showing the same centred label
(the d3's opposite faces), so a label may sit anywhere on its face."""

import itertools
import math
import os
import re
import sys


def header_constants():
    """ATLAS_WIDTH, ATLAS_HEIGHT, SOLID_TEXELS, SHADOW_RIM and
    SOLID_TRIANGLES from solids.h, which main.c builds against."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "solids.h")
    with open(path) as f:
        found = dict(re.findall(r"^#define (\w+) (\d+)$", f.read(), re.MULTILINE))
    names = (
        "ATLAS_WIDTH",
        "ATLAS_HEIGHT",
        "SOLID_TEXELS",
        "SHADOW_RIM",
        "SOLID_TRIANGLES",
    )
    return tuple(int(found[n]) for n in names)


# TEXELS: atlas texels per unit of a solid, the screen pixels per unit of a
# resting die, so a top face shows its label pixel for pixel.
ATLAS_WIDTH, ATLAS_HEIGHT, TEXELS, SHADOW_RIM, MAX_TRIANGLES = header_constants()
EPSILON = 1e-6

PHI = (1 + math.sqrt(5)) / 2
# Every die is a shrunk copy of its solid wrapped in a skin of radius r, so
# faces stay flat and edges and corners turn round: r is EDGE_ROUNDING of the
# edge length and at most MAX_ROUNDING of the inradius, which keeps the d4 and
# d8 from melting. An edge turns in as few steps as keep its outline within
# ROUND_ERROR pixels of the arc on a resting die.
EDGE_ROUNDING = 0.14
MAX_ROUNDING = 0.3
ROUND_ERROR = 0.5
# The d6's shadow radius in circumradii. No other shadow shows further past its
# resting die's outline than the d6's, neither on average around it nor where
# the outline comes closest to the centre.
D6_SHADOW = 0.9
# The d4 reads at its top corner, whose three faces the camera straight above
# sees at a slant. Once a d4 roll has settled the camera tilts back by D4_TILT
# degrees, and the face below that corner on the screen looks at the viewer.
D4_TILT = 50
# Indices are GL_UNSIGNED_BYTE.
MAX_VERTICES = 256


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def scale(a, s):
    return tuple(x * s for x in a)


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def length(a):
    return math.sqrt(dot(a, a))


def unit(a):
    return scale(a, 1 / length(a))


def mean(points):
    return scale(tuple(map(sum, zip(*points))), 1 / len(points))


def hull_faces(points):
    """The faces of a convex solid: vertex index lists, counter-clockwise seen
    from outside, with their outward unit normals."""
    faces = []
    count = len(points)
    for i in range(count):
        for j in range(i + 1, count):
            for k in range(j + 1, count):
                normal = cross(sub(points[j], points[i]), sub(points[k], points[i]))
                if length(normal) < EPSILON:
                    continue
                normal = unit(normal)
                offset = dot(normal, points[i])
                sides = [dot(normal, p) - offset for p in points]
                if max(sides) > 1e-5:
                    normal, sides = scale(normal, -1), [-s for s in sides]
                if max(sides) > 1e-5:
                    continue
                if any(dot(normal, n) > 1 - 1e-5 for _, n in faces):
                    continue
                on = [m for m in range(count) if abs(sides[m]) < 1e-5]
                centre = mean([points[m] for m in on])
                axis = unit(sub(points[on[0]], centre))
                other = cross(normal, axis)

                def turn(m, centre=centre, axis=axis, other=other):
                    d = sub(points[m], centre)
                    return math.atan2(dot(d, other), dot(d, axis))

                on.sort(key=turn)
                faces.append((on, normal))
    return faces


def tetrahedron():
    return [(1, 1, 1), (1, -1, -1), (-1, 1, -1), (-1, -1, 1)]


def cube():
    return [(x, y, z) for x in (-1, 1) for y in (-1, 1) for z in (-1, 1)]


def octahedron():
    return [(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)]


def trapezohedron():
    """Pentagonal trapezohedron: apexes on the z axis and a zigzag ring of ten
    at radius 1, the ring height solved so every kite is flat."""
    apex = 1.0

    def ring(z0):
        return [
            (
                math.cos(math.pi * k / 5),
                math.sin(math.pi * k / 5),
                z0 if k % 2 == 0 else -z0,
            )
            for k in range(10)
        ]

    def bend(z0):
        r = ring(z0)
        top = (0, 0, apex)
        normal = cross(sub(r[0], top), sub(r[2], top))
        return dot(normal, sub(r[1], top))

    low, high = 0.0, apex
    for _ in range(100):
        middle = (low + high) / 2
        if (bend(low) > 0) == (bend(middle) > 0):
            low = middle
        else:
            high = middle
    return [(0, 0, apex), (0, 0, -apex)] + ring(low)


def dodecahedron():
    points = cube()
    for a in (-1 / PHI, 1 / PHI):
        for b in (-PHI, PHI):
            points += [(0, a, b), (a, b, 0), (b, 0, a)]
    return points


def icosahedron():
    points = []
    for a in (-1, 1):
        for b in (-PHI, PHI):
            points += [(0, a, b), (a, b, 0), (b, 0, a)]
    return points


def opposite_pairs(faces):
    pairs = []
    used = set()
    for i, (_, n) in enumerate(faces):
        if i in used:
            continue
        j = next(j for j, (_, m) in enumerate(faces) if dot(n, m) < -1 + 1e-5)
        used |= {i, j}
        pairs.append((i, j))
    return pairs


def opposite_values(faces, low, high):
    """Face labels from low to high, opposite faces adding up to low + high."""
    values = [None] * len(faces)
    pairs = opposite_pairs(faces)
    # Walk the pairs in a fixed spatial order so the numbering is stable.
    pairs.sort(key=lambda p: tuple(round(-c, 4) for c in faces[p[0]][1][::-1]))
    for index, (i, j) in enumerate(pairs):
        values[i] = low + index
        values[j] = low + high - values[i]
    return values


class Solid:
    def __init__(self, name, shape, points, size, text, labels, values=None):
        """`shape` names the geometry, which solids with the same shape share;
        `size` is the circumradius; `text` names the faces' labels: "pips",
        "number" or "corners" (the d4); `labels` gives the label of each
        result value, in result order; `values` the label per face. Numbers
        on a die that also shows 9 underline its 6 and 9."""
        radius = max(length(p) for p in points)
        self.name = name
        self.shape = shape
        self.points = [scale(p, size / radius) for p in points]
        self.faces = hull_faces(self.points)
        self.text = text
        self.labels = labels
        self.values = values(self.faces) if values else None
        self.underline = text == "number" and "9" in labels


def solids():
    """In the order of enum solid_kind in solids.h."""
    numbers = [str(n) for n in range(1, 21)]
    tens = [f"{n * 10:02d}" for n in range(10)]
    units = [str(n) for n in range(10)]

    def d3(f):
        return [v if v <= 3 else 7 - v for v in opposite_values(f, 1, 6)]

    return [
        Solid("d4", "d4", tetrahedron(), 1.2, "corners", numbers[:4]),
        Solid(
            "d6",
            "cube",
            cube(),
            1.25,
            "pips",
            numbers[:6],
            lambda f: opposite_values(f, 1, 6),
        ),
        Solid("d3", "cube", cube(), 1.25, "number", numbers[:3], d3),
        Solid(
            "d8",
            "d8",
            octahedron(),
            1.1,
            "number",
            numbers[:8],
            lambda f: opposite_values(f, 1, 8),
        ),
        Solid(
            "d10",
            "d10",
            trapezohedron(),
            0.97,
            "number",
            numbers[:10],
            lambda f: opposite_values(f, 1, 10),
        ),
        Solid(
            "d10_tens",
            "d10",
            trapezohedron(),
            0.97,
            "number",
            tens,
            lambda f: opposite_values(f, 0, 9),
        ),
        Solid(
            "d10_units",
            "d10",
            trapezohedron(),
            0.97,
            "number",
            units,
            lambda f: opposite_values(f, 0, 9),
        ),
        Solid(
            "d12",
            "d12",
            dodecahedron(),
            0.97,
            "number",
            numbers[:12],
            lambda f: opposite_values(f, 1, 12),
        ),
        Solid(
            "d20",
            "d20",
            icosahedron(),
            1.0,
            "number",
            numbers[:20],
            lambda f: opposite_values(f, 1, 20),
        ),
    ]


def result_index(solid, value):
    """The rest-matrix slot of a face value: d10_tens and d10_units count from
    0, the others from 1."""
    return value if solid.name in ("d10_tens", "d10_units") else value - 1


def margin(polygon, x, y):
    """How far x, y lies inside a counter-clockwise polygon; negative outside."""
    best = math.inf
    for (ax, ay), (bx, by) in zip(polygon, polygon[1:] + polygon[:1]):
        ex, ey = bx - ax, by - ay
        best = min(best, ((y - ay) * ex - (x - ax) * ey) / math.hypot(ex, ey))
    return best


def face_frame(solid, face):
    """Centre, right and up of a face as a viewer outside it sees them, and
    its corners in that frame. Up points at the face's first vertex (a square:
    its first edge; the d10: its apex), right = up x normal. The centre is the
    point deepest inside the face, where a label has the most room, on whole
    texels so a resting label lands on the screen's pixel grid."""
    on, normal = face
    corners = [solid.points[m] for m in on]
    centroid = mean(corners)
    if solid.shape == "d10":
        first = max(corners, key=lambda p: abs(p[2]))
    elif len(corners) == 4:
        first = scale(add(corners[0], corners[1]), 0.5)
    else:
        first = corners[0]
    up = unit(sub(first, centroid))
    up = unit(sub(up, scale(normal, dot(up, normal))))
    right = cross(up, normal)
    polygon = [
        (dot(sub(p, centroid), right), dot(sub(p, centroid), up)) for p in corners
    ]
    # Along the up axis is enough: every face is symmetric about it.
    span = max(abs(y) for _, y in polygon)
    best_y = max(
        (span * (k / 200 - 0.5) for k in range(201)),
        key=lambda y: margin(polygon, 0, y),
    )
    best_y = round(best_y * TEXELS) / TEXELS
    centre = add(centroid, scale(up, best_y))
    return centre, right, up, [(x, y - best_y) for x, y in polygon]


def cell_key(solid, index):
    """Faces showing the same centred label share a cell (the d3's opposite
    faces); every d4 face carries its own three labels."""
    if solid.text == "corners":
        return solid.name, index
    return solid.name, solid.values[index]


def pack(sizes):
    """Shelves of cells, tallest first: sizes maps a key to (width, height),
    the result a key to (x, y)."""
    cells = {}
    x = y = shelf = 0
    for key, (w, h) in sorted(sizes.items(), key=lambda item: -item[1][1]):
        if x + w > ATLAS_WIDTH:
            x, y, shelf = 0, y + shelf, 0
        if y + h > ATLAS_HEIGHT:
            sys.exit("the label atlas is full")
        cells[key] = (x, y)
        x += w
        shelf = max(shelf, h)
    return cells


def rounding(solid):
    """The skin radius r, the factor the solid shrinks by underneath and the
    largest turn of one rounding step."""
    on, normal = solid.faces[0]
    inradius = dot(normal, solid.points[on[0]])
    edge = min(
        length(sub(solid.points[f[k - 1]], solid.points[f[k]]))
        for f, _ in solid.faces
        for k in range(len(f))
    )
    r = min(EDGE_ROUNDING * edge, MAX_ROUNDING * inradius)
    step = 2 * math.degrees(math.acos(1 - ROUND_ERROR / (r * TEXELS)))
    return r, (inradius - r) / inradius, step


def slerp(a, b, t):
    angle = math.acos(max(-1.0, min(1.0, dot(a, b))))
    if angle < EPSILON:
        return a
    return add(
        scale(a, math.sin((1 - t) * angle) / math.sin(angle)),
        scale(b, math.sin(t * angle) / math.sin(angle)),
    )


def arc(a, b, step):
    """Normals from a to b in even steps of at most `step` degrees."""
    angle = math.degrees(math.acos(max(-1.0, min(1.0, dot(a, b)))))
    steps = max(1, math.ceil(angle / step - 1e-9))
    return [slerp(a, b, k / steps) for k in range(steps + 1)]


def rounded_mesh(solid):
    """The die as triangles of (position, normal, face): every face flat, with
    its index so it can be labelled, every edge a strip and every corner a fan
    of the skin, face None, with normals that curve so the shading rounds
    them."""
    r, shrink, step = rounding(solid)
    inner = [scale(p, shrink) for p in solid.points]
    triangles = []

    def skin(m, n, face=None):
        return add(inner[m], scale(n, r)), n, face

    def emit(a, b, c):
        outward = add(add(a[1], b[1]), c[1])
        if dot(cross(sub(b[0], a[0]), sub(c[0], a[0])), outward) < 0:
            b, c = c, b
        triangles.extend((a, b, c))

    for index, (on, n) in enumerate(solid.faces):
        corners = [skin(m, n, index) for m in on]
        for k in range(1, len(on) - 1):
            emit(corners[0], corners[k], corners[k + 1])
    for i, (a_on, a_n) in enumerate(solid.faces):
        for b_on, b_n in solid.faces[i + 1 :]:
            shared = [m for m in a_on if m in b_on]
            if len(shared) != 2:
                continue
            normals = arc(a_n, b_n, step)
            for n0, n1 in itertools.pairwise(normals):
                emit(skin(shared[0], n0), skin(shared[1], n0), skin(shared[1], n1))
                emit(skin(shared[0], n0), skin(shared[1], n1), skin(shared[0], n1))
    for m, p in enumerate(solid.points):
        around = [n for on, n in solid.faces if m in on]
        out = unit(p)
        e1 = unit(sub(around[0], scale(out, dot(around[0], out))))
        e2 = cross(out, e1)
        around.sort(key=lambda n: math.atan2(dot(n, e2), dot(n, e1)))
        middle = unit(tuple(map(sum, zip(*around))))
        for k, n in enumerate(around):
            normals = arc(n, around[(k + 1) % len(around)], step)
            for n0, n1 in itertools.pairwise(normals):
                emit(skin(m, middle), skin(m, n0), skin(m, n1))
    return triangles


def indexed(triangles):
    """The triangles as a vertex list and indices into it."""
    vertices, indices, seen = [], [], {}
    for position, normal, face in triangles:
        key = (
            tuple(round(c, 6) for c in position),
            tuple(round(c, 6) for c in normal),
            face,
        )
        if key not in seen:
            seen[key] = len(vertices)
            vertices.append((position, normal, face))
        indices.append(seen[key])
    return vertices, indices


def text_width(text, s):
    return (6 * len(text) - 1) * s


def text_size(text, s, underline):
    """The box of a label at s pixels per font pixel, its underline included."""
    return text_width(text, s), (9 if underline else 7) * s


def underlined(solid, text):
    return solid.underline and text in ("6", "9")


def box_corners(x, y, w, h, angle):
    """The corners of a w x h box centred on x, y and turned counter-clockwise
    by angle degrees, y up."""
    c, s = math.cos(math.radians(angle)), math.sin(math.radians(angle))
    return [
        (x + c * dx - s * dy, y + s * dx + c * dy)
        for dx, dy in (
            (-w / 2, -h / 2),
            (w / 2, -h / 2),
            (w / 2, h / 2),
            (-w / 2, h / 2),
        )
    ]


def apart(a, b, gap):
    """Whether two convex polygons are at least gap apart along some edge
    normal of either."""
    for polygon in (a, b):
        for (ax, ay), (bx, by) in zip(polygon, polygon[1:] + polygon[:1]):
            nx, ny = ay - by, bx - ax
            size = math.hypot(nx, ny)
            pa = [(x * nx + y * ny) / size for x, y in a]
            pb = [(x * nx + y * ny) / size for x, y in b]
            if min(pb) - max(pa) >= gap or min(pa) - max(pb) >= gap:
                return True
    return False


def label_size(sizes, flats):
    """The largest font size, in quarter pixels per font pixel, whose boxes
    fit inside every face's flat part, a texel clear of its edges: sizes maps
    a font pixel size to the label boxes, flats holds each flat part as a
    polygon in texels around the label's centre."""

    def fits(q):
        w, h = sizes(q / 4)
        return all(
            margin(polygon, x, y) >= 1
            for polygon in flats
            for x, y in box_corners(0, 0, w, h, 0)
        )

    q = 4
    while fits(q + 1):
        q += 1
    return q


def corner_labels(solid, face, frame, share):
    """The d4's labels on one face: one per corner, `share` of the way out,
    top towards the corner, as (x, y, text, angle) in whole texels around the
    frame's centre with y up."""
    centre, right, up, _ = frame
    labels = []
    for m in face[0]:
        d = sub(solid.points[m], centre)
        x = round(dot(d, right) * TEXELS * share)
        y = round(dot(d, up) * TEXELS * share)
        angle = round(math.degrees(math.atan2(-x, y))) % 360
        labels.append((x, y, solid.labels[m], angle))
    return labels


def corner_boxes(solid, face, frame, q, share):
    s = q / 4
    return [
        box_corners(x, y, *text_size(text, s, False), angle)
        for x, y, text, angle in corner_labels(solid, face, frame, share)
    ]


def corner_layout(solid, frames, flats):
    """The largest font size and its share of the way out to the corners at
    which the d4's three labels on every face fit inside its flat part and
    stay two texels apart."""
    for q in range(12, 3, -1):
        for share in (k / 100 for k in range(60, 15, -1)):
            if all(
                all(margin(flats[index], x, y) >= 1 for box in boxes for x, y in box)
                and all(
                    apart(a, b, 2) for i, a in enumerate(boxes) for b in boxes[i + 1 :]
                )
                for index, face in enumerate(solid.faces)
                for boxes in [corner_boxes(solid, face, frames[index], q, share)]
            ):
                return q, share
    sys.exit("the d4's labels do not fit")


def layout(solid):
    """Where a solid's labels go: the frame of every face, its flat part in
    texels around the frame's centre, and the label size in quarter pixels per
    font pixel, pixel-sharp whole scales from 2 up and quarter steps below.
    The d4 adds the share of the way out to the corners."""
    _, shrink, _ = rounding(solid)
    frames = [face_frame(solid, face) for face in solid.faces]
    flats = [
        [(x * shrink * TEXELS, y * shrink * TEXELS) for x, y in frame[3]]
        for frame in frames
    ]
    share = None
    if solid.text == "number":
        font = min(
            label_size(lambda s, t=text: text_size(t, s, underlined(solid, t)), flats)
            for text in set(solid.labels)
        )
        if font >= 8:
            font -= font % 4
    elif solid.text == "corners":
        font, share = corner_layout(solid, frames, flats)
    else:
        # Pips a quarter of the face apart.
        font = round(max(abs(c) for c in solid.points[0]) * TEXELS / 2)
    return frames, flats, font, share


def pip_boxes(spacing, value):
    """The pips of a six-sided face as boxes around their circles, matching
    main.c: radius spacing * 5 / 14, a circle of radius r covering 2r + 1
    pixels, on a 3 x 3 grid row by row (y down there, up here)."""
    grid = {1: [4], 2: [0, 8], 3: [0, 4, 8], 4: [0, 2, 6, 8], 5: [0, 2, 4, 6, 8]}
    cells = grid.get(value, [0, 2, 3, 5, 6, 8])
    side = 2 * (spacing * 5 // 14) + 1
    return [
        box_corners((c % 3 - 1) * spacing, -(c // 3 - 1) * spacing, side, side, 0)
        for c in cells
    ]


def outline(vertices, rest):
    """How far a resting die's outline reaches from its centre, every degree
    around."""
    points = [apply(rest, p) for p, _, _ in vertices]
    return [
        max(x * math.cos(a) + y * math.sin(a) for x, y, _ in points)
        for a in (math.radians(k) for k in range(360))
    ]


def reach(reaches, radius):
    """How far, as a share of its radius, a shadow shows past an outline on
    average."""
    return sum(max(0, 1 - h / radius) for h in reaches) / len(reaches)


def polar(reaches, angle):
    """How far from the centre an outline lies towards angle degrees."""
    return min(
        h / math.cos(math.radians(a - angle))
        for a, h in enumerate(reaches)
        if math.cos(math.radians(a - angle)) > 0.05
    )


def shadows(all_solids, shapes, rests):
    """The shadow of every solid: a disc for a die in the air, the d6's at
    D6_SHADOW of its circumradius, the others as large as they can be
    without showing further past their outlines than the d6's does; and the
    rim at rest every 360 / SHADOW_RIM degrees: none for a disc, or for the d4
    its footprint grown to show as far past it on average."""
    outlines = [
        outline(shapes[solid.shape][0], rests[solid.name][0][0]) for solid in all_solids
    ]
    d6 = next(i for i, solid in enumerate(all_solids) if solid.name == "d6")
    radius = D6_SHADOW * max(length(p) for p in all_solids[d6].points)
    target = reach(outlines[d6], radius)
    closest = 1 - min(outlines[d6]) / radius
    radii = []
    for reaches in outlines:
        low, high = min(reaches), 4 * max(reaches)
        for _ in range(60):
            middle = (low + high) / 2
            low, high = (
                (middle, high) if reach(reaches, middle) < target else (low, middle)
            )
        radii.append(min(low, min(reaches) / (1 - closest)))
    rims = []
    for solid, reaches, radius in zip(all_solids, outlines, radii, strict=True):
        if solid.text == "corners":
            angles = (360 * k / SHADOW_RIM for k in range(SHADOW_RIM))
            rims.append([polar(reaches, a) / (1 - target) for a in angles])
        else:
            rims.append(None)
    return radii, rims


def to_fixed(v):
    return round(v * 65536)


def rest_row(m):
    """A 3 x 3 column-major rotation as a 4 x 4 GL matrix in fixed point."""
    full = [m[0], m[1], m[2], 0, m[3], m[4], m[5], 0, m[6], m[7], m[8], 0, 0, 0, 0, 1]
    return tuple(to_fixed(v) for v in full)


def build(all_solids):
    plans = {solid.name: layout(solid) for solid in all_solids}
    # The two dice of a d100 pair carry the same label size.
    pair = min(plans["d10_tens"][2], plans["d10_units"][2])
    for name in ("d10_tens", "d10_units"):
        plans[name] = plans[name][:2] + (pair, None)

    # A cell spans its face's flat part and every label on it, a texel clear
    # all round; origin is where the frame's centre falls inside it.
    sizes, origins = {"white": (4, 4)}, {"white": (2, 2)}
    for solid in all_solids:
        frames, flats, font, share = plans[solid.name]
        for index, face in enumerate(solid.faces):
            key = cell_key(solid, index)
            if key in sizes:
                continue
            points = list(flats[index])
            if solid.text == "corners":
                for box in corner_boxes(solid, face, frames[index], font, share):
                    points += box
            left = math.floor(min(x for x, _ in points)) - 1
            right = math.ceil(max(x for x, _ in points)) + 1
            bottom = math.floor(min(y for _, y in points)) - 1
            top = math.ceil(max(y for _, y in points)) + 1
            sizes[key] = (right - left, top - bottom)
            origins[key] = (-left, top)
    cells = pack(sizes)
    white = cells["white"]
    white_uv = ((white[0] + 2) / ATLAS_WIDTH, (white[1] + 2) / ATLAS_HEIGHT)

    shapes = {}
    for solid in all_solids:
        if solid.shape not in shapes:
            shapes[solid.shape] = indexed(rounded_mesh(solid))

    out = {"solids": [], "labels": [], "shapes": shapes}
    for solid in all_solids:
        frames, flats, font, share = plans[solid.name]
        rests = [None] * len(solid.labels)
        drawn = set()
        for index, face in enumerate(solid.faces):
            key = cell_key(solid, index)
            if key in drawn:
                continue
            drawn.add(key)
            (cx, cy), (ox, oy) = cells[key], origins[key]
            if solid.text == "corners":
                for x, y, text, angle in corner_labels(
                    solid, face, frames[index], share
                ):
                    out["labels"].append(
                        {
                            "cell_x": cx,
                            "cell_y": cy,
                            "x": ox + x,
                            "y": oy - y,
                            "text": text,
                            "size": font,
                            "angle": angle,
                            "pips": 0,
                            "underline": False,
                        }
                    )
                continue
            label = solid.labels[result_index(solid, solid.values[index])]
            pips = solid.values[index] if solid.text == "pips" else 0
            line = underlined(solid, label)
            if pips:
                boxes = pip_boxes(font, pips)
            else:
                boxes = [box_corners(0, 0, *text_size(label, font / 4, line), 0)]
            out["labels"].append(
                {
                    "cell_x": cx,
                    "cell_y": cy,
                    "x": ox,
                    "y": oy,
                    "text": label,
                    "size": font,
                    "angle": 0,
                    "pips": pips,
                    "underline": line,
                    "boxes": boxes,
                    "flat": flats[index],
                }
            )

        def face_uv(index, p, solid=solid, frames=frames):
            centre, right, up, _ = frames[index]
            (cx, cy), (ox, oy) = (
                cells[cell_key(solid, index)],
                origins[cell_key(solid, index)],
            )
            d = sub(p, centre)
            return (
                (cx + ox + dot(d, right) * TEXELS) / ATLAS_WIDTH,
                (cy + oy - dot(d, up) * TEXELS) / ATLAS_HEIGHT,
            )

        vertices, _ = shapes[solid.shape]
        uvs = [
            white_uv if face is None else face_uv(face, p) for p, _, face in vertices
        ]
        if solid.text == "corners":
            for m, p in enumerate(solid.points):
                # The corner on top and another one straight up the screen:
                # the face on the other two then faces down the screen.
                normal = unit(p)
                other = next(n for n in range(4) if n != m)
                back = sub(solid.points[other], p)
                up = unit(sub(back, scale(normal, dot(back, normal))))
                upright = rotation_matrix(cross(up, normal), up, normal)
                front = next(f for f in solid.faces if m in f[0] and other not in f[0])
                rests[m] = (upright, front)
        else:
            for index, (face, frame) in enumerate(zip(solid.faces, frames)):
                slot = result_index(solid, solid.values[index])
                if rests[slot] is None:
                    _, right, up, _ = frame
                    rests[slot] = (rotation_matrix(right, up, face[1]), face)
        out["solids"].append({"solid": solid, "uvs": uvs, "rests": rests})
    rests = {entry["solid"].name: entry["rests"] for entry in out["solids"]}
    radii, rims = shadows(all_solids, shapes, rests)
    for entry, radius, rim in zip(out["solids"], radii, rims, strict=True):
        entry["shadow"], entry["rim"] = radius, rim
    return out


def lean(degrees):
    """Column-major matrix that tilts the camera back by degrees: what lies
    down the screen comes towards it."""
    c, s = math.cos(math.radians(degrees)), math.sin(math.radians(degrees))
    return [1, 0, 0, 0, c, -s, 0, s, c]


def multiply(a, b):
    """Column-major a * b."""
    return [
        sum(a[k * 3 + r] * b[c * 3 + k] for k in range(3))
        for c in range(3)
        for r in range(3)
    ]


def rotation_matrix(right, up, normal):
    """Column-major GL matrix with rows right, up, normal: the face's frame
    onto the eye's x, y and z."""
    rows = (right, up, normal)
    return [rows[r][c] for c in range(3) for r in range(3)]


def apply(matrix, v):
    return tuple(sum(matrix[c * 3 + r] * v[c] for c in range(3)) for r in range(3))


def check_solid(entry, problems):
    solid = entry["solid"]
    name = solid.name
    expected = {"d4": 4, "d6": 6, "d3": 6, "d8": 8, "d12": 12, "d20": 20}.get(name, 10)
    if len(solid.faces) != expected:
        problems.append(f"{name}: {len(solid.faces)} faces")
    for on, normal in solid.faces:
        if dot(normal, mean([solid.points[m] for m in on])) <= 0:
            problems.append(f"{name}: inward normal")
        for k in range(1, len(on) - 1):
            a, b, c = (solid.points[on[m]] for m in (0, k, k + 1))
            if dot(cross(sub(b, a), sub(c, a)), normal) <= 0:
                problems.append(f"{name}: clockwise triangle")
    if solid.values:
        for i, j in opposite_pairs(solid.faces):
            total = solid.values[i] + solid.values[j]
            if name == "d3":
                if solid.values[i] != solid.values[j]:
                    problems.append("d3: opposite faces differ")
            elif total != min(solid.values) + max(solid.values):
                problems.append(f"{name}: opposite faces add up to {total}")
    for slot, rest in enumerate(entry["rests"]):
        if rest is None:
            problems.append(f"{name}: no rest for result {slot}")
            continue
        matrix, face = rest
        if solid.text == "corners":
            top = apply(matrix, solid.points[slot])
            if top[0] ** 2 + top[1] ** 2 > EPSILON or top[2] <= 0:
                problems.append(f"{name}: result {slot} not on top")
            matrix = multiply(lean(D4_TILT), matrix)
            facing = apply(matrix, face[1])
            corners = [apply(matrix, solid.points[m]) for m in face[0]]
            if slot not in face[0] or facing[2] < math.cos(math.radians(30)):
                problems.append(f"{name}: result {slot} does not face the camera")
            if max(corners, key=lambda c: c[1]) != apply(matrix, solid.points[slot]):
                problems.append(f"{name}: result {slot} is not the face's top corner")
        else:
            z = apply(matrix, face[1])
            if abs(z[2] - 1) > 1e-5:
                problems.append(f"{name}: result {slot} not towards the camera")
            if result_index(solid, solid.values[solid.faces.index(face)]) != slot:
                problems.append(f"{name}: result {slot} shows the wrong face")
        for c in range(3):
            if abs(length(matrix[c * 3 : c * 3 + 3]) - 1) > 1e-5:
                problems.append(f"{name}: rest {slot} is not a rotation")
    if any(not (0 <= u <= 1 and 0 <= v <= 1) for u, v in entry["uvs"]):
        problems.append(f"{name}: texture coordinate outside the atlas")


def check(built):
    problems = []
    for entry in built["solids"]:
        check_solid(entry, problems)
    for shape, (vertices, _) in built["shapes"].items():
        if len(vertices) > MAX_VERTICES:
            problems.append(f"{shape}: {len(vertices)} vertices")
    for label in built["labels"]:
        if label["size"] < 4 and not label["pips"]:
            problems.append(f"label {label['text']} has no room")
        for box in label.get("boxes", []):
            if any(margin(label["flat"], x, y) < 0 for x, y in box):
                problems.append(f"label {label['text']} leaves its face")
                break
        if not 0 <= label["x"] < 256 or not 0 <= label["y"] < 256:
            problems.append(f"label {label['text']} is off its cell")
    return problems


def c_numbers(values, per_line=10):
    lines = []
    for i in range(0, len(values), per_line):
        lines.append("    " + " ".join(f"{v}," for v in values[i : i + per_line]))
    return "\n".join(lines)


def write(built, target):
    out = ["/* Generated by solids.py; do not edit. */", "", '#include "solids.h"', ""]
    for shape, (vertices, indices) in built["shapes"].items():
        if len(indices) > 3 * MAX_TRIANGLES:
            sys.exit(f"{shape} has more than SOLID_TRIANGLES triangles")
        positions = [to_fixed(c) for p, _, _ in vertices for c in p]
        normals = [to_fixed(c) for _, n, _ in vertices for c in n]
        out += [
            f"static const GLfixed {shape}_vertices[] = {{",
            c_numbers(positions),
            "};",
        ]
        out += [
            f"static const GLfixed {shape}_normals[] = {{",
            c_numbers(normals),
            "};",
        ]
        out += [
            f"static const GLubyte {shape}_indices[] = {{",
            c_numbers(indices),
            "};",
        ]
        out.append("")
    # A solid whose rest matrices begin another's points into those.
    rest_rows = {}
    for entry in built["solids"]:
        rows = tuple(rest_row(m) for m, _ in entry["rests"])
        entry["rest_rows"] = rows
        if not any(other[: len(rows)] == rows for other in rest_rows.values()):
            for name, other in list(rest_rows.items()):
                if rows[: len(other)] == other:
                    del rest_rows[name]
            rest_rows[entry["solid"].name] = rows
    for entry in built["solids"]:
        name = entry["solid"].name
        uvs = [to_fixed(c) for uv in entry["uvs"] for c in uv]
        out += [f"static const GLfixed {name}_uvs[] = {{", c_numbers(uvs), "};"]
        entry["rest_name"] = next(
            other
            for other, rows in rest_rows.items()
            if rows[: len(entry["rest_rows"])] == entry["rest_rows"]
        )
        if entry["rim"]:
            rim = [f"{r:.5f}f" for r in entry["rim"]]
            out += [
                f"static const GLfloat {name}_shadow[] = {{",
                c_numbers(rim, 6),
                "};",
            ]
    for name, rows in rest_rows.items():
        out.append(f"static const GLfixed {name}_rest[][16] = {{")
        for row in rows:
            out.append("    {" + " ".join(f"{v}," for v in row) + "},")
        out += ["};", ""]
    out.append("const struct solid solids[SOLIDS] = {")
    for entry in built["solids"]:
        solid = entry["solid"]
        shape = solid.shape
        on, normal = solid.faces[0]
        height = dot(normal, solid.points[on[0]])
        tilt = D4_TILT if solid.text == "corners" else 0
        out.append(
            f"    {{{shape}_vertices, {shape}_normals, {shape}_indices, "
            f"{solid.name}_uvs, {entry['rest_name']}_rest, "
            f"{solid.name + '_shadow' if entry['rim'] else 'NULL'}, "
            f"{len(built['shapes'][shape][1])}, {entry['shadow']:.5f}f, "
            f"{height:.5f}f, {tilt}}},"
        )
    out += ["};", "", "const struct solid_label solid_labels[] = {"]
    for label in built["labels"]:
        cos64 = round(math.cos(math.radians(label["angle"])) * 64)
        sin64 = round(math.sin(math.radians(label["angle"])) * 64)
        fields = [
            label["cell_x"],
            label["cell_y"],
            label["x"],
            label["y"],
            label["size"],
            cos64,
            sin64,
            label["pips"],
            int(label["underline"]),
        ]
        out.append("    {" + ", ".join(map(str, fields)) + f', "{label["text"]}"}},')
    out += ["};", f"const unsigned solid_label_count = {len(built['labels'])};", ""]
    ring = []
    for k in range(SHADOW_RIM):
        angle = 2 * math.pi * k / SHADOW_RIM
        ring += [f"{math.cos(angle):.5f}f", f"{math.sin(angle):.5f}f"]
    out += [
        "const GLfloat shadow_directions[SHADOW_RIM * 2] = {",
        c_numbers(ring, 6),
        "};",
        "",
    ]
    with open(target, "w") as f:
        f.write("\n".join(out))


def main(argv):
    built = build(solids())
    problems = check(built)
    for problem in problems:
        print(problem, file=sys.stderr)
    if problems:
        return 1
    if argv[1:] == ["--check"]:
        return 0
    if len(argv) != 2:
        print("usage: solids.py solids.c | --check", file=sys.stderr)
        return 2
    write(built, argv[1])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
