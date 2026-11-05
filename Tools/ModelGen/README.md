# Adding a model

Our own models are made in Blender, kept as `.blend` files under `Mods/<Mod>/models/`, and exported to the client's
M2 format by `export_m2.py` when the mod patch is built. Nothing from Blizzard's files goes into them.

## 1. Model it in Blender

Open or create `Mods/<Mod>/models/<Name>.blend` (Blender 5.1; through the Blender MCP or by hand). The file name is
the model's name: it becomes `World\<Mod>\<Name>.m2` in the patch.

- Z up, 1 metre = 1 yard. The model's front faces -Y (what Blender's front view shows); its origin is where it
  stands on the ground.
- Every visible mesh object is exported; hidden ones (or ones disabled for render) are not.
- Each material needs an Image Texture node with a power-of-two image (256, 512...). Pack the image into the .blend
  (Image > Pack) so the file is self-contained. Each material becomes its own texture and draw batch.
- Faces are one-sided unless the material's Backface Culling is off.
- Keep it low-poly: under 65 536 vertices; a few hundred to a few thousand triangles is right for 3.3.5.
- Static only for now: one bone, a Stand animation, no particles. The render mesh is also its collision.
- `kit.py` builds props from code: painted stone, bands, glyph panels (numpy), lathed and prism meshes with their UVs.
  Load it with `runpy.run_path` in the Blender MCP. Give a texture the aspect of the surface it covers (a tall
  shaft gets a tall texture), or its glyphs and grain stretch.

## Placing doodads

Props that decorate the map are doodads: add them in `Doodads()` in `Tools/MpqTool/mapgen_drownedsun.cpp` with the
`World\DrownedSun\<Name>.m2` path. They need no DBC row. The server takes their collision for line of sight from
`mods.sh server`.

## 2. Give it a display

Game objects: add a row to `MODEL_DISPLAYS` in `Tools/MpqTool/mapgen_drownedsun.cpp` with the next free id (from
9700), the model path with an `.mdx` extension (the client loads the `.m2`), and the bounding box that the
export prints. Then point the object's `displayId` at it in `Mods/sql/*.sql`, and set its `size`.

Creatures need CreatureModelData and CreatureDisplayInfo rows, which MapGen doesn't write yet.

## 3. Build and install

```bash
Mods/mods.sh build    # MapGen, then every Mods/*/models/*.blend through Blender -> patch-enUS-4.MPQ
Mods/mods.sh server   # DBCs and SQL to the server
```

Restart the worldserver for the new DBC rows and templates. Reload the client to pick up the new MPQ.

To export one model by hand, without the whole build:

```bash
"/c/Program Files/Blender Foundation/Blender 5.1/blender.exe" -b Mods/DrownedSun/models/CanopicJar.blend --python Tools/ModelGen/export_m2.py -- build/mods/pack 'World\DrownedSun'
```

## Checking it

- `exported ...` with vertex and triangle counts and the box means the files were written.
- In game: `.gobject add <entry>` near you. Without a server restart, a Model frame shows it on any screen:
  `f = CreateFrame("Model"); f:SetAllPoints(); f:SetModel("World\\DrownedSun\\<Name>.m2"); f:SetModelScale(0.25)`.
- An invisible model usually means a missing texture-weight key or a bad texture path. The exporter writes both the
  way the client expects (see the comments in `export_m2.py`).
