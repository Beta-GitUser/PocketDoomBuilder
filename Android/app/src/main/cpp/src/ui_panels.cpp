// PocketDoomBuilder — Android port (Dear ImGui)
// ui_panels.cpp — Dockers panel, info panels, console and statistics.
//
// The info panel layout (group boxes, label/value pairs) follows
// Source/Core/Controls/{Vertex,Linedef,Sector,Thing}InfoPanel.Designer.cs;
// the bottom panel and its children follow panelinfo in
// MainForm.Designer.cs (console on the left, statistics docked right).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "editor.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "theme.h"
#include "ui.h"
#include "ui_internal.h"
#include "view2d.h"

namespace pdb {
namespace ui {

namespace {

const ThemeColors& T = theme();

bool g_autoGrid = false;

// The desktop draws info panels as WinForms GroupBoxes: a 1px border with a
// caption label sitting on the border line. Reproduce that look.
bool beginGroupBox(const char* caption, ImVec2 size) {
    ImGui::BeginChild(caption, size, ImGuiChildFlags_Borders);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::Text("%s", caption);
    ImGui::PopStyleColor();
    return true;
}

void endGroupBox() { ImGui::EndChild(); }

// A "Label: value" row, as in the info panels.
void infoRow(const char* label, const std::string& value, float labelWidth = 90.0f) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(labelWidth);
    ImGui::TextUnformatted(value.c_str());
}

std::string texOrNone(const std::string& tex) {
    if (tex.empty() || tex == "-") return "-";
    return tex;
}

void drawVertexInfo() {
    const Editor& ed = editor();
    if (ed.map.vertices.empty()) return;
    const Vertex& v = ed.map.vertices[0];
    beginGroupBox(" Vertex ", ImVec2(220, 98));
    infoRow("Position:", formatCoordinates(v.x, v.y), 62.0f);
    infoRow("Floor offset:", "0.00", 62.0f);
    infoRow("Ceiling offset:", "0.00", 62.0f);
    endGroupBox();
}

void drawLinedefInfo() {
    const Editor& ed = editor();
    if (ed.map.linedefs.empty()) return;
    const Linedef& l = ed.map.linedefs[0];
    char buf[128];

    beginGroupBox(" Linedef ", ImVec2(330, 98));
    std::snprintf(buf, sizeof(buf), "%d", (int)l.action);
    infoRow("Action:", buf, 62.0f);
    infoRow("Activation:", "0 - When player crosses", 62.0f);
    std::snprintf(buf, sizeof(buf), "%d", (int)l.tag);
    infoRow("Tag:", buf, 62.0f);
    std::snprintf(buf, sizeof(buf), "%d", (int)std::lround(
                     linedefAngleDegrees(ed.map, l)));
    infoRow("Angle:", buf, 62.0f);
    std::snprintf(buf, sizeof(buf), "%d", (int)std::lround(
                     linedefLength(ed.map, l)));
    infoRow("Length:", buf, 62.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::Text("Args: %d %d %d %d %d", l.args[0], l.args[1], l.args[2], l.args[3],
                l.args[4]);
    ImGui::PopStyleColor();
    endGroupBox();

    ImGui::SameLine();
    beginGroupBox(" Front ", ImVec2(230, 98));
    const Sidedef* front = (l.sideFront >= 0 && (size_t)l.sideFront < ed.map.sidedefs.size())
                               ? &ed.map.sidedefs[(size_t)l.sideFront]
                               : nullptr;
    infoRow("Offset:", front ? formatCoordinates(front->offsetX, front->offsetY) : "0, 0",
            56.0f);
    infoRow("High:", front ? texOrNone(front->texTop) : "-", 56.0f);
    infoRow("Middle:", front ? texOrNone(front->texMid) : "-", 56.0f);
    infoRow("Low:", front ? texOrNone(front->texBottom) : "-", 56.0f);
    endGroupBox();

    ImGui::SameLine();
    beginGroupBox(" Back ", ImVec2(230, 98));
    const Sidedef* back = (l.sideBack >= 0 && (size_t)l.sideBack < ed.map.sidedefs.size())
                              ? &ed.map.sidedefs[(size_t)l.sideBack]
                              : nullptr;
    if (back) {
        infoRow("Offset:", formatCoordinates(back->offsetX, back->offsetY), 56.0f);
        infoRow("High:", texOrNone(back->texTop), 56.0f);
        infoRow("Middle:", texOrNone(back->texMid), 56.0f);
        infoRow("Low:", texOrNone(back->texBottom), 56.0f);
    } else {
        ImGui::TextDisabled("(none)");
    }
    endGroupBox();
}

void drawSectorInfo() {
    const Editor& ed = editor();
    if (ed.map.sectors.empty()) return;
    const Sector& s = ed.map.sectors[0];
    char buf[128];

    beginGroupBox(" Sector ", ImVec2(300, 98));
    std::snprintf(buf, sizeof(buf), "%d", s.brightness);
    infoRow("Brightness:", buf, 76.0f);
    std::snprintf(buf, sizeof(buf), "%d", s.ceilHeight - s.floorHeight);
    infoRow("Height:", buf, 76.0f);
    std::snprintf(buf, sizeof(buf), "%d", (int)s.tag);
    infoRow("Tag:", buf, 76.0f);
    std::snprintf(buf, sizeof(buf), "%d", (int)s.special);
    infoRow("Effect:", buf, 76.0f);
    endGroupBox();

    ImGui::SameLine();
    beginGroupBox(" Floor ", ImVec2(200, 98));
    infoRow("Height:", std::to_string(s.floorHeight), 56.0f);
    infoRow("Texture:", texOrNone(s.floorTex), 56.0f);
    endGroupBox();

    ImGui::SameLine();
    beginGroupBox(" Ceiling ", ImVec2(200, 98));
    infoRow("Height:", std::to_string(s.ceilHeight), 56.0f);
    infoRow("Texture:", texOrNone(s.ceilTex), 56.0f);
    endGroupBox();
}

void drawThingInfo() {
    const Editor& ed = editor();
    if (ed.map.things.empty()) return;
    const Thing& t = ed.map.things[0];
    char buf[128];

    beginGroupBox(" Thing ", ImVec2(330, 98));
    std::snprintf(buf, sizeof(buf), "%d", t.type);
    infoRow("Type:", buf, 62.0f);
    infoRow("Class:", "-", 62.0f);
    std::snprintf(buf, sizeof(buf), "%d", t.action);
    infoRow("Action:", buf, 62.0f);
    std::snprintf(buf, sizeof(buf), "%d", t.angle);
    infoRow("Angle:", buf, 62.0f);
    std::snprintf(buf, sizeof(buf), "%d", (int)t.tag);
    infoRow("Tag:", buf, 62.0f);
    endGroupBox();

    ImGui::SameLine();
    beginGroupBox(" Position ", ImVec2(240, 98));
    infoRow("X:", std::to_string(t.x), 24.0f);
    infoRow("Y:", std::to_string(t.y), 24.0f);
    infoRow("Z:", std::to_string(t.z), 24.0f);
    endGroupBox();

    ImGui::SameLine();
    beginGroupBox(" Sprite ", ImVec2(110, 98));
    ImGui::TextDisabled("%s", "TROO");
    endGroupBox();
}

void drawStatistics() {
    const Editor& ed = editor();
    ImGui::BeginChild("##statistics", ImVec2(metrics().statisticsWidth, 0), 0,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

    // StatisticsControl.Designer.cs: Vertices / Linedefs / Sidedefs /
    // Sectors / Things, label on the left and the count right-aligned.
    auto row = [](const char* label, int count) {
        ImGui::TextUnformatted(label);
        const float x = ImGui::GetContentRegionAvail().x -
                        ImGui::CalcTextSize(std::to_string(count).c_str()).x - 2.0f;
        if (x > 0.0f) ImGui::SameLine(ImGui::GetCursorPosX() + x);
        ImGui::TextUnformatted(std::to_string(count).c_str());
    };
    row("Vertices", (int)ed.map.vertices.size());
    row("Linedefs", (int)ed.map.linedefs.size());
    row("Sidedefs", (int)ed.map.sidedefs.size());
    row("Sectors", (int)ed.map.sectors.size());
    row("Things", (int)ed.map.things.size());

    ImGui::PopStyleColor();
    ImGui::EndChild();
}

// --- Dockers ---------------------------------------------------------------

// ---------------------------------------------------------------------------
// Maps docker: the list of maps in the loaded WAD, like the desktop's
// maplist control. Clicking a name opens that map.
// ---------------------------------------------------------------------------

void drawMapsDocker() {
    Editor& ed = editor();
    if (!ed.wadLoaded) {
        ImGui::TextDisabled("No WAD is open.");
        return;
    }
    // The WAD name comes from the file picker's URI; never let a raw
    // content:// path or control bytes reach the display.
    std::string wadLabel;
    for (char c : ed.wadName) {
        if (c >= 32 && c < 127) wadLabel.push_back(c);
    }
    if (wadLabel.empty()) wadLabel = "(unnamed WAD)";
    ImGui::Text("WAD: %s", wadLabel.c_str());
    ImGui::Separator();
    if (ed.mapNames.empty()) {
        ImGui::TextDisabled("This WAD contains no maps.");
        return;
    }
    for (size_t i = 0; i < ed.mapNames.size(); ++i) {
        ImGui::PushID((int)i);
        const bool active = ed.mapOpen && ed.currentMapIndex == (int)i;
        const bool selected = ImGui::Selectable(ed.mapNames[i].c_str(), active);
        if (selected && !active) {
            ed.openMapByIndex((int)i);
            requestFitToScreen();
        }
        ImGui::PopID();
    }
}

void drawUndoRedoDocker() {
    ImGui::TextDisabled("Undo / Redo history");
    ImGui::Separator();
    ImGui::TextDisabled("(empty)");
    ImGui::Spacing();
    // The desktop shows a list of actions with icons; phase 1 has no
    // undo system yet, so the list stays empty.
    ImGui::BeginChild("##undolist", ImVec2(0, 0), ImGuiChildFlags_Borders);
    ImGui::TextDisabled("Undo history is not implemented in phase 1.");
    ImGui::EndChild();
}

void drawDrawSettingsDocker() {
    ImGui::TextDisabled("Draw Settings");
    ImGui::Separator();
    static bool autoClose = false;
    static bool autoBrightness = false;
    static bool autoSky = false;
    ImGui::Checkbox("Auto close drawing", &autoClose);
    ImGui::Checkbox("Auto brightness levels", &autoBrightness);
    ImGui::Checkbox("Auto sky floor/ceiling", &autoSky);
    ImGui::Spacing();
    ImGui::TextDisabled("(editing stubbed in phase 1)");
}

void drawTagExplorerDocker() {
    ImGui::TextDisabled("Tag Explorer");
    ImGui::Separator();
    if (!editor().mapOpen) {
        ImGui::TextDisabled("No map is open.");
        return;
    }
    // Group sectors by tag, like TagExplorer does.
    ImGui::BeginChild("##tags", ImVec2(0, 0), ImGuiChildFlags_Borders);
    for (size_t i = 0; i < editor().map.sectors.size(); ++i) {
        const Sector& s = editor().map.sectors[i];
        if (s.tag == 0) continue;
        ImGui::Text("%d: sector %d", (int)s.tag, (int)i);
    }
    ImGui::EndChild();
}

void drawCommentsDocker() {
    ImGui::TextDisabled("Comments");
    ImGui::Separator();
    ImGui::TextDisabled("(no comments in this map)");
}

void drawScriptsDocker() {
    ImGui::TextDisabled("Scripts");
    ImGui::Separator();
    const Editor& ed = editor();
    if (ed.mapOpen && !ed.map.behaviorLump.empty()) {
        ImGui::Text("BEHAVIOR lump: %d bytes", (int)ed.map.behaviorLump.size());
    } else {
        ImGui::TextDisabled("No scripts.");
    }
}

} // namespace

bool autoGridEnabled() { return g_autoGrid; }
void toggleAutoGrid() {
    g_autoGrid = !g_autoGrid;
    editor().status = g_autoGrid ? "Dynamic grid size enabled"
                                 : "Dynamic grid size disabled";
}

// ---------------------------------------------------------------------------
// Dockers panel (left, 236 x 467 in the design)
// ---------------------------------------------------------------------------

void drawDockersPanel() {
    Editor& ed = editor();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(T.windowBg));
    ImGui::BeginChild("##dockers", ImVec2(metrics().dockerWidth, 0),
                      ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);

    const std::vector<DockerInfo>& dockers = dockerList();
    if (ImGui::BeginTabBar("##dockertabs", ImGuiTabBarFlags_None)) {
        for (size_t i = 0; i < dockers.size(); ++i) {
            // Only the active docker's content is laid out.
            if (ImGui::BeginTabItem(dockers[i].title)) {
                ed.activeDocker = (int)i;
                ImGui::BeginChild("##dockercontent", ImVec2(0, 0));
                switch (dockers[i].tab) {
                    case DockerTab::Maps: drawMapsDocker(); break;
                    case DockerTab::UndoRedo: drawUndoRedoDocker(); break;
                    case DockerTab::DrawSettings: drawDrawSettingsDocker(); break;
                    case DockerTab::TagExplorer: drawTagExplorerDocker(); break;
                    case DockerTab::Comments: drawCommentsDocker(); break;
                    case DockerTab::Scripts: drawScriptsDocker(); break;
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// Bottom info panel (console + info panels + statistics)
// ---------------------------------------------------------------------------

void drawInfoPanel() {
    Editor& ed = editor();

    // MainForm.ToggleInfoPanel(): collapsed, panelinfo is only as tall as the
    // toggle button (21 px) and shows the mode name / last info caption; when
    // expanded it becomes heightpanel1.Height (106 px) and holds the info
    // panel, statistics and (optionally) the console.
    const float panelHeight =
        ed.infoPanelExpanded
            ? (ed.infoPanelHeight > 0.0f ? ed.infoPanelHeight * currentUiScale()
                                         : metrics().infoPanelHeight)
            : 21.0f * currentUiScale();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(T.windowBg));
    ImGui::BeginChild("##panelinfo", ImVec2(0, panelHeight), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const ImVec2 winPos = ImGui::GetWindowPos();
    const ImVec2 winSize = ImGui::GetWindowSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Console toggle, left of the collapse button (the desktop console is only
    // shown by default in DEBUG builds; this button stands in for that).
    ImGui::SetCursorScreenPos(ImVec2(winPos.x + winSize.x - 50.0f, winPos.y + 1.0f));
    if (ImGui::InvisibleButton("##toggleconsole", ImVec2(22.0f, 19.0f))) {
        ed.showConsole = !ed.showConsole;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show console");
    dl->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), T.border);
    ed.icons.draw(dl, "Script", ImVec2(ImGui::GetItemRectMin().x + 3.0f,
                                       ImGui::GetItemRectMin().y + 1.5f),
                  16.0f);

    // buttontoggleinfo: 22 x 19, anchored top-right (x = width - 24).
    ImGui::SetCursorScreenPos(ImVec2(winPos.x + winSize.x - 24.0f, winPos.y + 1.0f));
    if (ImGui::InvisibleButton("##toggleinfo", ImVec2(22.0f, 19.0f))) {
        ed.infoPanelExpanded = !ed.infoPanelExpanded;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(ed.infoPanelExpanded ? "Collapse info panel"
                                               : "Expand info panel");
    }
    const ImVec2 btnMin = ImGui::GetItemRectMin();
    const ImVec2 btnMax = ImGui::GetItemRectMax();
    dl->AddRect(btnMin, btnMax, T.border);
    ed.icons.draw(dl, ed.infoPanelExpanded ? "InfoPanelCollapse" : "InfoPanelExpand",
                  ImVec2(btnMin.x + 3.0f, btnMin.y + 1.5f), 16.0f);

    if (!ed.infoPanelExpanded) {
        // labelcollapsedinfo: short description of the mode or selection.
        ImGui::SetCursorPos(ImVec2(6, 3));
        const char* text = ed.modeInfo().displayName;
        switch (ed.selection.kind) {
            case PickKind::Vertex: text = "Vertex"; break;
            case PickKind::Linedef: text = "Linedef"; break;
            case PickKind::Sector: text = "Sector"; break;
            case PickKind::Thing: text = "Thing"; break;
            case PickKind::None: break;
        }
        ImGui::TextDisabled("%s", text);
        ImGui::EndChild();
        ImGui::PopStyleColor();
        return;
    }

    // Expanded layout. Like the desktop, the console and the info panels share
    // the same rectangle inside panelinfo (only one is visible at a time), and
    // the statistics block is docked to the right edge.
    ImGui::SetCursorPos(ImVec2(3.0f * currentUiScale(), 2.0f * currentUiScale()));

    if (ed.showConsole) {
        const float consoleW = ImGui::GetContentRegionAvail().x -
                               metrics().statisticsWidth - 6.0f * currentUiScale();
        ImGui::BeginChild("##console", ImVec2(consoleW, 96.0f * currentUiScale()),
                          ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_HorizontalScrollbar);
        for (const ConsoleLine& line : ed.console) {
            ImU32 color = T.text;
            if (line.level == 1) color = IM_COL32(150, 100, 0, 255);
            if (line.level == 2) color = IM_COL32(190, 40, 40, 255);
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImGui::ColorConvertU32ToFloat4(color));
            ImGui::TextUnformatted(line.text.c_str());
            ImGui::PopStyleColor();
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    } else {
        // The info panel shown depends on what is selected, falling back to the
        // current editing mode, exactly like MainForm.ShowInfo/HideInfo.
        switch (ed.selection.kind) {
            case PickKind::Vertex: drawVertexInfo(); break;
            case PickKind::Sector: drawSectorInfo(); break;
            case PickKind::Thing: drawThingInfo(); break;
            case PickKind::Linedef:
            case PickKind::None:
            default:
                if (ed.mode == EditMode::Things) drawThingInfo();
                else if (ed.mode == EditMode::Sectors) drawSectorInfo();
                else if (ed.mode == EditMode::Vertices) drawVertexInfo();
                else drawLinedefInfo();
                break;
        }
    }

    // StatisticsControl is anchored to the right edge of panelinfo.
    const float statsX = ImGui::GetWindowSize().x - metrics().statisticsWidth -
                         4.0f * currentUiScale();
    ImGui::SameLine();
    ImGui::SetCursorPosX(statsX);
    drawStatistics();

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

} // namespace ui
} // namespace pdb
