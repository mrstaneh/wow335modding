# Helpers for building props in Blender from code (through the Blender MCP or a script): procedural textures painted
# with numpy, and lathed or prism meshes with their UVs laid out as they are built. Load it with
#   kit = runpy.run_path(r'<repo>\Tools\ModelGen\kit.py')
# Texture coordinates: u across, v up (Blender's), both 0..1; arrays are [row from the bottom, column, rgb].
import math

import bmesh
import bpy
import numpy as np


def new_scene():
    bpy.ops.wm.read_homefile(use_empty=True)
    return bpy.context.scene


def _shape(size):
    return (size, size) if isinstance(size, int) else tuple(size)


def noise(size, cells, seed, octaves=4):
    """Value noise in 0..1, wrapping in u, on a size (or (rows, columns)) grid; cells across u, square on a
    texture whose pixels are square in the world."""
    rng = np.random.default_rng(seed)
    h, w = _shape(size)
    v, u = np.mgrid[0:h, 0:w] / np.array([h, w])[:, None, None]
    out, amp, total = np.zeros((h, w)), 1.0, 0.0
    for o in range(octaves):
        n = cells * 2 ** o
        nv = max(1, round(n * h / w))
        grid = rng.random((nv + 1, n))
        gv, gu = v * nv, u * n
        v0, u0 = gv.astype(int), gu.astype(int)
        fv, fu = gv - v0, gu - u0
        fv, fu = fv * fv * (3 - 2 * fv), fu * fu * (3 - 2 * fu)
        v1, u1 = np.minimum(v0 + 1, nv), (u0 + 1) % n
        a = grid[v0, u0] * (1 - fu) + grid[v0, u1] * fu
        b = grid[v1, u0] * (1 - fu) + grid[v1, u1] * fu
        out += (a * (1 - fv) + b * fv) * amp
        total += amp
        amp *= 0.5
    return out / total


def stone(size, rgb, seed, grain=0.18, veins=0.0):
    """Painted stone: a base colour with blotches and grain, darker veins if asked."""
    n = noise(size, 4, seed)
    fine = noise(size, 32, seed + 1, 2)
    img = np.array(rgb)[None, None, :] * (1 - grain + grain * 2 * n[..., None]) * (0.92 + 0.16 * fine[..., None])
    if veins:
        h, w = _shape(size)
        v, u = np.mgrid[0:h, 0:w] / np.array([h, w])[:, None, None]
        line = np.abs(np.sin((u * 7 + noise(size, 3, seed + 2) * 3 + v * 2) * math.pi))
        img *= 1 - veins * (1 - np.clip(line * 5, 0, 1))[..., None]
    return img


def region(img, u0, u1, v0, v1):
    """A boolean mask of a rectangle in UV space."""
    h, w = img.shape[:2]
    v, u = np.mgrid[0:h, 0:w] / np.array([h, w])[:, None, None]
    return (u >= u0) & (u < u1) & (v >= v0) & (v < v1)


def band(img, v0, v1, rgb, u0=0.0, u1=1.0, bevel=True, seed=0):
    """A horizontal painted (or gilded) band, rounded with light in the middle when bevelled."""
    h, w = img.shape[:2]
    v = (np.arange(h) / h)[:, None] * np.ones((1, w))
    m = region(img, u0, u1, v0, v1)
    shade = 0.72 + 0.4 * np.sin(np.clip((v - v0) / max(v1 - v0, 1e-6), 0, 1) * math.pi) if bevel else np.ones_like(v)
    grain = 0.9 + 0.2 * noise((h, w), 24, seed + 7, 2)
    img[m] = (np.array(rgb)[None, None, :] * (shade * grain)[..., None])[m]


GLYPHS = ['ankh', 'eye', 'sun', 'reed', 'water', 'bird', 'house', 'snake']


def glyph_mask(shape, cu, cv, s, kind):
    """One glyph centred at (cu, cv), s across in u (its height in v follows the texture's aspect, so it is square on
    a texture with square pixels), as a boolean mask."""
    h, w = shape[:2]
    v, u = np.mgrid[0:h, 0:w] / np.array([h, w])[:, None, None]
    x, y = (u - cu) / s, (v - cv) * h / w / s  # -0.5..0.5 inside the cell
    t = 0.09  # stroke width
    if kind == 'ankh':
        loop = np.abs(np.hypot(x / 0.6, (y - 0.25) / 0.8) - 0.18) < t * 0.6
        return loop | ((np.abs(x) < t / 2) & (y < 0.08) & (y > -0.45)) | ((np.abs(y - 0.02) < t / 2) & (np.abs(x) < 0.3))
    if kind == 'eye':
        lid = np.abs(np.hypot(x / 1.6, y / 0.7) - 0.2) < t * 0.5
        return lid | (np.hypot(x, y) < 0.08) | ((np.abs(x + 0.05 - (-y - 0.1) * 0.5) < t / 2) & (y < -0.1) & (y > -0.4))
    if kind == 'sun':
        return (np.hypot(x, y) < 0.3) & ~(np.hypot(x, y) < 0.3 - t) | (np.hypot(x, y) < 0.07)
    if kind == 'reed':
        return ((np.abs(x) < t / 2) & (np.abs(y) < 0.42)) | ((np.abs(x - 0.08 - y * 0.2) < t / 2) & (y > 0.15) & (y < 0.42))
    if kind == 'water':
        return (np.abs(y - 0.06 * np.sin(x * 40)) < t / 2) & (np.abs(x) < 0.42)
    if kind == 'bird':
        body = np.hypot((x + 0.02) / 1.5, y + 0.05) < 0.17
        head = np.hypot(x - 0.2, y - 0.15) < 0.08
        legs = (np.abs(np.abs(x) - 0.05) < t / 3) & (y < -0.2) & (y > -0.42)
        return body | head | legs
    if kind == 'house':
        outer = (np.abs(x) < 0.38) & (np.abs(y) < 0.3)
        return outer & ~((np.abs(x) < 0.38 - t) & (np.abs(y) < 0.3 - t) & (y > -0.3 + t)) | (outer & (y < -0.3 + t))
    # snake
    return (np.abs(y - 0.12 * np.sin(x * 14)) < t / 2) & (np.abs(x) < 0.4) | (np.hypot(x - 0.4, y) < 0.07)


def glyph_panel(img, u0, u1, v0, v1, cols, rows, rgb, seed, frame=None):
    """Columns of carved glyphs in a rectangle, cut a little darker into the stone; an optional frame colour."""
    rng = np.random.default_rng(seed)
    m = region(img, u0, u1, v0, v1)
    img[m] *= 0.93
    h, w = img.shape[:2]
    cw, ch = (u1 - u0) / cols, (v1 - v0) / rows
    s = min(cw, ch * h / w) * 0.85
    for r in range(rows):
        for c in range(cols):
            g = glyph_mask(img.shape, u0 + (c + 0.5) * cw, v0 + (r + 0.5) * ch, s, GLYPHS[rng.integers(len(GLYPHS))])
            img[g] = np.array(rgb)
    if frame is not None:
        du, dv = 2 / w, 2 / h
        edge = m & ~region(img, u0 + du, u1 - du, v0 + dv, v1 - dv)
        img[edge] = np.array(frame)


def shade_vertical(img, v0, v1, dark=0.6):
    """Darken towards v0 (grime and contact shadow at a model's foot)."""
    h = img.shape[0]
    v = np.arange(h) / h
    f = np.clip((v - v0) / max(v1 - v0, 1e-6), 0, 1)
    img *= (dark + (1 - dark) * f)[:, None, None]


def image(name, img):
    """A packed Blender image from an rgb array (rows bottom-up)."""
    h, w = img.shape[:2]
    rgba = np.concatenate([np.clip(img, 0, 1), np.ones((h, w, 1))], axis=2).astype(np.float32)
    im = bpy.data.images.new(name, w, h, alpha=True)
    im.pixels.foreach_set(rgba.ravel())
    im.pack()
    return im


def material(name, im, two_sided=False):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED')
    tex = nt.nodes.new('ShaderNodeTexImage')
    tex.image = im
    nt.links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
    bsdf.inputs['Roughness'].default_value = 0.8
    mat.use_backface_culling = not two_sided
    return mat


def _object(name, bm, mat, smooth):
    for f in bm.faces:
        f.smooth = smooth
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    me.materials.append(mat)
    return ob


def lathe(name, profile, mat, seg=24, u=(0.0, 1.0), lobes=0, smooth=True, cap_bottom=False, cap_top=False):
    """Turn a profile [(radius, z, v) or (radius, z, v, lobe amount)] around Z. u runs around (u[0] at the back, the
    middle facing front, -Y); lobes ripple the radius (a bundle of reeds, a fluted shaft)."""
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.new()
    rings = []
    for p in profile:
        r, z, amp = p[0], p[1], (p[3] if len(p) > 3 else 0)
        ring = []
        for i in range(seg + 1):
            a = i / seg * 2 * math.pi
            rr = r * (1 + amp * math.cos(lobes * a)) if lobes else r
            ring.append(bm.verts.new((-math.sin(a) * rr, math.cos(a) * rr, z)))
        rings.append(ring)
    for j in range(len(profile) - 1):
        for i in range(seg):
            quad = (rings[j][i], rings[j][i + 1], rings[j + 1][i + 1], rings[j + 1][i])
            if len(set(quad)) < 3:
                continue
            f = bm.faces.new(quad)
            for loop, (ii, jj) in zip(f.loops, ((i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1))):
                loop[uvl].uv = (u[0] + (u[1] - u[0]) * ii / seg, profile[jj][2])
    for cap, ring, p in ((cap_bottom, rings[0], profile[0]), (cap_top, rings[-1], profile[-1])):
        if cap:
            f = bm.faces.new(ring[:-1])
            for loop in f.loops:
                loop[uvl].uv = (u[0] + 0.01, p[2])
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    return _object(name, bm, mat, smooth)


def prism(name, rings, mat, sides=4, u=(0.0, 1.0), cap_top=False, cap_bottom=False, twist=math.pi / 4):
    """A tapered square (or n-sided) shaft through rings [(half width, z, v)], flat-shaded; every side shows the whole
    u range, so a carved panel appears on each face."""
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.new()
    k = 1 / math.cos(math.pi / sides)  # half width to corner radius
    corners = []
    for hw, z, v in rings:
        corners.append([(math.cos(twist + 2 * math.pi * s / sides) * hw * k, math.sin(twist + 2 * math.pi * s / sides) * hw * k, z)
                        for s in range(sides)])
    for j in range(len(rings) - 1):
        for s in range(sides):
            s1 = (s + 1) % sides
            vs = [bm.verts.new(c) for c in (corners[j][s], corners[j][s1], corners[j + 1][s1], corners[j + 1][s])]
            f = bm.faces.new(vs)
            for loop, (uu, vv) in zip(f.loops, ((u[0], rings[j][2]), (u[1], rings[j][2]), (u[1], rings[j + 1][2]), (u[0], rings[j + 1][2]))):
                loop[uvl].uv = (uu, vv)
    for cap, ring, r in ((cap_bottom, corners[0], rings[0]), (cap_top, corners[-1], rings[-1])):
        if cap:
            f = bm.faces.new([bm.verts.new(c) for c in ring])
            for loop in f.loops:
                loop[uvl].uv = (u[0] + 0.01, r[2])
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    return _object(name, bm, mat, False)


def save(path):
    bpy.ops.wm.save_as_mainfile(filepath=path, compress=True)
