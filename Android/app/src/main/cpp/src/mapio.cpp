// PocketDoomBuilder — Android port (Dear ImGui)
// mapio.cpp — Map format detection + dispatch, and WAD save.

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "mapio.h"

namespace pdb {

namespace {

bool mapHasLump(const WadFile& wad, int header, const char* name) {
    for (int i : mapLumpIndices(wad, header)) {
        if (wad.lumps[(size_t)i].name == name) return true;
    }
    return false;
}

} // namespace

MapLoadResult loadMap(const WadFile& wad, const std::string& mapName,
                      MapSet& out) {
    MapLoadResult res;
    out.clear();

    const int header = findMapHeader(wad, mapName);
    if (header < 0) {
        res.error = "Map '" + mapName + "' not found.";
        return res;
    }

    // Format detection mirrors DoomMapSetIO/HexenMapSetIO IdentifyMapFormat.
    if (mapHasLump(wad, header, "TEXTMAP")) {
        res.format = MapFormat::UDMF;
        res.ok = loadUdmfMap(wad, mapName, out, res.error, res.warnings);
    } else if (mapHasLump(wad, header, "BEHAVIOR")) {
        res.format = MapFormat::Hexen;
        res.ok = loadHexenMap(wad, mapName, out, res.error, res.warnings);
    } else {
        res.format = MapFormat::Doom;
        res.ok = loadDoomMap(wad, mapName, out, res.error, res.warnings);
    }

    out.warnings = res.warnings;
    if (res.ok) out.format = res.format;
    return res;
}

std::vector<std::string> listMaps(const WadFile& wad) {
    std::vector<std::string> maps;
    for (size_t i = 0; i < wad.lumps.size(); ++i) {
        const std::string& name = wad.lumps[i].name;
        // A map header is a lump followed directly by a lump named
        // THINGS or TEXTMAP.
        if (i + 1 >= wad.lumps.size()) continue;
        const std::string& next = wad.lumps[i + 1].name;
        if (next == "THINGS" || next == "TEXTMAP") {
            maps.push_back(name);
        }
    }
    return maps;
}

bool saveMap(WadFile& wad, MapSet& map, std::string& error) {
    error.clear();
    if (map.mapName.empty()) {
        error = "Map has no name; cannot save.";
        return false;
    }

    int header = findMapHeader(wad, map.mapName);
    if (header < 0) {
        // Create the map header lump at the end of the WAD.
        WadLump lump;
        lump.name = normaliseLumpName(map.mapName);
        wad.lumps.push_back(std::move(lump));
        header = (int)wad.lumps.size() - 1;
    }

    // Remove old data lumps so replaced lumps do not leave stale copies
    // (important when a map changes format).
    removeMapLumps(wad, header);

    switch (map.format) {
        case MapFormat::UDMF: {
            std::string err;
            std::vector<uint8_t> text = writeUdmfMap(map, err);
            if (!err.empty()) {
                error = err;
                return false;
            }
            putMapLump(wad, header, "TEXTMAP", std::move(text));
            putMapLump(wad, header, "ENDMAP", std::vector<uint8_t>());
            break;
        }
        case MapFormat::Hexen: {
            // Hexen binary layout: THINGS, LINEDEFS, SIDEDEFS, VERTEXES, SECTORS.
            std::string err;
            MapSet tmp = map;
            // writeHexenMap emits one concatenated buffer in the order
            // THINGS | VERTEXES | LINEDEFS | SIDEDEFS | SECTORS, so slice it.
            const std::vector<uint8_t> all = writeHexenMap(tmp, err);
            if (!err.empty()) {
                error = err;
                return false;
            }
            size_t ofs = 0;
            auto slice = [&](size_t bytes) {
                std::vector<uint8_t> part(all.begin() + (long)ofs,
                                          all.begin() + (long)(ofs + bytes));
                ofs += bytes;
                return part;
            };
            putMapLump(wad, header, "THINGS", slice(map.things.size() * 20));
            putMapLump(wad, header, "VERTEXES", slice(map.vertices.size() * 4));
            putMapLump(wad, header, "LINEDEFS", slice(map.linedefs.size() * 16));
            putMapLump(wad, header, "SIDEDEFS", slice(map.sidedefs.size() * 30));
            putMapLump(wad, header, "SECTORS", slice(map.sectors.size() * 26));
            // Preserve compiled ACS behaviour lump.
            if (!map.behaviorLump.empty()) {
                putMapLump(wad, header, "BEHAVIOR", map.behaviorLump);
            } else {
                putMapLump(wad, header, "BEHAVIOR", std::vector<uint8_t>());
            }
            break;
        }
        case MapFormat::Doom:
        default: {
            map.format = MapFormat::Doom;
            std::string err;
            const std::vector<uint8_t> all = writeDoomMap(map, err);
            if (!err.empty()) {
                error = err;
                return false;
            }
            size_t ofs = 0;
            auto slice = [&](size_t bytes) {
                std::vector<uint8_t> part(all.begin() + (long)ofs,
                                          all.begin() + (long)(ofs + bytes));
                ofs += bytes;
                return part;
            };
            putMapLump(wad, header, "THINGS", slice(map.things.size() * 10));
            putMapLump(wad, header, "VERTEXES", slice(map.vertices.size() * 4));
            putMapLump(wad, header, "LINEDEFS", slice(map.linedefs.size() * 14));
            putMapLump(wad, header, "SIDEDEFS", slice(map.sidedefs.size() * 30));
            putMapLump(wad, header, "SECTORS", slice(map.sectors.size() * 26));
            break;
        }
    }

    map.dirty = false;
    return true;
}

} // namespace pdb
