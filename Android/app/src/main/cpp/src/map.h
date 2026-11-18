// PocketDoomBuilder — Android port (Dear ImGui)
// map.h — Core map data model.
//
// Mirrors the desktop Ultimate Doom Builder model (Source/Core/Map/*):
//   MapSet { Vertices, Linedefs, Sidedefs, Sectors, Things }
// but simplified for phase 1 (2D classic view + selection).
// UDMF unknown fields are preserved verbatim in `extra` so a
// load → save round-trip does not drop data the editor does not
// yet understand.

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace pdb {

// ---------------------------------------------------------------------------
// Map format
// ---------------------------------------------------------------------------

enum class MapFormat {
    Unknown,
    Doom,   // Doom binary (THINGS/LINEDEFS/SIDEDEFS/VERTEXES/SECTORS)
    Hexen,  // Hexen binary (adds BEHAVIOR, 5 args, thing Z)
    UDMF,   // GZDoom UDMF (TEXTMAP lump)
};

// ---------------------------------------------------------------------------
// Map elements (indices are positions in MapSet vectors, like the desktop
// lookup tables in DoomMapSetIO / HexenMapSetIO).
// ---------------------------------------------------------------------------

struct Vertex {
    int x = 0;
    int y = 0;
    bool selected = false;
    bool marked = false;
    std::map<std::string, std::string> extra; // UDMF only
};

struct Linedef {
    int v1 = -1;
    int v2 = -1;
    uint16_t flags = 0;
    uint16_t action = 0; // Doom: u16 action; Hexen: u8 action (stored here)
    uint16_t tag = 0;
    int sideFront = -1;  // sidedef index or -1 (0xFFFF in binary)
    int sideBack = -1;
    int args[5] = {0, 0, 0, 0, 0}; // Hexen/UDMF only; zero for Doom
    bool selected = false;
    bool marked = false;
    std::map<std::string, std::string> extra; // UDMF only
};

struct Sidedef {
    int offsetX = 0;
    int offsetY = 0;
    std::string texTop = "-";
    std::string texMid = "-";
    std::string texBottom = "-";
    int sector = -1;
    bool selected = false;
    bool marked = false;
    std::map<std::string, std::string> extra; // UDMF only
};

struct Sector {
    int floorHeight = 0;
    int ceilHeight = 0;
    std::string floorTex = "FLOOR1_1";
    std::string ceilTex = "CEIL1_1";
    int brightness = 0;
    uint16_t special = 0;
    uint16_t tag = 0;
    bool selected = false;
    bool marked = false;
    std::map<std::string, std::string> extra; // UDMF only
};

struct Thing {
    int x = 0;
    int y = 0;
    int z = 0; // Hexen/UDMF only
    int angle = 0; // Doom angle units (0..2047, 0 = east, CCW)
    int type = 0;
    uint16_t flags = 0;
    uint16_t tag = 0; // Hexen/UDMF only
    int action = 0;   // Hexen/UDMF only
    int args[5] = {0, 0, 0, 0, 0}; // Hexen/UDMF only
    bool selected = false;
    bool marked = false;
    std::map<std::string, std::string> extra; // UDMF only
};

// ---------------------------------------------------------------------------
// MapSet — one loaded map (one map header lump in a WAD).
// ---------------------------------------------------------------------------

struct MapSet {
    std::vector<Vertex> vertices;
    std::vector<Linedef> linedefs;
    std::vector<Sidedef> sidedefs;
    std::vector<Sector> sectors;
    std::vector<Thing> things;

    MapFormat format = MapFormat::Unknown;
    std::string mapName;          // e.g. "MAP01", "E1M1"
    std::string sourceUri;        // SAF URI the map was loaded from (if any)
    bool dirty = false;           // modified since load/save
    std::vector<std::string> warnings; // load warnings (Errors dialog)

    // Hexen BEHAVIOR lump bytes, preserved verbatim on save.
    std::vector<uint8_t> behaviorLump;

    // UDMF: the map's namespace (zdoom, hexen, doom, ...) and any
    // root-level data the editor does not manage, both preserved on save.
    std::string udmfNamespace = "zdoom";
    std::map<std::string, std::string> udmfRootExtra;

    void clear() {
        vertices.clear();
        linedefs.clear();
        sidedefs.clear();
        sectors.clear();
        things.clear();
        format = MapFormat::Unknown;
        mapName.clear();
        sourceUri.clear();
        dirty = false;
        warnings.clear();
        behaviorLump.clear();
        udmfNamespace = "zdoom";
        udmfRootExtra.clear();
    }

    bool empty() const {
        return vertices.empty() && linedefs.empty() && sectors.empty() &&
               things.empty();
    }

    void clearSelection() {
        for (auto& v : vertices) v.selected = false;
        for (auto& l : linedefs) l.selected = false;
        for (auto& s : sidedefs) s.selected = false;
        for (auto& s : sectors) s.selected = false;
        for (auto& t : things) t.selected = false;
    }

    int selectedCount() const {
        int n = 0;
        for (auto& v : vertices) if (v.selected) ++n;
        for (auto& l : linedefs) if (l.selected) ++n;
        for (auto& s : sectors) if (s.selected) ++n;
        for (auto& t : things) if (t.selected) ++n;
        return n;
    }

    // Bounding box in map units. Returns false when the map is empty.
    bool boundingBox(double& minX, double& minY, double& maxX, double& maxY) const {
        if (vertices.empty()) return false;
        minX = maxX = vertices[0].x;
        minY = maxY = vertices[0].y;
        for (const auto& v : vertices) {
            if (v.x < minX) minX = v.x;
            if (v.x > maxX) maxX = v.x;
            if (v.y < minY) minY = v.y;
            if (v.y > maxY) maxY = v.y;
        }
        return true;
    }
};

// Build the default new map: a 256x256 room, one sector, one player start.
// Used by the editor's New Map action and covered by the unit tests.
void createDefaultMap(MapSet& map, MapFormat format);

// ---------------------------------------------------------------------------
// WAD container (Source/Core/IO/WAD.cs + Lump.cs).
// ---------------------------------------------------------------------------

struct WadLump {
    std::string name; // normalised: upper-case, no trailing NULs, max 8 chars
    std::vector<uint8_t> data;
};

struct WadFile {
    std::string type; // "IWAD" or "PWAD"
    std::vector<WadLump> lumps;

    void clear() {
        type.clear();
        lumps.clear();
    }

    // First lump index with `name` at or after `start`, or -1.
    int findLump(const std::string& name, int start = 0) const {
        for (size_t i = (size_t)(start < 0 ? 0 : start); i < lumps.size(); ++i) {
            if (lumps[i].name == name) return (int)i;
        }
        return -1;
    }

    const WadLump* findLumpPtr(const std::string& name, int start = 0) const {
        int i = findLump(name, start);
        return i >= 0 ? &lumps[(size_t)i] : nullptr;
    }
};

// Lump-name helpers (Lump.MakeNormalName in the desktop code).
std::string normaliseLumpName(const std::string& raw);
std::string makeLumpName(const char* bytes8); // 8 raw bytes -> string

// ---------------------------------------------------------------------------
// Edit modes (Source/Plugins/BuilderModes + EditModeAttribute).
// Phase 1 implements selection for the four classic modes; every other
// mode is selectable in the toolbar (1:1 layout) but its editing
// behaviour is stubbed with a status message.
// ---------------------------------------------------------------------------

enum class EditMode {
    DrawLines,
    DrawCurve,
    DrawRectangle,
    DrawEllipse,
    DrawGrid,
    Linedefs,
    Sectors,
    Things,
    Vertices,
    FloorAlign,
    CeilingAlign,
    MakeSectors,
    EditSelection,
    Bridge,
    FindReplace,
    MapAnalysis,
    BlockMap,
    Visual,
    ThreeDFloor,
    Slope,
    DrawSlopes,
    Automap,
    BlockmapExplorer,
    NodesViewer,
    RejectExplorer,
    VisplaneExplorer,
    SoundPropagation,
    SoundEnvironment,
    StairSectorBuilder,
    WadAuthor,
    TerrainImporter,
    ImageExample,
};

struct EditModeInfo {
    EditMode mode;
    const char* displayName;
    const char* buttonGroup; // matches desktop ButtonGroup ordering
    int buttonOrder;
    bool selectionImplemented; // phase-1: true for the 4 classic modes
};

// Full mode list in desktop toolbar order (group, then order).
const std::vector<EditModeInfo>& editModeList();

enum class ViewMode {
    Wireframe,      // "Wireframe" (desktop: Normal)
    Brightness,     // "Brightness Levels"
    FloorTextures,  // "Floor Textures"
    CeilingTextures // "Ceiling Textures"
};

} // namespace pdb
