// PocketDoomBuilder — Android port (Dear ImGui)
// mapio_binary.cpp — Doom and Hexen binary map lump codecs.
//
// Field layouts taken 1:1 from Source/Core/IO/DoomMapSetIO.cs and
// Source/Core/IO/HexenMapSetIO.cs (ReadBytes / WriteMap).

#include <cstdio>
#include <cstring>

#include "mapio.h"

namespace pdb {

namespace {

// --- little-endian primitive helpers ---------------------------------------

inline int16_t rdI16(const uint8_t* p) {
    return (int16_t)(uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
inline uint16_t rdU16(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
inline void wrI16(std::vector<uint8_t>& out, int v) {
    const uint16_t u = (uint16_t)(int16_t)v;
    out.push_back((uint8_t)(u & 0xFF));
    out.push_back((uint8_t)((u >> 8) & 0xFF));
}
inline void wrU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back((uint8_t)(v & 0xFF));
    out.push_back((uint8_t)((v >> 8) & 0xFF));
}

// 8-byte NUL-padded texture/lump name -> std::string (Lump.MakeNormalName).
std::string readTexName(const uint8_t* p) {
    char buf[9];
    std::memcpy(buf, p, 8);
    buf[8] = '\0';
    std::string s(buf);
    // Desktop keeps the raw name, trimming only trailing NULs; textures
    // that are "no texture" are stored as "-".
    for (char& c : s) c = (char)toupper((unsigned char)c);
    return s;
}

void writeTexName(std::vector<uint8_t>& out, const std::string& name) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(i < (int)name.size() ? (uint8_t)name[(size_t)i] : 0);
    }
}

// Returns the map's lump data, or nullptr with a message in `err`.
const std::vector<uint8_t>* mapLump(const WadFile& wad, const std::string& map,
                                    const char* lumpName, std::string& err) {
    const int header = findMapHeader(wad, map);
    if (header < 0) {
        err = "Map '" + map + "' not found in WAD.";
        return nullptr;
    }
    const std::vector<int> owned = mapLumpIndices(wad, header);
    for (int i : owned) {
        if (wad.lumps[(size_t)i].name == lumpName) {
            return &wad.lumps[(size_t)i].data;
        }
    }
    err = std::string("Map '") + map + "' is missing the " + lumpName + " lump.";
    return nullptr;
}

void checkSize(const std::vector<uint8_t>& data, size_t entrySize,
               const char* lumpName, std::vector<std::string>& warnings,
               size_t& count) {
    count = entrySize ? data.size() / entrySize : 0;
    if (entrySize && data.size() % entrySize != 0) {
        char buf[192];
        std::snprintf(buf, sizeof(buf),
                      "%s lump size %zu is not a multiple of %zu; trailing "
                      "bytes ignored.",
                      lumpName, data.size(), entrySize);
        warnings.push_back(buf);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Doom binary
// ---------------------------------------------------------------------------

bool loadDoomMap(const WadFile& wad, const std::string& mapName, MapSet& out,
                 std::string& error, std::vector<std::string>& warnings) {
    out.clear();
    out.mapName = normaliseLumpName(mapName);
    out.format = MapFormat::Doom;

    const std::vector<uint8_t>* vx = mapLump(wad, mapName, "VERTEXES", error);
    if (!vx) return false;
    const std::vector<uint8_t>* ld = mapLump(wad, mapName, "LINEDEFS", error);
    if (!ld) return false;
    const std::vector<uint8_t>* sd = mapLump(wad, mapName, "SIDEDEFS", error);
    if (!sd) return false;
    const std::vector<uint8_t>* sc = mapLump(wad, mapName, "SECTORS", error);
    if (!sc) return false;
    const std::vector<uint8_t>* th = mapLump(wad, mapName, "THINGS", error);
    if (!th) return false;

    size_t n = 0;

    // VERTEXES: 4 bytes each.
    checkSize(*vx, 4, "VERTEXES", warnings, n);
    out.vertices.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* p = vx->data() + i * 4;
        Vertex v;
        v.x = rdI16(p);
        v.y = rdI16(p + 2);
        out.vertices.push_back(v);
    }

    // SECTORS: 26 bytes each.
    checkSize(*sc, 26, "SECTORS", warnings, n);
    out.sectors.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* p = sc->data() + i * 26;
        Sector s;
        s.floorHeight = rdI16(p);
        s.ceilHeight = rdI16(p + 2);
        s.floorTex = readTexName(p + 4);
        s.ceilTex = readTexName(p + 12);
        s.brightness = rdI16(p + 20);
        s.special = rdU16(p + 22);
        s.tag = rdU16(p + 24);
        out.sectors.push_back(s);
    }

    // SIDEDEFS: 30 bytes each.
    checkSize(*sd, 30, "SIDEDEFS", warnings, n);
    out.sidedefs.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* p = sd->data() + i * 30;
        Sidedef s;
        s.offsetX = rdI16(p);
        s.offsetY = rdI16(p + 2);
        s.texTop = readTexName(p + 4);
        s.texBottom = readTexName(p + 12);
        s.texMid = readTexName(p + 20);
        const uint16_t sec = rdU16(p + 28);
        s.sector = sec == 0xFFFF ? -1 : (int)sec;
        out.sidedefs.push_back(s);
    }

    // LINEDEFS: 14 bytes each.
    checkSize(*ld, 14, "LINEDEFS", warnings, n);
    out.linedefs.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* p = ld->data() + i * 14;
        Linedef l;
        l.v1 = rdU16(p);
        l.v2 = rdU16(p + 2);
        l.flags = rdU16(p + 4);
        l.action = rdU16(p + 6);
        l.tag = rdU16(p + 8);
        const uint16_t s1 = rdU16(p + 10);
        const uint16_t s2 = rdU16(p + 12);
        l.sideFront = s1 == 0xFFFF ? -1 : (int)s1;
        l.sideBack = s2 == 0xFFFF ? -1 : (int)s2;
        out.linedefs.push_back(l);
    }

    // THINGS: 10 bytes each (x, y, angle, type, flags).
    checkSize(*th, 10, "THINGS", warnings, n);
    out.things.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* p = th->data() + i * 10;
        Thing t;
        t.x = rdI16(p);
        t.y = rdI16(p + 2);
        t.angle = rdU16(p + 4);
        t.type = rdU16(p + 6);
        t.flags = rdU16(p + 8);
        out.things.push_back(t);
    }

    // Validate indices (index 0 is valid, 0xFFFF means "none").
    for (size_t i = 0; i < out.linedefs.size(); ++i) {
        const Linedef& l = out.linedefs[i];
        if (l.v1 < 0 || (size_t)l.v1 >= out.vertices.size() ||
            l.v2 < 0 || (size_t)l.v2 >= out.vertices.size()) {
            char buf[192];
            std::snprintf(buf, sizeof(buf),
                          "Linedef %zu references a non-existent vertex "
                          "(v1=%d, v2=%d).",
                          i, l.v1, l.v2);
            warnings.push_back(buf);
        }
        for (int side : {l.sideFront, l.sideBack}) {
            if (side >= 0 && (size_t)side >= out.sidedefs.size()) {
                char buf[192];
                std::snprintf(buf, sizeof(buf),
                              "Linedef %zu references a non-existent sidedef "
                              "(%d).",
                              i, side);
                warnings.push_back(buf);
            }
        }
    }
    for (size_t i = 0; i < out.sidedefs.size(); ++i) {
        const Sidedef& s = out.sidedefs[i];
        if (s.sector >= 0 && (size_t)s.sector >= out.sectors.size()) {
            char buf[192];
            std::snprintf(buf, sizeof(buf),
                          "Sidedef %zu references a non-existent sector (%d).",
                          i, s.sector);
            warnings.push_back(buf);
        }
    }
    return true;
}

std::vector<uint8_t> writeDoomMap(const MapSet& map, std::string& error) {
    error.clear();
    std::vector<uint8_t> out;

    // THINGS
    for (const Thing& t : map.things) {
        wrI16(out, t.x);
        wrI16(out, t.y);
        wrU16(out, (uint16_t)t.angle);
        wrU16(out, (uint16_t)t.type);
        wrU16(out, t.flags);
    }
    // VERTEXES
    for (const Vertex& v : map.vertices) {
        wrI16(out, v.x);
        wrI16(out, v.y);
    }
    // LINEDEFS
    for (const Linedef& l : map.linedefs) {
        wrU16(out, (uint16_t)l.v1);
        wrU16(out, (uint16_t)l.v2);
        wrU16(out, l.flags);
        wrU16(out, (uint16_t)l.action);
        wrU16(out, l.tag);
        wrU16(out, l.sideFront < 0 ? 0xFFFF : (uint16_t)l.sideFront);
        wrU16(out, l.sideBack < 0 ? 0xFFFF : (uint16_t)l.sideBack);
    }
    // SIDEDEFS
    for (const Sidedef& s : map.sidedefs) {
        wrI16(out, s.offsetX);
        wrI16(out, s.offsetY);
        writeTexName(out, s.texTop);
        writeTexName(out, s.texBottom);
        writeTexName(out, s.texMid);
        wrU16(out, s.sector < 0 ? 0xFFFF : (uint16_t)s.sector);
    }
    // SECTORS
    for (const Sector& s : map.sectors) {
        wrI16(out, s.floorHeight);
        wrI16(out, s.ceilHeight);
        writeTexName(out, s.floorTex);
        writeTexName(out, s.ceilTex);
        wrI16(out, s.brightness);
        wrU16(out, s.special);
        wrU16(out, s.tag);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Hexen binary
// ---------------------------------------------------------------------------

bool loadHexenMap(const WadFile& wad, const std::string& mapName, MapSet& out,
                  std::string& error, std::vector<std::string>& warnings) {
    out.clear();
    out.mapName = normaliseLumpName(mapName);
    out.format = MapFormat::Hexen;

    const std::vector<uint8_t>* vx = mapLump(wad, mapName, "VERTEXES", error);
    if (!vx) return false;
    const std::vector<uint8_t>* ld = mapLump(wad, mapName, "LINEDEFS", error);
    if (!ld) return false;
    const std::vector<uint8_t>* sd = mapLump(wad, mapName, "SIDEDEFS", error);
    if (!sd) return false;
    const std::vector<uint8_t>* sc = mapLump(wad, mapName, "SECTORS", error);
    if (!sc) return false;
    const std::vector<uint8_t>* th = mapLump(wad, mapName, "THINGS", error);
    if (!th) return false;

    size_t n = 0;

    // VERTEXES: 4 bytes.
    checkSize(*vx, 4, "VERTEXES", warnings, n);
    out.vertices.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* p = vx->data() + i * 4;
        Vertex v;
        v.x = rdI16(p);
        v.y = rdI16(p + 2);
        out.vertices.push_back(v);
    }

    // SECTORS: 26 bytes (same field layout as Doom).
    checkSize(*sc, 26, "SECTORS", warnings, n);
    out.sectors.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* p = sc->data() + i * 26;
        Sector s;
        s.floorHeight = rdI16(p);
        s.ceilHeight = rdI16(p + 2);
        s.floorTex = readTexName(p + 4);
        s.ceilTex = readTexName(p + 12);
        s.brightness = rdI16(p + 20);
        s.special = rdU16(p + 22);
        s.tag = rdU16(p + 24);
        out.sectors.push_back(s);
    }

    // SIDEDEFS: 30 bytes (same as Doom).
    checkSize(*sd, 30, "SIDEDEFS", warnings, n);
    out.sidedefs.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* p = sd->data() + i * 30;
        Sidedef s;
        s.offsetX = rdI16(p);
        s.offsetY = rdI16(p + 2);
        s.texTop = readTexName(p + 4);
        s.texBottom = readTexName(p + 12);
        s.texMid = readTexName(p + 20);
        const uint16_t sec = rdU16(p + 28);
        s.sector = sec == 0xFFFF ? -1 : (int)sec;
        out.sidedefs.push_back(s);
    }

    // LINEDEFS: 16 bytes — v1, v2, flags, action(u8), args[5], front, back.
    checkSize(*ld, 16, "LINEDEFS", warnings, n);
    out.linedefs.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* p = ld->data() + i * 16;
        Linedef l;
        l.v1 = rdU16(p);
        l.v2 = rdU16(p + 2);
        l.flags = rdU16(p + 4);
        l.action = p[6];
        for (int a = 0; a < 5; ++a) l.args[a] = p[7 + a];
        const uint16_t s1 = rdU16(p + 12);
        const uint16_t s2 = rdU16(p + 14);
        l.sideFront = s1 == 0xFFFF ? -1 : (int)s1;
        l.sideBack = s2 == 0xFFFF ? -1 : (int)s2;
        out.linedefs.push_back(l);
    }

    // THINGS: 20 bytes — tid, x, y, z, angle, type, flags, action, args[5].
    checkSize(*th, 20, "THINGS", warnings, n);
    out.things.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* p = th->data() + i * 20;
        Thing t;
        t.tag = rdU16(p);
        t.x = rdI16(p + 2);
        t.y = rdI16(p + 4);
        t.z = rdI16(p + 6);
        t.angle = rdI16(p + 8);
        t.type = rdI16(p + 10);
        t.flags = rdU16(p + 12);
        t.action = p[14];
        for (int a = 0; a < 5; ++a) t.args[a] = p[15 + a];
        out.things.push_back(t);
    }

    // Preserve the BEHAVIOR (compiled ACS) lump for a lossless save.
    if (const WadLump* beh = wad.findLumpPtr("BEHAVIOR", findMapHeader(wad, mapName))) {
        const std::vector<int> owned = mapLumpIndices(wad, findMapHeader(wad, mapName));
        for (int i : owned) {
            if (wad.lumps[(size_t)i].name == "BEHAVIOR") {
                out.behaviorLump = wad.lumps[(size_t)i].data;
                break;
            }
        }
        (void)beh;
    }

    // Index validation (same rules as Doom).
    for (size_t i = 0; i < out.linedefs.size(); ++i) {
        const Linedef& l = out.linedefs[i];
        if (l.v1 < 0 || (size_t)l.v1 >= out.vertices.size() ||
            l.v2 < 0 || (size_t)l.v2 >= out.vertices.size()) {
            char buf[192];
            std::snprintf(buf, sizeof(buf),
                          "Linedef %zu references a non-existent vertex "
                          "(v1=%d, v2=%d).",
                          i, l.v1, l.v2);
            warnings.push_back(buf);
        }
    }
    for (size_t i = 0; i < out.sidedefs.size(); ++i) {
        const Sidedef& s = out.sidedefs[i];
        if (s.sector >= 0 && (size_t)s.sector >= out.sectors.size()) {
            char buf[192];
            std::snprintf(buf, sizeof(buf),
                          "Sidedef %zu references a non-existent sector (%d).",
                          i, s.sector);
            warnings.push_back(buf);
        }
    }
    return true;
}

std::vector<uint8_t> writeHexenMap(const MapSet& map, std::string& error) {
    error.clear();
    std::vector<uint8_t> out;

    // THINGS: 20 bytes.
    for (const Thing& t : map.things) {
        wrU16(out, t.tag);
        wrI16(out, t.x);
        wrI16(out, t.y);
        wrI16(out, t.z);
        wrI16(out, t.angle);
        wrI16(out, t.type);
        wrU16(out, t.flags);
        out.push_back((uint8_t)(t.action & 0xFF));
        for (int a = 0; a < 5; ++a) out.push_back((uint8_t)(t.args[a] & 0xFF));
    }
    // VERTEXES.
    for (const Vertex& v : map.vertices) {
        wrI16(out, v.x);
        wrI16(out, v.y);
    }
    // LINEDEFS: 16 bytes.
    for (const Linedef& l : map.linedefs) {
        wrU16(out, (uint16_t)l.v1);
        wrU16(out, (uint16_t)l.v2);
        wrU16(out, l.flags);
        out.push_back((uint8_t)(l.action & 0xFF));
        for (int a = 0; a < 5; ++a) out.push_back((uint8_t)(l.args[a] & 0xFF));
        wrU16(out, l.sideFront < 0 ? 0xFFFF : (uint16_t)l.sideFront);
        wrU16(out, l.sideBack < 0 ? 0xFFFF : (uint16_t)l.sideBack);
    }
    // SIDEDEFS: 30 bytes.
    for (const Sidedef& s : map.sidedefs) {
        wrI16(out, s.offsetX);
        wrI16(out, s.offsetY);
        writeTexName(out, s.texTop);
        writeTexName(out, s.texBottom);
        writeTexName(out, s.texMid);
        wrU16(out, s.sector < 0 ? 0xFFFF : (uint16_t)s.sector);
    }
    // SECTORS: 26 bytes.
    for (const Sector& s : map.sectors) {
        wrI16(out, s.floorHeight);
        wrI16(out, s.ceilHeight);
        writeTexName(out, s.floorTex);
        writeTexName(out, s.ceilTex);
        wrI16(out, s.brightness);
        wrU16(out, s.special);
        wrU16(out, s.tag);
    }
    return out;
}

} // namespace pdb
