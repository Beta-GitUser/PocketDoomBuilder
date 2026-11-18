// PocketDoomBuilder — Android port (Dear ImGui)
// render2d.h — 2D map drawing into an ImGui draw list.
//
// Equivalent of Source/Core/Rendering/Renderer2D.cs, minus the GL plumbing:
// the Android port draws through ImGui's draw list, so the map view is just
// another widget in the shell's layout.

#pragma once

#include <string>

#include "editor.h"
#include "imgui.h"

namespace pdb {

struct RenderOptions {
    ViewMode viewMode = ViewMode::Wireframe;
    bool drawGrid = true;
    bool drawVertices = true;
    bool drawThings = true;
    bool highlightMode = false; // draw the hover highlight
};

// Draw the map into `drawList`. `origin` is the screen position of the map
// view's top-left corner: View2D works in view-local pixels, the draw list in
// absolute screen pixels.
void renderMap2D(ImDrawList* drawList, const MapSet& map, const View2D& view,
                 const MapColors& colors, const RenderOptions& options,
                 const PickResult& hover, const PickResult& selection,
                 ImVec2 origin);

// Draw the thing as the desktop does: an arrow-less dot with a direction
// indicator for direction-sensitive types, plus a type label.
void renderThing(ImDrawList* drawList, const MapSet& map, const View2D& view,
                 const MapColors& colors, int index, bool selected,
                 bool highlighted, bool fixedScale, ImVec2 origin);

// Filled polygon approximation of a sector (used by the texture view modes).
void buildSectorPolygon(const MapSet& map, int sectorIndex,
                        std::vector<ImVec2>& outScreen, const View2D& view,
                        ImVec2 origin);

} // namespace pdb
