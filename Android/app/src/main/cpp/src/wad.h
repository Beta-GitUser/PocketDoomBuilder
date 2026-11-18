// PocketDoomBuilder — Android port (Dear ImGui)
// wad.h — WAD container read/write (Source/Core/IO/WAD.cs).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "map.h"

namespace pdb {

// Parse a complete WAD image held in memory. Returns false on a fatal
// error (bad magic, truncated directory); recoverable problems are
// reported in `errors` and parsing continues where possible.
bool readWad(const std::vector<uint8_t>& bytes, WadFile& out,
             std::vector<std::string>& errors);

// Serialise a WadFile back to a complete WAD image.
std::vector<uint8_t> writeWad(const WadFile& wad);

// Find the map-header lump for `mapName` (exact name match), or -1.
int findMapHeader(const WadFile& wad, const std::string& mapName);

// Collect the lump indices that belong to the map starting at header
// index `headerIdx`: the header itself plus the first occurrence of
// each known map lump after it. Used by both load and save.
std::vector<int> mapLumpIndices(const WadFile& wad, int headerIdx);

// Remove every lump belonging to the map at `headerIdx` except the
// header itself. Returns the header index (unchanged).
void removeMapLumps(WadFile& wad, int headerIdx);

// Insert (or replace) a lump belonging to the map at `headerIdx`.
// If a lump with `name` already exists after the header it is
// replaced in place; otherwise it is inserted right after the header
// (mirrors MapManager.RemoveSpecificLump + WAD.Insert on desktop).
void putMapLump(WadFile& wad, int headerIdx, const std::string& name,
                std::vector<uint8_t> data);

} // namespace pdb
