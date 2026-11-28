"""
TreeGen: low-poly placeholder trees as .glb, for the Crucible engine.

AI-generated placeholder tool: written with Claude (Anthropic, Claude Opus 5.5), November 2026.
Meant to be replaced by the team's own asset generator; nothing in the engine depends on it.

Every tree is one mesh, flat-shaded (each triangle has its own three vertices and one normal) and
vertex-coloured (no textures), in metres with +Y up and the trunk's base at the origin. The trunk
reaches SINK metres below the origin, so a tree placed on a slope never floats over it. The same
kind and seed always give the same tree.

Kinds:
  broadleaf  a bent trunk under 3-5 lumpy leaf blobs
  pine       a trunk under 3-4 stacked, ragged cones
  dead       a crooked trunk with bare branches, no leaves (grey-brown)
  burnt      a dead tree, charred (for war-torn ground)
Each kind also has a "far" version (--far): fewer sides and blobs, for the map's border.

Usage:
  python treegen.py --out <folder>                 the default set (see SET below)
  python treegen.py --out <folder> --kind pine --seed 7
  python treegen.py --out <folder> --preview       also a contact sheet (needs matplotlib)

Needs numpy only (matplotlib for --preview).
"""

import argparse
import json
import math
import os
import struct

import numpy as np

# ------------------------------------------------------------------------------------------------
# Palettes (linear 0-1 RGB). Each face picks a base colour and is nudged a little, so the facets
# read without a texture. Leaves facing down are darkened, a cheap stand-in for self-shadow.

PALETTES = {
    "broadleaf": {"bark": [(0.33, 0.22, 0.14), (0.38, 0.26, 0.16)],
                  "leaf": [(0.25, 0.47, 0.18), (0.31, 0.54, 0.20), (0.36, 0.58, 0.22), (0.22, 0.42, 0.17)]},
    "pine":      {"bark": [(0.30, 0.20, 0.13), (0.34, 0.23, 0.15)],
                  "leaf": [(0.13, 0.33, 0.20), (0.16, 0.38, 0.22), (0.19, 0.42, 0.24)]},
    "dead":      {"bark": [(0.36, 0.32, 0.28), (0.42, 0.37, 0.31), (0.31, 0.27, 0.23)], "leaf": []},
    "burnt":     {"bark": [(0.09, 0.08, 0.08), (0.13, 0.11, 0.10), (0.18, 0.14, 0.11)], "leaf": []},
}
EMBER = (0.55, 0.20, 0.08)   # a few faces of a burnt trunk glow faintly

SINK = 0.3   # metres the trunk goes on below the origin

# The default set: (kind, seeds). Sizes are for mechs about 4-5 m tall.
SET = [("broadleaf", range(1, 7)), ("pine", range(1, 5)), ("dead", range(1, 4)), ("burnt", range(1, 4))]


class Mesh:
    """Triangles with one colour each; flat-shaded on export."""

    def __init__(self):
        self.tris = []      # (3, 3) arrays, counter-clockwise seen from outside
        self.colours = []   # one RGB per triangle

    def add(self, a, b, c, colour):
        self.tris.append(np.array([a, b, c], dtype=np.float64))
        self.colours.append(colour)

    def merge(self, other):
        self.tris += other.tris
        self.colours += other.colours

    def __len__(self):
        return len(self.tris)


def shade(rng, base, spread=0.06):
    """A palette colour nudged by up to `spread`, kept in [0, 1]."""
    return tuple(float(np.clip(c * (1.0 + rng.uniform(-spread, spread)) + rng.uniform(-0.015, 0.015), 0.0, 1.0)) for c in base)


# ------------------------------------------------------------------------------------------------
# Shapes

def frustum_chain(mesh, rng, points, radii, sides, palette, twist=0.0, cap_top=True):
    """A tapered tube along `points` (a bent trunk or a branch): `sides`-gon rings, capped on top."""
    rings = []
    offset = rng.uniform(0, 2 * math.pi)
    for i, (p, r) in enumerate(zip(points, radii)):
        p = np.asarray(p, dtype=np.float64)
        # The ring's plane: perpendicular to the local direction of the tube.
        d = np.asarray(points[min(i + 1, len(points) - 1)]) - np.asarray(points[max(i - 1, 0)])
        d = d / (np.linalg.norm(d) + 1e-9)
        side = np.cross(d, [0.0, 0.0, 1.0]) if abs(d[2]) < 0.9 else np.cross(d, [1.0, 0.0, 0.0])
        side /= np.linalg.norm(side)
        up = np.cross(side, d)
        ring = []
        for s in range(sides):
            a = offset + twist * i + 2 * math.pi * s / sides
            wobble = 1.0 + rng.uniform(-0.08, 0.08)
            ring.append(p + (side * math.cos(a) + up * math.sin(a)) * r * wobble)
        rings.append(ring)
    for i in range(len(rings) - 1):
        lo, hi = rings[i], rings[i + 1]
        for s in range(sides):
            t = (s + 1) % sides
            col = shade(rng, palette[rng.integers(len(palette))])
            mesh.add(lo[s], lo[t], hi[t], col)
            mesh.add(lo[s], hi[t], hi[s], col)
    if cap_top:
        top = rings[-1]
        centre = np.mean(top, axis=0)
        col = shade(rng, palette[0])
        for s in range(sides):
            mesh.add(top[s], top[(s + 1) % sides], centre, col)


def icosphere(subdivisions):
    """Unit icosphere: vertices and counter-clockwise faces."""
    t = (1 + 5 ** 0.5) / 2
    v = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t),
         (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
    v = [np.array(p, dtype=np.float64) / np.linalg.norm(p) for p in v]
    f = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6),
         (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10),
         (8, 6, 7), (9, 8, 1)]
    for _ in range(subdivisions):
        cache, nf = {}, []

        def mid(a, b):
            key = (min(a, b), max(a, b))
            if key not in cache:
                m = v[a] + v[b]
                v.append(m / np.linalg.norm(m))
                cache[key] = len(v) - 1
            return cache[key]
        for a, b, c in f:
            ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
            nf += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        f = nf
    return np.array(v), f


def blob(mesh, rng, centre, size, subdivisions, palette):
    """A lumpy leaf blob: an icosphere stretched to `size`, its points pushed in or out by up to 18%."""
    verts, faces = icosphere(subdivisions)
    bumps = 1.0 + rng.uniform(-0.18, 0.18, len(verts))
    pts = np.asarray(centre) + verts * bumps[:, None] * np.asarray(size)
    pts[:, 1] = np.maximum(pts[:, 1], centre[1] - size[1] * 0.55)   # a flatter underside, like a crown
    for a, b, c in faces:
        n = np.cross(pts[b] - pts[a], pts[c] - pts[a])
        down = n[1] / (np.linalg.norm(n) + 1e-9) < -0.3
        col = shade(rng, palette[rng.integers(len(palette))])
        if down:
            col = tuple(x * 0.72 for x in col)
        mesh.add(pts[a], pts[b], pts[c], col)


def ragged_cone(mesh, rng, base_y, height, radius, sides, palette, top_x=0.0, top_z=0.0):
    """A pine tier: a cone whose rim dips at alternate points, so its edge looks ragged."""
    apex = np.array([top_x, base_y + height, top_z])
    rim = []
    turn = rng.uniform(0, 2 * math.pi)
    for s in range(sides):
        a = turn + 2 * math.pi * s / sides
        r = radius * (1.0 + rng.uniform(-0.1, 0.1))
        dip = (0.35 if s % 2 else 0.0) * height * rng.uniform(0.6, 1.0)
        rim.append(np.array([math.cos(a) * r, base_y - dip, math.sin(a) * r]))
    centre = np.array([0.0, base_y + height * 0.12, 0.0])   # the underside rises into the cone
    for s in range(sides):
        t = (s + 1) % sides
        mesh.add(rim[s], apex, rim[t], shade(rng, palette[rng.integers(len(palette))]))
        mesh.add(rim[t], centre, rim[s], tuple(x * 0.65 for x in shade(rng, palette[0])))


def bent_line(rng, start, direction, length, segments, bend, root=0.0):
    """Points along a gently bending line; with `root`, it first goes that far straight down from start."""
    pts = [np.asarray(start, dtype=np.float64)]
    if root > 0.0:
        pts = [pts[0] - np.array([0.0, root, 0.0]), pts[0]]
    d = np.asarray(direction, dtype=np.float64)
    d /= np.linalg.norm(d)
    for _ in range(segments):
        d = d + rng.uniform(-bend, bend, 3) * np.array([1, 0.3, 1])
        d /= np.linalg.norm(d)
        pts.append(pts[-1] + d * length / segments)
    return pts


# ------------------------------------------------------------------------------------------------
# Kinds

def make_tree(kind, seed, far=False):
    rng = np.random.default_rng([sum(map(ord, kind)), seed, 1 if far else 0])   # not hash(): it changes per run
    pal = PALETTES[kind]
    mesh = Mesh()
    sides = 4 if far else 6

    if kind == "broadleaf":
        height = rng.uniform(4.5, 6.5)
        trunk = bent_line(rng, (0, 0, 0), (0, 1, 0), height, 2 if far else 3, 0.18, root=SINK)
        radius = rng.uniform(0.35, 0.5)
        frustum_chain(mesh, rng, trunk, [radius * (1.0 - 0.45 * i / (len(trunk) - 1)) for i in range(len(trunk))], sides, pal["bark"])
        top = trunk[-1]
        count = 2 if far else int(rng.integers(3, 6))
        crown = rng.uniform(2.6, 3.4)
        for i in range(count):
            a = 2 * math.pi * i / count + rng.uniform(-0.4, 0.4)
            r = 0.0 if i == 0 else rng.uniform(0.9, 1.6)
            centre = top + np.array([math.cos(a) * r, rng.uniform(-0.2, 1.4) if i else 0.9, math.sin(a) * r])
            s = crown * (1.0 if i == 0 else rng.uniform(0.6, 0.85))
            blob(mesh, rng, centre, (s, s * rng.uniform(0.75, 0.95), s), 0 if far else 1, pal["leaf"])

    elif kind == "pine":
        height = rng.uniform(9.0, 12.5)
        trunk = bent_line(rng, (0, 0, 0), (0, 1, 0), height * 0.55, 2, 0.05, root=SINK)
        girth = rng.uniform(0.3, 0.42)
        frustum_chain(mesh, rng, trunk, [girth * (1 - 0.4 * i / (len(trunk) - 1)) for i in range(len(trunk))], sides, pal["bark"])
        tiers = 2 if far else int(rng.integers(3, 5))
        base = height * rng.uniform(0.18, 0.26)
        span = height - base
        for i in range(tiers):
            y = base + span * i / tiers * 0.85
            h = span * (0.55 - 0.08 * i)
            r = rng.uniform(2.4, 3.0) * (1.0 - 0.22 * i)
            ragged_cone(mesh, rng, y, h, r, 6 if far else 8, pal["leaf"],
                        rng.uniform(-0.15, 0.15), rng.uniform(-0.15, 0.15))

    else:  # dead / burnt
        height = rng.uniform(6.5, 9.0)
        trunk = bent_line(rng, (0, 0, 0), (0, 1, 0), height, 3 if far else 4, 0.22, root=SINK)
        radius = rng.uniform(0.45, 0.6)
        radii = [radius * (1.0 - 0.6 * i / (len(trunk) - 1)) for i in range(len(trunk))]
        frustum_chain(mesh, rng, trunk, radii, sides, pal["bark"])
        branches = 2 if far else int(rng.integers(3, 6))
        for i in range(branches):
            k = int(rng.integers(2, len(trunk) - 1))            # not the root (0) or the ground (1)
            a = rng.uniform(0, 2 * math.pi)
            d = (math.cos(a), rng.uniform(0.5, 1.2), math.sin(a))
            length = rng.uniform(2.2, 3.6) * (1.0 - 0.12 * k)
            pts = bent_line(rng, trunk[k], d, length, 2, 0.35)
            r = radii[k] * 0.55
            frustum_chain(mesh, rng, pts, [r, r * 0.6, r * 0.25], 4, pal["bark"])
        if kind == "burnt":
            for i in rng.choice(len(mesh), size=max(1, len(mesh) // 14), replace=False):
                mesh.colours[i] = shade(rng, EMBER, 0.15)
    return mesh


# ------------------------------------------------------------------------------------------------
# Export: glTF 2.0 binary, one mesh, one primitive, POSITION / NORMAL / COLOR_0, a plain white
# material (the vertex colours carry the look).

def to_glb(mesh, name):
    tris = np.array(mesh.tris, dtype=np.float32)                  # (n, 3, 3)
    normals = np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0])
    lengths = np.linalg.norm(normals, axis=1)
    keep = lengths > 1e-8                                         # drop slivers with no area
    tris, normals, lengths = tris[keep], normals[keep], lengths[keep]
    normals = (normals / lengths[:, None]).astype(np.float32)
    colours = np.array(mesh.colours, dtype=np.float32)[keep]

    positions = tris.reshape(-1, 3)
    normals = np.repeat(normals, 3, axis=0)
    colours = np.clip(np.repeat(colours, 3, axis=0), 0.0, 1.0)
    indices = np.arange(len(positions), dtype=np.uint32)

    blobs = [positions.tobytes(), normals.tobytes(), colours.tobytes(), indices.tobytes()]
    views, offset = [], 0
    for b in blobs:
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(b)})
        offset += len(b)
    binary = b"".join(blobs)
    count = len(positions)
    gltf = {
        "asset": {"version": "2.0", "generator": "TreeGen (AI-generated placeholder, Claude Opus 5.5)"},
        "scene": 0, "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": name}],
        "meshes": [{"name": name, "primitives": [{
            "attributes": {"POSITION": 0, "NORMAL": 1, "COLOR_0": 2}, "indices": 3, "material": 0}]}],
        "materials": [{"name": name + "_mat", "pbrMetallicRoughness": {
            "baseColorFactor": [1, 1, 1, 1], "metallicFactor": 0.0, "roughnessFactor": 1.0}}],
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": [dict(views[0], target=34962), dict(views[1], target=34962),
                        dict(views[2], target=34962), dict(views[3], target=34963)],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": count, "type": "VEC3",
             "min": positions.min(axis=0).tolist(), "max": positions.max(axis=0).tolist()},
            {"bufferView": 1, "componentType": 5126, "count": count, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": count, "type": "VEC3"},
            {"bufferView": 3, "componentType": 5125, "count": count, "type": "SCALAR"},
        ],
    }
    text = json.dumps(gltf, separators=(",", ":")).encode()
    text += b" " * (-len(text) % 4)
    binary += b"\0" * (-len(binary) % 4)
    total = 12 + 8 + len(text) + 8 + len(binary)
    return (struct.pack("<III", 0x46546C67, 2, total) + struct.pack("<II", len(text), 0x4E4F534A) + text
            + struct.pack("<II", len(binary), 0x004E4942) + binary)


def preview(trees, path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from mpl_toolkits.mplot3d.art3d import Poly3DCollection
    cols = 6
    rows = (len(trees) + cols - 1) // cols
    fig = plt.figure(figsize=(cols * 2.6, rows * 3.2), facecolor=(0.82, 0.86, 0.9))
    light = np.array([0.4, 0.8, 0.45]) / np.linalg.norm([0.4, 0.8, 0.45])
    for i, (label, mesh) in enumerate(trees):
        ax = fig.add_subplot(rows, cols, i + 1, projection="3d")
        tris = np.array(mesh.tris)[:, :, [0, 2, 1]]                 # matplotlib is z-up
        n = np.cross(np.array(mesh.tris)[:, 1] - np.array(mesh.tris)[:, 0], np.array(mesh.tris)[:, 2] - np.array(mesh.tris)[:, 0])
        n /= np.linalg.norm(n, axis=1)[:, None] + 1e-9
        lit = 0.45 + 0.55 * np.clip(n @ light, 0, 1)
        colours = np.clip(np.array(mesh.colours) * lit[:, None] * 1.25, 0, 1)
        ax.add_collection3d(Poly3DCollection(tris, facecolors=colours, edgecolors="none"))
        ax.set_xlim(-4, 4); ax.set_ylim(-4, 4); ax.set_zlim(0, 13)
        ax.set_box_aspect((8, 8, 13))
        ax.view_init(elev=12, azim=35)
        ax.set_axis_off()
        ax.set_title(f"{label}\n{len(mesh)} tris", fontsize=8)
    fig.tight_layout()
    fig.savefig(path, dpi=80, facecolor=fig.get_facecolor())


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", required=True, help="folder for the .glb files")
    parser.add_argument("--kind", choices=sorted(PALETTES), help="one kind only (with --seed)")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--far", action="store_true", help="also write the far (border) versions")
    parser.add_argument("--preview", action="store_true", help="write preview.png (needs matplotlib)")
    args = parser.parse_args()

    os.makedirs(args.out, exist_ok=True)
    jobs = [(args.kind, [args.seed])] if args.kind else SET
    shown = []
    for kind, seeds in jobs:
        for seed in seeds:
            for far in ([False, True] if args.far else [False]):
                mesh = make_tree(kind, seed, far)
                name = f"Tree_{kind.capitalize()}_{seed:02d}{'_Far' if far else ''}"
                with open(os.path.join(args.out, name + ".glb"), "wb") as f:
                    f.write(to_glb(mesh, name))
                shown.append((name, mesh))
                print(f"{name}: {len(mesh)} triangles")
    if args.preview:
        preview(shown, os.path.join(args.out, "preview.png"))


if __name__ == "__main__":
    main()
