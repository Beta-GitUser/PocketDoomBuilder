// PocketDoomBuilder — Android port (Dear ImGui)
// editor.h — Application state shared by the UI shell, the 2D renderer and
// the Android platform layer.
//
// This mirrors the pieces of the desktop's General.* statics that the shell
// needs: the open WAD, the loaded map, the renderer settings, the current
// edit mode, the status bar fields and the console.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "icons.h"
#include "map.h"
#include "mapio.h"
#include "theme.h"
#include "view2d.h"
#include "wad.h"

struct ImFont;

namespace pdb {

// ---------------------------------------------------------------------------
// Game configurations (Assets/Common/Configurations)
// ---------------------------------------------------------------------------

struct GameConfig {
    std::string name;      // e.g. "ZDoom (Doom in Hexen format)"
    MapFormat format;      // detected map format
    bool hexenActions;     // 5-argument actions
    bool udmf;
};

const std::vector<GameConfig>& gameConfigs();

// ---------------------------------------------------------------------------
// Console log (bottom panel, matches the desktop's console)
// ---------------------------------------------------------------------------

struct ConsoleLine {
    std::string text;
    int level = 0; // 0 = info, 1 = warning, 2 = error
};

// ---------------------------------------------------------------------------
// Dockers (left panel tabs)
// ---------------------------------------------------------------------------

enum class DockerTab {
    UndoRedo,
    DrawSettings,
    TagExplorer,
    Comments,
    Scripts,
};

struct DockerInfo {
    DockerTab tab;
    const char* title;
};

const std::vector<DockerInfo>& dockerList();

// ---------------------------------------------------------------------------
// Editor
// ---------------------------------------------------------------------------

class Editor {
public:
    // --- data --------------------------------------------------------------
    WadFile wad;
    bool wadLoaded = false;
    std::string wadName;      // display name of the opened file
    std::string wadUri;       // SAF URI
    MapSet map;
    bool mapOpen = false;

    std::vector<std::string> mapNames;
    int currentMapIndex = -1;

    // --- view / rendering --------------------------------------------------
    View2D view;
    MapColors colors;
    ViewMode viewMode = ViewMode::Wireframe;
    EditMode mode = EditMode::Linedefs;
    bool showGrid = true;
    bool showVertices = true;
    bool showThings = true;
    bool fixedThingsScale = false;
    bool fullBrightness = false;
    bool snapToGrid = false;

    // --- selection / hover -------------------------------------------------
    PickResult hover;
    PickResult selection;
    int selectedCount = 0;

    // --- UI state ----------------------------------------------------------
    bool infoPanelExpanded = true; // MainForm: panelinfo starts at 106 px
    int activeDocker = 0;
    bool dockerVisible = true;
    float dockerWidth = 236.0f;      // desktop dockerspanel width
    float infoPanelHeight = 106.0f;  // panelinfo height when expanded
    bool showConsole = false;     // console.Visible = false by default
    bool showAbout = false;
    bool showPreferences = false;
    bool showGameConfigs = false;
    bool showErrors = false;
    bool showThingFilter = false;
    bool showGridSetup = false;

    // Last pointer position in map coordinates (status bar).
    double cursorX = 0.0;
    double cursorY = 0.0;

    // --- fonts / icons -----------------------------------------------------
    ImFont* fontUI = nullptr;
    ImFont* fontBold = nullptr;
    ImFont* fontSmall = nullptr;
    IconAtlas icons;

    // --- console / status --------------------------------------------------
    std::vector<ConsoleLine> console;
    std::string status = "Ready";
    std::string configName = "ZDoom (Doom in Hexen format)";
    std::vector<std::string> warnings;

    // --- diagnostics -------------------------------------------------------
    // Called for every console line so the platform layer can mirror it to
    // the Android log / the diagnostics overlay (a black screen otherwise
    // leaves no trace of what the shell was doing).
    std::function<void(const std::string&, int)> logSink;

    // --- persistence (SAF) -------------------------------------------------
    // Set by the Java side / platform layer when a document is picked.
    std::string pendingOpenUri;
    bool requestOpen = false;
    bool requestSave = false;
    bool requestSaveAs = false;

    // --- API ---------------------------------------------------------------

    void log(const std::string& text, int level = 0);

    // Load WAD bytes (from SAF) and open the first map.
    bool openWadFromMemory(const std::vector<uint8_t>& bytes,
                           const std::string& displayName,
                           const std::string& uri);

    // Switch to another map inside the loaded WAD.
    bool openMapByIndex(int index);
    bool openMapByName(const std::string& name);

    // Serialise the current WAD (with the map written back) for saving.
    bool serialiseWad(std::vector<uint8_t>& out, std::string& error);

    // Selection helpers used by the UI.
    void selectAt(float screenX, float screenY);
    void clearSelection();
    void setMode(EditMode m);
    const EditModeInfo& modeInfo() const;
    bool modeHasSelection() const;

    // Grid helpers bound to the status bar controls.
    void zoomIn();
    void zoomOut();
    void fitToScreen(float viewW, float viewH);

    // Desktop equivalence: the status bar config label.
    void setConfigForMap();
};

// Global editor instance (the app has exactly one main window, like the
// desktop's MainForm).
Editor& editor();

} // namespace pdb
