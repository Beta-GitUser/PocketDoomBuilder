// PocketDoomBuilder — Android port (Dear ImGui)
// theme.h — Colours and metrics matching Ultimate Doom Builder's desktop UI.
//
// Map colours come straight from Source/Core/Rendering/ColorCollection.cs
// (indices + defaults). The widget chrome reproduces the WinForms /
// .NET "Visual Styles" look the desktop app uses on Windows 10: light
// grey toolbars, white document background, dark canvas border.

#pragma once

#include "imgui.h"

namespace pdb {

// ---------------------------------------------------------------------------
// Map render colours — mirror of ColorCollection indices/defaults.
// ---------------------------------------------------------------------------

enum ColorIndex {
    COLOR_BACKGROUND = 0,
    COLOR_VERTICES = 1,
    COLOR_LINEDEFS = 2,
    COLOR_MODELWIRE = 3,
    COLOR_INFOLINE = 4,
    COLOR_HIGHLIGHT = 5,
    COLOR_SELECTION = 6,
    COLOR_INDICATION = 7,
    COLOR_GRID = 8,
    COLOR_GRID64 = 9,
    COLOR_CROSSHAIR3D = 10,
    COLOR_HIGHLIGHT3D = 11,
    COLOR_SELECTION3D = 12,
    COLOR_SCRIPTBACKGROUND = 13,
    COLOR_LINENUMBERS = 14,
    COLOR_PLAINTEXT = 15,
    COLOR_COMMENTS = 16,
    COLOR_KEYWORDS = 17,
    COLOR_LITERALS = 18,
    COLOR_CONSTANTS = 19,
    COLOR_THINGCOLOR00 = 20, // .. +19
    COLOR_THREEDFLOOR = 40,
    COLOR_GUIDELINE = 52,
};

// The desktop stores ARGB ints; these are the same values as ImU32 ABGR
// (ImGui uses IM_COL32(r,g,b,a) which packs the same byte order).
struct MapColors {
    ImU32 colors[53];

    MapColors();
    ImU32& operator[](int index) { return colors[index]; }
    ImU32 operator[](int index) const { return colors[index]; }
};

// ARGB int (0xAARRGGBB, as in ColorCollection) -> ImU32.
ImU32 argbToImU32(int argb);

// Default thing colour for a thing type (mod 20, like the desktop).
ImU32 thingColor(const MapColors& colors, int thingType);

// ---------------------------------------------------------------------------
// Application chrome
// ---------------------------------------------------------------------------

struct ThemeColors {
    ImU32 windowBg;      // form background
    ImU32 childBg;       // panels
    ImU32 menuBg;        // menu strip
    ImU32 toolbarBg;     // tool strip
    ImU32 statusBg;      // status strip
    ImU32 border;        // control borders
    ImU32 text;          // control text
    ImU32 textDisabled;  // greyed text (e.g. "statistics")
    ImU32 highlight;     // selected toolbar button
    ImU32 buttonHover;
    ImU32 canvasBorder;  // Fixed3D border around the map view
    ImU32 selectionBg;   // text selection in inputs
};

const ThemeColors& theme();

// Apply the ImGui style used by the reference screenshots of the desktop UI
// (WinForms visual styles, 96 DPI metrics scaled by `scale`).
void applyTheme(float scale);

// Default font sizes (desktop uses 8.25pt Verdana for status, 8.25 Segoe UI
// for menus, 8.25 for panels).
struct FontSizes {
    float ui = 13.0f;
    float small = 12.0f;
    float bold = 13.0f;
    float icons = 16.0f;
};

} // namespace pdb
