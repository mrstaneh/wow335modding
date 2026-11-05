// The Sanctum of the Drowned Sun (801), the raid of Mods/DrownedSun/DESIGN.md: a temple cut into a desert canyon. Open
// rooms and passages at their own floor heights; everywhere else rises in steep rock walls to the canyon's plateau.
//
//                      Eclipse Pit (north, serpent)
//                              |
//   Hall of Weighing --- SUN COURT --- Flooded Nave
//   (west, jackal)        |   \        (east, crocodile)
//                         |    Sanctum of the Unmade (north-west, below)
//                      Dawn Gallery (south, scarab)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "mapgen.h"

namespace {

const uint32_t MAP_ID = 801;
const char* MAP_DIR = "DrownedSun";
const char* MAP_NAME = "Sanctum of the Drowned Sun";
const uint32_t TEMPLATE_MAP_ID = 209;      // Zul'Farrak: a 5-player dungeon with a desert loading screen
const uint32_t TEMPLATE_AREA_ID = 1176;    // Zul'Farrak's zone: its flags, music and ambience
const uint32_t DIFFICULTY_ID = 754;        // MapDifficulty.dbc ends at 753
const uint32_t MAX_PLAYERS = 5;
const int FIRST_TILE = 31;
const int LAST_TILE = 32;
const float HALF = (LAST_TILE - FIRST_TILE + 1) * TILE_SIZE / 2;

// Where players arrive and where the entrance portal stands on the test map (TestMapHeight gives its z).
const Point SPAWN = { -38, 0 };
const Point ENTRANCE = { -25, -25 };

// AreaTable.dbc ends at 4987.
enum Area : uint32_t {
    ZONE = 5000,
    SUN_COURT = 5001,
    FLOODED_NAVE = 5002,
    DAWN_GALLERY = 5003,
    HALL_OF_WEIGHING = 5004,
    ECLIPSE_PIT = 5005,
    SANCTUM = 5006,
};
struct AreaName {
    uint32_t id;
    const char* name;
};
const AreaName AREAS[] = {
    { ZONE, MAP_NAME },
    { SUN_COURT, "The Sun Court" },
    { FLOODED_NAVE, "The Flooded Nave" },
    { DAWN_GALLERY, "The Dawn Gallery" },
    { HALL_OF_WEIGHING, "The Hall of Weighing" },
    { ECLIPSE_PIT, "The Eclipse Pit" },
    { SANCTUM, "Sanctum of the Unmade" },
};

const std::vector<Texture> TEXTURES = {
    { "Tileset\\Tanaris\\TanarisSandBase01.blp", 0, 0xD2B07A },
    { "Tileset\\Silithis\\SilithisBrick01.blp", 0, 0x7A6650 },            // room floors: sandstone flags
    { "Tileset\\Feralas\\FeralasRocksGround.blp", 0, 0xBC9866 },          // passages: cobbles
    { "Tileset\\ThousandNeedles\\ThousandNeedlesRock.blp", 0, 0xB07A58 }, // the canyon walls
};

const uint16_t LIQUID_WATER = 1;
const float NAVE_WATER = 6.5f; // over floors at 5: wading depth

struct Room {
    Point at;
    float radius, z;
    Area area;
};
const Room ROOMS[] = {
    { { 0, 0 }, 48, 20, SUN_COURT },
    { { 0, -115 }, 30, 5, FLOODED_NAVE },     // the Sluices
    { { 0, -205 }, 44, 5, FLOODED_NAVE },     // Sebekhti's basin
    { { -215, 0 }, 42, 52, DAWN_GALLERY },    // Khepret's arena, at the top of the Long Ramp
    { { 0, 112 }, 28, 20, HALL_OF_WEIGHING }, // the Scales
    { { 0, 200 }, 38, 20, HALL_OF_WEIGHING }, // Anpuet's hall
    { { 235, 0 }, 45, 0, ECLIPSE_PIT },       // Apepheth's pit
    { { 125, 125 }, 42, -10, SANCTUM },       // Merenkhaal's sanctum
};

// A passage: a line of points with a floor height at each, and a half width.
struct Passage {
    std::vector<Point> points;
    std::vector<float> z;
    float halfWidth;
    Area area;
};
const std::vector<Passage> PASSAGES = {
    { { { 0, -46 }, { 0, -85 } }, { 20, 5 }, 7, FLOODED_NAVE },   // down into the flood
    { { { 0, -145 }, { 0, -161 } }, { 5, 5 }, 8, FLOODED_NAVE },
    { { { -46, 0 }, { -173, 0 } }, { 20, 52 }, 9, DAWN_GALLERY }, // the Long Ramp
    { { { 0, 46 }, { 0, 84 } }, { 20, 20 }, 7, HALL_OF_WEIGHING },
    { { { 0, 140 }, { 0, 162 } }, { 20, 20 }, 7, HALL_OF_WEIGHING },
    { { { 46, 0 }, { 80, 25 }, { 115, -15 }, { 150, 15 }, { 190, 0 } }, { 20, 15, 10, 5, 0 }, 6, ECLIPSE_PIT }, // the Lightless Way
    { { { 34, 34 }, { 95, 95 } }, { 20, -10 }, 8, SANCTUM },      // the stair down
};

// Where the Sluices' three wheels stand around its edge (degrees, 0 north, 90 west), clear of the ways in and out.
const float SLUICE_WHEELS[] = { 35, 140, 190 }; // between the palms (Doodads: every 51 degrees from 10)

// Dry stone islands in Sebekhti's basin.
const Point ISLANDS[] = { { 18, -190 }, { -20, -195 }, { 5, -228 } };
const float ISLAND_RADIUS = 6;

// A room's edge at an angle (radians): its radius, pushed in and out by up to an eighth so no room is a circle.
float EdgeRadius(const Room& room, float angle) {
    return room.radius * (1 + 0.13f * Noise(1.6f * std::cos(angle) + room.at.x / 37, 1.6f * std::sin(angle) + room.at.y / 37));
}

// The open space nearest to a point: how far outside it the point is (0 inside), how far inside (0 outside), its floor
// there, its area, how far outside the nearest passage (negative inside), and the floors of all spaces blended by
// nearness, which the walls between them rise from.
struct Open {
    float outside, inside;
    float floor;
    Area area;
    bool passage;
    float passageOutside;
    float wallFloor;
};

// Where open spaces overlap (a passage starting inside a room, two segments at a bend) the point belongs to the one it
// is deepest inside, and its floor is theirs blended by how deep inside each it is, so it hands over without a step.
Open Nearest(float x, float y) {
    float best = 1e9f; // signed: distance outside the space, negative inside
    Open open = { 0, 0, 0, ZONE, false, 1e9f, 0 };
    float blended = 0, blendWeights = 0, deep = 0, deepWeights = 0;
    auto blend = [&](float d, float floor) {
        float w = 1 / std::pow(std::max(d, 0.0f) + 4, 4.0f);
        blended += w * floor;
        blendWeights += w;
        if (d < 0) {
            deep += -d * floor;
            deepWeights += -d;
        }
    };
    for (const Room& room : ROOMS) {
        float d = Distance(room.at, x, y) - EdgeRadius(room, std::atan2(y - room.at.y, x - room.at.x));
        blend(d, room.z);
        if (d < best) {
            best = d;
            open = { 0, 0, room.z, room.area, false, 1e9f, 0 };
        }
    }
    // Inside a bend the floors of the two segments differ a little; blending them by depth keeps it a slope.
    float passageOutside = 1e9f;
    for (const Passage& p : PASSAGES) {
        float nearest = 1e9f, nearestFloor = 0, weighted = 0, weights = 0;
        for (size_t i = 0; i + 1 < p.points.size(); i++) {
            Point a = p.points[i], b = p.points[i + 1];
            float dx = b.x - a.x, dy = b.y - a.y;
            float t = Clamp01(((x - a.x) * dx + (y - a.y) * dy) / (dx * dx + dy * dy));
            float d = Distance({ a.x + t * dx, a.y + t * dy }, x, y) - p.halfWidth;
            float floor = Lerp(p.z[i], p.z[i + 1], t);
            if (d < nearest) {
                nearest = d;
                nearestFloor = floor;
            }
            if (d < 0) {
                weighted += -d * floor;
                weights += -d;
            }
        }
        float floor = weights > 0 ? weighted / weights : nearestFloor;
        blend(nearest, floor);
        if (nearest < best) {
            best = nearest;
            open = { 0, 0, floor, p.area, true, 0, 0 };
        }
        passageOutside = std::min(passageOutside, nearest);
    }
    open.passageOutside = passageOutside;
    if (deepWeights > 0) {
        open.floor = deep / deepWeights;
    }
    open.outside = std::max(0.0f, best);
    open.inside = std::max(0.0f, -best);
    // At the foot of a wall its own space's floor, further out all of them blended, so walls between spaces at different
    // heights don't crease where the nearest space changes.
    open.wallFloor = Lerp(open.floor, blended / blendWeights, Smooth(0, 12, open.outside));
    return open;
}

const float PLATEAU = 90; // the canyon's rim, give or take 12

float Height(float x, float y) {
    Open open = Nearest(x, y);
    float floor = open.floor + 0.3f * Fbm(x / 20 + 4, y / 20 - 6);
    // The Nave's rooms: a dry stone ledge a few yards wide along the walls, the middle under the flood; it slopes down
    // into the passages.
    if (open.area == FLOODED_NAVE && !open.passage) {
        floor += 2.6f * Smooth(8, 4, open.inside) * Smooth(-1, 3, open.passageOutside);
    }
    for (Point island : ISLANDS) {
        floor = std::max(floor, Lerp(open.floor, 8.0f, Smooth(ISLAND_RADIUS + 3, ISLAND_RADIUS, Distance(island, x, y))));
    }
    if (open.outside == 0) {
        return floor;
    }
    // Walls in three rock terraces up to the plateau, wider where they rise further so every riser is about as steep:
    // much steeper and the terrain's 2-4 yard grid draws them as saw teeth. The shelves wander with noise.
    float plateau = PLATEAU + 12 * Fbm(x / 60 + 2, y / 60 + 9);
    float width = std::max(18.0f, (plateau - open.wallFloor) * 0.42f);
    float t = Clamp01(open.outside / width + 0.06f * Fbm(x / 25 - 3, y / 25 + 8));
    const float STEPS = 3;
    float k = std::min(std::floor(t * STEPS), STEPS - 1);
    float wall = (k + Smooth(0.2f, 0.85f, t * STEPS - k)) / STEPS;
    float base = Lerp(floor, open.wallFloor, Smooth(0, 12, open.outside));
    return Lerp(base, plateau, wall) + 3 * Fbm(x / 9 - 5, y / 9 + 1) * Smooth(0, 6, open.outside);
}

float Slope(float x, float y) {
    const float e = 0.5f;
    float dx = (Height(x + e, y) - Height(x - e, y)) / (2 * e);
    float dy = (Height(x, y + e) - Height(x, y - e)) / (2 * e);
    return std::sqrt(dx * dx + dy * dy);
}

// Cracked flagstones on room floors, sandy stones in the passages, rock on the walls; drifted sand everywhere else.
void Weights(float x, float y, float w[4]) {
    Open open = Nearest(x, y);
    float inside = Smooth(2, 0, open.outside) * (0.75f + 0.25f * Noise(x / 4, y / 4));
    w[0] = 1;
    w[1] = open.passage ? 0 : inside * Smooth(-0.35f, 0.15f, Fbm(x / 18 + 30, y / 18 - 12));
    w[2] = open.passage ? inside : 0;
    w[3] = Smooth(0.6f, 1.0f, Slope(x, y));
}

float Water(float x, float y) {
    return y < -60 && std::abs(x) < 80 ? NAVE_WATER : NO_WATER;
}

uint32_t AreaAt(float x, float y) {
    return Nearest(x, y).area;
}

const char* BRAZIER = "World\\Expansion02\\Doodads\\Ulduar\\UL_Brazier01.M2";
const char* BROKEN_BRAZIER = "World\\Expansion02\\Doodads\\ULDUAR\\UL_Brazier_Broken.M2";
const char* SCONCE = "World\\KALIMDOR\\SILITHUS\\PASSIVEDOODADS\\AhnQirajSconces\\AhnQirajSconce01.m2";
const char* COFFIN = "World\\KALIMDOR\\SILITHUS\\PASSIVEDOODADS\\AhnQiraCoffins\\AQ_coffinbase.m2";
const char* COFFIN_LIDS[] = {
    "World\\KALIMDOR\\SILITHUS\\PASSIVEDOODADS\\AhnQiraCoffins\\AQ_coffinlid01.m2",
    "World\\KALIMDOR\\SILITHUS\\PASSIVEDOODADS\\AhnQiraCoffins\\AQ_coffinlid02.m2",
};
const char* PALMS[] = {
    "World\\Expansion02\\Doodads\\Scholazar\\Trees\\SholazarPalm_Tree01.M2",
    "World\\Expansion02\\Doodads\\Scholazar\\Trees\\SholazarPalm_Tree02.M2",
    "World\\Expansion02\\Doodads\\Scholazar\\Trees\\SholazarPalm_Tree03.M2",
};
const char* PALM_LOG = "World\\Expansion02\\Doodads\\Scholazar\\Trees\\SholazarPalm_Log01.M2";
const char* BONES[] = {
    "World\\AZEROTH\\THEBLASTEDLANDS\\PASSIVEDOODADS\\BONES\\BlastedLandsBonePile02.m2",
    "World\\AZEROTH\\THEBLASTEDLANDS\\PASSIVEDOODADS\\BONES\\BlastedLandsSkull01.m2",
    "World\\AZEROTH\\THEBLASTEDLANDS\\PASSIVEDOODADS\\BONES\\BlastedLandsSpine01.m2",
    "World\\AZEROTH\\THEBLASTEDLANDS\\PASSIVEDOODADS\\BONES\\BlastedLandsbone01.m2",
};
const char* CONDUIT = "World\\Expansion02\\Doodads\\Azjol-Nerub\\Columns\\Azjol_ObeliskGold01.M2";
const char* SUN_DISC = "World\\KALIMDOR\\SILITHUS\\ACTIVEDOODADS\\Ahn_Quiraj_OssirianCrystal\\Ahn_quiraj_ossirianCrystal.m2";
const char* TITAN_OBELISK = "World\\Expansion02\\Doodads\\Generic\\Titan\\TI_Obelisk01.M2";
// Our own (Mods/DrownedSun/models, Tools/ModelGen)
const char* LOTUS_COLUMN = "World\\DrownedSun\\LotusColumn.m2"; // 9.6 tall
const char* SUN_OBELISK = "World\\DrownedSun\\SunObelisk.m2";   // 8.1 tall
const char* SUN_ALTAR = "World\\DrownedSun\\SunAltar.m2";       // 4.6 tall, its disc facing -y

template <size_t N>
const char* Pick(const char* (&models)[N], float r) {
    return models[std::min(N - 1, size_t(r * N))];
}

const float PI = 3.14159265f;

// Facing a point, in MDDF yaw degrees (0 faces +x... turned like the client turns doodads).
float Facing(float x, float y, Point target) {
    return std::atan2(target.y - y, target.x - x) * 180 / PI;
}

std::vector<Doodad> Doodads() {
    std::vector<Doodad> out;
    auto add = [&](const char* model, float x, float y, float sink, float yaw, float scale) {
        out.push_back({ model, x, y, Height(x, y) - sink, yaw, scale });
    };
    auto ring = [&](const Room& room, int count, float offset, float inset, auto place) {
        for (int i = 0; i < count; i++) {
            float a = (offset + 360.0f * i / count) * PI / 180;
            float r = EdgeRadius(room, a) - inset;
            place(room.at.x + r * std::cos(a), room.at.y + r * std::sin(a));
        }
    };
    const Room& court = ROOMS[0];

    // The Sun Court: braziers between the five ways out (north, west, south, east and the stair at 45 degrees) and the
    // sun altar in the middle, its disc facing the way in from the south, four lotus columns around it; the steles between
    // the ways are buildings.
    ring(court, 8, 22.5f, 6, [&](float x, float y) { add(BRAZIER, x, y, 0.1f, Facing(x, y, court.at), 1.0f); });
    add(SUN_ALTAR, 0, 0, 0.05f, 0, 1.0f);
    for (int i = 0; i < 4; i++) {
        float a = (45 + 90 * i) * PI / 180;
        add(LOTUS_COLUMN, 10 * std::cos(a), 10 * std::sin(a), 0.05f, 0, 1.0f);
    }

    // The Flooded Nave: palms on the banks and the basin's islands, drowned logs, bones in the shallows.
    for (const Room& room : { ROOMS[1], ROOMS[2] }) {
        ring(room, 7, 10, 4, [&](float x, float y) {
            add(Pick(PALMS, Random(int(x), int(y), 11)), x, y, 0.4f, Random(int(x), int(y), 12) * 360, 0.9f);
        });
    }
    for (Point island : ISLANDS) {
        add(PALMS[0], island.x, island.y, 0.3f, Random(int(island.x), int(island.y), 13) * 360, 1.1f);
    }
    add(PALM_LOG, -8, -120, 0.6f, 40, 1.0f);
    add(PALM_LOG, 12, -212, 0.6f, 160, 1.0f);

    // The Dawn Gallery: sconces up both sides of the Long Ramp, obelisks around Khepret's arena.
    for (float x = -60; x > -170; x -= 18) {
        add(SCONCE, x, 8, 0.0f, -90, 1.0f);
        add(SCONCE, x, -8, 0.0f, 90, 1.0f);
    }
    ring(ROOMS[3], 6, 30, 5, [&](float x, float y) { add(SUN_OBELISK, x, y, 0.1f, Facing(x, y, ROOMS[3].at), 1.5f); });

    // The Hall of Weighing: coffins along the hall's walls inside a ring of lotus columns (clear of the way in, at
    // 270 degrees), braziers around the Scales.
    ring(ROOMS[4], 6, 30, 4, [&](float x, float y) { add(BRAZIER, x, y, 0.1f, Facing(x, y, ROOMS[4].at), 1.0f); });
    ring(ROOMS[5], 8, 22.5f, 12, [&](float x, float y) { add(LOTUS_COLUMN, x, y, 0.05f, Facing(x, y, ROOMS[5].at), 1.3f); });
    ring(ROOMS[5], 10, 18, 5, [&](float x, float y) {
        float yaw = Facing(x, y, ROOMS[5].at);
        add(COFFIN, x, y, 0.1f, yaw, 1.0f);
        add(Pick(COFFIN_LIDS, Random(int(x), int(y), 21)), x, y, 0.1f, yaw, 1.0f);
    });

    // The Eclipse Pit: dead braziers along the Lightless Way and around the pit.
    const Passage& way = PASSAGES[5];
    for (size_t i = 1; i + 1 < way.points.size(); i++) {
        add(BROKEN_BRAZIER, way.points[i].x, way.points[i].y + 4, 0.0f, 0, 1.0f);
    }
    ring(ROOMS[6], 8, 0, 4, [&](float x, float y) { add(BROKEN_BRAZIER, x, y, 0.0f, Facing(x, y, ROOMS[6].at), 1.2f); });

    // The Sanctum of the Unmade: the four conduits on the diagonals, the cracked sun-disc in the middle, titan stones
    // between them.
    const Room& sanctum = ROOMS[7];
    ring(sanctum, 4, 45, 8, [&](float x, float y) { add(CONDUIT, x, y, 0.2f, Facing(x, y, sanctum.at), 1.5f); });
    ring(sanctum, 4, 0, 4, [&](float x, float y) { add(TITAN_OBELISK, x, y, 0.2f, Facing(x, y, sanctum.at), 0.8f); });
    add(SUN_DISC, sanctum.at.x, sanctum.at.y, -4.0f, 0, 2.0f);

    // Bones scattered over the floors, never in the passages' middle.
    int n = int(HALF / 9);
    for (int i = -n; i < n; i++) {
        for (int j = -n; j < n; j++) {
            float x = (i + Random(i, j, 31)) * 9, y = (j + Random(i, j, 32)) * 9;
            Open open = Nearest(x, y);
            if (open.outside == 0 && !open.passage && Random(i, j, 33) < 0.12f && Distance(court.at, x, y) > 10) {
                add(Pick(BONES, Random(i, j, 34)), x, y, 0.05f, Random(i, j, 35) * 360, 0.8f + 0.4f * Random(i, j, 36));
            }
        }
    }
    return out;
}

// A WMO and its bounding box from its MOHD, in its own coordinates.
struct Candidate {
    const char* model;
    float lo[3], hi[3];
};

// Ahn'Qiraj's dark stone arch: it spans its own y, so passages run along its x.
const Candidate GATE = { "World\\wmo\\Dungeon\\KL_AhnQiraj\\Ahn_Qiraj_object02.wmo", { -11.3f, -23.7f, -20.5f }, { 11.3f, 23.7f, 20.5f } };
// Standing glyph slabs, their carved faces towards their own +x.
const Candidate STELES[] = {
    { "World\\wmo\\Dungeon\\KL_AhnQiraj\\Ahn_Qiraj_columnD.wmo", { -11.8f, -15.7f, -0.5f }, { 11.4f, 10.8f, 58.3f } },
    { "World\\wmo\\Dungeon\\KL_AhnQiraj\\Ahn_Qiraj_columnE.wmo", { -11.0f, -15.6f, -1.8f }, { 13.9f, 11.3f, 58.7f } },
};
// Fallen and broken ones.
const Candidate FALLEN_B = { "World\\wmo\\Dungeon\\KL_AhnQiraj\\Ahn_Qiraj_columnB.wmo", { -21.0f, -18.9f, -1.0f }, { 25.7f, 12.4f, 22.0f } };
const Candidate FALLEN_C = { "World\\wmo\\Dungeon\\KL_AhnQiraj\\Ahn_Qiraj_columnC.wmo", { -24.1f, -40.6f, -10.6f }, { 25.1f, 11.8f, 25.1f } };
const Candidate FALLEN_H = { "World\\wmo\\Dungeon\\KL_AhnQiraj\\Ahn_Qiraj_columnH.wmo", { -27.1f, -22.1f, -3.3f }, { 12.9f, 12.1f, 58.3f } };

// A WMO with the middle of its footprint at a point, its own +x towards facing (degrees, 0 north, 90 west), its lowest
// point sunk below z.
Building Place(const Candidate& c, Point at, float facing, float z, float sink) {
    Building b = { c.model, at, z - c.lo[2] - sink, facing - 180, 0, 0, { c.lo[0], c.lo[1], c.lo[2] }, { c.hi[0], c.hi[1], c.hi[2] } };
    Point middle = ToWorld(b, (c.lo[0] + c.hi[0]) / 2, (c.lo[1] + c.hi[1]) / 2);
    b.at = { 2 * at.x - middle.x, 2 * at.y - middle.y };
    return b;
}

Point AtEdge(const Room& room, float degrees, float inset) {
    float a = degrees * PI / 180, r = EdgeRadius(room, a) - inset;
    return { room.at.x + r * std::cos(a), room.at.y + r * std::sin(a) };
}

std::vector<Building> Buildings() {
    std::vector<Building> out;
    // Arches over the wings' ways out of the Sun Court, a little way in so they don't crowd the court, and over the
    // ways into their last rooms.
    auto gate = [&](const Passage& p, bool atEnd) {
        Point a = p.points[atEnd ? p.points.size() - 1 : 0], b = p.points[atEnd ? p.points.size() - 2 : 1];
        float length = Distance(a, b.x, b.y), in = atEnd ? 3.0f : 12.0f;
        Point at = { a.x + (b.x - a.x) / length * in, a.y + (b.y - a.y) / length * in };
        // Its bottom beam under the floor, all along its depth where the passage slopes.
        float dx = (b.x - a.x) / length * (GATE.hi[0] - GATE.lo[0]) / 2, dy = (b.y - a.y) / length * (GATE.hi[0] - GATE.lo[0]) / 2;
        float h = Height(at.x, at.y), low = std::min(Height(at.x - dx, at.y - dy), Height(at.x + dx, at.y + dy));
        out.push_back(Place(GATE, at, std::atan2(b.y - a.y, b.x - a.x) * 180 / PI, h, 8 + std::max(0.0f, h - low)));
    };
    for (int i : { 0, 2, 3, 5, 6 }) {
        gate(PASSAGES[i], false);
    }
    for (int i : { 1, 2, 4, 5, 6 }) {
        gate(PASSAGES[i], true);
    }
    // The Sun Court: steles half sunk into its walls between the ways, facing in.
    const Room& court = ROOMS[0];
    int n = 0;
    for (float a : { 135.0f, 225.0f, 315.0f }) {
        Point at = AtEdge(court, a, -5);
        out.push_back(Place(STELES[n++ % 2], at, Facing(at.x, at.y, court.at), court.z, 1.0f));
    }
    // Toppled stones in the wings' last rooms, against their walls.
    out.push_back(Place(FALLEN_B, AtEdge(ROOMS[3], 150, 6), 70, ROOMS[3].z, 0.5f));
    out.push_back(Place(FALLEN_C, AtEdge(ROOMS[6], 115, 8), 200, ROOMS[6].z, 0.5f));
    out.push_back(Place(FALLEN_H, AtEdge(ROOMS[5], 60, 6), 300, ROOMS[5].z, 0.5f));
    return out;
}

// MAPGEN_GALLERY=1 Mods/mods.sh build: candidate buildings in two rows on the plateau, outside the raid, and a line
// "GALLERY {shot}" for each to put in Tools/WebClient/shots/Gallery.json for wowPhoto('Gallery').
const Candidate ROW_SOUTH[] = {
    { "World\\wmo\\Dungeon\\KL_AhnQiraj\\Ahn_Qiraj_columnA.wmo", { -41.6f, -73.3f, -10.3f }, { 11.5f, 14.6f, 25.4f } },
    { "World\\wmo\\Dungeon\\KL_AhnQiraj\\Ahn_Qiraj_columnB.wmo", { -21.0f, -18.9f, -1.0f }, { 25.7f, 12.4f, 22.0f } },
    { "World\\wmo\\Dungeon\\KL_AhnQiraj\\Ahn_Qiraj_columnC.wmo", { -24.1f, -40.6f, -10.6f }, { 25.1f, 11.8f, 25.1f } },
    { "World\\wmo\\Dungeon\\KL_AhnQiraj\\Ahn_Qiraj_columnD.wmo", { -11.8f, -15.7f, -0.5f }, { 11.4f, 10.8f, 58.3f } },
    { "World\\wmo\\Dungeon\\KL_AhnQiraj\\Ahn_Qiraj_columnE.wmo", { -11.0f, -15.6f, -1.8f }, { 13.9f, 11.3f, 58.7f } },
    { "World\\wmo\\Dungeon\\KL_AhnQiraj\\Ahn_Qiraj_columnF.wmo", { -13.3f, -11.5f, -0.9f }, { 13.3f, 11.5f, 41.5f } },
    { "World\\wmo\\Dungeon\\KL_AhnQiraj\\Ahn_Qiraj_columnG.wmo", { -12.6f, -32.1f, 0.5f }, { 11.5f, 14.5f, 60.7f } },
    { "World\\wmo\\Dungeon\\KL_AhnQiraj\\Ahn_Qiraj_columnH.wmo", { -27.1f, -22.1f, -3.3f }, { 12.9f, 12.1f, 58.3f } },
    { "World\\wmo\\Dungeon\\KL_AhnQiraj\\Ahn_Qiraj_object02.wmo", { -11.3f, -23.7f, -20.5f }, { 11.3f, 23.7f, 20.5f } },
    { "World\\wmo\\Kalimdor\\CollidableDoodads\\Silithus\\QuirajTower01.wmo", { -16.1f, 7.7f, -1.9f }, { -5.6f, 19.4f, 36.7f } },
    { "World\\wmo\\Kalimdor\\CollidableDoodads\\Silithus\\QuirajTower02.wmo", { -5.2f, -5.5f, -0.9f }, { 5.2f, 5.5f, 42.3f } },
    { "World\\wmo\\Kalimdor\\CollidableDoodads\\Silithus\\QuirajTower03.wmo", { -4.8f, -5.5f, -0.8f }, { 5.2f, 5.1f, 23.1f } },
    { "World\\wmo\\Kalimdor\\CollidableDoodads\\Silithus\\QuirajTowerBase.wmo", { -4.9f, -6.2f, 0.0f }, { 6.4f, 5.5f, 14.9f } },
    { "World\\wmo\\Northrend\\Buildings\\Titan\\TitanRoadTall01.wmo", { -19.1f, -24.0f, 0.0f }, { 19.1f, 24.0f, 102.5f } },
    { "World\\wmo\\Northrend\\Buildings\\Titan\\TI_CircleWall_End.wmo", { -13.9f, -31.4f, -75.0f }, { 13.9f, 31.4f, 75.0f } },
    { "World\\wmo\\Dungeon\\Ulduar\\Ulduar_Arch01.wmo", { -9.2f, -28.8f, -54.5f }, { 8.9f, 28.8f, 22.7f } },
    { "World\\wmo\\Dungeon\\Ulduar\\Ulduar_Arch02.wmo", { -9.2f, -19.9f, -54.5f }, { 8.9f, 18.3f, 22.7f } },
};
const Candidate ROW_NORTH[] = {
    { "World\\wmo\\Kalimdor\\Buildings\\TanarisRuins\\TanarisRuins01.wmo", { -42.3f, -64.1f, -12.1f }, { 45.8f, 76.3f, 20.7f } },
    { "World\\wmo\\Kalimdor\\Buildings\\TanarisRuins\\TanarisRuins02.wmo", { -88.9f, -65.6f, -11.6f }, { 82.9f, 60.8f, 23.9f } },
    { "World\\wmo\\Kalimdor\\Buildings\\TanarisRuins\\TanarisRuins04.wmo", { -87.5f, -84.1f, -12.4f }, { 74.8f, 90.8f, 58.0f } },
    { "World\\wmo\\Kalimdor\\Buildings\\TanarisRuins\\TanarisRuins15.wmo", { -32.6f, -35.1f, -5.8f }, { 27.4f, 32.3f, 22.6f } },
    { "World\\wmo\\Northrend\\Buildings\\Titan\\TI_Temple01.wmo", { -50.6f, -61.8f, -73.4f }, { 50.6f, 61.8f, 73.4f } },
    { "World\\wmo\\Northrend\\Buildings\\Titan\\TI_CircleWall.wmo", { -43.6f, -44.4f, -74.3f }, { 43.6f, 44.4f, 74.3f } },
    { "World\\wmo\\Northrend\\Buildings\\Titan\\Titan_Dome.wmo", { -33.4f, -31.2f, -28.2f }, { 33.4f, 31.2f, 29.6f } },
};

// Lays a row out along y from -460 with its middles on line x; prints a photo.js shot for each, looking at it from
// the raid's side (towards -x when facing is 180, +x when 0).
template <size_t N>
void Row(std::vector<Building>& out, const Candidate (&row)[N], float line, float facing, const char* prefix) {
    float y = -460;
    for (size_t i = 0; i < N; i++) {
        const Candidate& c = row[i];
        Building b = { c.model, { line - (c.lo[0] + c.hi[0]) / 2, y - c.lo[1] }, 0, -180, 0, 0,
                       { c.lo[0], c.lo[1], c.lo[2] }, { c.hi[0], c.hi[1], c.hi[2] } };
        b.z = Height(line, b.at.y + (c.lo[1] + c.hi[1]) / 2);
        out.push_back(b);
        float size = std::max({ c.hi[0] - c.lo[0], c.hi[1] - c.lo[1], c.hi[2] - std::max(c.lo[2], 0.0f) });
        float d = size * 1.1f + 15;
        float cy = b.at.y + (c.lo[1] + c.hi[1]) / 2, top = b.z + c.hi[2];
        float cx = line + (facing == 0 ? -1 : 1) * ((c.hi[0] - c.lo[0]) / 2 + d);
        float cz = b.z + (top - b.z) * 0.6f + d * 0.3f;
        float pitch = -std::atan2(cz - (b.z + (top - b.z) * 0.4f), d + (c.hi[0] - c.lo[0]) / 2) * 180 / PI;
        const char* name = std::strrchr(c.model, '\\') + 1;
        printf("GALLERY { \"name\": \"%s%02zu_%.*s\", \"map\": 801, \"x\": %.0f, \"y\": %.0f, \"z\": %.0f, \"yaw\": %.0f, \"pitch\": %.0f },\n",
               prefix, i, int(std::strlen(name) - 4), name, cx, cy, cz, facing, pitch);
        y += c.hi[1] - c.lo[1] + 18;
    }
}

std::vector<Building> Gallery() {
    std::vector<Building> out;
    Row(out, ROW_SOUTH, -420, 180, "s");
    Row(out, ROW_NORTH, 420, 0, "n");
    return out;
}

// The light: one drowned dusk all day long, in LightIntBand's order (DayNight::LoadLightingBands), as 0xRRGGBB.
const uint32_t DUSK[18] = {
    0xD8935A, // sunlight on everything: a low orange sun
    0x4A4C6A, // ambient: cool violet shade
    0x161B3A, // sky: top
    0x2F3060, //      middle
    0x6E4F72, //      towards the horizon
    0xC27A5E, //      above the horizon
    0xE8A468, //      horizon
    0x6E5560, // fog
    0x404050, // shadows
    0xFFB868, // sun
    0xFF8C48, // sun halo
    0x5A4258, // clouds: shade
    0xD08A6A, //         lit
    0x3A2E44, //         base
    0x2C5248, // water: river near
    0x10282A, //        river far
    0x2C5248, //        ocean near
    0x10282A, //        ocean far
};
const float FOG_END = 340;     // yards
const float FOG_START = 0.25f; // of the fog's end
const float CLOUDS = 0.35f;    // density
const float GLOW = 0.35f;      // bloom strength (CGxDeviceWebGl::PostProcessWorld)

// Zul'Farrak's light, turned into ours: a Light.dbc row for the whole map (no position) whose clear-weather params
// and their 18 colour and 6 float bands are new rows with constant values.
void Light(Dbcs& dbcs) {
    Dbc& lights = dbcs.light;
    int32_t template_ = -1;
    uint32_t lightId = 0;
    for (uint32_t i = 0; i < lights.Count(); i++) {
        lightId = std::max(lightId, lights.Get(i, 0) + 1);
        if (lights.Get(i, 1) == TEMPLATE_MAP_ID && !lights.Get(i, 2) && !lights.Get(i, 3) && !lights.Get(i, 4)) {
            template_ = int32_t(i);
        }
    }
    std::vector<uint32_t> light = lights.Row(uint32_t(template_ < 0 ? 0 : template_));
    uint32_t paramsId = 0;
    for (uint32_t i = 0; i < dbcs.lightParams.Count(); i++) {
        paramsId = std::max(paramsId, dbcs.lightParams.Get(i, 0) + 1);
    }
    std::vector<uint32_t> params = dbcs.lightParams.Row(uint32_t(dbcs.lightParams.Find(0, light[7])));
    params[0] = paramsId;
    params[1] = 0; // no sky highlight
    params[2] = 0; // no skybox
    std::memcpy(&params[4], &GLOW, 4);
    dbcs.lightParams.Put(params);
    light[0] = lightId;
    light[1] = MAP_ID;
    light[7] = paramsId; // clear weather; the others (underwater, storms, death) stay Zul'Farrak's

    for (uint32_t band = 0; band < 18; band++) {
        std::vector<uint32_t> row(34, 0);
        row[0] = band + 18 * paramsId - 17;
        row[1] = 1; // one value, at time 0
        row[18] = DUSK[band];
        dbcs.lightIntBand.Put(row);
    }
    const float floats[6] = { FOG_END * 36, FOG_START, 0, CLOUDS, 0, 0 };
    for (uint32_t band = 0; band < 6; band++) {
        std::vector<uint32_t> row(34, 0);
        row[0] = band + 6 * paramsId - 5;
        row[1] = 1;
        std::memcpy(&row[18], &floats[band], 4);
        dbcs.lightFloatBand.Put(row);
    }
    lights.Put(light);
}

// The raid's own models (Mods/DrownedSun/models, exported by Tools/ModelGen): GameObjectDisplayInfo rows, ids from
// 9700 (the originals end at 9624), with the bounding box the exporter prints.
struct ModelDisplay {
    uint32_t id;
    const char* model;
    float lo[3], hi[3];
};
const ModelDisplay MODEL_DISPLAYS[] = {
    { 9700, "World\\DrownedSun\\CanopicJar.mdx", { -0.32f, -0.32f, 0 }, { 0.32f, 0.32f, 1.17f } },
};

void ModelDisplays(Dbcs& dbcs) {
    for (const ModelDisplay& d : MODEL_DISPLAYS) {
        std::vector<uint32_t> row(19, 0); // id, model, 10 sounds, box min, box max, effect package
        row[0] = d.id;
        row[1] = dbcs.gameObjectDisplayInfo.AddString(d.model);
        std::memcpy(&row[12], d.lo, sizeof(d.lo));
        std::memcpy(&row[15], d.hi, sizeof(d.hi));
        dbcs.gameObjectDisplayInfo.Put(row);
    }
}

// Zul'Farrak's rows, turned into ours: the map, its zone and subzones, its one difficulty and its light.
void Rows(Dbcs& dbcs) {
    Light(dbcs);
    ModelDisplays(dbcs);
    Dbc& map = dbcs.map;
    std::vector<uint32_t> row = map.Row(uint32_t(map.Find(0, TEMPLATE_MAP_ID)));
    row[0] = MAP_ID;
    row[1] = map.AddString(MAP_DIR);
    row[5] = map.AddString(MAP_NAME);
    for (uint32_t f = 6; f <= 20; f++) {
        row[f] = 0; // other locales' names
    }
    row[22] = ZONE;
    for (uint32_t f = 23; f <= 56; f++) {
        row[f] = 0; // Zul'Farrak's two descriptions, all locales
    }
    row[59] = 800; // dying inside sends the corpse to the entrance on the test map
    float corpse[2] = { ENTRANCE.x, ENTRANCE.y };
    std::memcpy(&row[60], corpse, sizeof(corpse));
    row[63] = 2; // Wrath of the Lich King
    row[65] = MAX_PLAYERS;
    map.Put(row);

    Dbc& areas = dbcs.areaTable;
    uint32_t nextBit = 0;
    for (uint32_t i = 0; i < areas.Count(); i++) {
        uint32_t id = areas.Get(i, 0);
        if (id < ZONE || id > SANCTUM) {
            nextBit = std::max(nextBit, areas.Get(i, 3) + 1);
        }
    }
    std::vector<uint32_t> zone = areas.Row(uint32_t(areas.Find(0, TEMPLATE_AREA_ID)));
    for (const AreaName& a : AREAS) {
        std::vector<uint32_t> area = zone;
        area[0] = a.id;
        area[1] = MAP_ID;
        area[2] = a.id == ZONE ? 0 : ZONE;
        area[3] = nextBit++; // exploration bit
        area[11] = areas.AddString(a.name);
        for (uint32_t f = 12; f <= 26; f++) {
            area[f] = 0;
        }
        areas.Put(area);
    }

    Dbc& difficulties = dbcs.mapDifficulty;
    int32_t template_ = -1;
    for (uint32_t i = 0; i < difficulties.Count(); i++) {
        if (difficulties.Get(i, 1) == TEMPLATE_MAP_ID && difficulties.Get(i, 2) == 0) {
            template_ = int32_t(i);
        }
    }
    std::vector<uint32_t> difficulty = difficulties.Row(uint32_t(template_));
    difficulty[0] = DIFFICULTY_ID;
    difficulty[1] = MAP_ID;
    for (uint32_t f = 3; f <= 19; f++) {
        difficulty[f] = 0; // "you must be level ..." messages
    }
    difficulty[20] = 0; // no lockout
    difficulty[21] = MAX_PLAYERS;
    difficulty[22] = 0;
    difficulties.Put(difficulty);
}

// From the Sun Court through each wing to its last room, along the middle of the passages.
const std::vector<std::vector<Point>> ROUTES = {
    { { 0, 0 }, { 0, -46 }, { 0, -85 }, { 0, -115 }, { 0, -145 }, { 0, -161 }, { 0, -205 } },
    { { 0, 0 }, { -46, 0 }, { -173, 0 }, { -215, 0 } },
    { { 0, 0 }, { 0, 46 }, { 0, 84 }, { 0, 112 }, { 0, 140 }, { 0, 162 }, { 0, 200 } },
    { { 0, 0 }, { 46, 0 }, { 80, 25 }, { 115, -15 }, { 150, 15 }, { 190, 0 }, { 235, 0 } },
    { { 0, 0 }, { 34, 34 }, { 95, 95 }, { 125, 125 } },
};
const float WALKABLE = 0.9f; // rise per yard: about 42 degrees, below what the server lets players walk up

// Walks every route every half yard, down its middle and 3 yards either side, and reports any step too steep to walk.
// Returns the number of steep places.
int CheckRoutes() {
    int steep = 0;
    for (const auto& route : ROUTES) {
        for (size_t i = 0; i + 1 < route.size(); i++) {
            Point a = route[i], b = route[i + 1];
            float length = Distance(a, b.x, b.y);
            float sx = -(b.y - a.y) / length, sy = (b.x - a.x) / length; // sideways
            for (float side : { -3.0f, 0.0f, 3.0f }) {
                float last = Height(a.x + sx * side, a.y + sy * side);
                for (float d = 0.5f; d <= length; d += 0.5f) {
                    float x = Lerp(a.x, b.x, d / length) + sx * side, y = Lerp(a.y, b.y, d / length) + sy * side;
                    float h = Height(x, y);
                    if (std::abs(h - last) / 0.5f > WALKABLE && steep++ < 10) {
                        printf("  too steep to walk at %.1f %.1f (rise %.2f per yard)\n", x, y, std::abs(h - last) / 0.5f);
                    }
                    last = h;
                }
            }
        }
    }
    return steep;
}

} // namespace

MapSpec DrownedSun() {
    MapSpec spec;
    spec.id = MAP_ID;
    spec.dir = MAP_DIR;
    spec.firstTile = FIRST_TILE;
    spec.lastTile = LAST_TILE;
    spec.textures = TEXTURES;
    spec.height = Height;
    spec.weights = Weights;
    spec.hole = [](float, float) { return false; };
    spec.water = Water;
    spec.area = AreaAt;
    spec.liquidType = LIQUID_WATER;
    spec.doodads = Doodads();
    spec.buildings = Buildings();
    if (std::getenv("MAPGEN_GALLERY")) {
        std::vector<Building> gallery = Gallery();
        spec.buildings.insert(spec.buildings.end(), gallery.begin(), gallery.end());
    }
    spec.rows = Rows;
    spec.planMinX = -280;
    spec.planMaxX = 300;
    spec.planMinY = -270;
    spec.planMaxY = 270;
    spec.planStep = 0.5f;
    spec.labels = {
        { "Sun Court", ROOMS[0].at }, { "Sluices", ROOMS[1].at }, { "Sebekhti's basin", ROOMS[2].at },
        { "Khepret's arena", ROOMS[3].at }, { "Long Ramp", { -110, 0 } }, { "Scales", ROOMS[4].at },
        { "Anpuet's hall", ROOMS[5].at }, { "Apepheth's pit", ROOMS[6].at }, { "Lightless Way", { 115, -15 } },
        { "Sanctum of the Unmade", ROOMS[7].at }, { "spawn", SPAWN },
    };

    printf("map %u '%s': spawn %.1f %.1f %.2f; entrance on map 800 at %.1f %.1f %.2f\n", MAP_ID, MAP_DIR, SPAWN.x, SPAWN.y,
           Height(SPAWN.x, SPAWN.y), ENTRANCE.x, ENTRANCE.y, TestMapHeight(ENTRANCE.x, ENTRANCE.y));
    for (const Room& room : ROOMS) {
        printf("  room at %.0f %.0f floor %.2f\n", room.at.x, room.at.y, Height(room.at.x, room.at.y));
    }
    int steep = CheckRoutes();
    printf(steep ? "  %d places on the routes are too steep to walk\n" : "  every wing walkable from the Sun Court\n", steep);
    // Spawn points for Mods/sql/drownedsun.sql: the Sluices' wheels stand on the dry ledge.
    for (float a : SLUICE_WHEELS) {
        Point p = AtEdge(ROOMS[1], a, 4);
        printf("  sluice wheel at %.1f %.1f %.2f\n", p.x, p.y, Height(p.x, p.y));
    }
    for (Point island : ISLANDS) {
        printf("  island at %.1f %.1f %.2f\n", island.x, island.y, Height(island.x, island.y));
    }
    // The Lightless Way's braziers: the torch's at the court end, then one at each bend and one at the pit, beside the
    // way's middle (the broken brazier doodads stand 4 yards the other side).
    const Passage& lightless = PASSAGES[5];
    for (size_t i = 0; i < lightless.points.size(); i++) {
        Point p = { lightless.points[i].x + (i == 0 ? 6.0f : 0.0f), lightless.points[i].y - 4 };
        printf("  brazier at %.1f %.1f %.2f\n", p.x, p.y, Height(p.x, p.y));
    }
    // Where Apepheth keeps those it swallows: a corner of the plateau no way leads to.
    printf("  belly at %.1f %.1f %.2f\n", -470.0f, -470.0f, Height(-470, -470));
    return spec;
}
