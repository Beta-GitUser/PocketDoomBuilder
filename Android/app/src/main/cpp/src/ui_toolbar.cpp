// PocketDoomBuilder — Android port (Dear ImGui)
// ui_toolbar.cpp — Menu strip, main toolbar, mode bar and status bar.
//
// The item order, separators and tooltips are copied from
// MainForm.Designer.cs (toolbar.Items / menumain / statusbar) so the
// Android shell reads the same as the desktop.

#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

#include "editor.h"
#include "icons.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "theme.h"
#include "ui.h"
#include "ui_internal.h"

namespace pdb {
namespace ui {

namespace {

const ThemeColors& T = theme();

// A toolbar button drawn as the desktop's ToolStripButton: a 16x16 icon on a
// light grey strip, with the visual-styles blue highlight when hot/pressed.
bool toolbarButton(const char* id, const char* iconName, const char* tooltip,
                   bool active, bool enabled = true, float size = 22.0f) {
    ImGui::PushID(id);
    size *= currentUiScale();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##btn", ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = enabled && ImGui::IsItemClicked();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (enabled && (hovered || active)) {
        dl->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size),
                          active ? T.highlight : T.buttonHover);
        dl->AddRect(pos, ImVec2(pos.x + size, pos.y + size), T.border);
    }
    const float iconSize = std::min(16.0f * currentUiScale(), size - 4.0f);
    const ImVec2 iconPos(pos.x + (size - iconSize) * 0.5f,
                         pos.y + (size - iconSize) * 0.5f);
    editor().icons.draw(dl, iconName, iconPos, iconSize,
                        enabled ? IM_COL32(255, 255, 255, 255)
                                : IM_COL32(160, 160, 160, 180));
    if (hovered && tooltip && tooltip[0] != '\0') {
        ImGui::SetTooltip("%s", tooltip);
    }
    ImGui::PopID();
    ImGui::SameLine(0.0f, 1.0f);
    return clicked;
}

// Layout toggle button. The icon set has no desktop/mobile glyph, so the
// button draws one: a monitor outline in desktop layout, a phone outline in
// mobile layout (the active state is highlighted by the shared chrome).
bool toolbarLayoutToggle() {
    ImGui::PushID("layout");
    const float size = 22.0f * currentUiScale();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##layoutbtn", ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool active = editor().mobileLayout;
    if (hovered || active) {
        dl->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size),
                          active ? T.highlight : T.buttonHover);
        dl->AddRect(pos, ImVec2(pos.x + size, pos.y + size), T.border);
    }
    const float glyph = std::min(14.0f * currentUiScale(), size - 4.0f);
    const ImVec2 c(pos.x + (size - glyph) * 0.5f, pos.y + (size - glyph) * 0.5f);
    const ImU32 col = IM_COL32(255, 255, 255, 255);
    if (active) {
        // Phone outline: rounded body + home line.
        const ImVec2 p1(c.x - glyph * 0.28f, c.y - glyph * 0.45f);
        const ImVec2 p2(c.x + glyph * 0.28f, c.y + glyph * 0.45f);
        dl->AddRect(p1, p2, col, 1.5f, 3.0f);
        dl->AddLine(ImVec2(p1.x + 2.0f, p2.y - 2.0f),
                    ImVec2(p2.x - 2.0f, p2.y - 2.0f), col, 1.0f);
    } else {
        // Monitor outline: screen + stand.
        const ImVec2 p1(c.x - glyph * 0.45f, c.y - glyph * 0.35f);
        const ImVec2 p2(c.x + glyph * 0.45f, c.y + glyph * 0.12f);
        dl->AddRect(p1, p2, col, 1.5f);
        dl->AddLine(ImVec2(c.x, p2.y), ImVec2(c.x, p2.y + glyph * 0.22f), col, 1.5f);
        dl->AddLine(ImVec2(c.x - glyph * 0.22f, p2.y + glyph * 0.22f),
                    ImVec2(c.x + glyph * 0.22f, p2.y + glyph * 0.22f), col, 1.5f);
    }
    if (hovered) {
        ImGui::SetTooltip("Toggle mobile / desktop layout");
    }
    ImGui::PopID();
    ImGui::SameLine(0.0f, 1.0f);
    return clicked;
}

void toolbarSeparator() {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeight() + 6.0f * currentUiScale();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x + 3.0f, pos.y + 2.0f),
                                        ImVec2(pos.x + 3.0f, pos.y + h - 2.0f),
                                        T.border, 1.0f);
    ImGui::Dummy(ImVec2(7.0f * currentUiScale(), 1.0f));
    ImGui::SameLine(0.0f, 0.0f);
}

// Menu item helper: greys out stubs but keeps the 1:1 layout.
bool menuItem(const char* label, const char* shortcut = nullptr, bool enabled = true,
              bool checked = false) {
    if (checked) {
        return ImGui::MenuItem(label, shortcut, true, enabled);
    }
    return ImGui::MenuItem(label, shortcut, false, enabled);
}

// Every unimplemented menu entry logs a consistent "stub" message, exactly
// how the phase-1 scope defines it.
void stub(const char* what) {
    std::string msg = std::string("\"") + what + "\" is not implemented in this build.";
    editor().log(msg, 1);
    editor().status = msg;
}

bool stubItem(const char* label, const char* shortcut = nullptr, bool enabled = true) {
    if (ImGui::MenuItem(label, shortcut, false, enabled)) {
        stub(label);
        return true;
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// Menu strip
// ---------------------------------------------------------------------------

void drawMenuBar() {
    Editor& ed = editor();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
    if (!ImGui::BeginMenuBar()) {
        ImGui::PopStyleVar();
        return;
    }

    if (ImGui::BeginMenu("File")) {
        if (menuItem("New Map")) {
            ed.newMapName = ed.nextMapName();
            ed.requestNewMap = true;
        }
        if (menuItem("Open Map...", "Ctrl+O")) {
            ed.requestOpen = true;
        }
        if (menuItem("Open Map in Current WAD...", nullptr, ed.wadLoaded)) {
            stub("Open Map in Current WAD");
        }
        if (menuItem("Close Map", nullptr, ed.mapOpen)) {
            ed.map.clear();
            ed.mapOpen = false;
            ed.status = "Map closed";
        }
        ImGui::Separator();
        if (menuItem("Save Map", "Ctrl+S", ed.mapOpen)) {
            ed.requestSave = true;
        }
        if (menuItem("Save Map As...", nullptr, ed.mapOpen)) {
            ed.requestSaveAs = true;
        }
        if (menuItem("Save Map Into...", nullptr, ed.mapOpen)) {
            ed.requestSaveAs = true;
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Import")) {
            stubItem("Export Map");
            stubItem("Export Picture");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Export")) {
            stubItem("Export Map");
            stubItem("Export Picture");
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Recent Files")) {
            if (ed.wadName.empty()) {
                ImGui::MenuItem("No recently opened files", nullptr, false, false);
            } else {
                ImGui::MenuItem(ed.wadName.c_str(), nullptr, false, false);
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (menuItem("Exit")) {
            ed.requestQuit = true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        if (menuItem("Undo", "Ctrl+Z", false)) stub("Undo");
        if (menuItem("Redo", "Ctrl+Y", false)) stub("Redo");
        ImGui::Separator();
        if (menuItem("Cut", "Ctrl+X", ed.mapOpen)) stub("Cut");
        if (menuItem("Copy", "Ctrl+C", ed.mapOpen)) stub("Copy");
        if (menuItem("Paste", "Ctrl+V", ed.mapOpen)) stub("Paste");
        if (menuItem("Paste Special...", nullptr, ed.mapOpen)) stub("Paste Special");
        ImGui::Separator();
        if (ImGui::BeginMenu("Merge Dragged Vertices Only")) {
            stubItem("Merge Dragged Vertices Only");
            ImGui::EndMenu();
        }
        stubItem("Merge Dragged Geometry");
        stubItem("Replace with Dragged Geometry");
        ImGui::Separator();
        if (menuItem("Snap to Grid", nullptr, ed.mapOpen, ed.snapToGrid)) {
            ed.snapToGrid = !ed.snapToGrid;
            ed.status = ed.snapToGrid ? "Snap to grid enabled" : "Snap to grid disabled";
        }
        if (menuItem("Dynamic Grid Size", nullptr, ed.mapOpen, autoGridEnabled())) {
            toggleAutoGrid();
        }
        stubItem("Snap to Geometry");
        stubItem("Split Joined Sectors when Drawing Lines");
        stubItem("Auto Clear Sidedef Textures");
        ImGui::Separator();
        if (menuItem("Increase Grid Size", "Ctrl+=")) {
            ed.view.increaseGridSize();
            ed.status = "Grid size: " + std::to_string(ed.view.gridSize) + " mp";
        }
        if (menuItem("Decrease Grid Size", "Ctrl+-")) {
            ed.view.decreaseGridSize();
            ed.status = "Grid size: " + std::to_string(ed.view.gridSize) + " mp";
        }
        stubItem("Snap Selection to Grid");
        stubItem("Align Grid To Selected Linedef");
        stubItem("Set Grid Origin To Selected Vertex");
        stubItem("Reset Grid Transform");
        if (menuItem("Grid and Backdrop Setup...")) ed.showGridSetup = true;
        ImGui::Separator();
        stubItem("Add Selection to Group");
        stubItem("Select Group");
        stubItem("Clear Group");
        ImGui::Separator();
        stubItem("Map Options...");
        stubItem("View Used Tags...");
        stubItem("View Thing Types...");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        stubItem("Configure Things Filters...");
        stubItem("Configure Linedef Colors...");
        ImGui::Separator();
        if (menuItem("Wireframe", nullptr, true, ed.viewMode == ViewMode::Wireframe)) {
            ed.viewMode = ViewMode::Wireframe;
            ed.status = "View mode: Wireframe";
        }
        if (menuItem("Brightness Levels", nullptr, true,
                     ed.viewMode == ViewMode::Brightness)) {
            ed.viewMode = ViewMode::Brightness;
            ed.status = "View mode: Brightness Levels";
        }
        if (menuItem("Floor Textures", nullptr, true,
                     ed.viewMode == ViewMode::FloorTextures)) {
            ed.viewMode = ViewMode::FloorTextures;
            ed.status = "View mode: Floor Textures";
        }
        if (menuItem("Ceiling Textures", nullptr, true,
                     ed.viewMode == ViewMode::CeilingTextures)) {
            ed.viewMode = ViewMode::CeilingTextures;
            ed.status = "View mode: Ceiling Textures";
        }
        ImGui::Separator();
        if (menuItem("Full Brightness", nullptr, true, ed.fullBrightness)) {
            ed.fullBrightness = !ed.fullBrightness;
            ed.status = ed.fullBrightness ? "Full brightness on" : "Full brightness off";
        }
        if (menuItem("Render Grid", nullptr, true, ed.showGrid)) {
            ed.showGrid = !ed.showGrid;
        }
        if (menuItem("Show Comments", nullptr, true, false)) stub("Show Comments");
        if (menuItem("Fixed Things Scale", nullptr, true, ed.fixedThingsScale)) {
            ed.fixedThingsScale = !ed.fixedThingsScale;
        }
        if (menuItem("Always Show Vertices", nullptr, true, ed.showVertices)) {
            ed.showVertices = !ed.showVertices;
        }
        ImGui::Separator();
        stubItem("Dynamic light rendering mode");
        stubItem("Model rendering mode");
        stubItem("Render fog (Visual mode)");
        stubItem("Render sky (Visual mode)");
        stubItem("Classic rendering (Visual mode)");
        stubItem("Event lines mode");
        stubItem("Show Editable Vertices (Visual mode)");
        ImGui::Separator();
        if (ImGui::BeginMenu("Zoom")) {
            struct ZoomItem { const char* label; float pct; };
            static const ZoomItem kZoom[] = {
                {"800%", 8.0f}, {"400%", 4.0f}, {"200%", 2.0f}, {"100%", 1.0f},
                {"50%", 0.5f},  {"25%", 0.25f}, {"10%", 0.10f}, {"5%", 0.05f},
            };
            for (const ZoomItem& z : kZoom) {
                if (ImGui::MenuItem(z.label)) {
                    // Map units at 100% are 1 px in the desktop's Zoom2D.
                    ed.view.zoom = z.pct;
                    ed.view.clampZoom();
                    ed.status = std::string("Zoom ") + z.label;
                }
            }
            ImGui::EndMenu();
        }
        if (menuItem("Go To Coordinates...")) stub("Go To Coordinates");
        if (menuItem("Fit to Screen")) requestFitToScreen();
        if (menuItem("Expanded Info Panel", nullptr, true, ed.infoPanelExpanded)) {
            ed.infoPanelExpanded = !ed.infoPanelExpanded;
            ed.infoPanelHeight = ed.infoPanelExpanded ? 300.0f : metrics().infoPanelHeight;
        }
        if (menuItem("Mobile Layout", nullptr, true, ed.mobileLayout)) {
            ed.mobileLayout = !ed.mobileLayout;
            ed.status = ed.mobileLayout ? "Mobile layout on" : "Desktop layout";
            requestFitToScreen();
        }
        ImGui::Separator();
        if (menuItem("Script Editor...")) stub("Script Editor");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Mode")) {
        bool first = true;
        const std::vector<EditModeInfo>& modes = editModeList();
        for (const EditModeInfo& info : modes) {
            if (first) first = false;
            if (ImGui::MenuItem(info.displayName, nullptr, ed.mode == info.mode)) {
                ed.setMode(info.mode);
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Prefabs")) {
        stubItem("Insert Prefab from File...", nullptr, ed.mapOpen);
        stubItem("Insert Previous Prefab", nullptr, ed.mapOpen);
        ImGui::Separator();
        stubItem("Create From Selection...", nullptr, ed.mapOpen);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Tools")) {
        stubItem("Reload Resources", nullptr, ed.wadLoaded);
        stubItem("Reload MODELDEF/VOXELDEF", nullptr, ed.wadLoaded);
        stubItem("Reload GLDEFS", nullptr, ed.wadLoaded);
        if (menuItem("Errors and Warnings...", nullptr, true)) ed.showErrors = true;
        ImGui::Separator();
        if (menuItem("Game Configurations...")) ed.showGameConfigs = true;
        if (menuItem("Preferences...")) ed.showPreferences = true;
        ImGui::Separator();
        stubItem("Save Screenshot");
        stubItem("Save Screenshot (active window)");
        ImGui::Separator();
        stubItem("Test Map", "F9", ed.mapOpen);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        // The desktop's help menu is 232 px wide for "Keyboard Shortcuts
        // Reference"; ImGui sizes menus to their content, so nothing to set.
        if (menuItem("Reference Manual")) {
            ed.showHelp = true;
            ed.helpPage = "introduction.html";
            ed.helpHistory.clear();
        }
        if (menuItem("Keyboard Shortcuts Reference")) ed.showShortcuts = true;
        if (menuItem("Program Configuration Folder")) ed.requestOpenConfigFolder = true;
        if (menuItem("About this Editing Mode", nullptr, ed.mapOpen)) {
            // General.Editing.Mode.OnHelp(): opens the manual page for the mode.
            ed.showHelp = true;
            ed.helpPage = "editingmodes.html";
            ed.helpHistory.clear();
        }
        if (menuItem("GitHub issues tracker")) {
            ed.pendingUrl = "https://github.com/Beta-GitUser/PocketDoomBuilder/issues";
            ed.requestOpenUrl = true;
        }
        if (menuItem("Check for updates...")) {
            // The desktop runs its updater against the UDB release feed; this
            // port is delivered through its own GitHub Actions builds.
            ed.pendingUrl = "https://github.com/Beta-GitUser/PocketDoomBuilder/releases";
            ed.requestOpenUrl = true;
        }
        ImGui::Separator();
        if (menuItem("Diagnostics and Log...")) {
            ed.showDiagnostics = true;
            ed.requestDiagnosticsLog = true;
        }
        if (menuItem("About PocketDoomBuilder...")) ed.showAbout = true;
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
    ImGui::PopStyleVar();
}

// ---------------------------------------------------------------------------
// Main toolbar
// ---------------------------------------------------------------------------

void drawToolbar() {
    Editor& ed = editor();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(T.toolbarBg));
    // Portrait screens are narrower than the full toolbar, so it scrolls
    // horizontally there; landscape keeps the fixed, non-scrolling strip.
    const ImGuiWindowFlags toolbarFlags = isPortrait()
                                              ? ImGuiWindowFlags_None
                                              : ImGuiWindowFlags_NoScrollbar |
                                                    ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::BeginChild("##toolbar", ImVec2(0, metrics().toolbarHeight), 0,
                      toolbarFlags);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    ImGui::SetCursorPos(ImVec2(2.0f * currentUiScale(), 1.0f * currentUiScale()));

    // File group
    if (toolbarButton("new", "NewMap", "New Map", false, !ed.requestNewMap)) {
        ed.newMapName = ed.nextMapName();
        ed.requestNewMap = true;
    }
    if (toolbarButton("open", "OpenMap", "Open Map", false)) ed.requestOpen = true;
    if (toolbarButton("save", "SaveMap", "Save Map", false, ed.mapOpen)) {
        ed.requestSave = true;
    }
    toolbarSeparator();
    // Script
    if (toolbarButton("script", "Script2", "Open Script Editor", false)) {
        stub("Script Editor");
    }
    toolbarSeparator();
    // Undo / redo
    if (toolbarButton("undo", "Undo", "Undo", false, false)) stub("Undo");
    if (toolbarButton("redo", "Redo", "Redo", false, false)) stub("Redo");
    toolbarSeparator();
    // Clipboard
    if (toolbarButton("cut", "Cut", "Cut Selection", false, ed.mapOpen)) stub("Cut");
    if (toolbarButton("copy", "Copy", "Copy Selection", false, ed.mapOpen)) stub("Copy");
    if (toolbarButton("paste", "Paste", "Paste Selection", false, ed.mapOpen)) {
        stub("Paste");
    }
    toolbarSeparator();
    // Prefabs
    if (toolbarButton("prefab", "Prefab", "Insert Prefab from File", false, ed.mapOpen)) {
        stub("Insert Prefab from File");
    }
    if (toolbarButton("prefabprev", "Prefab2", "Insert Previous Prefab", false,
                      ed.mapOpen)) {
        stub("Insert Previous Prefab");
    }
    toolbarSeparator();
    // Filters / colours
    if (toolbarButton("thingsfilter", "Filter", "Configure Things Filters", false)) {
        stub("Configure Things Filters");
    }
    if (toolbarButton("linedefcolors", "LinedefColorPresets",
                      "Configure Linedef Colors", false)) {
        stub("Configure Linedef Colors");
    }
    toolbarSeparator();
    // Render toggles
    if (toolbarButton("brightness", "Brightness", "Full Brightness", ed.fullBrightness)) {
        ed.fullBrightness = !ed.fullBrightness;
    }
    if (toolbarButton("grid", "Grid2", "Render Grid", ed.showGrid)) {
        ed.showGrid = !ed.showGrid;
    }
    if (toolbarButton("comments", "Comment", "Show Comments", false)) {
        stub("Show Comments");
    }
    if (toolbarButton("fixedthings", "FixedThingsScale", "Fixed Things Scale",
                      ed.fixedThingsScale)) {
        ed.fixedThingsScale = !ed.fixedThingsScale;
    }
    toolbarSeparator();
    // View modes
    if (toolbarButton("viewnormal", "ViewNormal", "View Wireframe",
                      ed.viewMode == ViewMode::Wireframe)) {
        ed.viewMode = ViewMode::Wireframe;
        ed.status = "View mode: Wireframe";
    }
    if (toolbarButton("viewbright", "ViewBrightness", "View Brightness Levels",
                      ed.viewMode == ViewMode::Brightness)) {
        ed.viewMode = ViewMode::Brightness;
        ed.status = "View mode: Brightness Levels";
    }
    if (toolbarButton("viewfloors", "ViewTextureFloor", "View Floor Textures",
                      ed.viewMode == ViewMode::FloorTextures)) {
        ed.viewMode = ViewMode::FloorTextures;
        ed.status = "View mode: Floor Textures";
    }
    if (toolbarButton("viewceils", "ViewTextureCeiling", "View Ceiling Textures",
                      ed.viewMode == ViewMode::CeilingTextures)) {
        ed.viewMode = ViewMode::CeilingTextures;
        ed.status = "View mode: Ceiling Textures";
    }
    toolbarSeparator();
    // Geometry merge modes
    if (toolbarButton("mergeclassic", "MergeGeoClassic",
                      "Merge Dragged Vertices Only", false)) {
        stub("Merge Dragged Vertices Only");
    }
    if (toolbarButton("mergegeo", "MergeGeo", "Merge Dragged Geometry", false)) {
        stub("Merge Dragged Geometry");
    }
    if (toolbarButton("placegeo", "MergeGeoRemoveLines",
                      "Replace with Dragged Geometry", false)) {
        stub("Replace with Dragged Geometry");
    }
    toolbarSeparator();
    // Geometry helpers
    if (toolbarButton("snaptogrid", "Grid4", "Snap to Grid", ed.snapToGrid)) {
        ed.snapToGrid = !ed.snapToGrid;
        ed.status = ed.snapToGrid ? "Snap to grid enabled" : "Snap to grid disabled";
    }
    if (toolbarButton("dynamicgrid", "GridDynamic", "Dynamic Grid Size",
                      autoGridEnabled())) {
        toggleAutoGrid();
    }
    if (toolbarButton("automerge", "mergegeometry2", "Snap to Geometry", false)) {
        stub("Snap to Geometry");
    }
    if (toolbarButton("splitsectors", "SplitSectors",
                      "Split Joined Sectors when Drawing Lines", false)) {
        stub("Split Joined Sectors");
    }
    if (toolbarButton("cleartextures", "ClearTextures", "Auto Clear Sidedef Textures",
                      false)) {
        stub("Auto Clear Sidedef Textures");
    }
    toolbarSeparator();
    // Visual-mode helpers
    if (toolbarButton("dynlight", "Light", "Dynamic light mode", false)) {
        stub("Dynamic light mode");
    }
    if (toolbarButton("model", "Model", "Model rendering mode", false)) {
        stub("Model rendering mode");
    }
    if (toolbarButton("fog", "fog", "Render Fog (Visual mode)", false)) {
        stub("Render Fog");
    }
    if (toolbarButton("sky", "Sky", "Render Sky (Visual mode)", false)) stub("Render Sky");
    if (toolbarButton("classic", "ClassicRendering", "Classic Rendering (Visual mode)",
                      false)) {
        stub("Classic Rendering");
    }
    if (toolbarButton("eventlines", "InfoLine_None", "Event lines mode", false)) {
        stub("Event lines mode");
    }
    if (toolbarButton("visualverts", "VisualVertices",
                      "Show Editable Vertices (Visual mode)", false)) {
        stub("Show Editable Vertices");
    }
    toolbarSeparator();
    // Testing
    if (toolbarButton("test", "Test", "Test Map", false, ed.mapOpen)) stub("Test Map");

    // Help group, right-aligned like a menu bar's help section. These replace the
    // floating buttons this port used to overlay on the GL surface: on a phone the
    // toolbar is the natural home for them, and they are drawn by the same code as
    // every other button so they follow the theme, hover states and UI scale.
    {
        const float buttons = 4.0f;
        const float size = 22.0f * currentUiScale();
        const float spacing = 1.0f * currentUiScale();
        const float reserved = buttons * size + (buttons - 1.0f) * spacing +
                               6.0f * currentUiScale();
        const float avail = ImGui::GetWindowWidth() - ImGui::GetCursorPosX();
        if (avail > reserved) {
            ImGui::SameLine(ImGui::GetWindowWidth() - reserved);
        }
    }
    if (toolbarButton("help", "Help", "Reference Manual (F1)", false, true)) {
        ed.showHelp = true;
        ed.helpPage = "introduction.html";
        ed.helpHistory.clear();
    }
    if (toolbarButton("settings", "Preferences", "Settings: safe area, diagnostics", false, true)) {
        ed.showDiagnostics = true;
        ed.requestPlatformSettings = true;
    }
    if (toolbarButton("log", "Warning", "Diagnostics and log", false, true)) {
        ed.showDiagnostics = true;
        ed.requestDiagnosticsLog = true;
    }
    if (toolbarLayoutToggle()) {
        ed.mobileLayout = !ed.mobileLayout;
        ed.status = ed.mobileLayout ? "Mobile layout on" : "Desktop layout";
        requestFitToScreen();
    }

    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// Mode toolbar (vertical strip, left edge)
// ---------------------------------------------------------------------------

void drawModeToolbar() {
    Editor& ed = editor();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(T.toolbarBg));
    // Mobile layout: the mode list is taller than the screen, so it scrolls.
    // Reserve the scrollbar width in the child so the icons keep their full
    // cell size instead of being squeezed next to the scrollbar.
    const ImGuiWindowFlags modeFlags = ed.mobileLayout
                                           ? ImGuiWindowFlags_None
                                           : ImGuiWindowFlags_NoScrollbar |
                                                 ImGuiWindowFlags_NoScrollWithMouse;
    const float barWidth = ed.mobileLayout
                               ? metrics().modeBarWidth + ImGui::GetStyle().ScrollbarSize
                               : metrics().modeBarWidth;
    ImGui::BeginChild("##modebar", ImVec2(barWidth, 0), 0, modeFlags);

    const std::vector<EditModeInfo>& modes = editModeList();
    // Icons come from the plugins' [EditMode(ButtonImage = ...)] attributes.
    static const char* kModeIcons[] = {
        "DrawGeometryMode", "DrawCurveMode",     "DrawRectangleMode",
        "DrawEllipseMode",  "DrawGridMode",      "LinesMode",
        "SectorsMode",      "ThingsMode",        "VerticesMode",
        "FloorAlign",       "CeilingAlign",      "NewSector2",
        "Selection3",       "BridgeMode",        "FindMode",
        "MapAnalysisMode",  "Blockmap",          "VisualMode",
        "ThreeDFloorIcon",  "SlopeModeIcon",     "DrawSlopeModeIcon",
        "automap",          "blockmap",          "NodesView",
        "reject",           "Gauge",             "SoundPropagationIcon",
        "ZDoomSoundEnvironment", "StairIcon",    "WAuthor",
        "TriangulatorMode", "ImageIcon",
    };

    // Vertical strip: one 28x28 cell per mode, positioned explicitly so the
    // layout is deterministic regardless of ImGui's line wrapping.
    // toolbarButton() applies the UI scale itself, so work in design units here.
    const float scale = currentUiScale();
    const float cell = 28.0f;
    float y = 2.0f;
    for (size_t i = 0; i < modes.size(); ++i) {
        const EditModeInfo& info = modes[i];
        ImGui::SetCursorPos(ImVec2(1.0f * scale, y));
        if (toolbarButton(info.displayName, kModeIcons[i], info.displayName,
                          ed.mode == info.mode, true, cell - 2.0f)) {
            ed.setMode(info.mode);
        }
        y += cell;
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// Status bar
// ---------------------------------------------------------------------------

void drawStatusBar() {
    Editor& ed = editor();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(T.statusBg));
    ImGui::BeginChild("##status", ImVec2(0, metrics().statusHeight), 0,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 0));
    ImGui::SetCursorPos(ImVec2(4, 2));

    // statuslabel (spring) with the desktop's status icon.
    ed.icons.draw(ImGui::GetWindowDrawList(), "Status2",
                  ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + 2),
                  16.0f);
    ImGui::Dummy(ImVec2(18.0f, 16.0f));
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::TextUnformatted(ed.status.c_str());

    // Right-aligned fields, in the desktop's order:
    // config (280), grid (64), zoom (54), x (50), y (50), warnings.
    const float avail = ImGui::GetContentRegionAvail().x;
    const float rightWidth = 280.0f + 64.0f + 54.0f + 50.0f + 50.0f + 40.0f;
    if (avail > rightWidth) {
        ImGui::SameLine(ImGui::GetCursorPosX() + avail - rightWidth);
    } else {
        ImGui::SameLine();
    }

    ImGui::TextUnformatted(ed.configName.c_str());
    ImGui::SameLine(0.0f, 8.0f);

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%d mp", ed.view.gridSize);
    ImGui::TextUnformatted(buf);
    if (ImGui::IsItemClicked()) stub("Grid size");
    ImGui::SameLine(0.0f, 8.0f);

    std::snprintf(buf, sizeof(buf), "%s", formatZoom(ed.view.zoom).c_str());
    ImGui::TextUnformatted(buf);
    ImGui::SameLine(0.0f, 8.0f);

    std::snprintf(buf, sizeof(buf), "%d", (int)std::lround(ed.cursorX));
    ImGui::TextUnformatted(buf);
    ImGui::SameLine(0.0f, 2.0f);
    ImGui::TextUnformatted(",");
    ImGui::SameLine(0.0f, 2.0f);
    std::snprintf(buf, sizeof(buf), "%d", (int)std::lround(ed.cursorY));
    ImGui::TextUnformatted(buf);

    const size_t warnCount = ed.warnings.size();
    if (warnCount > 0) {
        ImGui::SameLine(0.0f, 8.0f);
        ed.icons.draw(ImGui::GetWindowDrawList(), "Warning",
                      ImVec2(ImGui::GetCursorScreenPos().x,
                             ImGui::GetCursorScreenPos().y + 1),
                      14.0f);
        ImGui::Dummy(ImVec2(16.0f, 14.0f));
        if (ImGui::IsItemClicked()) ed.showErrors = true;
        ImGui::SameLine(0.0f, 2.0f);
        std::snprintf(buf, sizeof(buf), "%d", (int)warnCount);
        ImGui::TextUnformatted(buf);
    }

    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

} // namespace ui
} // namespace pdb
