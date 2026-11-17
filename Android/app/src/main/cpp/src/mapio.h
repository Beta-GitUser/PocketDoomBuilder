// PocketDoomBuilder — Android port (Dear ImGui)
// mapio.h — Map lump serialisation for all desktop-supported formats.
//
// Desktop equivalents:
//   Source/Core/IO/DoomMapSetIO.cs   -> Doom binary
//   Source/Core/IO/HexenMapSetIO.cs  -> Hexen binary
//   Source/Core/IO/UniversalMapSetIO.cs (+ UniversalStreamReader/Writer)
//                                    -> UDMF / TEXTMAP
//
// Format detection matches DoomMapSetIO/HexenMapSetIO IdentifyMapFormat:
//   BEHAVIOR lump present          -> Hexen binary
//   TEXTMAP lump present           -> UDMF
//   otherwise                      -> Doom binary

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "map.h"
#include "wad.h"

namespace pdb {

struct MapLoadResult {
    bool ok = false;
    MapFormat format = MapFormat::Unknown;
    std::string error;
    std::vector<std::string> warnings;
};

// Detect and load the map whose header lump is `mapName`.
MapLoadResult loadMap(const WadFile& wad, const std::string& mapName,
                      MapSet& out);

// Write `map` back into `wad`, creating the map header if necessary.
// Returns false and sets `error` on failure.
bool saveMap(WadFile& wad, MapSet& map, std::string& error);

// --- individual format codecs (exposed for unit tests) ----------------------

bool loadDoomMap(const WadFile& wad, const std::string& mapName, MapSet& out,
                 std::string& error, std::vector<std::string>& warnings);
bool loadHexenMap(const WadFile& wad, const std::string& mapName, MapSet& out,
                  std::string& error, std::vector<std::string>& warnings);
bool loadUdmfMap(const WadFile& wad, const std::string& mapName, MapSet& out,
                 std::string& error, std::vector<std::string>& warnings);

std::vector<uint8_t> writeDoomMap(const MapSet& map, std::string& error);
std::vector<uint8_t> writeHexenMap(const MapSet& map, std::string& error);
std::vector<uint8_t> writeUdmfMap(const MapSet& map, std::string& error);

// UDMF textual representation of one map (whole TEXTMAP lump contents).
std::string udmfWriteTextmap(const MapSet& map, std::string& error);
bool udmfReadTextmap(const std::string& text, MapSet& out, std::string& error,
                     std::vector<std::string>& warnings);

// Available map names inside a WAD (headers followed by map lumps).
std::vector<std::string> listMaps(const WadFile& wad);

} // namespace pdb
