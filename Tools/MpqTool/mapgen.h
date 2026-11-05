// MapGen: the maps it writes describe themselves with a MapSpec; mapgen.cpp turns each into a WDT, ADT tiles and DBC
// rows. Format notes are from the client's own loaders (WoW/Source/WorldClient/MapChunk.cpp, MapArea.cpp, Map.cpp,
// WoW/Source/LiquidSystem/LiquidData.cpp).
#ifndef TOOLS_MPQTOOL_MAPGEN_H
#define TOOLS_MPQTOOL_MAPGEN_H

#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

static const float TILE_SIZE = 533.33333f;
static const float CHUNK_SIZE = TILE_SIZE / 16;
static const float UNIT = CHUNK_SIZE / 8;
static const float ORIGIN = 32 * TILE_SIZE; // world x/y of tile 0, chunk 0
static const float NO_WATER = -1e9f;

// World axes: x points north, y points west.
struct Point {
    float x, y;
};

// A terrain texture, the ground effect (grass blades, pebbles; GroundEffectTexture.dbc, 0 for none) it carries, and
// roughly its average colour (0xRRGGBB) for the plan view.
// ponytail: colours picked by eye; read the BLPs' smallest mipmap if the plans need to match closely
struct Texture {
    const char* file;
    uint32_t effect;
    uint32_t color;
};

// A name written on the plan view.
struct Label {
    const char* text;
    Point at;
};

struct Doodad {
    const char* model;
    float x, y, z, yaw, scale;
};

struct Building {
    const char* model;
    Point at;
    float z;
    float yaw;                 // degrees, as stored in MODF
    uint16_t doodadSet, nameSet;
    float lo[3], hi[3];        // bounding box from the WMO's MOHD, in its own coordinates
};

// A DBC file as rows of 32-bit fields and a string block.
struct Dbc {
    uint32_t fields = 0, recordSize = 0;
    std::vector<uint8_t> records, strings;

    uint32_t Count() const { return uint32_t(records.size() / recordSize); }
    uint32_t Get(uint32_t row, uint32_t field) const;
    int32_t Find(uint32_t field, uint32_t value) const; // first row whose field has this value, -1 if none
    std::vector<uint32_t> Row(uint32_t row) const;
    void Put(const std::vector<uint32_t>& row);         // replaces the row with the same id (field 0), else appends
    uint32_t AddString(const char* s);
};

struct Dbcs {
    Dbc map, areaTable, mapDifficulty;
    Dbc light, lightParams, lightIntBand, lightFloatBand; // a map's own light: Light.dbc row -> params -> bands
    Dbc gameObjectDisplayInfo;                          // displays for our own models (Tools/ModelGen)
};

struct MapSpec {
    uint32_t id;
    const char* dir;
    int firstTile, lastTile;                            // the square block of ADT tiles the map has
    std::vector<Texture> textures;                      // at most 4; layer 0 is the base everywhere
    std::function<float(float, float)> height;
    std::function<void(float, float, float[4])> weights; // alpha of texture layers 1..3 at a point (w[0] unused)
    std::function<bool(float, float)> hole;             // whether this point's terrain hole square is cut out
    std::function<float(float, float)> water;           // water surface over a chunk's centre, NO_WATER for none
    std::function<uint32_t(float, float)> area;         // AreaTable id of the chunk around a point
    uint16_t liquidType;                                // LiquidType.dbc
    std::vector<Doodad> doodads;
    std::vector<Building> buildings;
    std::function<void(Dbcs&)> rows;                    // its Map.dbc row and anything else it needs

    // The plan view (MapGen's third argument): the part of the map drawn, in world x (north) and y (west), and yards
    // per pixel. All zero draws the whole block of tiles.
    float planMinX = 0, planMaxX = 0, planMinY = 0, planMaxY = 0, planStep = 1;
    std::vector<Label> labels;
};

MapSpec TestMap();
MapSpec DrownedSun();
float TestMapHeight(float x, float y); // for things the raid places on the test map

float Clamp01(float t);
float Smooth(float e0, float e1, float v); // Hermite step from 0 at e0 to 1 at e1; e0 > e1 gives a falling step
float Lerp(float a, float b, float t);
float Distance(Point a, float x, float y);
float Random(int32_t x, int32_t y, uint32_t seed);
float Noise(float x, float y); // value noise in -1..1
float Fbm(float x, float y);   // 4 octaves of it
Point ToWorld(const Building& b, float lx, float ly);
Point ToLocal(const Building& b, float x, float y);
float FootprintDistance(const Building& b, float x, float y); // 0 inside the building's footprint

#endif
