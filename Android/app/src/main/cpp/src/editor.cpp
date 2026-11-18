// PocketDoomBuilder — Android port (Dear ImGui)
// editor.cpp — Editor state helpers.

#include "editor.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace pdb {

namespace {

Editor g_editor;

} // namespace

Editor& editor() { return g_editor; }

const std::vector<GameConfig>& gameConfigs() {
    // Mirrors Assets/Common/Configurations: Doom / Heretic / Hexen / Strife
    // in Doom or Hexen map format, plus the UDMF namespaces.
    static const std::vector<GameConfig> configs = {
        {"Doom: Doom (Doom format)", MapFormat::Doom, false, false},
        {"Doom: Doom (Doom in Hexen format)", MapFormat::Hexen, true, false},
        {"Doom: Doom (Doom in UDMF format)", MapFormat::UDMF, true, true},
        {"Doom: Doom 2 (Doom format)", MapFormat::Doom, false, false},
        {"Doom: Doom 2 (Doom in Hexen format)", MapFormat::Hexen, true, false},
        {"Doom: Doom 2 (Doom in UDMF format)", MapFormat::UDMF, true, true},
        {"Heretic: Heretic (Doom format)", MapFormat::Doom, false, false},
        {"Heretic: Heretic (Doom in Hexen format)", MapFormat::Hexen, true, false},
        {"Hexen: Hexen (Hexen format)", MapFormat::Hexen, true, false},
        {"Hexen: Hexen (Hexen in UDMF format)", MapFormat::UDMF, true, true},
        {"Strife: Strife (Doom format)", MapFormat::Doom, false, false},
        {"Strife: Strife (Doom in Hexen format)", MapFormat::Hexen, true, false},
        {"Boom: Boom (Doom format)", MapFormat::Doom, false, false},
        {"Boom: Boom (Doom in Hexen format)", MapFormat::Hexen, true, false},
        {"Eternity: Eternity (Doom format)", MapFormat::Doom, false, false},
        {"Eternity: Eternity (Doom in Hexen format)", MapFormat::Hexen, true, false},
        {"Eternity: Eternity (UDMF format)", MapFormat::UDMF, true, true},
        {"GZDoom: GZDoom (Doom format)", MapFormat::Doom, false, false},
        {"GZDoom: GZDoom (Doom in Hexen format)", MapFormat::Hexen, true, false},
        {"GZDoom: GZDoom (Doom in UDMF format)", MapFormat::UDMF, true, true},
        {"GZDoom: GZDoom (Hexen in UDMF format)", MapFormat::UDMF, true, true},
        {"GZDoom: GZDoom (UDMF format)", MapFormat::UDMF, true, true},
        {"Edge: EDGE (Doom format)", MapFormat::Doom, false, false},
        {"DSDA-Doom: DSDA (Doom format)", MapFormat::Doom, false, false},
    };
    return configs;
}

const std::vector<DockerInfo>& dockerList() {
    // Dockers registered by BuilderModes, TagExplorer, CommentsPanel and
    // UDBScript (Source/Plugins/*/BuilderPlug.cs).
    static const std::vector<DockerInfo> dockers = {
        {DockerTab::Maps, "Maps"},
        {DockerTab::UndoRedo, "Undo / Redo"},
        {DockerTab::DrawSettings, "Draw Settings"},
        {DockerTab::TagExplorer, "Tag Explorer"},
        {DockerTab::Comments, "Comments"},
        {DockerTab::Scripts, "Scripts"},
    };
    return dockers;
}

void Editor::log(const std::string& text, int level) {
    ConsoleLine line;
    line.text = text;
    line.level = level;
    console.push_back(std::move(line));
    // Keep the console bounded; the desktop trims old lines too.
    if (console.size() > 500) {
        console.erase(console.begin(), console.begin() + 100);
    }
    if (level > 0) warnings.push_back(text);
    if (logSink) logSink(text, level);
}

bool Editor::openWadFromMemory(const std::vector<uint8_t>& bytes,
                               const std::string& displayName,
                               const std::string& uri) {
    std::vector<std::string> errors;
    WadFile parsed;
    if (!readWad(bytes, parsed, errors)) {
        for (const std::string& e : errors) log(e, 2);
        status = "Failed to open WAD";
        return false;
    }
    for (const std::string& e : errors) log(e, 1);

    wad = std::move(parsed);
    wadLoaded = true;
    wadName = displayName;
    wadUri = uri;
    mapOpen = false;
    map.clear();
    mapNames = listMaps(wad);
    currentMapIndex = -1;
    warnings.clear();

    char buf[256];
    std::snprintf(buf, sizeof(buf), "Loaded %s (%s, %d lumps, %d maps)",
                  displayName.c_str(), wad.type.c_str(), (int)wad.lumps.size(),
                  (int)mapNames.size());
    log(buf);

    if (mapNames.empty()) {
        log("No maps found in this WAD.", 1);
        status = "WAD has no maps";
        return true;
    }
    return openMapByIndex(0);
}

std::string Editor::nextMapName() {
    for (int i = 1; i < 1000; ++i) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "MAP%02d", i);
        if (findMapHeader(wad, buf) < 0) return buf;
    }
    return "MAP01";
}

bool Editor::newMap(MapFormat format, const std::string& name) {
    // No WAD open yet: start a fresh in-memory PWAD, exactly like the
    // desktop, which creates the map inside a new WAD when none is loaded.
    if (!wadLoaded) {
        wad.clear();
        wad.type = "PWAD";
        wadLoaded = true;
        wadName = "newmap.wad";
        wadUri.clear();
        log("Created a new in-memory WAD.");
    }

    const std::string mapName = name.empty() ? nextMapName() : name;
    if (findMapHeader(wad, mapName) >= 0) {
        log("A map named " + mapName + " already exists.", 2);
        status = "Map already exists: " + mapName;
        return false;
    }

    MapSet fresh;
    createDefaultMap(fresh, format);
    fresh.mapName = mapName;

    std::string error;
    if (!saveMap(wad, fresh, error)) {
        log("Failed to create map: " + error, 2);
        status = "New map failed: " + error;
        return false;
    }

    // The WAD now contains the new map; list and open it.
    mapNames = listMaps(wad);
    int idx = -1;
    for (size_t i = 0; i < mapNames.size(); ++i) {
        if (mapNames[i] == mapName) { idx = (int)i; break; }
    }
    if (idx < 0) {
        log("Created map " + mapName + " but could not reopen it.", 2);
        return false;
    }
    map.clear();
    mapOpen = false;
    return openMapByIndex(idx);
}

bool Editor::openMapByIndex(int index) {
    if (index < 0 || index >= (int)mapNames.size()) return false;
    const MapLoadResult result = loadMap(wad, mapNames[(size_t)index], map);
    if (!result.ok) {
        log(result.error, 2);
        status = "Failed to load " + mapNames[(size_t)index];
        return false;
    }
    for (const std::string& w : result.warnings) log(w, 1);
    mapOpen = true;
    currentMapIndex = index;
    selection = PickResult();
    hover = PickResult();
    selectedCount = 0;
    map.dirty = false;

    const char* formatName = "Doom";
    if (map.format == MapFormat::Hexen) formatName = "Hexen";
    if (map.format == MapFormat::UDMF) formatName = "UDMF";

    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "Map %s loaded (%s format): %d vertices, %d linedefs, "
                  "%d sectors, %d things",
                  map.mapName.c_str(), formatName, (int)map.vertices.size(),
                  (int)map.linedefs.size(), (int)map.sectors.size(),
                  (int)map.things.size());
    log(buf);
    status = std::string("Loaded map ") + map.mapName;
    setConfigForMap();
    return true;
}

bool Editor::openMapByName(const std::string& name) {
    for (size_t i = 0; i < mapNames.size(); ++i) {
        if (mapNames[i] == name) return openMapByIndex((int)i);
    }
    return false;
}

bool Editor::serialiseWad(std::vector<uint8_t>& out, std::string& error) {
    if (!wadLoaded) {
        error = "No WAD is open.";
        return false;
    }
    if (mapOpen) {
        if (!saveMap(wad, map, error)) return false;
    }
    out = writeWad(wad);
    return true;
}

void Editor::clearSelection() {
    map.clearSelection();
    selection = PickResult();
    selectedCount = 0;
}

void Editor::selectAt(float screenX, float screenY) {
    if (!mapOpen) return;
    const PickResult hit = view.pick(map, screenX, screenY, 8.0f);
    selection = hit;
    map.clearSelection();
    selectedCount = 0;

    switch (hit.kind) {
        case PickKind::None:
            status = "Nothing selected";
            break;
        case PickKind::Vertex:
            if ((size_t)hit.index < map.vertices.size()) {
                map.vertices[(size_t)hit.index].selected = true;
                selectedCount = 1;
                const Vertex& v = map.vertices[(size_t)hit.index];
                char buf[128];
                std::snprintf(buf, sizeof(buf), "Selected vertex %d (%d, %d)",
                              hit.index, v.x, v.y);
                status = buf;
            }
            break;
        case PickKind::Linedef:
            if ((size_t)hit.index < map.linedefs.size()) {
                map.linedefs[(size_t)hit.index].selected = true;
                selectedCount = 1;
                char buf[128];
                std::snprintf(buf, sizeof(buf), "Selected linedef %d", hit.index);
                status = buf;
            }
            break;
        case PickKind::Sector:
            if ((size_t)hit.index < map.sectors.size()) {
                map.sectors[(size_t)hit.index].selected = true;
                selectedCount = 1;
                char buf[128];
                std::snprintf(buf, sizeof(buf), "Selected sector %d", hit.index);
                status = buf;
            }
            break;
        case PickKind::Thing:
            if ((size_t)hit.index < map.things.size()) {
                map.things[(size_t)hit.index].selected = true;
                selectedCount = 1;
                char buf[128];
                std::snprintf(buf, sizeof(buf), "Selected thing %d", hit.index);
                status = buf;
            }
            break;
    }
}

void Editor::setMode(EditMode m) {
    mode = m;
    const EditModeInfo& info = modeInfo();
    status = std::string("Mode: ") + info.displayName;
    log(status);
    if (!info.selectionImplemented) {
        log(std::string(info.displayName) +
                " is not implemented yet; selection is stubbed in phase 1.",
            1);
    }
    clearSelection();
}

const EditModeInfo& Editor::modeInfo() const {
    const std::vector<EditModeInfo>& modes = editModeList();
    for (const EditModeInfo& info : modes) {
        if (info.mode == mode) return info;
    }
    return modes[5]; // Linedefs Mode
}

bool Editor::modeHasSelection() const {
    switch (mode) {
        case EditMode::Linedefs:
            return selection.kind == PickKind::Linedef;
        case EditMode::Sectors:
            return selection.kind == PickKind::Sector;
        case EditMode::Things:
            return selection.kind == PickKind::Thing;
        case EditMode::Vertices:
            return selection.kind == PickKind::Vertex;
        default:
            return false;
    }
}

void Editor::zoomIn() {
    view.zoom *= 1.25f;
    view.clampZoom();
    status = "Zoom " + formatZoom(view.zoom);
}

void Editor::zoomOut() {
    view.zoom /= 1.25f;
    view.clampZoom();
    status = "Zoom " + formatZoom(view.zoom);
}

void Editor::fitToScreen(float viewW, float viewH) {
    view.fitToMap(map, viewW, viewH);
    status = "Zoom to fit: " + formatZoom(view.zoom);
}

void Editor::setConfigForMap() {
    switch (map.format) {
        case MapFormat::Doom:
            configName = "Doom: Doom 2 (Doom format)";
            break;
        case MapFormat::Hexen:
            configName = "ZDoom (Doom in Hexen format)";
            break;
        case MapFormat::UDMF:
            configName = "GZDoom (Doom in UDMF format)";
            break;
        default:
            configName = "Unknown configuration";
            break;
    }
}

} // namespace pdb
