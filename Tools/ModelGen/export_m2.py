# Blender -> WoW 3.3.5 M2: writes <name>.m2, <name>00.skin and a BLP per material for the open .blend, from its visible
# mesh objects. Static models: one bone, one Stand sequence, no animation. Layouts are the client's own loader's
# (Engine/Source/Model2/M2Data.h, M2Shared.cpp; Engine/Source/BLPFile/blp.cpp). Workflow: Tools/ModelGen/README.md.
#
#   blender -b Model.blend --python Tools/ModelGen/export_m2.py -- <pack dir> <MPQ dir, e.g. World\DrownedSun>
#
# Blender conventions: Z up, 1 metre = 1 yard, the model's front faces -Y (Blender's front view). Each material needs
# an Image Texture node with a power-of-two image; faces are drawn one-sided unless the material shows backfaces.
import os
import struct
import sys

import bmesh
import bpy
import numpy as np


class Blob:
    """The file body: a fixed header, then data blocks at 16-byte boundaries. Arrays are (count, offset) pairs."""

    def __init__(self, header_size):
        self.data = bytearray(header_size)

    def add(self, raw, count):
        if not count:
            return (0, 0)
        self.data += b'\0' * (-len(self.data) % 16)
        offset = len(self.data)
        self.data += raw
        return (count, offset)


def pack(fmt, items):
    return b''.join(struct.pack(fmt, *i) if isinstance(i, tuple) else struct.pack(fmt, i) for i in items)


def to_wow(v):
    # Blender front (-Y) -> WoW front (+X): a quarter turn about Z
    return (-v[1], v[0], v[2])


def collect():
    """Triangles per material, with vertices split wherever position, normal or UV differ."""
    depsgraph = bpy.context.evaluated_depsgraph_get()
    groups = {}  # material -> (vertex key -> index, vertices, triangles)
    for obj in bpy.context.scene.objects:
        if obj.type != 'MESH' or obj.hide_render or not obj.visible_get():
            continue
        mesh = obj.evaluated_get(depsgraph).to_mesh()
        bm = bmesh.new()
        bm.from_mesh(mesh)
        bmesh.ops.triangulate(bm, faces=bm.faces)
        bm.transform(obj.matrix_world)
        normal_matrix = obj.matrix_world.to_3x3().inverted().transposed()
        uv_layer = bm.loops.layers.uv.active
        for face in bm.faces:
            material = obj.material_slots[face.material_index].material if obj.material_slots else None
            index, vertices, triangles = groups.setdefault(material, ({}, [], []))
            tri = []
            for loop in face.loops:
                n = (normal_matrix @ (loop.vert.normal if face.smooth else face.normal)).normalized()
                uv = loop[uv_layer].uv if uv_layer else (0, 0)
                key = (tuple(round(c, 5) for c in loop.vert.co), tuple(round(c, 3) for c in n), (round(uv[0], 5), round(uv[1], 5)))
                if key not in index:
                    index[key] = len(vertices)
                    vertices.append((to_wow(loop.vert.co), to_wow(n), (uv[0], 1 - uv[1])))
                tri.append(index[key])
            triangles.append(tri)
        bm.free()
        obj.evaluated_get(depsgraph).to_mesh_clear()
    return [(m, v, t) for m, (_, v, t) in groups.items()]


def write_blp(image, path):
    """BLP2, colour encoding 3: raw BGRA mipmaps, which the client copies straight into an ARGB8888 texture."""
    w, h = image.size
    assert w & (w - 1) == 0 and h & (h - 1) == 0, f'{image.name}: {w}x{h} is not a power of two'
    pixels = np.array(image.pixels[:], dtype=np.float32).reshape(h, w, 4)[::-1]  # Blender stores the bottom row first
    mips = []
    while True:
        mips.append((np.clip(pixels, 0, 1) * 255 + 0.5).astype(np.uint8)[:, :, [2, 1, 0, 3]].tobytes())
        if pixels.shape[0] == 1 and pixels.shape[1] == 1:
            break
        ph, pw = max(pixels.shape[0] // 2, 1), max(pixels.shape[1] // 2, 1)
        pixels = pixels[:ph * 2 if pixels.shape[0] > 1 else 1, :pw * 2 if pixels.shape[1] > 1 else 1]
        pixels = pixels.reshape(ph, pixels.shape[0] // ph, pw, pixels.shape[1] // pw, 4).mean(axis=(1, 3))
    mips = mips[:16]
    offsets, sizes, offset = [], [], 0x94 + 1024
    for m in mips:
        offsets.append(offset)
        sizes.append(len(m))
        offset += len(m)
    header = struct.pack('<4sIBBBBII', b'BLP2', 1, 3, 8, 2, 1, w, h)
    header += struct.pack('<16I', *(offsets + [0] * (16 - len(offsets))))
    header += struct.pack('<16I', *(sizes + [0] * (16 - len(sizes))))
    with open(path, 'wb') as f:
        f.write(header + b'\0' * 1024 + b''.join(mips))


def texture_of(material):
    if material and material.node_tree:
        for node in material.node_tree.nodes:
            if node.type == 'TEX_IMAGE' and node.image:
                return node.image
    return None


def export(pack_dir, mpq_dir):
    name = os.path.splitext(os.path.basename(bpy.data.filepath))[0]
    out = os.path.join(pack_dir, *mpq_dir.split('\\'))
    os.makedirs(out, exist_ok=True)
    groups = collect()
    assert groups, 'no visible mesh objects'

    vertices, sections, indices = [], [], []
    for material, verts, tris in groups:
        start = len(vertices)
        vertices += verts
        sections.append((start, len(verts), len(indices), len(tris) * 3))
        indices += [start + i for t in tris for i in t]
    assert len(vertices) < 65536, 'too many vertices for one skin'
    positions = np.array([v[0] for v in vertices])
    lo, hi = positions.min(axis=0), positions.max(axis=0)
    radius = float(np.linalg.norm(positions, axis=1).max())
    bounds = struct.pack('<7f', *lo, *hi, radius)

    blob = Blob(0x130)
    a = {}
    a['name'] = blob.add(name.encode() + b'\0', len(name) + 1)
    # Stand (id 0), its data in this file (flags 0x20), its own alias (aliasNext = its index)
    a['sequences'] = blob.add(struct.pack('<HHIfIIiiI', 0, 0, 3000, 0, 0x20, 0x7FFF, 0, 0, 150) + bounds + struct.pack('<HH', 0xFFFF, 0), 1)
    # No sequence hash table: the client then searches the sequences one by one. (A one-slot table hangs it: looking up
    # an animation the model lacks, its quadratic probing never meets an empty slot and loops forever.)
    static_track = struct.pack('<HHIIII', 0, 0xFFFF, 0, 0, 0, 0)
    a['bones'] = blob.add(struct.pack('<IIHHI', 0xFFFFFFFF, 0, 0xFFFF, 0, 0) + static_track * 3 + struct.pack('<3f', 0, 0, 0), 1)
    a['vertices'] = blob.add(pack('<3f4B4B3f2f2f', [(*p, 255, 0, 0, 0, 0, 0, 0, 0, *n, *uv, 0, 0) for p, n, uv in vertices]), len(vertices))

    textures, materials = [], []
    for i, (material, _, _) in enumerate(groups):
        image = texture_of(material)
        assert image, f'material {material.name if material else "(none)"} has no Image Texture node'
        tex_name = f'{name}_{bpy.path.clean_name(material.name)}'
        write_blp(image, os.path.join(out, tex_name + '.blp'))
        path = f'{mpq_dir}\\{tex_name}.blp'.encode() + b'\0'
        textures.append((0, 3, blob.add(path, len(path))))  # flags 3: wrap in u and v
        two_sided = not material.use_backface_culling
        materials.append((4 if two_sided else 0, 0))  # flags 4: no backface culling; blend 0: opaque
    raw = b''.join(struct.pack('<IIII', t, f, *arr) for t, f, arr in textures)
    a['textures'] = blob.add(raw, len(textures))
    # One texture weight, keyed at full: without a key the client leaves it at 0 and the model is invisible
    times = blob.add(struct.pack('<I', 0), 1)
    keys = blob.add(struct.pack('<h', 0x7FFF), 1)
    times_arr = blob.add(struct.pack('<II', *times), 1)
    keys_arr = blob.add(struct.pack('<II', *keys), 1)
    a['textureWeights'] = blob.add(struct.pack('<HHIIII', 0, 0xFFFF, *times_arr, *keys_arr), 1)
    a['textureIndicesById'] = blob.add(struct.pack('<H', 0xFFFF), 1)
    a['materials'] = blob.add(pack('<HH', materials), len(materials))
    a['boneCombos'] = blob.add(struct.pack('<H', 0), 1)
    a['textureCombos'] = blob.add(pack('<H', list(range(len(textures)))), len(textures))
    a['textureCoordCombos'] = blob.add(struct.pack('<H', 0), 1)
    a['textureWeightCombos'] = blob.add(struct.pack('<H', 0), 1)
    a['textureTransformCombos'] = blob.add(struct.pack('<H', 0xFFFF), 1)  # none: identity
    # Collision: the render mesh itself
    tris = [indices[i:i + 3] for i in range(0, len(indices), 3)]
    a['collisionIndices'] = blob.add(pack('<H', indices), len(indices))
    a['collisionPositions'] = blob.add(pack('<3f', [tuple(p) for p in positions]), len(positions))
    normals = []
    for t in tris:
        n = np.cross(positions[t[1]] - positions[t[0]], positions[t[2]] - positions[t[0]])
        normals.append(tuple(n / (np.linalg.norm(n) or 1)))
    a['collisionFaceNormals'] = blob.add(pack('<3f', normals), len(normals))

    none = (0, 0)
    header = struct.pack('<4sI', b'MD20', 0x108)
    header += struct.pack('<II', *a['name']) + struct.pack('<I', 0)
    for key in ('loops', 'sequences', 'sequenceIdxHashById', 'bones', 'boneIndicesById', 'vertices'):
        header += struct.pack('<II', *a.get(key, none))
    header += struct.pack('<I', 1)  # skin profiles
    for key in ('colors', 'textures', 'textureWeights', 'textureTransforms', 'textureIndicesById', 'materials', 'boneCombos',
                'textureCombos', 'textureCoordCombos', 'textureWeightCombos', 'textureTransformCombos'):
        header += struct.pack('<II', *a.get(key, none))
    header += bounds + bounds
    for key in ('collisionIndices', 'collisionPositions', 'collisionFaceNormals', 'attachments', 'attachmentIndicesById',
                'events', 'lights', 'cameras', 'cameraIndicesById', 'ribbons', 'particles'):
        header += struct.pack('<II', *a.get(key, none))
    header += b'\0' * 8  # textureCombinerCombos, unused without flag 8
    assert len(header) == 0x138
    blob.data[0:0x130] = header[:0x130]
    with open(os.path.join(out, name + '.m2'), 'wb') as f:
        f.write(blob.data)

    # The skin: one section and one batch per material, every vertex on bone 0
    skin = Blob(0x30)
    s = {}
    s['vertices'] = skin.add(pack('<H', list(range(len(vertices)))), len(vertices))
    s['indices'] = skin.add(pack('<H', indices), len(indices))
    s['bones'] = skin.add(b'\0\0\0\0' * len(vertices), len(vertices))
    raw_sections, raw_batches = b'', b''
    for i, (vstart, vcount, istart, icount) in enumerate(sections):
        p = positions[vstart:vstart + vcount]
        center = (p.min(axis=0) + p.max(axis=0)) / 2
        raw_sections += struct.pack('<IHHHHHHHH3f3ff', 0, vstart, vcount, istart, icount, 1, 0, 1, 0, *center, *center,
                                    float(np.linalg.norm(p - center, axis=1).max()))
        raw_batches += struct.pack('<BbHHHHHHHHHHH', 0x10, 0, 0, i, i, 0xFFFF, i, 0, 1, i, 0, 0, 0)
    s['sections'] = skin.add(raw_sections, len(sections))
    s['batches'] = skin.add(raw_batches, len(sections))
    skin.data[0:0x30] = struct.pack('<4s10II', b'SKIN', *s['vertices'], *s['indices'], *s['bones'], *s['sections'],
                                    *s['batches'], 21)
    with open(os.path.join(out, name + '00.skin'), 'wb') as f:
        f.write(skin.data)
    print(f'exported {mpq_dir}\\{name}.m2: {len(vertices)} vertices, {len(indices) // 3} triangles, '
          f'{len(textures)} textures, box {tuple(round(float(c), 2) for c in lo)} .. {tuple(round(float(c), 2) for c in hi)}')
    return lo, hi


if __name__ == '__main__' and '--' in sys.argv:
    export(*sys.argv[sys.argv.index('--') + 1:][:2])
