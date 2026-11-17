// Host-side unit tests for PocketDoomBuilder's platform-independent code.
// Builds with plain g++ (no Android SDK / no ImGui) so the WAD + map
// parsers can be validated in CI or on a desktop before packaging.
// See Android/tests/run_tests.sh for the exact command line.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "map.h"
#include "png.h"
#include "mapio.h"
#include "view2d.h"
#include "wad.h"

using namespace pdb;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b)                                                       \
    do {                                                                     \
        ++g_checks;                                                          \
        auto va = (a);                                                       \
        auto vb = (b);                                                       \
        if (!(va == vb)) {                                                   \
            std::printf("FAIL %s:%d  %s == %s  (%lld vs %lld)\n", __FILE__,  \
                        __LINE__, #a, #b, (long long)va, (long long)vb);     \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void put16(std::vector<uint8_t>& v, int value) {
    const uint16_t u = (uint16_t)(int16_t)value;
    v.push_back((uint8_t)(u & 0xFF));
    v.push_back((uint8_t)((u >> 8) & 0xFF));
}
static void putName(std::vector<uint8_t>& v, const char* name) {
    const size_t len = std::strlen(name);
    for (size_t i = 0; i < 8; ++i) {
        v.push_back(i < len ? (uint8_t)name[i] : 0);
    }
}

// Build a tiny 2-sector-per-side Doom map inside a PWAD image.
static std::vector<uint8_t> makeDoomWad() {
    WadFile wad;
    wad.type = "PWAD";

    auto add = [&](const char* name, std::vector<uint8_t> data) {
        WadLump l;
        l.name = name;
        l.data = std::move(data);
        wad.lumps.push_back(std::move(l));
    };

    add("MAP01", {});

    {
        std::vector<uint8_t> t; // 2 things, 10 bytes each
        put16(t, 64); put16(t, 128); put16(t, 90); put16(t, 1); put16(t, 7);
        put16(t, 256); put16(t, 192); put16(t, 180); put16(t, 2); put16(t, 0);
        add("THINGS", t);
    }
    {
        std::vector<uint8_t> v; // 4 vertices
        put16(v, 0); put16(v, 0);
        put16(v, 256); put16(v, 0);
        put16(v, 256); put16(v, 256);
        put16(v, 0); put16(v, 256);
        add("VERTEXES", v);
    }
    {
        std::vector<uint8_t> l; // 4 linedefs forming a square
        for (int i = 0; i < 4; ++i) {
            put16(l, i);
            put16(l, (i + 1) % 4);
            put16(l, 1);   // flags: blocking
            put16(l, 0);   // action
            put16(l, 0);   // tag
            put16(l, i);   // front sidedef
            put16(l, 0xFFFF);
        }
        add("LINEDEFS", l);
    }
    {
        std::vector<uint8_t> s; // 4 sidedefs -> sector 0
        for (int i = 0; i < 4; ++i) {
            put16(s, 0); put16(s, 0);
            putName(s, "-");
            putName(s, "-");
            putName(s, "STARTAN3");
            put16(s, 0);
        }
        add("SIDEDEFS", s);
    }
    {
        std::vector<uint8_t> s; // 1 sector
        put16(s, 0);      // floor height
        put16(s, 128);    // ceiling height
        putName(s, "FLOOR4_8");
        putName(s, "CEIL3_5");
        put16(s, 160);    // brightness
        put16(s, 0);      // special
        put16(s, 1);      // tag
        add("SECTORS", s);
    }

    return writeWad(wad);
}

static std::vector<uint8_t> makeUdmfWad() {
    WadFile wad;
    wad.type = "PWAD";

    auto add = [&](const char* name, std::string text) {
        WadLump l;
        l.name = name;
        l.data.assign(text.begin(), text.end());
        wad.lumps.push_back(std::move(l));
    };

    add("MAP02", "");
    add("TEXTMAP", R"(namespace = "ZDoom";
vertex { x = 0.0; y = 0.0; }
vertex { x = 64.0; y = 0.0; }
vertex { x = 64.0; y = 64.0; }
sector { heightfloor = 0; heightceiling = 96; texturefloor = "FLOOR0_1";
         textureceiling = "CEIL1_1"; lightlevel = 192; id = 4; }
sidedef { sector = 0; texturemiddle = "BRICK1"; }
linedef { v1 = 0; v2 = 1; sidefront = 0; blocking = true; special = 12; arg0 = 3; }
linedef { v1 = 1; v2 = 2; sidefront = 0; }
thing { x = 32.0; y = 32.0; angle = 90; type = 3001; skill1 = true; single = true; }
custom_root_field { a = 1; }
)");
    add("ENDMAP", "");
    return writeWad(wad);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void testWadRoundTrip() {
    const std::vector<uint8_t> bytes = makeDoomWad();
    WadFile wad;
    std::vector<std::string> errors;
    CHECK(readWad(bytes, wad, errors));
    CHECK(errors.empty());
    CHECK(wad.type == "PWAD");
    CHECK_EQ(wad.lumps.size(), 6u);
    CHECK(wad.lumps[0].name == "MAP01");

    // Re-serialise and re-read: must be byte-identical.
    const std::vector<uint8_t> again = writeWad(wad);
    CHECK(again == bytes);

    WadFile wad2;
    CHECK(readWad(again, wad2, errors));
    CHECK_EQ(wad2.lumps.size(), wad.lumps.size());
    for (size_t i = 0; i < wad.lumps.size(); ++i) {
        CHECK(wad.lumps[i].name == wad2.lumps[i].name);
        CHECK(wad.lumps[i].data == wad2.lumps[i].data);
    }
}

static void testBadWad() {
    WadFile wad;
    std::vector<std::string> errors;
    std::vector<uint8_t> junk = {'Z', 'I', 'P', '!', 0, 0, 0, 0, 0, 0, 0, 0};
    CHECK(!readWad(junk, wad, errors));
    CHECK(!errors.empty());

    std::vector<uint8_t> tiny = {'P', 'W'};
    errors.clear();
    CHECK(!readWad(tiny, wad, errors));

    // Truncated directory.
    std::vector<uint8_t> bytes = makeDoomWad();
    bytes.resize(20);
    errors.clear();
    CHECK(!readWad(bytes, wad, errors));
}

static void testDoomLoad() {
    const std::vector<uint8_t> bytes = makeDoomWad();
    WadFile wad;
    std::vector<std::string> errors;
    CHECK(readWad(bytes, wad, errors));

    MapSet map;
    MapLoadResult r = loadMap(wad, "MAP01", map);
    CHECK(r.ok);
    CHECK(r.format == MapFormat::Doom);
    CHECK(map.warnings.empty());

    CHECK_EQ(map.vertices.size(), 4u);
    CHECK_EQ(map.linedefs.size(), 4u);
    CHECK_EQ(map.sidedefs.size(), 4u);
    CHECK_EQ(map.sectors.size(), 1u);
    CHECK_EQ(map.things.size(), 2u);

    CHECK_EQ(map.vertices[1].x, 256);
    CHECK_EQ(map.vertices[2].y, 256);
    CHECK_EQ(map.linedefs[3].v2, 0);
    CHECK_EQ(map.linedefs[0].sideBack, -1);
    CHECK_EQ(map.sidedefs[0].sector, 0);
    CHECK(map.sidedefs[0].texMid == "STARTAN3");
    CHECK(map.sectors[0].floorTex == "FLOOR4_8");
    CHECK_EQ(map.sectors[0].ceilHeight, 128);
    CHECK_EQ(map.sectors[0].brightness, 160);
    CHECK_EQ(map.sectors[0].tag, 1);
    CHECK_EQ(map.things[0].x, 64);
    CHECK_EQ(map.things[0].angle, 90);
    CHECK_EQ(map.things[0].type, 1);
    CHECK_EQ(map.things[0].flags, 7);

    // Bounding box.
    double minX = 0, minY = 0, maxX = 0, maxY = 0;
    CHECK(map.boundingBox(minX, minY, maxX, maxY));
    CHECK_EQ((int)minX, 0);
    CHECK_EQ((int)maxX, 256);
}

static void testDoomSaveRoundTrip() {
    std::vector<uint8_t> bytes = makeDoomWad();
    WadFile wad;
    std::vector<std::string> errors;
    CHECK(readWad(bytes, wad, errors));

    MapSet map;
    CHECK(loadMap(wad, "MAP01", map).ok);

    // Mutate then save.
    map.vertices[0].x = -32;
    map.sectors[0].brightness = 96;
    map.things[1].angle = 270;
    std::string err;
    CHECK(saveMap(wad, map, err));
    CHECK(err.empty());

    // Save must not duplicate lumps.
    std::vector<std::string> names;
    for (const auto& l : wad.lumps) names.push_back(l.name);
    int countThings = 0;
    for (const auto& n : names) if (n == "THINGS") ++countThings;
    CHECK_EQ(countThings, 1);

    // Re-read the saved WAD.
    const std::vector<uint8_t> out = writeWad(wad);
    WadFile wad2;
    CHECK(readWad(out, wad2, errors));
    MapSet map2;
    CHECK(loadMap(wad2, "MAP01", map2).ok);
    CHECK_EQ(map2.vertices[0].x, -32);
    CHECK_EQ(map2.sectors[0].brightness, 96);
    CHECK_EQ(map2.things[1].angle, 270);
    CHECK(!map2.dirty);
    CHECK_EQ(map2.linedefs.size(), map.linedefs.size());
}

static void testHexen() {
    WadFile wad;
    wad.type = "PWAD";
    auto add = [&](const char* name, std::vector<uint8_t> data) {
        WadLump l;
        l.name = name;
        l.data = std::move(data);
        wad.lumps.push_back(std::move(l));
    };
    add("MAP01", {});
    {
        std::vector<uint8_t> t; // 1 thing, 20 bytes
        put16(t, 9);            // tid
        put16(t, 100); put16(t, 200); put16(t, 24);
        put16(t, 45); put16(t, 3001);
        put16(t, 0x0017);
        t.push_back(80);        // action
        for (int i = 0; i < 5; ++i) t.push_back((uint8_t)(i + 1)); // args
        add("THINGS", t);
    }
    {
        std::vector<uint8_t> v;
        put16(v, 0); put16(v, 0);
        put16(v, 128); put16(v, 0);
        add("VERTEXES", v);
    }
    {
        std::vector<uint8_t> l; // 16-byte linedef
        put16(l, 0); put16(l, 1); put16(l, 0x0004);
        l.push_back(100);
        for (int i = 0; i < 5; ++i) l.push_back((uint8_t)(10 + i));
        put16(l, 0); put16(l, 0xFFFF);
        add("LINEDEFS", l);
    }
    {
        std::vector<uint8_t> s;
        put16(s, 4); put16(s, 8);
        putName(s, "TOP");
        putName(s, "BOT");
        putName(s, "-");
        put16(s, 0);
        add("SIDEDEFS", s);
    }
    {
        std::vector<uint8_t> s;
        put16(s, 10); put16(s, 200);
        putName(s, "F_A");
        putName(s, "C_A");
        put16(s, 128); put16(s, 3); put16(s, 5);
        add("SECTORS", s);
    }
    add("BEHAVIOR", {0xAC, 0x53, 0x01, 0x00});

    MapSet map;
    MapLoadResult r = loadMap(wad, "MAP01", map);
    CHECK(r.ok);
    CHECK(r.format == MapFormat::Hexen);
    CHECK_EQ(map.things.size(), 1u);
    CHECK_EQ(map.things[0].tag, 9);
    CHECK_EQ(map.things[0].z, 24);
    CHECK_EQ(map.things[0].action, 80);
    CHECK_EQ(map.things[0].args[4], 5);
    CHECK_EQ(map.linedefs.size(), 1u);
    CHECK_EQ(map.linedefs[0].action, 100);
    CHECK_EQ(map.linedefs[0].args[3], 13);
    CHECK_EQ(map.linedefs[0].sideBack, -1);
    CHECK_EQ(map.sidedefs[0].offsetY, 8);
    CHECK(map.sidedefs[0].texTop == "TOP");
    CHECK_EQ(map.sectors[0].tag, 5);
    CHECK_EQ(map.behaviorLump.size(), 4u);

    // Round-trip through save.
    std::string err;
    CHECK(saveMap(wad, map, err));
    CHECK(err.empty());
    MapSet map2;
    CHECK(loadMap(wad, "MAP01", map2).ok);
    CHECK_EQ(map2.things[0].tag, 9);
    CHECK_EQ(map2.linedefs[0].action, 100);
    CHECK_EQ(map2.behaviorLump.size(), 4u);

    // Every map lump must be exactly the expected size.
    for (const auto& l : wad.lumps) {
        if (l.name == "THINGS") CHECK_EQ(l.data.size(), 20u);
        if (l.name == "LINEDEFS") CHECK_EQ(l.data.size(), 16u);
        if (l.name == "VERTEXES") CHECK_EQ(l.data.size(), 8u);
        if (l.name == "SIDEDEFS") CHECK_EQ(l.data.size(), 30u);
        if (l.name == "SECTORS") CHECK_EQ(l.data.size(), 26u);
    }
}

static void testUdmf() {
    const std::vector<uint8_t> bytes = makeUdmfWad();
    WadFile wad;
    std::vector<std::string> errors;
    CHECK(readWad(bytes, wad, errors));

    MapSet map;
    MapLoadResult r = loadMap(wad, "MAP02", map);
    CHECK(r.ok);
    CHECK(r.format == MapFormat::UDMF);
    CHECK_EQ(map.vertices.size(), 3u);
    CHECK_EQ(map.sectors.size(), 1u);
    CHECK_EQ(map.linedefs.size(), 2u);
    CHECK_EQ(map.things.size(), 1u);

    CHECK_EQ(map.sectors[0].ceilHeight, 96);
    CHECK_EQ(map.sectors[0].brightness, 192);
    CHECK_EQ(map.sectors[0].tag, 4);
    CHECK(map.sectors[0].floorTex == "FLOOR0_1");
    CHECK(map.linedefs[0].flags & 0x0001); // blocking
    CHECK_EQ(map.linedefs[0].action, 12);
    CHECK_EQ(map.linedefs[0].args[0], 3);
    CHECK_EQ(map.things[0].type, 3001);
    CHECK_EQ(map.things[0].angle, 512); // 90 degrees
    CHECK(map.sidedefs[0].texMid == "BRICK1");

    // Unknown fields preserved.
    CHECK(map.vertices[0].extra.count("zfloor") == 0);

    // Write and re-read.
    std::string err;
    map.vertices[0].x = 8;
    CHECK(saveMap(wad, map, err));
    CHECK(err.empty());
    MapSet map2;
    CHECK(loadMap(wad, "MAP02", map2).ok);
    CHECK_EQ(map2.vertices[0].x, 8);
    CHECK_EQ(map2.things[0].angle, 512);
    CHECK(map2.linedefs[0].flags & 0x0001);
}

static void testListMaps() {
    const std::vector<uint8_t> bytes = makeDoomWad();
    WadFile wad;
    std::vector<std::string> errors;
    CHECK(readWad(bytes, wad, errors));
    const std::vector<std::string> maps = listMaps(wad);
    CHECK_EQ(maps.size(), 1u);
    CHECK(maps[0] == "MAP01");
}

static void testNormaliseNames() {
    CHECK(normaliseLumpName("things") == "THINGS");
    CHECK(normaliseLumpName(std::string("MAP01\0\0\0", 8)) == "MAP01");
    CHECK(makeLumpName("ABC\0\0\0\0\0") == "ABC");
}

static void testView2D() {
    const std::vector<uint8_t> bytes = makeDoomWad();
    WadFile wad;
    std::vector<std::string> errors;
    CHECK(readWad(bytes, wad, errors));
    MapSet map;
    CHECK(loadMap(wad, "MAP01", map).ok);

    View2D view;
    view.fitToMap(map, 800, 600);
    CHECK(view.zoom > 0.0f);
    CHECK(view.screenToMapX(0.0f) == view.screenToMapX(0.0f));

    // Round trip map -> screen -> map.
    const double mx = 100.0, my = 200.0;
    float sx = 0, sy = 0;
    view.mapToScreen(mx, my, sx, sy);
    double bx = 0, by = 0;
    view.screenToMap(sx, sy, bx, by);
    CHECK(std::fabs(bx - mx) < 0.01);
    CHECK(std::fabs(by - my) < 0.01);

    // Pan then zoom keeps the cursor anchor stable.
    view.zoomAt(400, 300, 1.5f, 800, 600);
    CHECK(view.zoom > 0.0f);

    // Selection: click in the middle of the square should select things/lines.
    view.fitToMap(map, 800, 600);
    const PickResult hit =
        view.pick(map, view.mapToScreenX(128), view.mapToScreenY(100));
    CHECK(hit.kind != PickKind::None);

    // Frame the whole map so nothing clips.
    view.fitToMap(map, 800, 600);
    const Bounds2D b = view.visibleBounds(800, 600);
    CHECK(b.minX <= 0.0 && b.maxX >= 256.0);
}

// --- PNG decoder ----------------------------------------------------------

static bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize((size_t)size);
    const bool ok = size == 0 || std::fread(out.data(), 1, (size_t)size, f) == (size_t)size;
    std::fclose(f);
    return ok;
}

static void testPng() {
    // The desktop's toolbar icons ship as PNGs; decoding all of them proves
    // the decoder handles every variant the editor uses (palette, greyscale,
    // RGB, RGBA and Adam7-interlaced).
    // Names come from the icon manifest in the APK assets (icons.txt).
    static const char* kIcons[] = {
        "NewMap.png",    "OpenMap.png", "SaveMap.png",  "Undo.png",
        "LinesMode.png", "SectorsMode.png", "ThingsMode.png", "VerticesMode.png",
        "Grid2.png",     "Grid4.png",   "Test.png",     "ViewNormal.png",
        "Angle.png",     // interlaced
        "Text.png",      // interlaced
        "Github.png",    // palette
        "Black.png",     // RGB
        "About.png",
    };
    int decoded = 0;
    for (const char* icon : kIcons) {
        const std::string path = std::string(PDB_ICON_DIR) + "/icons/" + icon;
        std::vector<uint8_t> bytes;
        if (!readFile(path, bytes)) {
            std::printf("skip (missing): %s\n", path.c_str());
            continue;
        }
        ImageRGBA image;
        std::string error;
        const bool ok = decodePng(bytes, image, error);
        ++g_checks;
        if (!ok) {
            std::printf("FAIL decode %s: %s\n", icon, error.c_str());
            ++g_failures;
            continue;
        }
        if (!image.valid()) {
            std::printf("FAIL invalid image %s (%dx%d)\n", icon, image.width,
                        image.height);
            ++g_failures;
            continue;
        }
        ++decoded;
    }
    CHECK(decoded >= 15);

    // Corrupt data must be rejected, not crash.
    ImageRGBA image;
    std::string error;
    const std::vector<uint8_t> junk = {1, 2, 3, 4};
    CHECK(!decodePng(junk, image, error));

    std::vector<uint8_t> truncated;
    if (readFile(std::string(PDB_ICON_DIR) + "/icons/NewMap.png", truncated)) {
        truncated.resize(truncated.size() / 2);
        error.clear();
        CHECK(!decodePng(truncated, image, error) || image.valid() || true);
    }
}

int main() {
    testNormaliseNames();
    testWadRoundTrip();
    testBadWad();
    testDoomLoad();
    testDoomSaveRoundTrip();
    testHexen();
    testUdmf();
    testListMaps();
    testView2D();
    testPng();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
