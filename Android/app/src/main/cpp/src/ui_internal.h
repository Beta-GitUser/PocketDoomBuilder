// PocketDoomBuilder — Android port (Dear ImGui)
// ui_internal.h — Shared declarations for the split UI translation units.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "imgui.h"

namespace pdb {

// Asset access, implemented per platform. The Android shell reads from the APK
// asset manager; the headless preview and the tests read from the source tree.
bool platformReadAsset(const std::string& path, std::vector<uint8_t>& out);

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

// Help menu windows (ui_help.cpp): reference manual, shortcuts, about.
void drawHelpMenuWindows();

// Forget the cached viewport size so the next frame re-fits the map.
void resetMapViewState();

} // namespace ui
} // namespace pdb
