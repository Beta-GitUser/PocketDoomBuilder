// PocketDoomBuilder — Android port (Dear ImGui)
// ui.cpp — Main window assembly and the design-space scaler.

#include "ui.h"

#include <algorithm>
#include <cmath>

#include "editor.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "theme.h"
#include "ui_internal.h"

namespace pdb {

namespace {
float g_uiScale = 1.0f;
// True when the display is taller than it is wide; the desktop layout assumes
// landscape, so portrait stacks the panels instead of squeezing them.
bool g_portrait = false;
Metrics g_metrics;

// Design-space metrics scaled to the display, like the desktop's DPI
// auto-scaling (MainForm.AutoScaleMode = Dpi).
void updateMetrics(float scale) {
    g_metrics.designWidth = 1012.0f * scale;
    g_metrics.designHeight = 693.0f * scale;
    g_metrics.menuHeight = 24.0f * scale;
    g_metrics.toolbarHeight = 25.0f * scale;
    g_metrics.statusHeight = 23.0f * scale;
    g_metrics.modeBarWidth = 30.0f * scale;
    g_metrics.infoPanelHeight = 106.0f * scale;
    g_metrics.dockerWidth = 236.0f * scale;
    g_metrics.statisticsWidth = 138.0f * scale;
    g_metrics.iconSize = 16.0f * scale;
}
} // namespace

float currentUiScale() { return g_uiScale; }

const Metrics& metrics() { return g_metrics; }

void requestFitToScreen() { ui::requestFitToScreen(); }

void drawMainWindow(float displayWidth, float displayHeight) {
    Editor& ed = editor();

    // Scale the 1012x693 design space to the display, like the desktop's
    // DPI auto-scaling. The desktop is landscape (MainForm is wider than it is
    // tall); a phone in portrait gives a very different shape. Fitting 1012px
    // of width into a 720px screen by scale alone shrank everything to 0.71 and
    // still laid out the window as if it were landscape, so in portrait the
    // scale is what the *width* allows (clamped for legibility) and the panels
    // are stacked rather than squeezed side by side.
    const bool portrait = displayHeight > displayWidth * 1.05f;
    g_portrait = portrait;
    const float sx = displayWidth / 1012.0f;
    const float sy = displayHeight / 693.0f;
    g_uiScale = portrait ? std::max(0.55f, std::min(sx, 1.15f))
                         : std::max(0.75f, std::min(sx, sy));
    updateMetrics(g_uiScale);

    // Re-apply the theme so paddings, borders and the font size follow the
    // current scale (safe every frame: applyTheme sets absolute values).
    applyTheme(g_uiScale);
    ImGui::GetStyle().FontSizeBase = 13.0f * g_uiScale;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->WorkPos);
    ImGui::SetNextWindowSize(ImGui::GetMainViewport()->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
                          ImGui::ColorConvertU32ToFloat4(theme().windowBg));
    ImGui::Begin("##mainform", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ui::drawMenuBar();
    ui::drawToolbar();

    // Middle band: mode bar | dockers | (mode controls + map view)
    const float panelHeight =
        ed.infoPanelExpanded
            ? (ed.infoPanelHeight > 0.0f ? ed.infoPanelHeight * g_uiScale
                                         : metrics().infoPanelHeight)
            : 21.0f * g_uiScale;
    const float bandHeight =
        ImGui::GetContentRegionAvail().y - panelHeight - metrics().statusHeight;
    ImGui::BeginChild("##band", ImVec2(0, std::max(80.0f, bandHeight)), 0,
                      ImGuiWindowFlags_NoScrollbar);
    // MainForm: modestoolbar.Visible = maploaded; when hidden it takes no
    // space, so the dockers panel slides to the left edge.
    if (ed.mapOpen) {
        ui::drawModeToolbar();
        ImGui::SameLine(0.0f, 0.0f);
    }
    if (ed.dockerVisible && !g_portrait) {
        ui::drawDockersPanel();
        ImGui::SameLine(0.0f, 0.0f);
    }
    // The desktop's modecontrolsloolbar is invisible by default (only some
    // modes populate it), so the map view fills the column here too.
    ImGui::BeginChild("##viewcolumn", ImVec2(0, 0), 0, ImGuiWindowFlags_NoScrollbar);
    ui::drawMapView();
    ImGui::EndChild();
    ImGui::EndChild();

    ui::drawInfoPanel();
    ui::drawStatusBar();

    ImGui::End();

    ui::drawDialogs();
}

} // namespace pdb
