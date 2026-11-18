// PocketDoomBuilder — Android port (Dear ImGui)
// ui.h — The 1:1 re-creation of Ultimate Doom Builder's main window.
//
// Layout reproduced from Source/Core/Windows/MainForm.Designer.cs
// (design size 1012 x 693 at 96 DPI):
//
//   +--------------------------------------------------------+
//   | menu strip: File Edit View Mode Prefabs Tools Help      |  24 px
//   +--------------------------------------------------------+
//   | toolbar (file / script / undo / clipboard / prefabs /   |  25 px
//   |          filters / grid / view modes / merge / testing) |
//   +------+-----------------+-------------------------------+
//   | mode | dockers panel   | 2D map view (Fixed3D border)   |
//   | bar  | (tabs)          |                                |
//   | 30px | 236 x 467       |                                |
//   +------+-----------------+-------------------------------+
//   | info panel / console + statistics                       | 106 px
//   +--------------------------------------------------------+
//   | status bar: status, config, grid, zoom, x, y, warnings  |  23 px
//   +--------------------------------------------------------+

#pragma once

#include "imgui.h"

namespace pdb {

// Design metrics from MainForm.Designer.cs (96 DPI pixels), scaled to the
// current display. Values are recomputed every frame by drawMainWindow().
struct Metrics {
    float designWidth = 1012.0f;
    float designHeight = 693.0f;
    float menuHeight = 24.0f;      // menumain
    float toolbarHeight = 25.0f;   // toolbar
    float statusHeight = 23.0f;    // statusbar
    float modeBarWidth = 30.0f;    // modestoolbar
    float infoPanelHeight = 106.0f;// panelinfo (heightpanel1)
    float dockerWidth = 236.0f;    // dockerspanel
    float statisticsWidth = 138.0f;// statistics
    float iconSize = 16.0f;        // ScaledIconSize at 96 DPI
};

const Metrics& metrics();

// Build and draw the whole interface for one frame.
// `displayWidth`/`displayHeight` are in framebuffer pixels; the shell scales
// its 1012x693 design space to fit while keeping the 1:1 proportions.
void drawMainWindow(float displayWidth, float displayHeight);

// Current UI scale (design units -> pixels). Exposed for the platform layer
// so it can size the font atlas and touch slop.
float currentUiScale();

// Request the shell to re-fit the map on the next frame.
void requestFitToScreen();

} // namespace pdb
