// MapGen: writes the test map (a WDT plus a 2x2 block of ADT tiles) and adds its row to Map.dbc.
//   MapGen <outdir> <original Map.dbc>
// Output goes to <outdir>/World/Maps/TestMap/ and <outdir>/DBFilesClient/Map.dbc, ready for MpqTool pack.
// Format notes are from the client's own loaders (WoW/Source/WorldClient/MapChunk.cpp, MapArea.cpp, Map.cpp,
// WoW/Source/LiquidSystem/LiquidData.cpp).
//
// The map: rolling Elwynn-style hills ringed by unclimbable mountains, a road from the south to the north
// mountains through a clearing at the world origin (the spawn), a side path to a pond, forests, bushes and rocks.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static const uint32_t MAP_ID = 800;
static const uint32_t TEMPLATE_MAP_ID = 1; // Kalimdor: a plain continent, copied for every field we don't set
static const char* MAP_DIR = "TestMap";
static const char* MAP_NAME = "Test Map";
static const int FIRST_TILE = 31; // tiles 31..32 on both axes, so the map is centred on world (0, 0)
static const int LAST_TILE = 32;

static const float TILE_SIZE = 533.33333f;
static const float CHUNK_SIZE = TILE_SIZE / 16;
static const float UNIT = CHUNK_SIZE / 8;
static const float ORIGIN = 32 * TILE_SIZE; // world x/y of tile 0, chunk 0
static const float HALF = (LAST_TILE - FIRST_TILE + 1) * TILE_SIZE / 2;

// Terrain textures with the ground effect (grass blades, pebbles) Blizzard pairs them with in Elwynn.
struct Texture {
    const char* file;
    uint32_t effect;
};
static const Texture TEXTURES[] = {
    { "Tileset\\Elwynn\\ElwynnGrassBase.blp", 757 },
    { "Tileset\\Elwynn\\ElwynnFlowerBase.blp", 761 },
    { "Tileset\\Elwynn\\ElwynnDirtBase2.blp", 1106 },
    { "Tileset\\Elwynn\\ElwynnRockBaseTest2.blp", 993 },
};

static const uint16_t LIQUID_WATER = 1; // LiquidType.dbc
static const float WATER_LEVEL = 2.0f;

struct Point {
    float x, y;
};
static const Point CLEARING = { 0, 0 };
static const float CLEARING_RADIUS = 45;
static const Point POND = { -40, 200 };
static const float POND_RADIUS = 55;
static const float ROAD_HALF_WIDTH = 4;
// World axes: x points north, y points west.
static const std::vector<std::vector<Point>> ROADS = {
    { { -330, 30 }, { -220, -40 }, { -90, -20 }, { 0, 0 }, { 110, 50 }, { 220, 30 }, { 330, 100 } },
    { { 0, 0 }, { -20, 70 }, { -35, 130 } },
};

// The Lion's Pride Inn from Goldshire, with the furniture set and rotation Blizzard gave it there (Azeroth_31_49.adt).
struct Building {
    const char* model;
    Point at;
    float yaw;                 // degrees, as stored in MODF
    uint16_t doodadSet, nameSet;
    float lo[3], hi[3];        // bounding box from the WMO's MOHD, in its own coordinates
};
// Turned to -90 (Goldshire has -97) so its walls follow the terrain grid, and placed so the stairwell lands exactly on
// one 8.33-yard terrain hole square: inn x -29.6..-21.27, y -8.4..-0.07 becomes world x 75..83.33, y -75..-66.67.
static const Building INN = { "World\\wmo\\Azeroth\\Buildings\\GoldshireInn\\GoldshireInn.wmo",
                              { 74.933f, -45.4f }, -90, 2, 2, { -37.8f, -13.5f, -6.8f }, { 21.9f, 18.7f, 23.3f } };
static const float BASEMENT[3] = { -34.5f, -2.5f, -6.7f }; // open basement floor beside the foot of the stairs, inn coordinates
// Where the stairs down (inn y -8.6..-5.2, from x -21 at the ground floor to -33) pass through ground level.
static const float STAIRWELL_LO[2] = { -29.6f, -8.4f }, STAIRWELL_HI[2] = { -21.27f, -0.07f };

static float Clamp01(float t) {
    return t < 0 ? 0 : t > 1 ? 1 : t;
}

// Hermite step from 0 at e0 to 1 at e1; e0 > e1 gives a falling step.
static float Smooth(float e0, float e1, float v) {
    float t = Clamp01((v - e0) / (e1 - e0));
    return t * t * (3 - 2 * t);
}

static float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

static float Distance(Point a, float x, float y) {
    return std::sqrt((x - a.x) * (x - a.x) + (y - a.y) * (y - a.y));
}

static uint32_t Hash(int32_t x, int32_t y, uint32_t seed) {
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static float Random(int32_t x, int32_t y, uint32_t seed) {
    return (Hash(x, y, seed) & 0xFFFFFF) / float(0xFFFFFF);
}

// Value noise in -1..1, then 4 octaves of it.
static float Noise(float x, float y) {
    int32_t ix = int32_t(std::floor(x)), iy = int32_t(std::floor(y));
    float fx = x - ix, fy = y - iy;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    float a = Lerp(Random(ix, iy, 0), Random(ix + 1, iy, 0), fx);
    float b = Lerp(Random(ix, iy + 1, 0), Random(ix + 1, iy + 1, 0), fx);
    return Lerp(a, b, fy) * 2 - 1;
}

static float Fbm(float x, float y) {
    float sum = 0, amplitude = 0.5f;
    for (int i = 0; i < 4; i++) {
        sum += amplitude * Noise(x, y);
        x = x * 2 + 17.3f;
        y = y * 2 - 9.1f;
        amplitude *= 0.5f;
    }
    return sum;
}

static float RoadDistance(float x, float y) {
    float best = 1e9f;
    for (const auto& road : ROADS) {
        for (size_t i = 0; i + 1 < road.size(); i++) {
            Point a = road[i], b = road[i + 1];
            float dx = b.x - a.x, dy = b.y - a.y;
            float t = Clamp01(((x - a.x) * dx + (y - a.y) * dy) / (dx * dx + dy * dy));
            best = std::min(best, Distance({ a.x + t * dx, a.y + t * dy }, x, y));
        }
    }
    return best;
}

// Large-scale land without the small bumps: what roads and the clearing are flattened towards.
static float BaseHeight(float x, float y) {
    float edge = std::max(std::abs(x), std::abs(y)) / HALF; // 0 in the middle, 1 at the map border
    float mountains = 160 * Smooth(0.68f, 0.93f, edge) * (0.7f + 0.3f * Fbm(x / 90 + 3, y / 90 + 5));
    return 4 + 10 * (0.5f + 0.5f * Fbm(x / 170, y / 170)) + mountains;
}

// The client turns WMOs by yaw + 180 degrees around the vertical axis (CMap::CreateMapObjDef).
static Point ToWorld(const Building& b, float lx, float ly) {
    float a = (b.yaw + 180) * 3.14159265f / 180;
    return { b.at.x + lx * std::cos(a) - ly * std::sin(a), b.at.y + lx * std::sin(a) + ly * std::cos(a) };
}

static Point ToLocal(const Building& b, float x, float y) {
    float a = (b.yaw + 180) * 3.14159265f / 180;
    float dx = x - b.at.x, dy = y - b.at.y;
    return { dx * std::cos(a) + dy * std::sin(a), -dx * std::sin(a) + dy * std::cos(a) };
}

// Distance from a point to the building's footprint, 0 inside it.
static float FootprintDistance(const Building& b, float x, float y) {
    Point l = ToLocal(b, x, y);
    float ox = std::max({ b.lo[0] - l.x, 0.0f, l.x - b.hi[0] }), oy = std::max({ b.lo[1] - l.y, 0.0f, l.y - b.hi[1] });
    return std::sqrt(ox * ox + oy * oy);
}

// Terrain collides even inside buildings, so the hole squares over the stairwell are cut out of it.
static bool IsHole(float x, float y) {
    Point l = ToLocal(INN, x, y);
    return l.x > STAIRWELL_LO[0] && l.x < STAIRWELL_HI[0] && l.y > STAIRWELL_LO[1] && l.y < STAIRWELL_HI[1];
}

// The inn's origin sits on the smoothed land; its outer walls reach 0.2 below the origin, its floors are above it.
static float InnZ() {
    return BaseHeight(INN.at.x, INN.at.y);
}

// ponytail: terrain is formulas; read heightmap/mask images once zones are designed by hand
static float Height(float x, float y) {
    float h = BaseHeight(x, y) + 3 * Fbm(x / 45 + 11, y / 45 - 7);

    float pond = Distance(POND, x, y);
    float bowl = WATER_LEVEL - 1 - 7 * (1 - Clamp01(pond / POND_RADIUS));
    h = Lerp(h, bowl, Smooth(POND_RADIUS * 1.5f, POND_RADIUS * 0.7f, pond));

    h = Lerp(h, BaseHeight(x, y), Smooth(ROAD_HALF_WIDTH * 3, ROAD_HALF_WIDTH, RoadDistance(x, y)));
    h = Lerp(h, BaseHeight(CLEARING.x, CLEARING.y), Smooth(CLEARING_RADIUS * 1.6f, CLEARING_RADIUS, Distance(CLEARING, x, y)));
    // a pad just under the bottom of the walls, so the inn sits on the ground and no grass pokes through its floors
    h = Lerp(h, InnZ() - 0.3f, Smooth(14, 3, FootprintDistance(INN, x, y)));
    return h;
}

static float Slope(float x, float y) {
    const float e = 0.5f;
    float dx = (Height(x + e, y) - Height(x - e, y)) / (2 * e);
    float dy = (Height(x, y + e) - Height(x, y - e)) / (2 * e);
    return std::sqrt(dx * dx + dy * dy);
}

// Alpha of texture layers 1..3 (flowers, dirt, rock) at a point; layer 0 (grass) is the base.
static void Weights(float x, float y, float w[4]) {
    float h = Height(x, y);
    float rock = Smooth(0.55f, 0.9f, Slope(x, y));
    float road = Smooth(ROAD_HALF_WIDTH + 3, ROAD_HALF_WIDTH - 1, RoadDistance(x, y)) * (0.85f + 0.15f * Noise(x / 3, y / 3));
    float shore = Smooth(WATER_LEVEL + 2.5f, WATER_LEVEL + 0.5f, h);
    float yard = Smooth(8, 2, FootprintDistance(INN, x, y)) * (0.8f + 0.2f * Noise(x / 2, y / 2));
    float dirt = std::max({ road, shore, yard });
    w[0] = 1;
    w[1] = Smooth(0.15f, 0.45f, Fbm(x / 40 + 50, y / 40 - 20)) * (1 - dirt);
    w[2] = dirt;
    w[3] = rock;
}

struct Doodad {
    const char* model;
    float x, y, z, yaw, scale;
};

static const char* TREES[] = {
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Trees\\ElwynnTreeCanopy01.m2",
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Trees\\ElwynnTreeCanopy02.m2",
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Trees\\ElwynnTreeCanopy03.m2",
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Trees\\ElwynnTreeCanopy04.m2",
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Trees\\ElwynnTreeMid01.m2",
};
static const char* PINES[] = {
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Trees\\ElwynnTree01\\ElwynnPine01.m2",
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Trees\\ElwynnTree01\\ElwynnPine02.m2",
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Trees\\ElwynnFirTree01.m2",
};
static const char* BUSHES[] = {
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Bush\\ElwynnBush01.m2", "World\\Azeroth\\Elwynn\\PassiveDoodads\\Bush\\ElwynnBush02.m2",
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Bush\\ElwynnBush03.m2", "World\\Azeroth\\Elwynn\\PassiveDoodads\\Bush\\ElwynnBush04.m2",
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Bush\\ElwynnBush05.m2", "World\\Azeroth\\Elwynn\\PassiveDoodads\\Bush\\ElwynnBush06.m2",
};
static const char* FOREST_FLOOR[] = {
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Tree\\ElwynnLog01.m2",
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Tree\\ElwynnLog02.m2",
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Trees\\Stumps\\ElwynnTreeStump01.m2",
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Trees\\Stumps\\ElwynnTreeStump02.m2",
};
static const char* CLIFF_ROCKS[] = {
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\CliffRocks\\ElwynnCliffRock01.m2",
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\CliffRocks\\ElwynnCliffRock02.m2",
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\CliffRocks\\ElwynnCliffRock03.m2",
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\CliffRocks\\ElwynnCliffRock04.m2",
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\CliffRocks\\ElwynnCliffRock05.m2",
};
static const char* SMALL_ROCKS[] = {
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Detail\\ElwynnRock1\\ElwynnRock1.m2",
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Detail\\ElwynnRock2\\ElwynnRock2.m2",
};
static const char* LAMP_POST = "World\\Azeroth\\Elwynn\\PassiveDoodads\\LampPost\\LampPost.m2";
static const char* SIGN_POST = "World\\Azeroth\\Elwynn\\PassiveDoodads\\Signs\\GenericPosts\\GenericSmallSignPost01.m2";
static const char* MONUMENT = "World\\Azeroth\\Elwynn\\PassiveDoodads\\MonumentRock\\MonumentRock.m2";

template <size_t N>
static const char* Pick(const char* (&models)[N], float r) {
    return models[std::min(N - 1, size_t(r * N))];
}

// Scatters doodads on jittered grids; every choice comes from Random(cell, seed) so the map is the same every run.
static std::vector<Doodad> Doodads() {
    std::vector<Doodad> out;
    auto add = [&](const char* model, float x, float y, float sink, float yaw, float scale) {
        out.push_back({ model, x, y, Height(x, y) - sink, yaw, scale });
    };
    auto scatter = [&](float cell, uint32_t seed, auto place) {
        int n = int(HALF / cell);
        for (int i = -n; i < n; i++) {
            for (int j = -n; j < n; j++) {
                float x = (i + Random(i, j, seed)) * cell;
                float y = (j + Random(i, j, seed + 1)) * cell;
                place(x, y, Random(i, j, seed + 2), Random(i, j, seed + 3), Random(i, j, seed + 4));
            }
        }
    };
    // Flat, dry ground away from the road, pond and clearing.
    auto open = [&](float x, float y, float margin) {
        return RoadDistance(x, y) > ROAD_HALF_WIDTH + margin && Distance(POND, x, y) > POND_RADIUS + margin &&
               Distance(CLEARING, x, y) > CLEARING_RADIUS + margin && FootprintDistance(INN, x, y) > 4 + margin &&
               Slope(x, y) < 0.6f && Height(x, y) < 70;
    };
    auto forest = [&](float x, float y) { return Smooth(-0.05f, 0.25f, Fbm(x / 130 + 7, y / 130 + 3)); };

    scatter(15, 100, [&](float x, float y, float r, float pick, float spin) {
        if (r < forest(x, y) * 0.85f && open(x, y, 5)) {
            bool high = Height(x, y) > 25 || pick < 0.2f;
            add(high ? Pick(PINES, pick) : Pick(TREES, pick), x, y, 0.3f, spin * 360, 0.8f + 0.5f * r);
        }
    });
    scatter(11, 200, [&](float x, float y, float r, float pick, float spin) {
        if (r < 0.1f + 0.25f * forest(x, y) && open(x, y, 2)) {
            add(Pick(BUSHES, pick), x, y, 0.1f, spin * 360, 0.8f + 0.6f * pick);
        }
    });
    scatter(23, 300, [&](float x, float y, float r, float pick, float spin) {
        if (r < 0.08f * forest(x, y) && open(x, y, 3)) {
            add(Pick(FOREST_FLOOR, pick), x, y, 0.2f, spin * 360, 1.0f);
        }
    });
    scatter(25, 400, [&](float x, float y, float r, float pick, float spin) {
        if (r < 0.35f && Slope(x, y) > 0.5f && FootprintDistance(INN, x, y) > 6) {
            add(Pick(CLIFF_ROCKS, pick), x, y, 1.0f, spin * 360, 0.6f + 0.6f * r);
        }
    });
    scatter(17, 500, [&](float x, float y, float r, float pick, float spin) {
        if (r < 0.06f && RoadDistance(x, y) > ROAD_HALF_WIDTH + 1 && Height(x, y) > WATER_LEVEL && FootprintDistance(INN, x, y) > 6) {
            add(Pick(SMALL_ROCKS, pick), x, y, 0.2f, spin * 360, 0.7f + 0.6f * r);
        }
    });

    // Lamp posts along the main road, alternating sides, none inside the clearing.
    const auto& road = ROADS[0];
    float next = 25;
    float walked = 0;
    bool left = true;
    for (size_t i = 0; i + 1 < road.size(); i++) {
        Point a = road[i], b = road[i + 1];
        float length = Distance(a, b.x, b.y);
        for (; next < walked + length; next += 50) {
            float t = (next - walked) / length;
            float x = Lerp(a.x, b.x, t), y = Lerp(a.y, b.y, t);
            float side = (left = !left) ? 1.0f : -1.0f;
            float px = x - (b.y - a.y) / length * 6.5f * side, py = y + (b.x - a.x) / length * 6.5f * side;
            if (Distance(CLEARING, px, py) > CLEARING_RADIUS) {
                add(LAMP_POST, px, py, 0.0f, 0, 1.0f);
            }
        }
        walked += length;
    }
    add(SIGN_POST, CLEARING.x + 7, CLEARING.y - 9, 0.0f, 30, 1.0f);
    add(MONUMENT, POND.x + POND_RADIUS * 0.9f, POND.y - POND_RADIUS * 0.7f, 0.5f, 210, 1.0f);
    return out;
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

static std::vector<uint8_t> Wdt() {
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
            bool tile = x >= FIRST_TILE && x <= LAST_TILE && y >= FIRST_TILE && y <= LAST_TILE;
            w.U32(tile ? 1 : 0); // 1 = this tile has an ADT
            w.U32(0);
        }
    }
    w.End(c);
    w.Empty("MWMO");
    return w.data;
}

static void Chunk(Writer& w, int tx, int ty, int cx, int cy, const std::vector<uint32_t>& refs, uint32_t wmoRefs) {
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
    field(0x38, wmoRefs);
    // 4x4 hole squares per chunk, bit (row * 4 + column), rows running south like the vertex rows
    uint16_t holes = 0;
    for (int r = 0; r < 4; r++) {
        for (int k = 0; k < 4; k++) {
            if (IsHole(top - (r + 0.5f) * CHUNK_SIZE / 4, left - (k + 0.5f) * CHUNK_SIZE / 4)) {
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
        w.F32(Height(xs[i], ys[i]));
    }
    w.End(c);

    field(0x18, here());
    c = w.Begin("MCNR");
    for (size_t i = 0; i < xs.size(); i++) {
        const float e = 0.5f;
        float nx = -(Height(xs[i] + e, ys[i]) - Height(xs[i] - e, ys[i])) / (2 * e);
        float ny = -(Height(xs[i], ys[i] + e) - Height(xs[i], ys[i] - e)) / (2 * e);
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
            Weights(top - (ay + 0.5f) * CHUNK_SIZE / 64, left - (ax + 0.5f) * CHUNK_SIZE / 64, weights);
            for (int l = 1; l < 4; l++) {
                alpha[l][ay * 64 + ax] = uint8_t(std::lround(weights[l] * 15));
            }
        }
    }
    std::vector<int> layers = { 0 };
    for (int l = 1; l < 4; l++) {
        if (std::any_of(alpha[l].begin(), alpha[l].end(), [](uint8_t v) { return v > 0; })) {
            layers.push_back(l);
        }
    }
    field(0x0C, uint32_t(layers.size()));

    field(0x1C, here());
    c = w.Begin("MCLY");
    for (size_t i = 0; i < layers.size(); i++) {
        // texture, flags (0x100 = has an alpha map), offset into MCAL, ground effect
        uint32_t layer[4] = { uint32_t(layers[i]), i ? 0x100u : 0u, i ? uint32_t((i - 1) * 2048) : 0u, TEXTURES[layers[i]].effect };
        w.Bytes(layer, sizeof(layer));
    }
    w.End(c);

    field(0x20, here());
    c = w.Begin("MCRF"); // indices into this tile's MDDF of the doodads standing in this chunk, then into MODF
    for (uint32_t ref : refs) {
        w.U32(ref);
    }
    for (uint32_t i = 0; i < wmoRefs; i++) {
        w.U32(i);
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

// Pond water: one flat instance per chunk whose terrain dips below the water level, with per-vertex depth.
static void Water(Writer& w, int tx, int ty) {
    size_t mh2o = w.Begin("MH2O");
    size_t base = w.Size(); // MH2O offsets are relative to its data
    w.Zeros(256 * 12);      // per chunk: offset of instances, instance count, offset of attributes
    for (int cy = 0; cy < 16; cy++) {
        for (int cx = 0; cx < 16; cx++) {
            float top = ORIGIN - (ty * 16 + cy) * CHUNK_SIZE;
            float left = ORIGIN - (tx * 16 + cx) * CHUNK_SIZE;
            uint8_t depth[81];
            uint8_t exists[8] = {};
            bool any = false;
            for (int i = 0; i <= 8; i++) {
                for (int j = 0; j <= 8; j++) {
                    float d = WATER_LEVEL - Height(top - i * UNIT, left - j * UNIT);
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

            uint16_t type[2] = { LIQUID_WATER, 0 }; // vertex format 0: height floats then depth bytes
            w.Bytes(type, 4);
            w.F32(WATER_LEVEL);
            w.F32(WATER_LEVEL);
            uint8_t rect[4] = { 0, 0, 8, 8 }; // x offset, y offset, width, height in cells
            w.Bytes(rect, 4);
            w.U32(bitmap);
            w.U32(vertices);

            w.Bytes("\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF", 8); // fishable everywhere
            w.Zeros(8);                                    // nowhere counts as deep water
            w.Bytes(exists, 8);
            for (int v = 0; v < 81; v++) {
                w.F32(WATER_LEVEL);
            }
            w.Bytes(depth, 81);
        }
    }
    w.End(mh2o);
}

static std::vector<uint8_t> Adt(int tx, int ty, const std::vector<Doodad>& all, uint32_t& uniqueId) {
    // Doodads standing on this tile, and the chunk each one is in.
    std::vector<const Doodad*> doodads;
    std::vector<std::vector<uint32_t>> refs(256);
    for (const Doodad& d : all) {
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
    for (const Texture& texture : TEXTURES) {
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

    // The inn's world bounding box: its rotated footprint corners, plus its height range.
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    for (float lx : { INN.lo[0], INN.hi[0] }) {
        for (float ly : { INN.lo[1], INN.hi[1] }) {
            Point p = ToWorld(INN, lx, ly);
            minX = std::min(minX, p.x), maxX = std::max(maxX, p.x), minY = std::min(minY, p.y), maxY = std::max(maxY, p.y);
        }
    }
    float tileTop = ORIGIN - ty * TILE_SIZE, tileLeft = ORIGIN - tx * TILE_SIZE;
    bool inn = minX < tileTop && maxX > tileTop - TILE_SIZE && minY < tileLeft && maxY > tileLeft - TILE_SIZE;

    c = w.Begin("MWMO");
    if (inn) {
        w.Bytes(INN.model, std::strlen(INN.model) + 1);
    }
    w.End(c);
    link(4, c);
    c = w.Begin("MWID");
    if (inn) {
        w.U32(0);
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
    if (inn) {
        float z = InnZ();
        w.U32(0); // name index
        w.U32(1); // unique id: the same in every tile the inn overlaps, so the client places it once
        // position, rotation, then the bounding box, all in placement axes (O - y, z, O - x)
        float placement[12] = { ORIGIN - INN.at.y, z, ORIGIN - INN.at.x, 0, INN.yaw, 0,
                                ORIGIN - maxY, z + INN.lo[2], ORIGIN - maxX, ORIGIN - minY, z + INN.hi[2], ORIGIN - minX };
        w.Bytes(placement, sizeof(placement));
        uint16_t flags[4] = { 0, INN.doodadSet, INN.nameSet, 0 };
        w.Bytes(flags, sizeof(flags));
    }
    w.End(c);
    link(7, c);

    size_t water = w.Size();
    Water(w, tx, ty);
    link(9, water);

    for (int cy = 0; cy < 16; cy++) {
        for (int cx = 0; cx < 16; cx++) {
            size_t start = w.Size();
            float top = tileTop - cy * CHUNK_SIZE, left = tileLeft - cx * CHUNK_SIZE;
            bool overlaps = inn && minX < top && maxX > top - CHUNK_SIZE && minY < left && maxY > left - CHUNK_SIZE;
            Chunk(w, tx, ty, cx, cy, refs[cy * 16 + cx], overlaps ? 1 : 0);
            size_t entry = mcin + 8 + (cy * 16 + cx) * 16;
            w.Set32(entry, uint32_t(start));
            w.Set32(entry + 4, uint32_t(w.Size() - start));
        }
    }
    return w.data;
}

// Copies the template row of Map.dbc to a new row with our id, folder and name; replaces the row if it exists.
static bool AddMapRow(const fs::path& in, const fs::path& out) {
    std::ifstream file(in, std::ios::binary);
    std::vector<uint8_t> dbc((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    uint32_t header[5];
    if (dbc.size() < sizeof(header)) {
        return false;
    }
    std::memcpy(header, dbc.data(), sizeof(header));
    uint32_t count = header[1], fields = header[2], recordSize = header[3], stringSize = header[4];
    if (header[0] != 0x43424457 || fields != 66 || recordSize != 264 || dbc.size() != 20 + count * recordSize + stringSize) {
        fprintf(stderr, "%s is not a 3.3.5 Map.dbc\n", in.string().c_str());
        return false;
    }
    std::vector<uint8_t> records(dbc.begin() + 20, dbc.begin() + 20 + count * recordSize);
    std::vector<uint8_t> strings(dbc.begin() + 20 + count * recordSize, dbc.end());
    auto get = [&](uint32_t row, uint32_t f) {
        uint32_t v;
        std::memcpy(&v, &records[row * recordSize + f * 4], 4);
        return v;
    };

    std::vector<uint8_t> row;
    uint32_t existing = count;
    for (uint32_t i = 0; i < count; i++) {
        if (get(i, 0) == TEMPLATE_MAP_ID) {
            row.assign(records.begin() + i * recordSize, records.begin() + (i + 1) * recordSize);
        }
        if (get(i, 0) == MAP_ID) {
            existing = i;
        }
    }
    if (row.empty()) {
        fprintf(stderr, "template map %u not in Map.dbc\n", TEMPLATE_MAP_ID);
        return false;
    }
    auto set = [&](uint32_t f, uint32_t v) { std::memcpy(&row[f * 4], &v, 4); };
    auto addString = [&](const char* s) {
        uint32_t at = uint32_t(strings.size());
        strings.insert(strings.end(), s, s + std::strlen(s) + 1);
        return at;
    };
    set(0, MAP_ID);
    set(1, addString(MAP_DIR));
    set(5, addString(MAP_NAME)); // enUS name; the other 15 locales stay empty
    for (uint32_t f = 6; f <= 20; f++) {
        set(f, 0);
    }
    set(22, 0); // no AreaTable entry yet

    if (existing < count) {
        std::copy(row.begin(), row.end(), records.begin() + existing * recordSize);
    } else {
        records.insert(records.end(), row.begin(), row.end());
        count++;
    }
    Writer w;
    uint32_t newHeader[5] = { header[0], count, fields, recordSize, uint32_t(strings.size()) };
    w.Bytes(newHeader, sizeof(newHeader));
    w.Bytes(records.data(), records.size());
    w.Bytes(strings.data(), strings.size());
    Save(out, w.data);
    return true;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: MapGen <outdir> <original Map.dbc>\n");
        return 1;
    }
    fs::path out = argv[1];
    fs::path maps = out / "World" / "Maps" / MAP_DIR;
    std::vector<Doodad> doodads = Doodads();
    uint32_t uniqueId = 1;
    Save(maps / (std::string(MAP_DIR) + ".wdt"), Wdt());
    for (int ty = FIRST_TILE; ty <= LAST_TILE; ty++) {
        for (int tx = FIRST_TILE; tx <= LAST_TILE; tx++) {
            Save(maps / (std::string(MAP_DIR) + "_" + std::to_string(tx) + "_" + std::to_string(ty) + ".adt"),
                 Adt(tx, ty, doodads, uniqueId));
        }
    }
    if (!AddMapRow(argv[2], out / "DBFilesClient" / "Map.dbc")) {
        return 1;
    }
    Point basement = ToWorld(INN, BASEMENT[0], BASEMENT[1]);
    printf("map %u '%s': %d tiles, %zu doodads, spawn %.1f %.1f %.1f, inn %.1f %.1f %.1f, basement %.2f %.2f %.2f\n", MAP_ID,
           MAP_DIR, (LAST_TILE - FIRST_TILE + 1) * (LAST_TILE - FIRST_TILE + 1), doodads.size(), CLEARING.x, CLEARING.y,
           Height(CLEARING.x, CLEARING.y), INN.at.x, INN.at.y, InnZ(), basement.x, basement.y, InnZ() + BASEMENT[2]);
    return 0;
}
