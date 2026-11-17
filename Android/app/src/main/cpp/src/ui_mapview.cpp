// PocketDoomBuilder — Android port (Dear ImGui)
// ui_mapview.cpp — The 2D map view widget (desktop: display /
// RenderTargetControl) with pan, zoom and tap selection.

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "editor.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "render2d.h"
#include "theme.h"
#include "ui.h"
#include "ui_internal.h"

namespace pdb {
namespace ui {

namespace {

const ThemeColors& T = theme();

bool g_fitRequested = true;
float g_lastW = 0.0f;
float g_lastH = 0.0f;

// Middle-drag / two-finger drag pan state.
ImVec2 g_panAnchor(0, 0);
bool g_panning = false;

// Tap-vs-drag discrimination so a tap selects and a drag pans.
ImVec2 g_pressPos(0, 0);
bool g_pressed = false;

} // namespace

void drawMapView() {
    Editor& ed = editor();

    // The desktop's display control has a Fixed3D border; draw the map into a
    // child with a 2px border to reproduce that.
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
                          ImGui::ColorConvertU32ToFloat4(T.canvasBorder));
    ImGui::BeginChild("##displayframe", ImVec2(0, 0), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const ImVec2 framePos = ImGui::GetCursorScreenPos();
    const ImVec2 frameSize = ImGui::GetContentRegionAvail();
    const float viewW = std::max(16.0f, frameSize.x);
    const float viewH = std::max(16.0f, frameSize.y);

    if (g_fitRequested || g_lastW != viewW || g_lastH != viewH) {
        if (ed.mapOpen) {
            // Keep the same world point centred when only the size changed.
            const double cx = ed.view.screenToMapX(g_lastW * 0.5f);
            const double cy = ed.view.screenToMapY(g_lastH * 0.5f);
            if (g_fitRequested || g_lastW <= 0.0f) {
                ed.view.fitToMap(ed.map, viewW, viewH);
            } else {
                ed.view.centreOn(cx, cy, viewW, viewH);
            }
        }
        g_lastW = viewW;
        g_lastH = viewH;
        g_fitRequested = false;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(framePos, ImVec2(framePos.x + viewW, framePos.y + viewH), true);

    // Input: an invisible button covering the view handles press/drag/tap.
    ImGui::SetCursorScreenPos(framePos);
    ImGui::InvisibleButton("##mapcanvas", ImVec2(viewW, viewH),
                           ImGuiButtonFlags_MouseButtonLeft |
                               ImGuiButtonFlags_MouseButtonMiddle);

    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 mouse = ImGui::GetIO().MousePos;

    if (ed.mapOpen) {
        // Track the cursor for the status bar.
        ed.cursorX = ed.view.screenToMapX(mouse.x - framePos.x);
        ed.cursorY = ed.view.screenToMapY(mouse.y - framePos.y);

        // Hover highlight (desktop: Highlight mode).
        if (hovered && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ed.hover = ed.view.pick(ed.map, mouse.x - framePos.x,
                                    mouse.y - framePos.y, 8.0f);
        } else {
            ed.hover = PickResult();
        }
    }

    // Panning: middle-drag (mouse) or a one-finger drag (touch), matching
    // the desktop's pan_view action.
    const ImGuiIO& io = ImGui::GetIO();
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        g_panning = true;
        g_panAnchor = mouse;
    }
    if (hovered && io.MouseDown[0] && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 6.0f)) {
        g_panning = true;
    }
    if (g_panning && (io.MouseDown[0] || io.MouseDown[2])) {
        const ImVec2 delta(io.MouseDelta.x, io.MouseDelta.y);
        if (delta.x != 0.0f || delta.y != 0.0f) {
            ed.view.pan(delta.x, delta.y);
            g_fitRequested = false;
        }
    } else {
        g_panning = false;
    }

    // Tap selection: press and release without moving.
    if (hovered && ImGui::IsItemActivated()) {
        g_pressed = true;
        g_pressPos = mouse;
    }
    if (g_pressed && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        const float dx = mouse.x - g_pressPos.x;
        const float dy = mouse.y - g_pressPos.y;
        if (dx * dx + dy * dy < 64.0f && ed.mapOpen) {
            ed.selectAt(mouse.x - framePos.x, mouse.y - framePos.y);
        }
        g_pressed = false;
    }

    // Zoom: wheel (mouse / trackpad) and pinch (touch is handled by the
    // platform layer's mouse emulation for phase 1).
    if (hovered && io.MouseWheel != 0.0f && ed.mapOpen) {
        const float factor = io.MouseWheel > 0.0f ? 1.25f : 1.0f / 1.25f;
        ed.view.zoomAt(mouse.x - framePos.x, mouse.y - framePos.y, factor, viewW,
                       viewH);
        ed.status = "Zoom " + formatZoom(ed.view.zoom);
        g_fitRequested = false;
    }

    // Render.
    RenderOptions options;
    options.viewMode = ed.viewMode;
    options.drawGrid = ed.showGrid;
    options.drawVertices = ed.showVertices;
    options.drawThings = ed.showThings;
    options.highlightMode = true;

    if (ed.mapOpen) {
        dl->PushClipRect(framePos, ImVec2(framePos.x + viewW, framePos.y + viewH), true);
        renderMap2D(dl, ed.map, ed.view, ed.colors, options, ed.hover,
                    ed.selection, framePos);
        dl->PopClipRect();
    } else {
        dl->AddRectFilled(framePos, ImVec2(framePos.x + viewW, framePos.y + viewH),
                          ed.colors[COLOR_BACKGROUND]);
        const char* msg = "No map open";
        const ImVec2 size = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(framePos.x + (viewW - size.x) * 0.5f,
                           framePos.y + (viewH - size.y) * 0.5f),
                    IM_COL32(180, 180, 180, 255), msg);
        const char* hint = "File > Open Map... to load a WAD";
        const ImVec2 hintSize = ImGui::CalcTextSize(hint);
        dl->AddText(ImVec2(framePos.x + (viewW - hintSize.x) * 0.5f,
                           framePos.y + (viewH - hintSize.y) * 0.5f + 18.0f),
                    IM_COL32(140, 140, 140, 255), hint);
    }

    dl->PopClipRect();
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void requestFitToScreen() { g_fitRequested = true; }

void resetMapViewState() {
    g_fitRequested = true;
    g_lastW = 0.0f;
    g_lastH = 0.0f;
}

} // namespace ui
} // namespace pdb
