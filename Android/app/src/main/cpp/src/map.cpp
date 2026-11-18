// PocketDoomBuilder — Android port (Dear ImGui)
// map.cpp — Map model helpers + edit-mode table.

#include "map.h"

#include <cstdio>

#include <algorithm>
#include <cctype>

namespace pdb {

std::string normaliseLumpName(const std::string& raw) {
    std::string out;
    out.reserve(8);
    for (char c : raw) {
        if (c == '\0') break;
        out.push_back((char)std::toupper((unsigned char)c));
        if (out.size() >= 8) break;
    }
    while (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

std::string makeLumpName(const char* bytes8) {
    std::string out;
    for (int i = 0; i < 8; ++i) {
        char c = bytes8[i];
        if (c == '\0') break;
        out.push_back(c);
    }
    return out;
}

const std::vector<EditModeInfo>& editModeList() {
    // Order/groups mirror the desktop [EditMode] attributes:
    //   000_drawing  : Draw Lines, Draw Curve, Draw Rectangle, Draw Ellipse, Draw Grid
    //   000_editing  : Linedefs, Sectors, Things, Vertices, Floor Align, Ceiling Align, Make Sectors
    //   002_modify   : Edit Selection, Bridge
    //   002_tools    : Find/Replace, Map Analysis, BlockMap
    //   Visual / plugin modes follow.
    static const std::vector<EditModeInfo> modes = {
        {EditMode::DrawLines,        "Draw Lines Mode",        "000_drawing",  1,   false},
        {EditMode::DrawCurve,        "Draw Curve Mode",        "000_drawing",  2,   false},
        {EditMode::DrawRectangle,    "Draw Rectangle Mode",    "000_drawing",  3,   false},
        {EditMode::DrawEllipse,      "Draw Ellipse Mode",      "000_drawing",  4,   false},
        {EditMode::DrawGrid,         "Draw Grid Mode",         "000_drawing",  5,   false},
        {EditMode::Linedefs,         "Linedefs Mode",          "000_editing",  100, true},
        {EditMode::Sectors,          "Sectors Mode",           "000_editing",  200, true},
        {EditMode::Things,           "Things Mode",            "000_editing",  300, true},
        {EditMode::Vertices,         "Vertices Mode",          "000_editing",  400, true},
        {EditMode::FloorAlign,       "Floor Align Mode",       "000_editing",  310, false},
        {EditMode::CeilingAlign,     "Ceiling Align Mode",     "000_editing",  311, false},
        {EditMode::MakeSectors,      "Make Sectors Mode",      "000_editing",  302, false},
        {EditMode::EditSelection,    "Edit Selection Mode",    "002_modify",   1,   false},
        {EditMode::Bridge,           "Bridge Mode",            "002_modify",   2,   false},
        {EditMode::FindReplace,      "Find and Replace Mode",  "002_tools",    100, false},
        {EditMode::MapAnalysis,      "Map Analysis Mode",      "002_tools",    200, false},
        {EditMode::BlockMap,         "BlockMap Mode",          "002_tools",    400, false},
        {EditMode::Visual,           "Visual Mode",            "visual",       1,   false},
        {EditMode::ThreeDFloor,      "3D Floor Mode",          "visual",       2,   false},
        {EditMode::Slope,            "Slope Mode",             "visual",       3,   false},
        {EditMode::DrawSlopes,       "Draw Slopes Mode",       "visual",       4,   false},
        {EditMode::Automap,          "Automap Mode",           "visual",       5,   false},
        {EditMode::BlockmapExplorer, "Blockmap Explorer Mode", "tools",        1,   false},
        {EditMode::NodesViewer,      "Nodes Viewer Mode",      "tools",        2,   false},
        {EditMode::RejectExplorer,   "Reject Explorer",        "tools",        3,   false},
        {EditMode::VisplaneExplorer, "Visplane Explorer",      "tools",        4,   false},
        {EditMode::SoundPropagation, "Sound Propagation Mode", "tools",        5,   false},
        {EditMode::SoundEnvironment, "Sound Environment Mode", "tools",        6,   false},
        {EditMode::StairSectorBuilder,"Stair Sector Builder Mode","tools",     7,   false},
        {EditMode::WadAuthor,        "WadAuthor Mode",         "tools",        8,   false},
        {EditMode::TerrainImporter,  "Terrain Importer",       "tools",        9,   false},
        {EditMode::ImageExample,     "Image Example",          "tools",        10,  false},
    };
    return modes;
}

} // namespace pdb


namespace pdb {

void createDefaultMap(MapSet& map, MapFormat format) {
    map.clear();
    map.format = format;
    map.udmfNamespace = "zdoom";

    // A 256x256 room.
    map.vertices = {
        {0, 0}, {256, 0}, {256, 256}, {0, 256},
    };
    for (int i = 0; i < 4; ++i) {
        Linedef ld;
        ld.v1 = i;
        ld.v2 = (i + 1) % 4;
        ld.flags = 1; // blocking
        ld.sideFront = 0;
        ld.sideBack = -1; // outside
        map.linedefs.push_back(ld);
    }
    Sidedef sd;
    sd.sector = 0;
    sd.texTop = "-";
    sd.texBottom = "-";
    sd.texMid = "STARTAN3";
    map.sidedefs.push_back(sd);
    Sector sec;
    sec.floorHeight = 0;
    sec.ceilHeight = 64;
    sec.floorTex = "FLOOR1_1";
    sec.ceilTex = "CEIL1_1";
    sec.brightness = 160;
    map.sectors.push_back(sec);

    Thing player;
    player.x = 128;
    player.y = 128;
    player.angle = 90; // Doom angle units: 90 = facing north
    player.type = (format == MapFormat::Doom) ? 1 : 3001; // Doom / Hexen+ player start
    player.flags = 0x0007; // SKILL1 | SKILL2 | SKILL3
    map.things.push_back(player);
}

} // namespace pdb
