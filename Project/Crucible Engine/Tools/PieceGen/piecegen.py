"""
PieceGen: low-poly placeholder outpost pieces as .glb, for the Crucible engine.

AI-generated placeholder tool: written with Claude (Anthropic, Claude Opus 5.5), November 2026.
Meant to be replaced by the team's own art; nothing in the engine depends on it. Uses TreeGen's
mesh and export code (../TreeGen/treegen.py), so the pieces match the trees: one mesh each,
flat-shaded, vertex-coloured (no textures), metres, +Y up, base at the origin, sunk SINK metres
below it so a piece on uneven ground never floats. The same piece always comes out the same.

Pieces (the GDD's Capture villages: wooden houses, outpost towers, mineshafts, rocks and dust),
with the outpost slot each is meant for:
  Watchtower   ~3 x 9.5 x 3 m   timber legs, platform, railing, pointed roof        Centre
  Barricade    ~4 x 1.2 x 1 m   log wall on posts, chest-high cover                 Wall Edge
  RockPile     ~3.5 x 1.4 x 2.5 m  a mound of boulders, chest-high cover            Wall Edge
  Crates       ~2.4 x 2.2 x 2 m stacked supply crates                               High / Flat
  Hut          ~4.6 x 4 x 4.6 m plank walls, door, gabled roof                      Flat
  Mineshaft    ~5 x 3 x 4 m     timber-framed entrance in a dirt mound              Flat
Stone set (ruined stone structures from rough-cut blocks):
  StoneObelisk       ~3.6 x 9 x 3.6 m  tapering stone on a stepped base          Centre
  StonePillar        ~1.7 x 5.5 x 1.7 m  drums on a plinth, with a capital       Flat
  StonePillarBroken  ~3.5 x 2.6 x 3.5 m  a toppled pillar's stump and rubble     Flat / High
  StoneWall          ~4 x 2.4 x 0.8 m  a full wall of staggered courses         Wall Edge
  StoneWallLow       ~4 x 1.2 x 0.8 m  chest-high cover                         Wall Edge
  StoneWallRuined    ~4 x 2.4 x 0.8 m  a wall breaking off towards one end      Wall Edge
  StoneDoorway       ~4.2 x 4.2 x 1.1 m two jambs and a lintel, walk-through   Flat

Usage:
  python piecegen.py --out <folder>             every piece
  python piecegen.py --out <folder> --preview   also a contact sheet (needs matplotlib)
"""

import argparse
import math
import os
import sys

import numpy as np

sys.dont_write_bytecode = True   # no __pycache__ in the repo when TreeGen is imported
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "TreeGen"))
from treegen import Mesh, blob, shade, to_glb  # noqa: E402

SINK = 0.2   # metres every piece reaches below its origin

WOOD = [(0.42, 0.29, 0.17), (0.47, 0.33, 0.19), (0.38, 0.26, 0.15)]
WOOD_DARK = [(0.27, 0.18, 0.11), (0.30, 0.20, 0.12)]
ROOF = [(0.30, 0.22, 0.16), (0.34, 0.25, 0.18), (0.27, 0.20, 0.15)]
ROCK = [(0.45, 0.43, 0.40), (0.50, 0.48, 0.44), (0.40, 0.38, 0.36), (0.55, 0.52, 0.47)]
DIRT = [(0.40, 0.31, 0.21), (0.45, 0.35, 0.23), (0.36, 0.28, 0.19)]
DARK = [(0.05, 0.04, 0.04)]


# ------------------------------------------------------------------------------------------------
# Shapes. `poly` faces each polygon away from a reference point inside the solid, so every shape
# comes out counter-clockwise seen from outside without hand-ordering corners.

def poly(mesh, rng, pts, palette, inside, colour=None):
    pts = [np.asarray(p, dtype=np.float64) for p in pts]
    col = colour or shade(rng, palette[rng.integers(len(palette))])
    n = np.cross(pts[1] - pts[0], pts[2] - pts[0])
    if np.dot(n, np.mean(pts, axis=0) - np.asarray(inside)) < 0:
        pts = pts[::-1]
    for i in range(1, len(pts) - 1):
        mesh.add(pts[0], pts[i], pts[i + 1], col)


def oriented_box(mesh, rng, centre, axes, half, palette, skip_bottom=True):
    """A box at `centre` with unit `axes` (3) and half sizes `half` (3); each face its own shade."""
    c = np.asarray(centre, dtype=np.float64)
    ax = [np.asarray(a, dtype=np.float64) * h for a, h in zip(axes, half)]
    corner = lambda sx, sy, sz: c + sx * ax[0] + sy * ax[1] + sz * ax[2]
    faces = [
        [corner(1, -1, -1), corner(1, 1, -1), corner(1, 1, 1), corner(1, -1, 1)],
        [corner(-1, -1, -1), corner(-1, 1, -1), corner(-1, 1, 1), corner(-1, -1, 1)],
        [corner(-1, 1, -1), corner(1, 1, -1), corner(1, 1, 1), corner(-1, 1, 1)],
        [corner(-1, -1, 1), corner(1, -1, 1), corner(1, 1, 1), corner(-1, 1, 1)],
        [corner(-1, -1, -1), corner(1, -1, -1), corner(1, 1, -1), corner(-1, 1, -1)],
    ]
    if not skip_bottom:
        faces.append([corner(-1, -1, -1), corner(1, -1, -1), corner(1, -1, 1), corner(-1, -1, 1)])
    for f in faces:
        poly(mesh, rng, f, palette, c)


def box(mesh, rng, lo, hi, palette, skip_bottom=True):
    lo, hi = np.asarray(lo, dtype=np.float64), np.asarray(hi, dtype=np.float64)
    oriented_box(mesh, rng, (lo + hi) / 2, [(1, 0, 0), (0, 1, 0), (0, 0, 1)], (hi - lo) / 2, palette, skip_bottom)


def beam(mesh, rng, a, b, thickness, palette):
    """A square timber from point a to point b."""
    a, b = np.asarray(a, dtype=np.float64), np.asarray(b, dtype=np.float64)
    d = b - a
    length = np.linalg.norm(d)
    d /= length
    side = np.cross(d, [0, 1, 0]) if abs(d[1]) < 0.95 else np.cross(d, [1, 0, 0])
    side /= np.linalg.norm(side)
    up = np.cross(side, d)
    oriented_box(mesh, rng, (a + b) / 2, [d, side, up], (length / 2, thickness / 2, thickness / 2), palette, skip_bottom=False)


def log(mesh, rng, a, b, radius, sides, palette):
    """A round log (an n-sided prism) from a to b, capped at both ends."""
    a, b = np.asarray(a, dtype=np.float64), np.asarray(b, dtype=np.float64)
    d = (b - a) / np.linalg.norm(b - a)
    side = np.cross(d, [0, 1, 0]) if abs(d[1]) < 0.95 else np.cross(d, [1, 0, 0])
    side /= np.linalg.norm(side)
    up = np.cross(side, d)
    ring = lambda p: [p + (side * math.cos(2 * math.pi * s / sides) + up * math.sin(2 * math.pi * s / sides)) * radius
                      for s in range(sides)]
    ra, rb, mid = ring(a), ring(b), (a + b) / 2
    for s in range(sides):
        t = (s + 1) % sides
        poly(mesh, rng, [ra[s], ra[t], rb[t], rb[s]], palette, mid)
    end = shade(rng, WOOD_DARK[0])
    poly(mesh, rng, ra, palette, mid, end)
    poly(mesh, rng, rb, palette, mid, end)


def mound(mesh, rng, centre, size, palette, subdivisions=1):
    """A boulder or dirt heap: a lumpy blob cut flat underneath, sunk into the ground."""
    blob(mesh, rng, centre, size, subdivisions, palette)


# ------------------------------------------------------------------------------------------------
# Pieces

def watchtower(rng):
    m = Mesh()
    h, s = 6.0, 1.2                                  # platform height, half the leg spread
    legs = [(-s, -s), (s, -s), (s, s), (-s, s)]
    for x, z in legs:
        beam(m, rng, (x * 1.1, -SINK, z * 1.1), (x, h, z), 0.3, WOOD)
    for i in range(4):                               # diagonal braces on every side, two tiers
        (x0, z0), (x1, z1) = legs[i], legs[(i + 1) % 4]
        beam(m, rng, (x0, 0.4, z0), (x1, 3.0, z1), 0.16, WOOD_DARK)
        beam(m, rng, (x1, 3.0, z1), (x0, 5.6, z0), 0.16, WOOD_DARK)
    box(m, rng, (-1.6, h, -1.6), (1.6, h + 0.3, 1.6), WOOD, skip_bottom=False)
    for i in range(4):                               # railing
        (x0, z0), (x1, z1) = legs[i], legs[(i + 1) % 4]
        beam(m, rng, (x0 * 1.3, h + 1.0, z0 * 1.3), (x1 * 1.3, h + 1.0, z1 * 1.3), 0.12, WOOD_DARK)
        beam(m, rng, (x0 * 1.3, h + 0.3, z0 * 1.3), (x0 * 1.3, h + 2.3, z0 * 1.3), 0.18, WOOD)
    apex = (0.0, h + 3.6, 0.0)                       # pointed roof on the corner posts
    eave = [(-2.1, h + 2.3, -2.1), (2.1, h + 2.3, -2.1), (2.1, h + 2.3, 2.1), (-2.1, h + 2.3, 2.1)]
    for i in range(4):
        poly(m, rng, [eave[i], eave[(i + 1) % 4], apex], ROOF, (0, h + 2.6, 0))
        poly(m, rng, [eave[i], eave[(i + 1) % 4], apex], ROOF, (0, h + 9.0, 0))   # underside
    return m


def barricade(rng):
    m = Mesh()
    for i, y in enumerate((0.25, 0.65, 1.02)):       # three stacked logs, slightly uneven
        dz = rng.uniform(-0.05, 0.05)
        log(m, rng, (-2.0 + rng.uniform(0, 0.15), y, dz), (2.0 - rng.uniform(0, 0.15), y + rng.uniform(-0.04, 0.04), dz),
            0.21, 6, WOOD)
    for x in (-1.6, 0.0, 1.6):                       # posts both sides
        for z in (-0.32, 0.32):
            beam(m, rng, (x, -SINK, z), (x + rng.uniform(-0.05, 0.05), 1.35, z), 0.16, WOOD_DARK)
    return m


def rock_pile(rng):
    m = Mesh()
    boulders = [((-0.9, 0.45, 0.0), (1.1, 0.85, 1.0)), ((0.6, 0.4, -0.3), (1.0, 0.75, 0.9)),
                ((0.2, 0.35, 0.7), (0.8, 0.6, 0.7)), ((1.3, 0.25, 0.5), (0.6, 0.45, 0.55)),
                ((-0.2, 0.95, -0.1), (0.7, 0.55, 0.6))]
    for centre, size in boulders:
        mound(m, rng, centre, size, ROCK, subdivisions=1)
    return m


def crates(rng):
    m = Mesh()
    stack = [((-0.6, 0.0, 0.0), 1.1), ((0.6, 0.0, 0.15), 1.0), ((-0.1, 1.1, 0.05), 1.0)]
    for (x, y, z), size in stack:
        half = size / 2
        turn = rng.uniform(-0.25, 0.25)
        axes = [(math.cos(turn), 0, math.sin(turn)), (0, 1, 0), (-math.sin(turn), 0, math.cos(turn))]
        base = y - (SINK if y == 0 else 0)
        oriented_box(m, rng, (x, base + (size + (SINK if y == 0 else 0)) / 2, z), axes,
                     (half, (size + (SINK if y == 0 else 0)) / 2, half), WOOD, skip_bottom=y > 0)
        for sx in (-1, 1):                           # dark corner battens on the front and back
            for sz in (-1, 1):
                p = np.array([x, 0, z]) + sx * half * 0.92 * np.array(axes[0]) + sz * half * 1.02 * np.array(axes[2])
                beam(m, rng, (p[0], y + 0.02, p[2]), (p[0], y + size - 0.02, p[2]), 0.1, WOOD_DARK)
    return m


def hut(rng):
    m = Mesh()
    w, d, wall = 2.0, 2.0, 2.6                       # half width, half depth, wall height
    box(m, rng, (-w, -SINK, -d), (w, wall, d), WOOD)
    for sx in (-1, 1):                               # corner posts
        for sz in (-1, 1):
            beam(m, rng, (sx * w, -SINK, sz * d), (sx * w, wall + 0.1, sz * d), 0.3, WOOD_DARK)
    box(m, rng, (-0.55, 0.0, d - 0.02), (0.55, 1.9, d + 0.08), DARK)               # door
    for x in (-1.3, 1.3):                            # windows
        box(m, rng, (x - 0.35, 1.2, d - 0.02), (x + 0.35, 1.8, d + 0.06), DARK)
    ridge, eave_y, over = wall + 1.5, wall - 0.15, 0.35
    a, b = (-w - over, eave_y, -d - over), (w + over, eave_y, -d - over)
    c, e = (w + over, eave_y, d + over), (-w - over, eave_y, d + over)
    r0, r1 = (-w - over, ridge, 0.0), (w + over, ridge, 0.0)
    inside = (0, wall + 0.4, 0)
    poly(m, rng, [a, b, r1, r0], ROOF, inside)       # the two roof slopes, top and underside
    poly(m, rng, [e, c, r1, r0], ROOF, inside)
    poly(m, rng, [a, b, r1, r0], ROOF, (0, ridge + 5, 0))
    poly(m, rng, [e, c, r1, r0], ROOF, (0, ridge + 5, 0))
    for x in (-w, w):                                # gable ends
        poly(m, rng, [(x, wall, -d), (x, wall, d), (x, ridge - 0.1, 0.0)], WOOD, (0, wall + 0.4, 0))
    return m


def mineshaft(rng):
    m = Mesh()
    mound(m, rng, (0.0, 0.6, -1.1), (2.6, 2.0, 1.6), DIRT, subdivisions=2)
    mound(m, rng, (-1.6, 0.3, 0.3), (1.0, 0.8, 0.9), ROCK)
    mound(m, rng, (1.7, 0.25, 0.2), (0.9, 0.7, 0.8), ROCK)
    z = 0.9                                          # the timber frame at the front
    for x in (-1.0, 1.0):
        beam(m, rng, (x, -SINK, z), (x, 2.4, z), 0.3, WOOD)
    beam(m, rng, (-1.35, 2.4, z), (1.35, 2.4, z), 0.34, WOOD)
    beam(m, rng, (-1.0, 0.3, z - 0.05), (1.0, 2.2, z - 0.05), 0.12, WOOD_DARK)     # a cross brace
    box(m, rng, (-0.85, 0.0, z - 0.9), (0.85, 2.25, z - 0.15), DARK)               # the dark opening
    return m


# ---- Stone set: ruined stone structures built from rough-cut blocks.

STONE = [(0.52, 0.50, 0.47), (0.57, 0.55, 0.51), (0.47, 0.45, 0.43), (0.61, 0.58, 0.53)]
MOSS = [(0.33, 0.40, 0.24), (0.38, 0.45, 0.27)]


def stone_block(mesh, rng, centre, half, turn=0.05, palette=STONE):
    """One rough-cut block: a box nudged in size and turned a little about Y."""
    a = rng.uniform(-turn, turn)
    axes = [(math.cos(a), 0, math.sin(a)), (0, 1, 0), (-math.sin(a), 0, math.cos(a))]
    h = [x * rng.uniform(0.94, 1.0) for x in half]
    oriented_box(mesh, rng, centre, axes, h, palette)


def course(mesh, rng, x0, x1, y, height, depth, z=0.0, block=1.0, offset=0.0, top_moss=False):
    """A row of blocks from x0 to x1 at height y (its bottom), `depth` thick, staggered by `offset`."""
    x = x0 - offset
    while x < x1 - 0.05:
        w = block * rng.uniform(0.8, 1.2)
        a, b = max(x, x0), min(x + w, x1)
        if b - a > 0.2:
            stone_block(mesh, rng, ((a + b) / 2, y + height / 2, z), ((b - a) / 2 - 0.02, height / 2, depth / 2),
                        palette=MOSS if top_moss and rng.random() < 0.25 else STONE)
        x += w


def stone_wall(mesh, rng, length, height, depth, ruin=0.0):
    """A wall of staggered courses; with ruin > 0 the top courses break off towards one end."""
    y, row, h = -SINK, 0, 0.55
    while y < height - 0.05:
        ch = min(h, height - y)
        cut = length / 2 - (ruin * length * (max(0.0, y) / height) ** 1.5 if ruin else 0.0)
        course(mesh, rng, -length / 2, cut, y, ch, depth, block=1.1, offset=0.55 * (row % 2), top_moss=y + ch >= height - 0.1)
        y += ch
        row += 1


def stone_pillar(rng, height=5.0, broken=False):
    m = Mesh()
    stone_block(m, rng, (0, 0.2 - SINK / 2, 0), (0.85, 0.2 + SINK / 2, 0.85), turn=0.02)       # plinth
    y, top = 0.4, height - (0.0 if broken else 0.5)
    while y < top - 0.05:                                                                      # drums
        h = min(rng.uniform(0.7, 1.0), top - y)
        stone_block(m, rng, (rng.uniform(-0.04, 0.04), y + h / 2, rng.uniform(-0.04, 0.04)), (0.55, h / 2, 0.55), turn=0.12)
        y += h
    if broken:
        for _ in range(4):                                                                     # rubble at the foot
            a, r = rng.uniform(0, 2 * math.pi), rng.uniform(1.0, 1.7)
            s = rng.uniform(0.2, 0.4)
            stone_block(m, rng, (math.cos(a) * r, s - SINK / 2, math.sin(a) * r), (s * 1.3, s, s), turn=0.6)
    else:
        stone_block(m, rng, (0, top + 0.25, 0), (0.8, 0.25, 0.8), turn=0.02)                   # capital
    return m


def pillar(rng):
    return stone_pillar(rng, 5.0)


def broken_pillar(rng):
    return stone_pillar(rng, 2.6, broken=True)


def wall_high(rng):
    m = Mesh()
    stone_wall(m, rng, 4.0, 2.4, 0.8)
    return m


def wall_low(rng):
    m = Mesh()
    stone_wall(m, rng, 4.0, 1.2, 0.8)
    return m


def wall_ruined(rng):
    m = Mesh()
    stone_wall(m, rng, 4.0, 2.4, 0.8, ruin=0.6)
    for _ in range(3):
        s = rng.uniform(0.2, 0.35)
        stone_block(m, rng, (rng.uniform(1.0, 2.2), s - SINK / 2, rng.uniform(-1.0, 1.0)), (s * 1.4, s, s), turn=0.6)
    return m


def doorway(rng):
    """Two jambs and a lintel: a stone door frame you can walk through (opening 2.2 m wide, 3 m tall)."""
    m = Mesh()
    for side in (-1, 1):
        x = side * 1.6
        y, row = -SINK, 0
        while y < 3.0 - 0.05:
            h = min(0.6, 3.0 - y)
            stone_block(m, rng, (x, y + h / 2, 0), (0.5, h / 2, 0.5), turn=0.04)
            y += h
            row += 1
    stone_block(m, rng, (0, 3.35, 0), (2.25, 0.35, 0.55), turn=0.0)                       # lintel
    for x in (-1.3, 0.0, 1.3):                                                             # broken cap stones
        if rng.random() < 0.7:
            stone_block(m, rng, (x, 3.95, 0), (0.55, 0.25, 0.45), turn=0.1)
    return m


def obelisk(rng):
    """The centre landmark: a tall tapering stone on a stepped base."""
    m = Mesh()
    for i, (half, h) in enumerate(((1.8, 0.4), (1.35, 0.4), (0.95, 0.4))):                 # three steps
        y = i * 0.4
        stone_block(m, rng, (0, y + h / 2 - (SINK / 2 if i == 0 else 0), 0), (half, h / 2 + (SINK / 2 if i == 0 else 0), half), turn=0.0)
    y, w = 1.2, 0.6
    while y < 7.0:                                                                         # tapering shaft
        h = 1.0
        stone_block(m, rng, (0, y + h / 2, 0), (w, h / 2, w), turn=0.03)
        y += h
        w *= 0.9
    apex = (0.0, y + 0.9, 0.0)
    base = [(-w, y, -w), (w, y, -w), (w, y, w), (-w, y, w)]
    for i in range(4):
        poly(m, rng, [base[i], base[(i + 1) % 4], apex], STONE, (0, y + 0.3, 0))
    return m


PIECES = [("Watchtower", watchtower, 11), ("Barricade", barricade, 12), ("RockPile", rock_pile, 13),
          ("Crates", crates, 14), ("Hut", hut, 15), ("Mineshaft", mineshaft, 16),
          ("StonePillar", pillar, 21), ("StonePillarBroken", broken_pillar, 22), ("StoneWall", wall_high, 23),
          ("StoneWallLow", wall_low, 24), ("StoneWallRuined", wall_ruined, 25), ("StoneDoorway", doorway, 26),
          ("StoneObelisk", obelisk, 27)]


def preview(items, path, azim=-60):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from mpl_toolkits.mplot3d.art3d import Poly3DCollection
    fig = plt.figure(figsize=(min(len(items), 7) * 2.8, 3.4 * ((len(items) + 6) // 7)), facecolor=(0.82, 0.86, 0.9))
    light = np.array([0.4, 0.8, 0.45]) / np.linalg.norm([0.4, 0.8, 0.45])
    for i, (label, mesh) in enumerate(items):
        ax = fig.add_subplot((len(items) + 6) // 7, min(len(items), 7), i + 1, projection="3d")
        t = np.array(mesh.tris)
        n = np.cross(t[:, 1] - t[:, 0], t[:, 2] - t[:, 0])
        n /= np.linalg.norm(n, axis=1)[:, None] + 1e-9
        lit = 0.45 + 0.55 * np.clip(n @ light, 0, 1)
        colours = np.clip(np.array(mesh.colours) * lit[:, None] * 1.25, 0, 1)
        ax.add_collection3d(Poly3DCollection(t[:, :, [0, 2, 1]], facecolors=colours, edgecolors="none"))
        ax.set_xlim(-3, 3); ax.set_ylim(-3, 3); ax.set_zlim(0, 10)
        ax.set_box_aspect((6, 6, 10))
        ax.view_init(elev=14, azim=azim)
        ax.set_axis_off()
        ax.set_title(f"{label}\n{len(mesh)} tris", fontsize=8)
    fig.tight_layout()
    fig.savefig(path, dpi=80, facecolor=fig.get_facecolor())


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", required=True, help="folder for the .glb files")
    parser.add_argument("--preview", action="store_true", help="write preview.png (needs matplotlib)")
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)
    shown = []
    for label, build, seed in PIECES:
        mesh = build(np.random.default_rng(seed))
        name = f"Piece_{label}"
        with open(os.path.join(args.out, name + ".glb"), "wb") as f:
            f.write(to_glb(mesh, name))
        t = np.array(mesh.tris)
        lo, hi = t.reshape(-1, 3).min(axis=0), t.reshape(-1, 3).max(axis=0)
        print(f"{name}: {len(mesh)} triangles, bounds {np.round(lo, 2).tolist()} .. {np.round(hi, 2).tolist()}")
        shown.append((label, mesh))
    if args.preview:
        preview(shown, os.path.join(args.out, "preview.png"))
        preview(shown, os.path.join(args.out, "preview_front.png"), azim=60)


if __name__ == "__main__":
    main()
