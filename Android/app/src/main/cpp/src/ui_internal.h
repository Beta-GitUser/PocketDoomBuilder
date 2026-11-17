// PocketDoomBuilder — Android port (Dear ImGui)
// ui_internal.h — Shared declarations for the split UI translation units.

#pragma once

#include "imgui.h"

namespace pdb {
namespace ui {

void drawMenuBar();
void drawToolbar();
void drawModeToolbar();
void drawStatusBar();

void drawDockersPanel();
void drawInfoPanel();
void drawMapView();
void drawDialogs();
void requestFitToScreen();

// Dynamic grid size toggle (Edit > Dynamic Grid Size).
bool autoGridEnabled();
void toggleAutoGrid();

// Forget the cached viewport size so the next frame re-fits the map.
void resetMapViewState();

} // namespace ui
} // namespace pdb
