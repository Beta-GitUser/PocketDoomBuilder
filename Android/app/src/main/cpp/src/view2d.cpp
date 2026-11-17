// PocketDoomBuilder — Android port (Dear ImGui)
// view2d.cpp — Camera, grid, hit-testing and geometry helpers.

#include "view2d.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace pdb {

namespace {

const int kGridSizes[] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024};

int nearestGridIndex(int size) {
    int best = 0;
    int bestDelta = std::abs(kGridSizes[0] - size);
    for (int i = 1; i < (int)(sizeof(kGridSizes) / sizeof(kGridSizes[0])); ++i) {
        const int d = std::abs(kGridSizes[i] - size);
        if (d < bestDelta) {
            bestDelta = d;
            best = i;
        }
    }
    return best;
}

} // namespace

void View2D::zoomAt(float sx, float sy, float factor, float viewW, float viewH) {
    (void)viewW;
    (void)viewH;
    // Keep the map point under (sx, sy) fixed while scaling.
    const double mx = screenToMapX(sx);
    const double my = screenToMapY(sy);
    zoom *= factor;
    clampZoom();
    offsetX = sx - (float)(mx * (double)zoom);
    offsetY = sy + (float)(my * (double)zoom);
}

void View2D::centreOn(double mx, double my, float viewW, float viewH) {
    offsetX = viewW * 0.5f - (float)(mx * (double)zoom);
    offsetY = viewH * 0.5f + (float)(my * (double)zoom);
}

void View2D::fitToMap(const MapSet& map, float viewW, float viewH, float marginPx) {
    double minX = 0, minY = 0, maxX = 0, maxY = 0;
    if (!map.boundingBox(minX, minY, maxX, maxY)) {
        zoom = 0.05f;
        offsetX = viewW * 0.5f;
        offsetY = viewH * 0.5f;
        return;
    }
    // Include thing positions so nothing sits off-screen in an empty map.
    for (const Thing& t : map.things) {
        minX = std::min(minX, (double)t.x);
        minY = std::min(minY, (double)t.y);
        maxX = std::max(maxX, (double)t.x);
        maxY = std::max(maxY, (double)t.y);
    }

    const double spanX = std::max(1.0, maxX - minX);
    const double spanY = std::max(1.0, maxY - minY);
    const float availW = std::max(1.0f, viewW - marginPx * 2.0f);
    const float availH = std::max(1.0f, viewH - marginPx * 2.0f);
    zoom = (float)std::min(availW / spanX, availH / spanY);
    clampZoom();

    const double cx = (minX + maxX) * 0.5;
    const double cy = (minY + maxY) * 0.5;
    centreOn(cx, cy, viewW, viewH);
}

Bounds2D View2D::visibleBounds(float viewW, float viewH) const {
    Bounds2D b;
    b.minX = screenToMapX(0.0f);
    b.maxX = screenToMapX(viewW);
    b.maxY = screenToMapY(0.0f);
    b.minY = screenToMapY(viewH);
    return b;
}

void View2D::setGridSize(int size) {
    gridSize = kGridSizes[nearestGridIndex(size)];
}

void View2D::decreaseGridSize() {
    const int idx = nearestGridIndex(gridSize);
    if (idx > 0) gridSize = kGridSizes[idx - 1];
}

void View2D::increaseGridSize() {
    const int idx = nearestGridIndex(gridSize);
    if (idx + 1 < (int)(sizeof(kGridSizes) / sizeof(kGridSizes[0]))) {
        gridSize = kGridSizes[idx + 1];
    }
}

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------

double linedefAngleDegrees(const MapSet& map, const Linedef& line) {
    if (line.v1 < 0 || line.v2 < 0) return 0.0;
    if ((size_t)line.v1 >= map.vertices.size() ||
        (size_t)line.v2 >= map.vertices.size()) {
        return 0.0;
    }
    const Vertex& a = map.vertices[(size_t)line.v1];
    const Vertex& b = map.vertices[(size_t)line.v2];
    const double dx = (double)b.x - (double)a.x;
    const double dy = (double)b.y - (double)a.y;
    double deg = std::atan2(dy, dx) * 180.0 / M_PI;
    if (deg < 0.0) deg += 360.0;
    return deg;
}

double linedefLength(const MapSet& map, const Linedef& line) {
    if (line.v1 < 0 || line.v2 < 0) return 0.0;
    if ((size_t)line.v1 >= map.vertices.size() ||
        (size_t)line.v2 >= map.vertices.size()) {
        return 0.0;
    }
    const Vertex& a = map.vertices[(size_t)line.v1];
    const Vertex& b = map.vertices[(size_t)line.v2];
    const double dx = (double)b.x - (double)a.x;
    const double dy = (double)b.y - (double)a.y;
    return std::sqrt(dx * dx + dy * dy);
}

double distanceToSegment(double px, double py, double x1, double y1,
                         double x2, double y2) {
    const double dx = x2 - x1;
    const double dy = y2 - y1;
    const double lenSq = dx * dx + dy * dy;
    if (lenSq <= 1e-12) {
        const double ax = px - x1, ay = py - y1;
        return std::sqrt(ax * ax + ay * ay);
    }
    double t = ((px - x1) * dx + (py - y1) * dy) / lenSq;
    t = std::max(0.0, std::min(1.0, t));
    const double cx = x1 + t * dx;
    const double cy = y1 + t * dy;
    const double ex = px - cx, ey = py - cy;
    return std::sqrt(ex * ex + ey * ey);
}

std::vector<int> sectorLinedefs(const MapSet& map, int sectorIndex) {
    std::vector<int> result;
    if (sectorIndex < 0) return result;
    for (size_t i = 0; i < map.linedefs.size(); ++i) {
        const Linedef& l = map.linedefs[i];
        bool matches = false;
        for (int sd : {l.sideFront, l.sideBack}) {
            if (sd >= 0 && (size_t)sd < map.sidedefs.size() &&
                map.sidedefs[(size_t)sd].sector == sectorIndex) {
                matches = true;
                break;
            }
        }
        if (matches) result.push_back((int)i);
    }
    return result;
}

bool pointInSector(const MapSet& map, int sectorIndex, double x, double y) {
    if (sectorIndex < 0) return false;
    // Rightward ray crossing test over the sector's bounding linedefs.
    int crossings = 0;
    const std::vector<int> lines = sectorLinedefs(map, sectorIndex);
    for (int li : lines) {
        const Linedef& l = map.linedefs[(size_t)li];
        if (l.v1 < 0 || l.v2 < 0) continue;
        if ((size_t)l.v1 >= map.vertices.size() ||
            (size_t)l.v2 >= map.vertices.size()) {
            continue;
        }
        const Vertex& a = map.vertices[(size_t)l.v1];
        const Vertex& b = map.vertices[(size_t)l.v2];
        const double ay = (double)a.y, by = (double)b.y;
        if ((ay > y) == (by > y)) continue; // no vertical overlap
        const double t = (y - ay) / (by - ay);
        const double crossX = (double)a.x + t * ((double)b.x - (double)a.x);
        if (crossX > x) ++crossings;
    }
    return (crossings % 2) == 1;
}

int sectorAtPoint(const MapSet& map, double x, double y) {
    int best = -1;
    double bestArea = 0.0;
    for (size_t i = 0; i < map.sectors.size(); ++i) {
        if (!pointInSector(map, (int)i, x, y)) continue;
        // Prefer the smallest containing sector (inner sectors on top).
        const std::vector<int> lines = sectorLinedefs(map, (int)i);
        double minX = 1e30, minY = 1e30, maxX = -1e30, maxY = -1e30;
        for (int li : lines) {
            const Linedef& l = map.linedefs[(size_t)li];
            if (l.v1 < 0 || (size_t)l.v1 >= map.vertices.size()) continue;
            const Vertex& v = map.vertices[(size_t)l.v1];
            minX = std::min(minX, (double)v.x);
            maxX = std::max(maxX, (double)v.x);
            minY = std::min(minY, (double)v.y);
            maxY = std::max(maxY, (double)v.y);
        }
        const double area = std::max(0.0, maxX - minX) * std::max(0.0, maxY - minY);
        if (best < 0 || area < bestArea) {
            best = (int)i;
            bestArea = area;
        }
    }
    return best;
}

PickResult View2D::pick(const MapSet& map, float sx, float sy,
                        float thresholdPx, bool thingsFirst) const {
    PickResult result;
    const double mx = screenToMapX(sx);
    const double my = screenToMapY(sy);
    const double threshold = (double)thresholdPx / (double)zoom;

    // Things: inside the thing's "radius" (16 units like the desktop
    // default) or within the pick threshold for small radii.
    auto pickThing = [&]() -> bool {
        double bestDist = 1e30;
        int bestIdx = -1;
        for (size_t i = 0; i < map.things.size(); ++i) {
            const Thing& t = map.things[i];
            const double dx = mx - (double)t.x;
            const double dy = my - (double)t.y;
            const double radius = std::max(16.0, threshold * 0.5);
            if (dx * dx + dy * dy <= radius * radius) {
                const double d = dx * dx + dy * dy;
                if (d < bestDist) {
                    bestDist = d;
                    bestIdx = (int)i;
                }
            }
        }
        if (bestIdx >= 0) {
            result.kind = PickKind::Thing;
            result.index = bestIdx;
            return true;
        }
        return false;
    };

    if (thingsFirst && pickThing()) return result;

    // Vertices have highest priority (small, easy to miss).
    {
        double bestDist = 1e30;
        int bestIdx = -1;
        for (size_t i = 0; i < map.vertices.size(); ++i) {
            const Vertex& v = map.vertices[i];
            const double dx = mx - (double)v.x;
            const double dy = my - (double)v.y;
            const double d = std::sqrt(dx * dx + dy * dy);
            if (d <= threshold && d < bestDist) {
                bestDist = d;
                bestIdx = (int)i;
            }
        }
        if (bestIdx >= 0) {
            result.kind = PickKind::Vertex;
            result.index = bestIdx;
            return result;
        }
    }

    // Linedefs.
    {
        double bestDist = 1e30;
        int bestIdx = -1;
        for (size_t i = 0; i < map.linedefs.size(); ++i) {
            const Linedef& l = map.linedefs[i];
            if (l.v1 < 0 || l.v2 < 0) continue;
            if ((size_t)l.v1 >= map.vertices.size() ||
                (size_t)l.v2 >= map.vertices.size()) {
                continue;
            }
            const Vertex& a = map.vertices[(size_t)l.v1];
            const Vertex& b = map.vertices[(size_t)l.v2];
            const double d = distanceToSegment(mx, my, (double)a.x, (double)a.y,
                                               (double)b.x, (double)b.y);
            if (d <= threshold && d < bestDist) {
                bestDist = d;
                bestIdx = (int)i;
            }
        }
        if (bestIdx >= 0) {
            result.kind = PickKind::Linedef;
            result.index = bestIdx;
            return result;
        }
    }

    if (!thingsFirst && pickThing()) return result;

    // Sectors: innermost containing sector.
    {
        const int sec = sectorAtPoint(map, mx, my);
        if (sec >= 0) {
            result.kind = PickKind::Sector;
            result.index = sec;
        }
    }
    return result;
}

std::string formatCoordinates(double x, double y) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%d, %d", (int)std::lround(x),
                  (int)std::lround(y));
    return buf;
}

std::string formatZoom(double zoom) {
    // Desktop shows the zoom as a percentage of the base 1:1 scale.
    char buf[32];
    const double pct = zoom * 100.0;
    if (pct >= 100.0) {
        std::snprintf(buf, sizeof(buf), "%d%%", (int)std::lround(pct));
    } else {
        std::snprintf(buf, sizeof(buf), "%d%%", (int)std::lround(pct));
    }
    return buf;
}

} // namespace pdb
