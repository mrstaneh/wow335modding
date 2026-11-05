// MapGen: writes the custom maps (a WDT plus a block of ADT tiles each) and their DBC rows.
//   MapGen <outdir> <folder with the original DBCs of Mods/mods.sh DBCS>
// Output goes to <outdir>/World/Maps/<map>/ and <outdir>/DBFilesClient/, ready for MpqTool pack. The maps themselves are
// in mapgen_testmap.cpp and mapgen_drownedsun.cpp.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include "mapgen.h"

namespace fs = std::filesystem;

float Clamp01(float t) {
    return t < 0 ? 0 : t > 1 ? 1 : t;
}

float Smooth(float e0, float e1, float v) {
    float t = Clamp01((v - e0) / (e1 - e0));
    return t * t * (3 - 2 * t);
}

float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

float Distance(Point a, float x, float y) {
    return std::sqrt((x - a.x) * (x - a.x) + (y - a.y) * (y - a.y));
}

static uint32_t Hash(int32_t x, int32_t y, uint32_t seed) {
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

float Random(int32_t x, int32_t y, uint32_t seed) {
    return (Hash(x, y, seed) & 0xFFFFFF) / float(0xFFFFFF);
}

float Noise(float x, float y) {
    int32_t ix = int32_t(std::floor(x)), iy = int32_t(std::floor(y));
    float fx = x - ix, fy = y - iy;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    float a = Lerp(Random(ix, iy, 0), Random(ix + 1, iy, 0), fx);
    float b = Lerp(Random(ix, iy + 1, 0), Random(ix + 1, iy + 1, 0), fx);
    return Lerp(a, b, fy) * 2 - 1;
}

float Fbm(float x, float y) {
    float sum = 0, amplitude = 0.5f;
    for (int i = 0; i < 4; i++) {
        sum += amplitude * Noise(x, y);
        x = x * 2 + 17.3f;
        y = y * 2 - 9.1f;
        amplitude *= 0.5f;
    }
    return sum;
}

// The client turns WMOs by yaw + 180 degrees around the vertical axis (CMap::CreateMapObjDef).
Point ToWorld(const Building& b, float lx, float ly) {
    float a = (b.yaw + 180) * 3.14159265f / 180;
    return { b.at.x + lx * std::cos(a) - ly * std::sin(a), b.at.y + lx * std::sin(a) + ly * std::cos(a) };
}

Point ToLocal(const Building& b, float x, float y) {
    float a = (b.yaw + 180) * 3.14159265f / 180;
    float dx = x - b.at.x, dy = y - b.at.y;
    return { dx * std::cos(a) + dy * std::sin(a), -dx * std::sin(a) + dy * std::cos(a) };
}

float FootprintDistance(const Building& b, float x, float y) {
    Point l = ToLocal(b, x, y);
    float ox = std::max({ b.lo[0] - l.x, 0.0f, l.x - b.hi[0] }), oy = std::max({ b.lo[1] - l.y, 0.0f, l.y - b.hi[1] });
    return std::sqrt(ox * ox + oy * oy);
}

// A building's world bounding box in x and y: its rotated footprint corners.
struct Box {
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;

    bool Overlaps(float top, float left, float size) const {
        return minX < top && maxX > top - size && minY < left && maxY > left - size;
    }
};

static Box WorldBox(const Building& b) {
    Box box;
    for (float lx : { b.lo[0], b.hi[0] }) {
        for (float ly : { b.lo[1], b.hi[1] }) {
            Point p = ToWorld(b, lx, ly);
            box.minX = std::min(box.minX, p.x), box.maxX = std::max(box.maxX, p.x);
            box.minY = std::min(box.minY, p.y), box.maxY = std::max(box.maxY, p.y);
        }
    }
    return box;
}

struct Writer {
    std::vector<uint8_t> data;

    size_t Size() const { return data.size(); }
    void U32(uint32_t v) { Bytes(&v, 4); }
    void F32(float v) { Bytes(&v, 4); }
    void Bytes(const void* p, size_t n) { data.insert(data.end(), (const uint8_t*)p, (const uint8_t*)p + n); }
    void Zeros(size_t n) { data.insert(data.end(), n, 0); }
    void Set(size_t at, const void* p, size_t n) { std::memcpy(&data[at], p, n); }
    void Set32(size_t at, uint32_t v) { Set(at, &v, 4); }

    // Chunk ids are stored as little-endian uint32s of the readable name, so "MVER" lands as "REVM".
    size_t Begin(const char* id) {
        U32(uint32_t(id[0]) << 24 | uint32_t(id[1]) << 16 | uint32_t(id[2]) << 8 | uint32_t(id[3]));
        U32(0);
        return Size() - 8;
    }
    void End(size_t start) { Set32(start + 4, uint32_t(Size() - start - 8)); }
    size_t Empty(const char* id) {
        size_t start = Begin(id);
        End(start);
        return start;
    }
};

static void Save(const fs::path& path, const std::vector<uint8_t>& data) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary).write((const char*)data.data(), data.size());
}

static std::vector<uint8_t> Wdt(const MapSpec& spec) {
    Writer w;
    size_t c = w.Begin("MVER");
    w.U32(18);
    w.End(c);
    c = w.Begin("MPHD");
    w.Zeros(32); // flags 0: no global WMO, 4-bit alpha maps
    w.End(c);
    c = w.Begin("MAIN");
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            bool tile = x >= spec.firstTile && x <= spec.lastTile && y >= spec.firstTile && y <= spec.lastTile;
            w.U32(tile ? 1 : 0); // 1 = this tile has an ADT
            w.U32(0);
        }
    }
    w.End(c);
    w.Empty("MWMO");
    return w.data;
}

// wmoRefs: indices into this tile's MODF of the buildings standing in this chunk.
static void Chunk(Writer& w, const MapSpec& spec, int tx, int ty, int cx, int cy, const std::vector<uint32_t>& refs,
                  const std::vector<uint32_t>& wmoRefs) {
    float top = ORIGIN - (ty * 16 + cy) * CHUNK_SIZE;
    float left = ORIGIN - (tx * 16 + cx) * CHUNK_SIZE;
    size_t start = w.Begin("MCNK");
    size_t header = w.Size();
    w.Zeros(128);
    auto field = [&](size_t at, uint32_t v) { w.Set32(header + at, v); };
    auto here = [&]() { return uint32_t(w.Size() - start); }; // sub-chunk offsets count from the MCNK id

    field(0x04, cx);
    field(0x08, cy);
    field(0x10, uint32_t(refs.size()));
    field(0x34, spec.area(top - CHUNK_SIZE / 2, left - CHUNK_SIZE / 2));
    field(0x38, uint32_t(wmoRefs.size()));
    // 4x4 hole squares per chunk, bit (row * 4 + column), rows running south like the vertex rows
    uint16_t holes = 0;
    for (int r = 0; r < 4; r++) {
        for (int k = 0; k < 4; k++) {
            if (spec.hole(top - (r + 0.5f) * CHUNK_SIZE / 4, left - (k + 0.5f) * CHUNK_SIZE / 4)) {
                holes |= uint16_t(1 << (r * 4 + k));
                printf("terrain hole x %.2f..%.2f y %.2f..%.2f\n", top - (r + 1) * CHUNK_SIZE / 4, top - r * CHUNK_SIZE / 4,
                       left - (k + 1) * CHUNK_SIZE / 4, left - k * CHUNK_SIZE / 4);
            }
        }
    }
    w.Set(header + 0x3C, &holes, 2);
    float position[3] = { top, left, 0.0f }; // only z is read: the base the MCVT heights are added to
    w.Set(header + 0x68, position, sizeof(position));

    // 145 vertices: rows of 9 outer and 8 inner points, alternating, 17 rows in total
    std::vector<float> xs, ys;
    for (int i = 0; i < 17; i++) {
        bool inner = i % 2;
        for (int j = 0; j < (inner ? 8 : 9); j++) {
            xs.push_back(top - (i / 2) * UNIT - (inner ? UNIT / 2 : 0));
            ys.push_back(left - j * UNIT - (inner ? UNIT / 2 : 0));
        }
    }

    field(0x14, here());
    size_t c = w.Begin("MCVT");
    for (size_t i = 0; i < xs.size(); i++) {
        w.F32(spec.height(xs[i], ys[i]));
    }
    w.End(c);

    field(0x18, here());
    c = w.Begin("MCNR");
    for (size_t i = 0; i < xs.size(); i++) {
        const float e = 0.5f;
        float nx = -(spec.height(xs[i] + e, ys[i]) - spec.height(xs[i] - e, ys[i])) / (2 * e);
        float ny = -(spec.height(xs[i], ys[i] + e) - spec.height(xs[i], ys[i] - e)) / (2 * e);
        float len = std::sqrt(nx * nx + ny * ny + 1);
        int8_t n[3] = { int8_t(127 * nx / len), int8_t(127 * ny / len), int8_t(127 / len) };
        w.Bytes(n, 3);
    }
    w.Zeros(13); // the client treats MCNR as 448 bytes: 145 normals plus 13 bytes of padding
    w.End(c);

    // Alpha maps: 64x64 per layer, sampled at pixel centres. Layers that are empty in this chunk are left out.
    std::vector<uint8_t> alpha[4];
    for (auto& a : alpha) {
        a.resize(64 * 64);
    }
    for (int ay = 0; ay < 64; ay++) {
        for (int ax = 0; ax < 64; ax++) {
            float weights[4];
            spec.weights(top - (ay + 0.5f) * CHUNK_SIZE / 64, left - (ax + 0.5f) * CHUNK_SIZE / 64, weights);
            for (int l = 1; l < 4; l++) {
                alpha[l][ay * 64 + ax] = uint8_t(std::lround(weights[l] * 15));
            }
        }
    }
    std::vector<int> layers = { 0 };
    for (int l = 1; l < int(spec.textures.size()); l++) {
        if (std::any_of(alpha[l].begin(), alpha[l].end(), [](uint8_t v) { return v > 0; })) {
            layers.push_back(l);
        }
    }
    field(0x0C, uint32_t(layers.size()));

    field(0x1C, here());
    c = w.Begin("MCLY");
    for (size_t i = 0; i < layers.size(); i++) {
        // texture, flags (0x100 = has an alpha map), offset into MCAL, ground effect
        uint32_t layer[4] = { uint32_t(layers[i]), i ? 0x100u : 0u, i ? uint32_t((i - 1) * 2048) : 0u, spec.textures[layers[i]].effect };
        w.Bytes(layer, sizeof(layer));
    }
    w.End(c);

    field(0x20, here());
    c = w.Begin("MCRF"); // indices into this tile's MDDF of the doodads standing in this chunk, then into MODF
    for (uint32_t ref : refs) {
        w.U32(ref);
    }
    for (uint32_t ref : wmoRefs) {
        w.U32(ref);
    }
    w.End(c);

    field(0x24, here());
    c = w.Begin("MCAL");
    for (size_t i = 1; i < layers.size(); i++) {
        const auto& a = alpha[layers[i]];
        for (size_t p = 0; p < a.size(); p += 2) {
            uint8_t b = uint8_t(a[p] | a[p + 1] << 4); // 4 bits per value, low nibble first
            w.Bytes(&b, 1);
        }
    }
    w.End(c);
    field(0x28, uint32_t(w.Size() - c)); // sizeAlpha includes the 8-byte chunk header

    w.End(start);
}

// Water: one flat instance per chunk whose terrain dips below that chunk's water surface, with per-vertex depth.
static void Water(Writer& w, const MapSpec& spec, int tx, int ty) {
    size_t mh2o = w.Begin("MH2O");
    size_t base = w.Size(); // MH2O offsets are relative to its data
    w.Zeros(256 * 12);      // per chunk: offset of instances, instance count, offset of attributes
    for (int cy = 0; cy < 16; cy++) {
        for (int cx = 0; cx < 16; cx++) {
            float top = ORIGIN - (ty * 16 + cy) * CHUNK_SIZE;
            float left = ORIGIN - (tx * 16 + cx) * CHUNK_SIZE;
            float level = spec.water(top - CHUNK_SIZE / 2, left - CHUNK_SIZE / 2);
            if (level == NO_WATER) {
                continue;
            }
            uint8_t depth[81];
            uint8_t exists[8] = {};
            bool any = false;
            for (int i = 0; i <= 8; i++) {
                for (int j = 0; j <= 8; j++) {
                    float d = level - spec.height(top - i * UNIT, left - j * UNIT);
                    depth[i * 9 + j] = uint8_t(Clamp01(d / 8) * 255);
                }
            }
            for (int i = 0; i < 8; i++) {
                for (int j = 0; j < 8; j++) {
                    // a cell has water when any of its corners is below the surface
                    if (depth[i * 9 + j] || depth[i * 9 + j + 1] || depth[(i + 1) * 9 + j] || depth[(i + 1) * 9 + j + 1]) {
                        exists[(i * 8 + j) / 8] |= uint8_t(1 << ((i * 8 + j) % 8));
                        any = true;
                    }
                }
            }
            if (!any) {
                continue;
            }
            size_t desc = base + (cy * 16 + cx) * 12;
            uint32_t instance = uint32_t(w.Size() - base);
            uint32_t attributes = instance + 24;
            uint32_t bitmap = attributes + 16;
            uint32_t vertices = bitmap + 8;
            w.Set32(desc, instance);
            w.Set32(desc + 4, 1);
            w.Set32(desc + 8, attributes);

            uint16_t type[2] = { spec.liquidType, 0 }; // vertex format 0: height floats then depth bytes
            w.Bytes(type, 4);
            w.F32(level);
            w.F32(level);
            uint8_t rect[4] = { 0, 0, 8, 8 }; // x offset, y offset, width, height in cells
            w.Bytes(rect, 4);
            w.U32(bitmap);
            w.U32(vertices);

            w.Bytes("\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF", 8); // fishable everywhere
            w.Zeros(8);                                    // nowhere counts as deep water
            w.Bytes(exists, 8);
            for (int v = 0; v < 81; v++) {
                w.F32(level);
            }
            w.Bytes(depth, 81);
        }
    }
    w.End(mh2o);
}

static std::vector<uint8_t> Adt(const MapSpec& spec, int tx, int ty, uint32_t& uniqueId) {
    // Doodads standing on this tile, and the chunk each one is in.
    std::vector<const Doodad*> doodads;
    std::vector<std::vector<uint32_t>> refs(256);
    for (const Doodad& d : spec.doodads) {
        float u = ORIGIN - d.y - tx * TILE_SIZE, v = ORIGIN - d.x - ty * TILE_SIZE;
        if (u >= 0 && u < TILE_SIZE && v >= 0 && v < TILE_SIZE) {
            refs[int(v / CHUNK_SIZE) * 16 + int(u / CHUNK_SIZE)].push_back(uint32_t(doodads.size()));
            doodads.push_back(&d);
        }
    }
    std::map<std::string, uint32_t> modelIndex;
    std::vector<std::string> models;
    for (const Doodad* d : doodads) {
        if (modelIndex.emplace(d->model, uint32_t(models.size())).second) {
            models.push_back(d->model);
        }
    }

    // Buildings reaching into this tile; each keeps its map-wide index + 1 as unique id, the same in every tile it
    // overlaps, so the client places it once.
    float tileTop = ORIGIN - ty * TILE_SIZE, tileLeft = ORIGIN - tx * TILE_SIZE;
    std::vector<uint32_t> buildings;
    std::vector<Box> boxes;
    std::map<std::string, uint32_t> wmoIndex;
    std::vector<std::string> wmos;
    for (uint32_t i = 0; i < spec.buildings.size(); i++) {
        Box box = WorldBox(spec.buildings[i]);
        if (box.Overlaps(tileTop, tileLeft, TILE_SIZE)) {
            buildings.push_back(i);
            boxes.push_back(box);
            if (wmoIndex.emplace(spec.buildings[i].model, uint32_t(wmos.size())).second) {
                wmos.push_back(spec.buildings[i].model);
            }
        }
    }

    Writer w;
    size_t c = w.Begin("MVER");
    w.U32(18);
    w.End(c);

    size_t mhdr = w.Begin("MHDR");
    size_t mhdrData = w.Size();
    w.Zeros(64);
    w.End(mhdr);
    // MHDR fields after flags: MCIN, MTEX, MMDX, MMID, MWMO, MWID, MDDF, MODF, MFBO, MH2O, MTXF, relative to MHDR's data
    auto link = [&](int index, size_t chunk) { w.Set32(mhdrData + 4 + 4 * index, uint32_t(chunk - mhdrData)); };

    size_t mcin = w.Begin("MCIN");
    w.Zeros(256 * 16);
    w.End(mcin);
    link(0, mcin);

    c = w.Begin("MTEX");
    for (const Texture& texture : spec.textures) {
        w.Bytes(texture.file, std::strlen(texture.file) + 1);
    }
    w.End(c);
    link(1, c);

    std::vector<uint32_t> nameOffsets;
    c = w.Begin("MMDX");
    for (const std::string& model : models) {
        nameOffsets.push_back(uint32_t(w.Size() - c - 8));
        w.Bytes(model.c_str(), model.size() + 1);
    }
    w.End(c);
    link(2, c);
    c = w.Begin("MMID");
    for (uint32_t offset : nameOffsets) {
        w.U32(offset);
    }
    w.End(c);
    link(3, c);

    std::vector<uint32_t> wmoOffsets;
    c = w.Begin("MWMO");
    for (const std::string& wmo : wmos) {
        wmoOffsets.push_back(uint32_t(w.Size() - c - 8));
        w.Bytes(wmo.c_str(), wmo.size() + 1);
    }
    w.End(c);
    link(4, c);
    c = w.Begin("MWID");
    for (uint32_t offset : wmoOffsets) {
        w.U32(offset);
    }
    w.End(c);
    link(5, c);

    c = w.Begin("MDDF");
    for (const Doodad* d : doodads) {
        w.U32(modelIndex[d->model]);
        w.U32(uniqueId++);
        float placement[6] = { ORIGIN - d->y, d->z, ORIGIN - d->x, 0, d->yaw, 0 }; // position, then rotation in degrees
        w.Bytes(placement, sizeof(placement));
        uint16_t scale[2] = { uint16_t(d->scale * 1024), 0 }; // 1024 = 1.0, flags
        w.Bytes(scale, 4);
    }
    w.End(c);
    link(6, c);

    c = w.Begin("MODF");
    for (size_t i = 0; i < buildings.size(); i++) {
        const Building& b = spec.buildings[buildings[i]];
        const Box& box = boxes[i];
        w.U32(wmoIndex[b.model]);
        w.U32(buildings[i] + 1);
        // position, rotation, then the bounding box, all in placement axes (O - y, z, O - x)
        float placement[12] = { ORIGIN - b.at.y, b.z, ORIGIN - b.at.x, 0, b.yaw, 0,
                                ORIGIN - box.maxY, b.z + b.lo[2], ORIGIN - box.maxX, ORIGIN - box.minY, b.z + b.hi[2], ORIGIN - box.minX };
        w.Bytes(placement, sizeof(placement));
        uint16_t flags[4] = { 0, b.doodadSet, b.nameSet, 0 };
        w.Bytes(flags, sizeof(flags));
    }
    w.End(c);
    link(7, c);

    size_t water = w.Size();
    Water(w, spec, tx, ty);
    link(9, water);

    for (int cy = 0; cy < 16; cy++) {
        for (int cx = 0; cx < 16; cx++) {
            size_t start = w.Size();
            float top = tileTop - cy * CHUNK_SIZE, left = tileLeft - cx * CHUNK_SIZE;
            std::vector<uint32_t> wmoRefs;
            for (uint32_t i = 0; i < boxes.size(); i++) {
                if (boxes[i].Overlaps(top, left, CHUNK_SIZE)) {
                    wmoRefs.push_back(i);
                }
            }
            Chunk(w, spec, tx, ty, cx, cy, refs[cy * 16 + cx], wmoRefs);
            size_t entry = mcin + 8 + (cy * 16 + cx) * 16;
            w.Set32(entry, uint32_t(start));
            w.Set32(entry + 4, uint32_t(w.Size() - start));
        }
    }
    return w.data;
}

uint32_t Dbc::Get(uint32_t row, uint32_t field) const {
    uint32_t v;
    std::memcpy(&v, &records[row * recordSize + field * 4], 4);
    return v;
}

int32_t Dbc::Find(uint32_t field, uint32_t value) const {
    for (uint32_t i = 0; i < Count(); i++) {
        if (Get(i, field) == value) {
            return int32_t(i);
        }
    }
    return -1;
}

std::vector<uint32_t> Dbc::Row(uint32_t row) const {
    std::vector<uint32_t> out(fields);
    std::memcpy(out.data(), &records[row * recordSize], recordSize);
    return out;
}

void Dbc::Put(const std::vector<uint32_t>& row) {
    int32_t existing = Find(0, row[0]);
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(row.data());
    if (existing >= 0) {
        std::copy(bytes, bytes + recordSize, records.begin() + existing * recordSize);
    } else {
        records.insert(records.end(), bytes, bytes + recordSize);
    }
}

uint32_t Dbc::AddString(const char* s) {
    uint32_t at = uint32_t(strings.size());
    strings.insert(strings.end(), s, s + std::strlen(s) + 1);
    return at;
}

static bool Load(Dbc& dbc, const fs::path& path, uint32_t fields, uint32_t recordSize) {
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    uint32_t header[5];
    if (data.size() < sizeof(header)) {
        fprintf(stderr, "%s is missing\n", path.string().c_str());
        return false;
    }
    std::memcpy(header, data.data(), sizeof(header));
    uint32_t count = header[1];
    if (header[0] != 0x43424457 || header[2] != fields || header[3] != recordSize || data.size() != 20 + count * recordSize + header[4]) {
        fprintf(stderr, "%s is not the 3.3.5 layout (%u fields, %u bytes a row)\n", path.string().c_str(), fields, recordSize);
        return false;
    }
    dbc.fields = fields;
    dbc.recordSize = recordSize;
    dbc.records.assign(data.begin() + 20, data.begin() + 20 + count * recordSize);
    dbc.strings.assign(data.begin() + 20 + count * recordSize, data.end());
    return true;
}

static void Save(const Dbc& dbc, const fs::path& path) {
    Writer w;
    uint32_t header[5] = { 0x43424457, dbc.Count(), dbc.fields, dbc.recordSize, uint32_t(dbc.strings.size()) };
    w.Bytes(header, sizeof(header));
    w.Bytes(dbc.records.data(), dbc.records.size());
    w.Bytes(dbc.strings.data(), dbc.strings.size());
    Save(path, w.data);
}

// A PNG of 8-bit RGB rows, zlib-wrapped as uncompressed (stored) deflate blocks: big, but no compressor needed.
static std::vector<uint8_t> Png(int width, int height, const std::vector<uint8_t>& rgb) {
    static uint32_t table[256];
    if (!table[1]) {
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++) {
                c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[n] = c;
        }
    }
    std::vector<uint8_t> out = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    auto be32 = [](std::vector<uint8_t>& v, uint32_t x) {
        for (int s = 24; s >= 0; s -= 8) {
            v.push_back(uint8_t(x >> s));
        }
    };
    auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
        be32(out, uint32_t(data.size()));
        size_t start = out.size();
        out.insert(out.end(), type, type + 4);
        out.insert(out.end(), data.begin(), data.end());
        uint32_t crc = 0xFFFFFFFFu;
        for (size_t i = start; i < out.size(); i++) {
            crc = table[(crc ^ out[i]) & 0xFF] ^ (crc >> 8);
        }
        be32(out, crc ^ 0xFFFFFFFFu);
    };
    std::vector<uint8_t> header;
    be32(header, uint32_t(width));
    be32(header, uint32_t(height));
    header.insert(header.end(), { 8, 2, 0, 0, 0 }); // 8 bits, RGB, deflate, no filter method, not interlaced
    chunk("IHDR", header);

    std::vector<uint8_t> raw;
    for (int y = 0; y < height; y++) {
        raw.push_back(0); // filter: none
        raw.insert(raw.end(), rgb.begin() + size_t(y) * width * 3, rgb.begin() + size_t(y + 1) * width * 3);
    }
    std::vector<uint8_t> z = { 0x78, 0x01 };
    uint32_t a = 1, b = 0;
    for (size_t at = 0; at < raw.size(); at += 65535) {
        uint16_t n = uint16_t(std::min<size_t>(65535, raw.size() - at));
        z.push_back(at + n == raw.size() ? 1 : 0);
        z.insert(z.end(), { uint8_t(n), uint8_t(n >> 8), uint8_t(~n), uint8_t(~n >> 8) });
        z.insert(z.end(), raw.begin() + at, raw.begin() + at + n);
    }
    for (uint8_t v : raw) {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    be32(z, b << 16 | a);
    chunk("IDAT", z);
    chunk("IEND", {});
    return out;
}

static std::string JsonString(const char* s) {
    std::string out = "\"";
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') {
            out += '\\';
        }
        out += *s;
    }
    return out + "\"";
}

// The plan view: <map>.png, the terrain from above (north up, west left) coloured by its textures, shaded from the
// north-west, with water and a contour every 5 yards; and <map>.json, what Tools/WebClient/plan.html draws over it
// (labels, doodads, building footprints).
static void Plan(const MapSpec& spec, const fs::path& dir) {
    float minX = spec.planMinX, maxX = spec.planMaxX, minY = spec.planMinY, maxY = spec.planMaxY, step = spec.planStep;
    if (minX == maxX) {
        maxX = maxY = ORIGIN - spec.firstTile * TILE_SIZE;
        minX = minY = ORIGIN - (spec.lastTile + 1) * TILE_SIZE;
    }
    int width = int((maxY - minY) / step), height = int((maxX - minX) / step);
    std::vector<float> h(size_t(width) * height);
    for (int r = 0; r < height; r++) {
        for (int c = 0; c < width; c++) {
            h[size_t(r) * width + c] = spec.height(maxX - (r + 0.5f) * step, maxY - (c + 0.5f) * step);
        }
    }
    auto colour = [](uint32_t rgb, int shift) { return float((rgb >> shift) & 0xFF); };
    std::vector<uint8_t> rgb(size_t(width) * height * 3);
    for (int r = 0; r < height; r++) {
        for (int c = 0; c < width; c++) {
            float x = maxX - (r + 0.5f) * step, y = maxY - (c + 0.5f) * step;
            float here = h[size_t(r) * width + c];
            float north = h[size_t(std::max(r - 1, 0)) * width + c], south = h[size_t(std::min(r + 1, height - 1)) * width + c];
            float west = h[size_t(r) * width + std::max(c - 1, 0)], east = h[size_t(r) * width + std::min(c + 1, width - 1)];

            float w[4];
            spec.weights(x, y, w);
            float col[3];
            for (int k = 0; k < 3; k++) {
                col[k] = colour(spec.textures[0].color, 16 - 8 * k);
                for (size_t l = 1; l < spec.textures.size(); l++) {
                    col[k] = Lerp(col[k], colour(spec.textures[l].color, 16 - 8 * k), w[l]);
                }
            }
            // light from the north-west, 45 degrees up
            float dx = (north - south) / (2 * step), dy = (west - east) / (2 * step);
            float len = std::sqrt(dx * dx + dy * dy + 1);
            float light = (dx * 0.5f + dy * 0.5f + 0.7071f) / len;
            float shade = 0.35f + 0.75f * Clamp01(light);
            bool contour = int(std::floor(here / 5)) != int(std::floor(east / 5)) || int(std::floor(here / 5)) != int(std::floor(south / 5));
            for (int k = 0; k < 3; k++) {
                col[k] *= shade * (contour ? 0.75f : 1.0f);
            }
            float level = spec.water(x, y);
            if (level != NO_WATER && level > here) {
                float deep = Clamp01((level - here) / 4);
                const float water[3] = { 40, 95, 130 };
                for (int k = 0; k < 3; k++) {
                    col[k] = Lerp(col[k], water[k], 0.55f + 0.35f * deep);
                }
            }
            for (int k = 0; k < 3; k++) {
                rgb[(size_t(r) * width + c) * 3 + k] = uint8_t(std::min(255.0f, std::max(0.0f, col[k])));
            }
        }
    }
    Save(dir / (std::string(spec.dir) + ".png"), Png(width, height, rgb));

    std::string json = "{\"map\":" + std::to_string(spec.id) + ",\"name\":" + JsonString(spec.dir);
    char buf[256];
    snprintf(buf, sizeof(buf), ",\"minX\":%g,\"maxX\":%g,\"minY\":%g,\"maxY\":%g,\"step\":%g,\"width\":%d,\"height\":%d", minX,
             maxX, minY, maxY, step, width, height);
    json += buf;
    json += ",\"labels\":[";
    for (size_t i = 0; i < spec.labels.size(); i++) {
        snprintf(buf, sizeof(buf), "%s{\"text\":%s,\"x\":%g,\"y\":%g}", i ? "," : "", JsonString(spec.labels[i].text).c_str(),
                 spec.labels[i].at.x, spec.labels[i].at.y);
        json += buf;
    }
    json += "],\"doodads\":[";
    for (size_t i = 0; i < spec.doodads.size(); i++) {
        const Doodad& d = spec.doodads[i];
        json += (i ? ",{\"model\":" : "{\"model\":") + JsonString(d.model);
        snprintf(buf, sizeof(buf), ",\"x\":%.2f,\"y\":%.2f,\"z\":%.2f,\"yaw\":%.1f,\"scale\":%.2f}", d.x, d.y, d.z, d.yaw, d.scale);
        json += buf;
    }
    json += "],\"buildings\":[";
    for (size_t i = 0; i < spec.buildings.size(); i++) {
        const Building& b = spec.buildings[i];
        json += (i ? ",{\"model\":" : "{\"model\":") + JsonString(b.model) + ",\"corners\":[";
        const float corners[4][2] = { { b.lo[0], b.lo[1] }, { b.hi[0], b.lo[1] }, { b.hi[0], b.hi[1] }, { b.lo[0], b.hi[1] } };
        for (int k = 0; k < 4; k++) {
            Point p = ToWorld(b, corners[k][0], corners[k][1]);
            snprintf(buf, sizeof(buf), "%s[%.2f,%.2f]", k ? "," : "", p.x, p.y);
            json += buf;
        }
        json += "]}";
    }
    json += "]}\n";
    Save(dir / (std::string(spec.dir) + ".json"), std::vector<uint8_t>(json.begin(), json.end()));
}

int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) {
        fprintf(stderr, "usage: MapGen <outdir> <folder with the original DBCs of Mods/mods.sh DBCS> [plan dir]\n");
        return 1;
    }
    fs::path out = argv[1];
    fs::path dbcIn = argv[2];
    Dbcs dbcs;
    if (!Load(dbcs.map, dbcIn / "Map.dbc", 66, 264) || !Load(dbcs.areaTable, dbcIn / "AreaTable.dbc", 36, 144) ||
        !Load(dbcs.mapDifficulty, dbcIn / "MapDifficulty.dbc", 23, 92) || !Load(dbcs.light, dbcIn / "Light.dbc", 15, 60) ||
        !Load(dbcs.lightParams, dbcIn / "LightParams.dbc", 9, 36) ||
        !Load(dbcs.lightIntBand, dbcIn / "LightIntBand.dbc", 34, 136) ||
        !Load(dbcs.lightFloatBand, dbcIn / "LightFloatBand.dbc", 34, 136) ||
        !Load(dbcs.gameObjectDisplayInfo, dbcIn / "GameObjectDisplayInfo.dbc", 19, 76)) {
        return 1;
    }

    for (const MapSpec& spec : { TestMap(), DrownedSun() }) {
        fs::path maps = out / "World" / "Maps" / spec.dir;
        uint32_t uniqueId = 1;
        Save(maps / (std::string(spec.dir) + ".wdt"), Wdt(spec));
        for (int ty = spec.firstTile; ty <= spec.lastTile; ty++) {
            for (int tx = spec.firstTile; tx <= spec.lastTile; tx++) {
                Save(maps / (std::string(spec.dir) + "_" + std::to_string(tx) + "_" + std::to_string(ty) + ".adt"),
                     Adt(spec, tx, ty, uniqueId));
            }
        }
        spec.rows(dbcs);
        if (argc == 4) {
            Plan(spec, argv[3]);
        }
        int tiles = spec.lastTile - spec.firstTile + 1;
        printf("map %u '%s': %d tiles, %zu doodads, %zu buildings\n", spec.id, spec.dir, tiles * tiles, spec.doodads.size(),
               spec.buildings.size());
    }
    Save(dbcs.map, out / "DBFilesClient" / "Map.dbc");
    Save(dbcs.areaTable, out / "DBFilesClient" / "AreaTable.dbc");
    Save(dbcs.mapDifficulty, out / "DBFilesClient" / "MapDifficulty.dbc");
    Save(dbcs.light, out / "DBFilesClient" / "Light.dbc");
    Save(dbcs.lightParams, out / "DBFilesClient" / "LightParams.dbc");
    Save(dbcs.lightIntBand, out / "DBFilesClient" / "LightIntBand.dbc");
    Save(dbcs.lightFloatBand, out / "DBFilesClient" / "LightFloatBand.dbc");
    Save(dbcs.gameObjectDisplayInfo, out / "DBFilesClient" / "GameObjectDisplayInfo.dbc");
    return 0;
}
