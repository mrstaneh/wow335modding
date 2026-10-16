// MapGen: writes the test map (a WDT plus one ADT tile) and adds its row to Map.dbc.
//   MapGen <outdir> <original Map.dbc>
// Output goes to <outdir>/World/Maps/TestMap/ and <outdir>/DBFilesClient/Map.dbc, ready for MpqTool pack.
// Format notes are from the client's own loader (WoW/Source/WorldClient/MapChunk.cpp, Map.cpp).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static const uint32_t MAP_ID = 800;
static const uint32_t TEMPLATE_MAP_ID = 1; // Kalimdor: a plain continent, copied for every field we don't set
static const char* MAP_DIR = "TestMap";
static const char* MAP_NAME = "Test Map";
static const int TILE_X = 32;
static const int TILE_Y = 32;
static const char* TEXTURES[] = { "Tileset\\Elwynn\\ElwynnGrassBase.blp", "Tileset\\Elwynn\\ElwynnDirtBase2.blp" };

static const float TILE_SIZE = 533.33333f;
static const float CHUNK_SIZE = TILE_SIZE / 16;
static const float UNIT = CHUNK_SIZE / 8;
static const float ORIGIN = 32 * TILE_SIZE; // world x/y of tile 0, chunk 0

// World axes: x points north and shrinks as the tile/chunk row grows, y points west and shrinks with the column.
static const float CENTER_X = ORIGIN - (TILE_Y + 0.5f) * TILE_SIZE;
static const float CENTER_Y = ORIGIN - (TILE_X + 0.5f) * TILE_SIZE;

// ponytail: terrain is a formula; read a heightmap image once real zones are made
static float Height(float x, float y) {
    float d2 = (x - CENTER_X) * (x - CENTER_X) + (y - CENTER_Y) * (y - CENTER_Y);
    return 45.0f * std::exp(-d2 / (2 * 90.0f * 90.0f)) + 3.0f * std::sin(x / 23.0f) * std::cos(y / 31.0f);
}

// Dirt on the hill top, grass elsewhere.
static float DirtAlpha(float x, float y) {
    float t = (Height(x, y) - 25.0f) / 12.0f;
    return t < 0 ? 0 : t > 1 ? 1 : t;
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
            w.U32(x == TILE_X && y == TILE_Y ? 1 : 0); // 1 = this tile has an ADT
            w.U32(0);
        }
    }
    w.End(c);
    w.Empty("MWMO");
    return w.data;
}

static void Chunk(Writer& w, int cx, int cy) {
    float top = ORIGIN - (TILE_Y * 16 + cy) * CHUNK_SIZE;
    float left = ORIGIN - (TILE_X * 16 + cx) * CHUNK_SIZE;
    size_t start = w.Begin("MCNK");
    size_t header = w.Size();
    w.Zeros(128);
    auto field = [&](size_t at, uint32_t v) { w.Set32(header + at, v); };
    auto here = [&]() { return uint32_t(w.Size() - start); }; // sub-chunk offsets count from the MCNK id

    field(0x04, cx);
    field(0x08, cy);
    field(0x0C, 2); // layers
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

    field(0x1C, here());
    c = w.Begin("MCLY");
    uint32_t layers[2][4] = { { 0, 0, 0, 0 }, { 1, 0x100, 0, 0 } }; // texture, flags (0x100 = has alpha), alpha offset, effect
    w.Bytes(layers, sizeof(layers));
    w.End(c);

    field(0x20, here());
    w.Empty("MCRF"); // no doodad or WMO references

    field(0x24, here());
    c = w.Begin("MCAL");
    // 64x64 alpha for layer 1, 4 bits per value, low nibble first
    for (int ay = 0; ay < 64; ay++) {
        for (int ax = 0; ax < 64; ax += 2) {
            uint8_t v[2];
            for (int k = 0; k < 2; k++) {
                float x = top - (ay + 0.5f) * CHUNK_SIZE / 64;
                float y = left - (ax + k + 0.5f) * CHUNK_SIZE / 64;
                v[k] = uint8_t(std::lround(DirtAlpha(x, y) * 15));
            }
            uint8_t b = uint8_t(v[0] | v[1] << 4);
            w.Bytes(&b, 1);
        }
    }
    w.End(c);
    field(0x28, uint32_t(w.Size() - c)); // sizeAlpha includes the 8-byte chunk header

    w.End(start);
}

static std::vector<uint8_t> Adt() {
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

    size_t mtex = w.Begin("MTEX");
    for (const char* texture : TEXTURES) {
        w.Bytes(texture, std::strlen(texture) + 1);
    }
    w.End(mtex);
    link(1, mtex);

    const char* empty[] = { "MMDX", "MMID", "MWMO", "MWID", "MDDF", "MODF" };
    for (int i = 0; i < 6; i++) {
        link(2 + i, w.Empty(empty[i]));
    }

    for (int cy = 0; cy < 16; cy++) {
        for (int cx = 0; cx < 16; cx++) {
            size_t start = w.Size();
            Chunk(w, cx, cy);
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
    Save(maps / (std::string(MAP_DIR) + ".wdt"), Wdt());
    Save(maps / (std::string(MAP_DIR) + "_" + std::to_string(TILE_X) + "_" + std::to_string(TILE_Y) + ".adt"), Adt());
    if (!AddMapRow(argv[2], out / "DBFilesClient" / "Map.dbc")) {
        return 1;
    }
    printf("map %u '%s': tile %d_%d, center %.1f %.1f height %.1f\n", MAP_ID, MAP_DIR, TILE_X, TILE_Y, CENTER_X, CENTER_Y,
           Height(CENTER_X, CENTER_Y));
    return 0;
}
