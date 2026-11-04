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
    { "Tileset\\Tanaris\\TanarisSandBase01.blp", 0 },
    { "Tileset\\Tanaris\\TanarisCrackedGround.blp", 0 }, // room floors
    { "Tileset\\Tanaris\\TanarisSandStones.blp", 0 },    // passages
    { "Tileset\\Tanaris\\TanarisRockBase01.blp", 0 },    // the canyon walls
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

// Dry stone islands in Sebekhti's basin.
const Point ISLANDS[] = { { 18, -190 }, { -20, -195 }, { 5, -228 } };
const float ISLAND_RADIUS = 6;

// The open space nearest to a point: how far outside it the point is (0 inside), its floor there, and its area.
struct Open {
    float outside;
    float floor;
    Area area;
    bool passage;
};

// Where open spaces overlap (a passage starting inside a room, two segments at a bend) the point belongs to the one it
// is deepest inside, so the floor hands over where both have the same height instead of stepping.
Open Nearest(float x, float y) {
    float best = 1e9f; // signed: distance outside the space, negative inside
    Open open = { 0, 0, ZONE, false };
    for (const Room& room : ROOMS) {
        float d = Distance(room.at, x, y) - room.radius;
        if (d < best) {
            best = d;
            open = { 0, room.z, room.area, false };
        }
    }
    // Inside a bend the floors of the two segments differ a little; blending them by depth keeps it a slope.
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
        if (nearest < best) {
            best = nearest;
            open = { 0, weights > 0 ? weighted / weights : nearestFloor, p.area, true };
        }
    }
    open.outside = std::max(0.0f, best);
    return open;
}

float Height(float x, float y) {
    Open open = Nearest(x, y);
    float floor = open.floor + 0.3f * Fbm(x / 20 + 4, y / 20 - 6);
    for (Point island : ISLANDS) {
        floor = std::max(floor, Lerp(open.floor, 8.0f, Smooth(ISLAND_RADIUS + 3, ISLAND_RADIUS, Distance(island, x, y))));
    }
    float plateau = std::max(open.floor + 30, 78.0f) + 12 * Fbm(x / 60 + 2, y / 60 + 9);
    // Over 20 yards: much steeper and the terrain's 2-4 yard grid draws the walls as saw teeth.
    float wall = Smooth(0, 20, open.outside);
    return Lerp(floor, plateau, wall) + 5 * Fbm(x / 12 - 5, y / 12 + 1) * Smooth(0, 6, open.outside);
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
const char* OBELISK = "World\\KALIMDOR\\SILITHUS\\PASSIVEDOODADS\\AhnQirajStones\\Ahn_Qirajobelisk.m2";
const char* ALTAR = "World\\KALIMDOR\\SILITHUS\\PASSIVEDOODADS\\AhnQirajAltar\\AhnQirajAlter.m2";
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
            place(room.at.x + (room.radius - inset) * std::cos(a), room.at.y + (room.radius - inset) * std::sin(a));
        }
    };
    const Room& court = ROOMS[0];

    // The Sun Court: braziers between the five ways out (north, west, south, east and the stair at 45 degrees), an
    // obelisk either side of each wing's way, and the altar in the middle.
    ring(court, 8, 22.5f, 6, [&](float x, float y) { add(BRAZIER, x, y, 0.1f, Facing(x, y, court.at), 1.0f); });
    for (float way : { 0.0f, 90.0f, 180.0f, 270.0f }) {
        for (float side : { -13.0f, 13.0f }) {
            float a = (way + side) * PI / 180;
            add(OBELISK, 44 * std::cos(a), 44 * std::sin(a), 0.3f, way + 90, 0.8f);
        }
    }
    add(ALTAR, 0, 0, 0.2f, 180, 1.2f);

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
    ring(ROOMS[3], 6, 0, 5, [&](float x, float y) { add(OBELISK, x, y, 0.3f, Facing(x, y, ROOMS[3].at), 0.8f); });

    // The Hall of Weighing: coffins along the hall's walls, braziers around the Scales.
    ring(ROOMS[4], 6, 30, 4, [&](float x, float y) { add(BRAZIER, x, y, 0.1f, Facing(x, y, ROOMS[4].at), 1.0f); });
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

// Zul'Farrak's rows, turned into ours: the map, its zone and subzones, and its one difficulty.
void Rows(Dbcs& dbcs) {
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
    spec.rows = Rows;

    printf("map %u '%s': spawn %.1f %.1f %.2f; entrance on map 800 at %.1f %.1f %.2f\n", MAP_ID, MAP_DIR, SPAWN.x, SPAWN.y,
           Height(SPAWN.x, SPAWN.y), ENTRANCE.x, ENTRANCE.y, TestMapHeight(ENTRANCE.x, ENTRANCE.y));
    for (const Room& room : ROOMS) {
        printf("  room at %.0f %.0f floor %.2f\n", room.at.x, room.at.y, Height(room.at.x, room.at.y));
    }
    int steep = CheckRoutes();
    printf(steep ? "  %d places on the routes are too steep to walk\n" : "  every wing walkable from the Sun Court\n", steep);
    return spec;
}
