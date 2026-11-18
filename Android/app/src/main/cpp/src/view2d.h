// PocketDoomBuilder — Android port (Dear ImGui)
// view2d.h — 2D map view transform, hit-testing and geometry helpers.
//
// Screen space matches ImGui draw lists: origin top-left, +y down.
// Map space matches the desktop editor: origin bottom-left, +y up.
//
// Desktop equivalents: Renderer2D.cs (camera/zoom in Zoom2D) and the
// classic-mode selection code (Vertex/Linedef/Sector/ThingMode).

#pragma once

#include <cmath>
#include <string>
#include <vector>

#include "map.h"

namespace pdb {

struct Bounds2D {
    double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;
};

// Which map element a click resolved to.
enum class PickKind { None, Vertex, Linedef, Sector, Thing };

struct PickResult {
    PickKind kind = PickKind::None;
    int index = -1;
};

// ---------------------------------------------------------------------------
// Camera / transform
// ---------------------------------------------------------------------------

class View2D {
public:
    float zoom = 0.05f;   // screen pixels per map unit
    float offsetX = 0.0f; // screen position of map origin
    float offsetY = 0.0f;

    // --- transforms --------------------------------------------------------

    void mapToScreen(double mx, double my, float& sx, float& sy) const {
        sx = offsetX + (float)(mx * (double)zoom);
        sy = offsetY - (float)(my * (double)zoom);
    }
    float mapToScreenX(double mx) const { return offsetX + (float)(mx * (double)zoom); }
    float mapToScreenY(double my) const { return offsetY - (float)(my * (double)zoom); }

    void screenToMap(float sx, float sy, double& mx, double& my) const {
        mx = (double)(sx - offsetX) / (double)zoom;
        my = (double)(offsetY - sy) / (double)zoom;
    }
    double screenToMapX(float sx) const {
        return (double)(sx - offsetX) / (double)zoom;
    }
    double screenToMapY(float sy) const {
        return (double)(offsetY - sy) / (double)zoom;
    }

    // --- navigation --------------------------------------------------------

    // Pan by a screen-space delta (drag).
    void pan(float dx, float dy) {
        offsetX += dx;
        offsetY += dy;
    }

    // Zoom around a screen anchor point (pinch / wheel).
    void zoomAt(float sx, float sy, float factor, float viewW, float viewH);

    // Centre the view on a map coordinate.
    void centreOn(double mx, double my, float viewW, float viewH);

    // Fit the whole map into the viewport with a margin.
    void fitToMap(const MapSet& map, float viewW, float viewH, float marginPx = 16.0f);

    // Clamp zoom into the same limits as the desktop (0.015625..32 px/unit).
    void clampZoom() {
        if (zoom < 0.015625f) zoom = 0.015625f;
        if (zoom > 32.0f) zoom = 32.0f;
    }

    // Visible map-space rectangle for the current viewport.
    Bounds2D visibleBounds(float viewW, float viewH) const;

    // --- grid --------------------------------------------------------------

    // Grid size in map units, snapped to the desktop's power-of-two ladder.
    int gridSize = 32;
    bool gridVisible = true;
    bool grid64Visible = true;

    void setGridSize(int size);
    void decreaseGridSize();
    void increaseGridSize();

    // --- picking -----------------------------------------------------------

    // Resolve a click at a screen position. `thresholdPx` widens the hit
    // band around vertices and linedefs so the touch UI stays usable.
    // Thing and sector hits are computed when nothing closer was found.
    PickResult pick(const MapSet& map, float sx, float sy,
                    float thresholdPx = 8.0f, bool thingsFirst = false) const;
};

// ---------------------------------------------------------------------------
// Geometry helpers (shared with rendering + info panels)
// ---------------------------------------------------------------------------

// Linedef direction in degrees, 0 = east, counter-clockwise, 0..359.99.
double linedefAngleDegrees(const MapSet& map, const Linedef& line);

// Linedef length in map units.
double linedefLength(const MapSet& map, const Linedef& line);

// Point-in-sector test: casts a ray to the right and counts crossings.
bool pointInSector(const MapSet& map, int sectorIndex, double x, double y);

// Sector index containing the point, honouring inner sectors (smallest
// containing sector wins), or -1.
int sectorAtPoint(const MapSet& map, double x, double y);

// Distance from a point to a line segment, in map units.
double distanceToSegment(double px, double py, double x1, double y1,
                         double x2, double y2);

// All linedefs bounding a sector, in map order.
std::vector<int> sectorLinedefs(const MapSet& map, int sectorIndex);

// Human-readable coordinate string, matching the status bar format
// ("x, y" with the desktop's rounding).
std::string formatCoordinates(double x, double y);

// Zoom percentage shown in the status bar.
std::string formatZoom(double zoom);

} // namespace pdb
