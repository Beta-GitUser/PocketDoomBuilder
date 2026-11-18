// PocketDoomBuilder — Android port (Dear ImGui)
// render2d.cpp — 2D map drawing.

#include "render2d.h"

#include <algorithm>
#include <cmath>

namespace pdb {

namespace {

// Desktop metrics (Renderer2D constants).
const float VERTEX_SIZE = 2.0f;      // half-size of the vertex box, px
const float THING_RADIUS = 16.0f;    // map units, default thing radius
const float THING_SELECT_EXTRA = 4.0f;

ImU32 applyBrightness(ImU32 color, float factor) {
    return IM_COL32((int)((color & 0xFF) * factor),
                    (int)(((color >> 8) & 0xFF) * factor),
                    (int)(((color >> 16) & 0xFF) * factor),
                    (int)((color >> 24) & 0xFF));
}

void drawGridLines(ImDrawList* dl, const View2D& view, float size,
                   ImU32 color, const ImVec2& min, const ImVec2& max,
                   ImVec2 origin) {
    if (size <= 0.0f) return;
    const float step = size * view.zoom;
    if (step < 4.0f) return; // too dense to be readable, same as the desktop

    // Anchor to the world origin so lines stay put while panning.
    const float startX = origin.x + view.mapToScreenX(0.0);
    const float startY = origin.y + view.mapToScreenY(0.0);

    float x = startX - std::floor((startX - min.x) / step) * step;
    for (; x <= max.x; x += step) {
        dl->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), color, 1.0f);
    }
    float y = startY - std::floor((startY - min.y) / step) * step;
    for (; y <= max.y; y += step) {
        dl->AddLine(ImVec2(min.x, y), ImVec2(max.x, y), color, 1.0f);
    }
}

// Sector polygon: walk the sector's linedefs and stitch them into a loop.
bool sectorLoop(const MapSet& map, int sectorIndex, std::vector<int>& outVerts) {
    outVerts.clear();
    const std::vector<int> lines = sectorLinedefs(map, sectorIndex);
    if (lines.empty()) return false;

    // Simple approach: order linedefs by shared vertices. Works for convex
    // and mildly concave sectors, which covers the view-mode shading use.
    std::vector<int> remaining = lines;
    std::vector<char> used(remaining.size(), 0);

    const Linedef& first = map.linedefs[(size_t)remaining[0]];
    if (first.v1 < 0 || (size_t)first.v1 >= map.vertices.size()) return false;
    outVerts.push_back(first.v1);
    used[0] = 1;
    int current = first.v2;

    for (;;) {
        outVerts.push_back(current);
        bool advanced = false;
        for (size_t i = 0; i < remaining.size(); ++i) {
            if (used[i]) continue;
            const Linedef& l = map.linedefs[(size_t)remaining[i]];
            if (l.v1 == current) {
                used[i] = 1;
                current = l.v2;
                advanced = true;
                break;
            }
            if (l.v2 == current) {
                used[i] = 1;
                current = l.v1;
                advanced = true;
                break;
            }
        }
        if (!advanced) break;
        if (current == outVerts.front()) break;
        if (outVerts.size() > remaining.size() + 2) break;
    }
    return outVerts.size() >= 3;
}

} // namespace

void buildSectorPolygon(const MapSet& map, int sectorIndex,
                        std::vector<ImVec2>& outScreen, const View2D& view,
                        ImVec2 origin) {
    outScreen.clear();
    std::vector<int> loop;
    if (!sectorLoop(map, sectorIndex, loop)) return;
    for (int vi : loop) {
        if (vi < 0 || (size_t)vi >= map.vertices.size()) continue;
        const Vertex& v = map.vertices[(size_t)vi];
        outScreen.push_back(ImVec2(origin.x + view.mapToScreenX(v.x),
                                   origin.y + view.mapToScreenY(v.y)));
    }
}

void renderThing(ImDrawList* dl, const MapSet& map, const View2D& view,
                 const MapColors& colors, int index, bool selected,
                 bool highlighted, bool fixedScale, ImVec2 origin) {
    if (index < 0 || (size_t)index >= map.things.size()) return;
    const Thing& t = map.things[(size_t)index];
    (void)map;

    // The desktop draws each Thing as a small square (its sprite frame) with
    // a thin outline; selection and hover are rectangles around it. No
    // arrow, no type number on the map itself.
    float half = THING_RADIUS * view.zoom;
    if (fixedScale) half = THING_RADIUS * 0.5f; // fixed-size marker
    half = std::max(3.0f, half);

    const float cx = origin.x + view.mapToScreenX(t.x);
    const float cy = origin.y + view.mapToScreenY(t.y);
    ImU32 color = thingColor(colors, t.type);
    if (!selected && !highlighted) {
        color = applyBrightness(color, 0.95f);
    }

    const ImVec2 topLeft(cx - half, cy - half);
    const ImVec2 bottomRight(cx + half, cy + half);

    // Sprite-frame square: filled with the thing colour, thin outline.
    dl->AddRectFilled(topLeft, bottomRight, color, 0);
    dl->AddRect(topLeft, bottomRight, IM_COL32(255, 255, 255, 200), 1.0f);
    // Centre dot so the marker stays readable at small sizes.
    dl->AddCircleFilled(ImVec2(cx, cy), std::max(1.5f, half * 0.3f),
                        IM_COL32(255, 255, 255, 230), 8);

    if (highlighted && !selected) {
        dl->AddRect(ImVec2(cx - half - THING_SELECT_EXTRA, cy - half - THING_SELECT_EXTRA),
                    ImVec2(cx + half + THING_SELECT_EXTRA, cy + half + THING_SELECT_EXTRA),
                    colors[COLOR_HIGHLIGHT], 1.5f);
    } else if (selected) {
        dl->AddRect(ImVec2(cx - half - 2.0f, cy - half - 2.0f),
                    ImVec2(cx + half + 2.0f, cy + half + 2.0f),
                    colors[COLOR_HIGHLIGHT], 2.0f);
    }
}

void renderMap2D(ImDrawList* dl, const MapSet& map, const View2D& view,
                 const MapColors& colors, const RenderOptions& options,
                 const PickResult& hover, const PickResult& selection,
                 ImVec2 origin) {
    const ImVec2 min = dl->GetClipRectMin();
    const ImVec2 end = dl->GetClipRectMax();

    dl->AddRectFilled(min, end, colors[COLOR_BACKGROUND]);

    if (options.drawGrid) {
        // Desktop: normal grid + a coarser 64-unit grid when the size <= 64.
        drawGridLines(dl, view, 64.0f, colors[COLOR_GRID64], min, end, origin);
        if (view.gridSize > 0 && view.gridSize < 64) {
            drawGridLines(dl, view, (float)view.gridSize, colors[COLOR_GRID], min,
                          end, origin);
        }
    }

    // --- sector shading (texture / brightness view modes) ------------------
    if (options.viewMode != ViewMode::Wireframe) {
        for (size_t i = 0; i < map.sectors.size(); ++i) {
            std::vector<ImVec2> poly;
            buildSectorPolygon(map, (int)i, poly, view, origin);
            if (poly.size() < 3) continue;

            const Sector& s = map.sectors[i];
            ImU32 fill = IM_COL32(0, 0, 0, 0);
            switch (options.viewMode) {
                case ViewMode::Brightness: {
                    const int b = std::max(0, std::min(255, s.brightness));
                    fill = IM_COL32(b, b, b, 90);
                    break;
                }
                case ViewMode::FloorTextures:
                    fill = IM_COL32(60, 110, 70, 70);
                    break;
                case ViewMode::CeilingTextures:
                    fill = IM_COL32(70, 90, 130, 70);
                    break;
                case ViewMode::Wireframe:
                    break;
            }
            if ((fill & 0xFF) != 0) {
                dl->AddConvexPolyFilled(poly.data(), (int)poly.size(), fill);
            }
        }
    }

    // --- linedefs ----------------------------------------------------------
    const float thickness = view.zoom < 0.05f ? 1.0f : 1.5f;
    for (size_t i = 0; i < map.linedefs.size(); ++i) {
        const Linedef& l = map.linedefs[i];
        if (l.v1 < 0 || l.v2 < 0) continue;
        if ((size_t)l.v1 >= map.vertices.size() ||
            (size_t)l.v2 >= map.vertices.size()) {
            continue;
        }
        const Vertex& a = map.vertices[(size_t)l.v1];
        const Vertex& b = map.vertices[(size_t)l.v2];

        ImU32 color = colors[COLOR_LINEDEFS];
        if (l.sideBack < 0) {
            // Single-sided lines are drawn a touch brighter in the desktop
            // editor to separate them from two-sided lines.
            color = IM_COL32(255, 255, 255, 255);
        }

        const bool isHover = options.highlightMode &&
                             hover.kind == PickKind::Linedef &&
                             hover.index == (int)i;
        const bool isSelected = selection.kind == PickKind::Linedef &&
                                selection.index == (int)i;
        if (isSelected) color = colors[COLOR_SELECTION];
        else if (isHover) color = colors[COLOR_HIGHLIGHT];

        dl->AddLine(ImVec2(origin.x + view.mapToScreenX(a.x),
                           origin.y + view.mapToScreenY(a.y)),
                    ImVec2(origin.x + view.mapToScreenX(b.x),
                           origin.y + view.mapToScreenY(b.y)), color,
                    isSelected || isHover ? thickness + 1.5f : thickness);
    }

    // --- vertices ----------------------------------------------------------
    if (options.drawVertices) {
        for (size_t i = 0; i < map.vertices.size(); ++i) {
            const Vertex& v = map.vertices[i];
            const float x = origin.x + view.mapToScreenX(v.x);
            const float y = origin.y + view.mapToScreenY(v.y);
            if (x < min.x - 4 || x > end.x + 4 || y < min.y - 4 || y > end.y + 4) {
                continue;
            }
            const bool isSelected = selection.kind == PickKind::Vertex &&
                                    selection.index == (int)i;
            const bool isHover = options.highlightMode &&
                                 hover.kind == PickKind::Vertex &&
                                 hover.index == (int)i;
            ImU32 color = colors[COLOR_VERTICES];
            if (isSelected) color = colors[COLOR_SELECTION];
            else if (isHover) color = colors[COLOR_HIGHLIGHT];

            const float s = isSelected || isHover ? VERTEX_SIZE + 1.5f : VERTEX_SIZE;
            dl->AddRectFilled(ImVec2(x - s, y - s), ImVec2(x + s, y + s), color);
        }
    }

    // --- things ------------------------------------------------------------
    if (options.drawThings) {
        for (size_t i = 0; i < map.things.size(); ++i) {
            const bool isSelected = selection.kind == PickKind::Thing &&
                                    selection.index == (int)i;
            const bool isHover = options.highlightMode &&
                                 hover.kind == PickKind::Thing &&
                                 hover.index == (int)i;
            const float cx = origin.x + view.mapToScreenX(map.things[i].x);
            const float cy = origin.y + view.mapToScreenY(map.things[i].y);
            if (cx < min.x - 32 || cx > end.x + 32 || cy < min.y - 32 ||
                cy > end.y + 32) {
                continue;
            }
            renderThing(dl, map, view, colors, (int)i, isSelected, isHover, false,
                        origin);
        }
    }

    // --- hovered / selected sector outline ---------------------------------
    if (options.highlightMode && hover.kind == PickKind::Sector) {
        std::vector<ImVec2> poly;
        buildSectorPolygon(map, hover.index, poly, view, origin);
        if (poly.size() >= 3) {
            dl->AddPolyline(poly.data(), (int)poly.size(), colors[COLOR_HIGHLIGHT],
                            ImDrawFlags_Closed, 2.0f);
        }
    }
    if (selection.kind == PickKind::Sector) {
        std::vector<ImVec2> poly;
        buildSectorPolygon(map, selection.index, poly, view, origin);
        if (poly.size() >= 3) {
            dl->AddPolyline(poly.data(), (int)poly.size(), colors[COLOR_SELECTION],
                            ImDrawFlags_Closed, 2.0f);
        }
    }
}

} // namespace pdb
