// PocketDoomBuilder — Android port (Dear ImGui)
// wad.cpp — WAD container read/write.

#include "wad.h"

#include <cstdio>
#include <cstring>

namespace pdb {

namespace {

uint32_t readU32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

void appendU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back((uint8_t)(v & 0xFF));
    out.push_back((uint8_t)((v >> 8) & 0xFF));
    out.push_back((uint8_t)((v >> 16) & 0xFF));
    out.push_back((uint8_t)((v >> 24) & 0xFF));
}

void appendFixedName(std::vector<uint8_t>& out, const std::string& name) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(i < (int)name.size() ? (uint8_t)name[(size_t)i] : 0);
    }
}

// Map lump names the editor understands; order used when creating a
// fresh map (matches MapSetIO.WriteMap order loosely and is not
// significant for reading, but keeps new maps tidy).
const char* const kMapLumpNames[] = {
    "THINGS",   "LINEDEFS", "SIDEDEFS", "VERTEXES", "SEGS", "SSECTORS",
    "NODES",    "SECTORS",  "REJECT",   "BLOCKMAP", "BEHAVIOR", "TEXTMAP",
    "ZNODES",   "DIALOGUE", "ENDMAP",   "SCRIPTS",  "LEAFS",    "GL_NODES",
    "GL_SEGS",  "GL_SSECT", "GL_PVS",
};

bool isMapLumpName(const std::string& n) {
    for (const char* k : kMapLumpNames) {
        if (n == k) return true;
    }
    return false;
}

} // namespace

bool readWad(const std::vector<uint8_t>& bytes, WadFile& out,
             std::vector<std::string>& errors) {
    out.clear();
    if (bytes.size() < 12) {
        errors.push_back("File is too small to be a WAD.");
        return false;
    }

    const uint8_t* p = bytes.data();
    const char magic0 = (char)p[0], magic1 = (char)p[1], magic2 = (char)p[2];
    if (!((magic0 == 'I' || magic0 == 'P') && magic1 == 'W' && magic2 == 'A')) {
        errors.push_back("Not a WAD file (bad magic bytes).");
        return false;
    }
    out.type.assign(1, magic0);
    out.type += "WAD";

    const uint32_t numLumps = readU32(p + 4);
    const uint32_t dirOfs = readU32(p + 8);

    if (numLumps == 0) {
        // Empty WAD: header only. Legal, nothing to do.
        return true;
    }
    if ((uint64_t)dirOfs + (uint64_t)numLumps * 16ull > (uint64_t)bytes.size()) {
        errors.push_back("WAD directory points past the end of the file.");
        return false;
    }

    out.lumps.reserve(numLumps);
    for (uint32_t i = 0; i < numLumps; ++i) {
        const uint8_t* e = p + dirOfs + (size_t)i * 16;
        const uint32_t ofs = readU32(e);
        const uint32_t size = readU32(e + 4);
        WadLump lump;
        lump.name = makeLumpName((const char*)(e + 8));
        if ((uint64_t)ofs + (uint64_t)size > (uint64_t)bytes.size()) {
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                          "Lump '%s' data extends past end of file; truncated.",
                          lump.name.c_str());
            errors.push_back(buf);
            lump.data.clear();
        } else if (size > 0) {
            lump.data.assign(p + ofs, p + ofs + size);
        }
        out.lumps.push_back(std::move(lump));
    }
    return true;
}

std::vector<uint8_t> writeWad(const WadFile& wad) {
    std::vector<uint8_t> out;
    const uint32_t numLumps = (uint32_t)wad.lumps.size();

    // Header placeholder (patched after we know the directory offset).
    out.push_back((uint8_t)(wad.type.empty() ? 'P' : wad.type[0]));
    out.push_back('W');
    out.push_back('A');
    out.push_back('D');
    appendU32(out, numLumps);
    appendU32(out, 0);

    std::vector<uint32_t> offsets(numLumps, 0);
    for (uint32_t i = 0; i < numLumps; ++i) {
        offsets[i] = (uint32_t)out.size();
        const auto& data = wad.lumps[i].data;
        out.insert(out.end(), data.begin(), data.end());
    }

    const uint32_t dirOfs = (uint32_t)out.size();
    for (uint32_t i = 0; i < numLumps; ++i) {
        appendU32(out, offsets[i]);
        appendU32(out, (uint32_t)wad.lumps[i].data.size());
        appendFixedName(out, wad.lumps[i].name);
    }

    // Patch the directory offset in the header.
    out[8] = (uint8_t)(dirOfs & 0xFF);
    out[9] = (uint8_t)((dirOfs >> 8) & 0xFF);
    out[10] = (uint8_t)((dirOfs >> 16) & 0xFF);
    out[11] = (uint8_t)((dirOfs >> 24) & 0xFF);
    return out;
}

int findMapHeader(const WadFile& wad, const std::string& mapName) {
    const std::string want = normaliseLumpName(mapName);
    for (size_t i = 0; i < wad.lumps.size(); ++i) {
        if (wad.lumps[i].name == want) return (int)i;
    }
    return -1;
}

std::vector<int> mapLumpIndices(const WadFile& wad, int headerIdx) {
    std::vector<int> result;
    if (headerIdx < 0 || headerIdx >= (int)wad.lumps.size()) return result;
    result.push_back(headerIdx);

    // Scan forward, stopping at the next map header (any lump that is
    // not a known map lump and is followed by THINGS, or simply any
    // unknown lump name that looks like a map name).
    std::vector<bool> seen;
    for (size_t i = 0; i < wad.lumps.size(); ++i) seen.push_back(false);

    for (int i = headerIdx + 1; i < (int)wad.lumps.size(); ++i) {
        const std::string& n = wad.lumps[(size_t)i].name;
        if (!isMapLumpName(n)) break; // start of the next map / other data
        if (seen[(size_t)i]) break;
        seen[(size_t)i] = true;
        result.push_back(i);
    }
    return result;
}

void removeMapLumps(WadFile& wad, int headerIdx) {
    if (headerIdx < 0 || headerIdx >= (int)wad.lumps.size()) return;
    // Collect by name first (indices shift while erasing).
    std::vector<std::string> names;
    for (int i : mapLumpIndices(wad, headerIdx)) {
        if (i != headerIdx) names.push_back(wad.lumps[(size_t)i].name);
    }
    for (const std::string& name : names) {
        int idx = wad.findLump(name, headerIdx + 1);
        if (idx > headerIdx) wad.lumps.erase(wad.lumps.begin() + idx);
    }
}

void putMapLump(WadFile& wad, int headerIdx, const std::string& name,
                std::vector<uint8_t> data) {
    if (headerIdx < 0 || headerIdx >= (int)wad.lumps.size()) return;
    const std::string n = normaliseLumpName(name);
    const std::vector<int> owned = mapLumpIndices(wad, headerIdx);
    for (int i : owned) {
        if (i != headerIdx && wad.lumps[(size_t)i].name == n) {
            wad.lumps[(size_t)i].data = std::move(data);
            return;
        }
    }
    WadLump lump;
    lump.name = n;
    lump.data = std::move(data);
    wad.lumps.insert(wad.lumps.begin() + headerIdx + 1, std::move(lump));
}

} // namespace pdb
