// The test map (800): rolling Elwynn-style hills ringed by unclimbable mountains, a road from the south to the north
// mountains through a clearing at the world origin (the spawn), a side path to a pond, forests, bushes and rocks, and
// the Goldshire inn with a basement.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "mapgen.h"

namespace {

const uint32_t MAP_ID = 800;
const uint32_t TEMPLATE_MAP_ID = 1; // Kalimdor: a plain continent, copied for every field we don't set
const char* MAP_DIR = "TestMap";
const char* MAP_NAME = "Test Map";
const int FIRST_TILE = 31; // tiles 31..32 on both axes, so the map is centred on world (0, 0)
const int LAST_TILE = 32;
const float HALF = (LAST_TILE - FIRST_TILE + 1) * TILE_SIZE / 2;

const std::vector<Texture> TEXTURES = {
    { "Tileset\\Elwynn\\ElwynnGrassBase.blp", 757, 0x5C7A2E },
    { "Tileset\\Elwynn\\ElwynnFlowerBase.blp", 761, 0x7A8A3A },
    { "Tileset\\Elwynn\\ElwynnDirtBase2.blp", 1106, 0x8A6E4A },
    { "Tileset\\Elwynn\\ElwynnRockBaseTest2.blp", 993, 0x7A7468 },
};

const uint16_t LIQUID_WATER = 1; // LiquidType.dbc
const float WATER_LEVEL = 2.0f;

const Point CLEARING = { 0, 0 };
const float CLEARING_RADIUS = 45;
const Point POND = { -40, 200 };
const float POND_RADIUS = 55;
const float ROAD_HALF_WIDTH = 4;
const std::vector<std::vector<Point>> ROADS = {
    { { -330, 30 }, { -220, -40 }, { -90, -20 }, { 0, 0 }, { 110, 50 }, { 220, 30 }, { 330, 100 } },
    { { 0, 0 }, { -20, 70 }, { -35, 130 } },
};

// The Lion's Pride Inn from Goldshire, with the furniture set and rotation Blizzard gave it there (Azeroth_31_49.adt).
// Turned to -90 (Goldshire has -97) so its walls follow the terrain grid, and placed so the stairwell lands exactly on
// one 8.33-yard terrain hole square: inn x -29.6..-21.27, y -8.4..-0.07 becomes world x 75..83.33, y -75..-66.67.
// Its z is filled in by InnZ.
Building INN = { "World\\wmo\\Azeroth\\Buildings\\GoldshireInn\\GoldshireInn.wmo",
                 { 74.933f, -45.4f }, 0, -90, 2, 2, { -37.8f, -13.5f, -6.8f }, { 21.9f, 18.7f, 23.3f } };
const float BASEMENT[3] = { -34.5f, -2.5f, -6.7f }; // open basement floor beside the foot of the stairs, inn coordinates
// Where the stairs down (inn y -8.6..-5.2, from x -21 at the ground floor to -33) pass through ground level.
const float STAIRWELL_LO[2] = { -29.6f, -8.4f }, STAIRWELL_HI[2] = { -21.27f, -0.07f };

float RoadDistance(float x, float y) {
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
float BaseHeight(float x, float y) {
    float edge = std::max(std::abs(x), std::abs(y)) / HALF; // 0 in the middle, 1 at the map border
    float mountains = 160 * Smooth(0.68f, 0.93f, edge) * (0.7f + 0.3f * Fbm(x / 90 + 3, y / 90 + 5));
    return 4 + 10 * (0.5f + 0.5f * Fbm(x / 170, y / 170)) + mountains;
}

// Terrain collides even inside buildings, so the hole squares over the stairwell are cut out of it.
bool IsHole(float x, float y) {
    Point l = ToLocal(INN, x, y);
    return l.x > STAIRWELL_LO[0] && l.x < STAIRWELL_HI[0] && l.y > STAIRWELL_LO[1] && l.y < STAIRWELL_HI[1];
}

// The inn's origin sits on the smoothed land; its outer walls reach 0.2 below the origin, its floors are above it.
float InnZ() {
    return BaseHeight(INN.at.x, INN.at.y);
}

// ponytail: terrain is formulas; read heightmap/mask images once zones are designed by hand
float Height(float x, float y) {
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

float Slope(float x, float y) {
    const float e = 0.5f;
    float dx = (Height(x + e, y) - Height(x - e, y)) / (2 * e);
    float dy = (Height(x, y + e) - Height(x, y - e)) / (2 * e);
    return std::sqrt(dx * dx + dy * dy);
}

// Alpha of texture layers 1..3 (flowers, dirt, rock) at a point; layer 0 (grass) is the base.
void Weights(float x, float y, float w[4]) {
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

const char* TREES[] = {
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Trees\\ElwynnTreeCanopy01.m2",
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Trees\\ElwynnTreeCanopy02.m2",
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Trees\\ElwynnTreeCanopy03.m2",
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Trees\\ElwynnTreeCanopy04.m2",
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Trees\\ElwynnTreeMid01.m2",
};
const char* PINES[] = {
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Trees\\ElwynnTree01\\ElwynnPine01.m2",
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Trees\\ElwynnTree01\\ElwynnPine02.m2",
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Trees\\ElwynnFirTree01.m2",
};
const char* BUSHES[] = {
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Bush\\ElwynnBush01.m2", "World\\Azeroth\\Elwynn\\PassiveDoodads\\Bush\\ElwynnBush02.m2",
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Bush\\ElwynnBush03.m2", "World\\Azeroth\\Elwynn\\PassiveDoodads\\Bush\\ElwynnBush04.m2",
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Bush\\ElwynnBush05.m2", "World\\Azeroth\\Elwynn\\PassiveDoodads\\Bush\\ElwynnBush06.m2",
};
const char* FOREST_FLOOR[] = {
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Tree\\ElwynnLog01.m2",
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Tree\\ElwynnLog02.m2",
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Trees\\Stumps\\ElwynnTreeStump01.m2",
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Trees\\Stumps\\ElwynnTreeStump02.m2",
};
const char* CLIFF_ROCKS[] = {
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\CliffRocks\\ElwynnCliffRock01.m2",
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\CliffRocks\\ElwynnCliffRock02.m2",
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\CliffRocks\\ElwynnCliffRock03.m2",
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\CliffRocks\\ElwynnCliffRock04.m2",
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\CliffRocks\\ElwynnCliffRock05.m2",
};
const char* SMALL_ROCKS[] = {
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Detail\\ElwynnRock1\\ElwynnRock1.m2",
    "World\\Azeroth\\Elwynn\\PassiveDoodads\\Detail\\ElwynnRock2\\ElwynnRock2.m2",
};
const char* LAMP_POST = "World\\Azeroth\\Elwynn\\PassiveDoodads\\LampPost\\LampPost.m2";
const char* SIGN_POST = "World\\Azeroth\\Elwynn\\PassiveDoodads\\Signs\\GenericPosts\\GenericSmallSignPost01.m2";
const char* MONUMENT = "World\\Azeroth\\Elwynn\\PassiveDoodads\\MonumentRock\\MonumentRock.m2";

template <size_t N>
const char* Pick(const char* (&models)[N], float r) {
    return models[std::min(N - 1, size_t(r * N))];
}

// Scatters doodads on jittered grids; every choice comes from Random(cell, seed) so the map is the same every run.
std::vector<Doodad> Doodads() {
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

// Kalimdor's row with our id, folder and name.
void Rows(Dbcs& dbcs) {
    Dbc& map = dbcs.map;
    std::vector<uint32_t> row = map.Row(uint32_t(map.Find(0, TEMPLATE_MAP_ID)));
    row[0] = MAP_ID;
    row[1] = map.AddString(MAP_DIR);
    row[5] = map.AddString(MAP_NAME); // enUS name; the other 15 locales stay empty
    for (uint32_t f = 6; f <= 20; f++) {
        row[f] = 0;
    }
    row[22] = 0; // no AreaTable entry yet
    map.Put(row);
}

} // namespace

float TestMapHeight(float x, float y) {
    return Height(x, y);
}

MapSpec TestMap() {
    INN.z = InnZ();
    MapSpec spec;
    spec.id = MAP_ID;
    spec.dir = MAP_DIR;
    spec.firstTile = FIRST_TILE;
    spec.lastTile = LAST_TILE;
    spec.textures = TEXTURES;
    spec.height = Height;
    spec.weights = Weights;
    spec.hole = IsHole;
    spec.water = [](float, float) { return WATER_LEVEL; };
    spec.area = [](float, float) { return 0u; };
    spec.liquidType = LIQUID_WATER;
    spec.doodads = Doodads();
    spec.buildings = { INN };
    spec.rows = Rows;
    spec.labels = { { "Clearing (spawn)", CLEARING }, { "Pond", POND }, { "Inn", INN.at } };

    Point basement = ToWorld(INN, BASEMENT[0], BASEMENT[1]);
    printf("map %u '%s': spawn %.1f %.1f %.1f, inn %.1f %.1f %.1f, basement %.2f %.2f %.2f\n", MAP_ID, MAP_DIR, CLEARING.x,
           CLEARING.y, Height(CLEARING.x, CLEARING.y), INN.at.x, INN.at.y, INN.z, basement.x, basement.y, INN.z + BASEMENT[2]);
    return spec;
}
