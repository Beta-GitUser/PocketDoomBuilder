// PocketDoomBuilder — Android port (Dear ImGui)
// icons.cpp — Icon atlas packing + drawing.

#include "icons.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>

#include "png.h"

namespace pdb {

namespace {

void blit(std::vector<uint8_t>& dst, int dstW, int dstH, int dx, int dy,
          const ImageRGBA& src) {
    for (int y = 0; y < src.height; ++y) {
        const int ty = dy + y;
        if (ty < 0 || ty >= dstH) continue;
        for (int x = 0; x < src.width; ++x) {
            const int tx = dx + x;
            if (tx < 0 || tx >= dstW) continue;
            const uint8_t* s = src.pixels.data() + ((size_t)y * src.width + x) * 4u;
            uint8_t* d = dst.data() + ((size_t)ty * dstW + tx) * 4u;
            // Straight alpha copy; icons are drawn over an opaque background.
            const uint32_t a = s[3];
            if (a == 255) {
                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];
                d[3] = 255;
            } else if (a > 0) {
                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];
                d[3] = (uint8_t)a;
            }
        }
    }
}

std::string stripExtension(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return name;
    return name.substr(0, dot);
}

} // namespace

void IconAtlas::build(const std::map<std::string, std::vector<uint8_t>>& images,
                      const std::map<std::string, int>& widths,
                      const std::map<std::string, int>& heights) {
    regions_.clear();
    pixels_.clear();
    width_ = height_ = 0;
    if (images.empty()) return;

    // Shelf packing, sized by the icons themselves.
    //
    // This used to place every icon in a square cell sized to the largest
    // image. That is quadratic in the worst case: one 1024x512 icon made every
    // one of the 295 cells 1025x1025, i.e. a 1226 MB atlas. On a phone that
    // allocation fails (the texture stays unbound, so every icon draws as a
    // black box) and the zero-fill alone stalls startup for half a minute.
    // The icons are 8.9 MB of pixels in total; shelf packing gets them into a
    // single 1024x2995 texture.
    struct Entry {
        const std::string* name;
        const std::vector<uint8_t>* pixels;
        int w, h;
    };
    std::vector<Entry> entries;
    entries.reserve(images.size());
    for (const auto& kv : images) {
        const int w = widths.count(kv.first) ? widths.at(kv.first) : 0;
        const int h = heights.count(kv.first) ? heights.at(kv.first) : 0;
        if (w <= 0 || h <= 0) continue;
        if (kv.second.size() < (size_t)w * (size_t)h * 4u) continue;
        entries.push_back({&kv.first, &kv.second, w, h});
    }
    if (entries.empty()) return;

    // Tallest first keeps the shelves tight.
    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) { return a.h > b.h; });

    const int kMaxWidth = 1024;
    const int padding = 1;
    int widest = 0;
    for (const Entry& e : entries) widest = std::max(widest, e.w);
    // Nothing is downscaled here: an icon wider than the atlas gets a shelf of
    // its own, however wide that has to be.
    const int atlasWidth = std::max(widest, std::min(kMaxWidth, widest * 2));

    // Assign shelves.
    struct Placement {
        const Entry* entry;
        int x, y;
    };
    std::vector<Placement> placements;
    placements.reserve(entries.size());
    int x = 0, y = 0, shelfHeight = 0;
    for (const Entry& e : entries) {
        if (x > 0 && x + e.w + padding > atlasWidth) {
            x = 0;
            y += shelfHeight + padding;
            shelfHeight = 0;
        }
        placements.push_back({&e, x, y});
        x += e.w + padding;
        shelfHeight = std::max(shelfHeight, e.h);
    }
    const int atlasHeight = y + shelfHeight;

    width_ = atlasWidth;
    height_ = atlasHeight;
    pixels_.assign((size_t)width_ * (size_t)height_ * 4u, 0);

    for (const Placement& pl : placements) {
        const Entry& e = *pl.entry;
        IconRegion region;
        region.x = pl.x;
        region.y = pl.y;
        region.w = e.w;
        region.h = e.h;
        regions_[*e.name] = region;

        ImageRGBA img;
        img.width = e.w;
        img.height = e.h;
        img.pixels = *e.pixels;
        blit(pixels_, width_, height_, pl.x, pl.y, img);
    }
}

std::vector<std::string> IconAtlas::names() const {
    std::vector<std::string> out;
    out.reserve(regions_.size());
    for (const auto& kv : regions_) out.push_back(kv.first);
    return out;
}

void IconAtlas::draw(ImDrawList* drawList, const char* name, ImVec2 pos,
                     float size, ImU32 tint) const {
    if (drawList == nullptr || name == nullptr) return;
    const std::string key = stripExtension(name);
    const IconRegion* region = find(key);
    if (region == nullptr && !texture_) {
        // No atlas yet (asset load failed): draw a neutral placeholder so
        // the layout still reads as the desktop's toolbar.
        drawList->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size),
                                IM_COL32(120, 120, 120, 255));
        return;
    }
    if (region == nullptr) {
        drawList->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size),
                                IM_COL32(120, 120, 120, 255));
        return;
    }
    if (!texture_) {
        drawList->AddRect(pos, ImVec2(pos.x + size, pos.y + size),
                          IM_COL32(90, 90, 90, 255));
        return;
    }

    const float u0 = (float)region->x / (float)width_;
    const float v0 = (float)region->y / (float)height_;
    const float u1 = (float)(region->x + region->w) / (float)width_;
    const float v1 = (float)(region->y + region->h) / (float)height_;
    drawList->AddImage(ImTextureRef(texture_), pos,
                       ImVec2(pos.x + size, pos.y + size), ImVec2(u0, v0),
                       ImVec2(u1, v1), tint);
}

bool loadIconsFromAssets(IconAtlas& atlas, const std::string& manifestPath,
                         AssetReader readAsset, void* userData,
                         std::vector<std::string>& warnings) {
    if (readAsset == nullptr) {
        warnings.push_back("No asset reader available; toolbar icons disabled.");
        return false;
    }

    std::vector<uint8_t> manifest;
    if (!readAsset(manifestPath, manifest, userData)) {
        warnings.push_back("Icon manifest '" + manifestPath + "' not found.");
        return false;
    }

    std::map<std::string, std::vector<uint8_t>> pixels;
    std::map<std::string, int> widths;
    std::map<std::string, int> heights;

    std::istringstream stream(std::string(manifest.begin(), manifest.end()));
    std::string line;
    while (std::getline(stream, line)) {
        // Trim whitespace and skip comments/blank lines.
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string name = line.substr(0, eq);
        const std::string path = line.substr(eq + 1);

        std::vector<uint8_t> bytes;
        if (!readAsset(path, bytes, userData)) {
            warnings.push_back("Icon asset missing: " + path);
            continue;
        }
        ImageRGBA img;
        std::string error;
        const auto decodeStart = std::chrono::steady_clock::now();
        if (!decodePng(bytes, img, error)) {
            warnings.push_back("Icon '" + path + "' could not be decoded: " + error);
            continue;
        }
        const double decodeMs = std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() - decodeStart)
                                    .count();
        if (decodeMs > 150.0) {
            warnings.push_back("Slow icon decode: " + path + " " +
                               std::to_string(img.width) + "x" +
                               std::to_string(img.height) + " took " +
                               std::to_string((int)decodeMs) + " ms");
        }
        pixels[name] = std::move(img.pixels);
        widths[name] = img.width;
        heights[name] = img.height;
    }

    atlas.build(pixels, widths, heights);
    if (atlas.empty()) {
        warnings.push_back("No toolbar icons could be loaded.");
        return false;
    }
    return true;
}

} // namespace pdb
