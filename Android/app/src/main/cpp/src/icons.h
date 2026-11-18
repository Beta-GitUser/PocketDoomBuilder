// PocketDoomBuilder — Android port (Dear ImGui)
// icons.h — Toolbar/menu icon atlas.
//
// The desktop app embeds its icons as .NET resources; the files live in
// Source/Core/Resources/*.png (core) and Source/Plugins/*/Resources/*.png
// (plugins). They are shipped in the APK assets and decoded at startup
// into one RGBA atlas uploaded as a single GL texture.

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "imgui.h"

namespace pdb {

struct IconRegion {
    int x = 0, y = 0, w = 0, h = 0;
    bool valid() const { return w > 0 && h > 0; }
};

class IconAtlas {
public:
    // Build the atlas from decoded images. `images` maps icon name (without
    // ".png") to RGBA pixels. All icons are packed into a grid of square
    // cells sized to the largest icon.
    void build(const std::map<std::string, std::vector<uint8_t>>& images,
               const std::map<std::string, int>& widths,
               const std::map<std::string, int>& heights);

    bool empty() const { return regions_.empty(); }
    int width() const { return width_; }
    int height() const { return height_; }
    int cellSize() const { return cell_; }
    const std::vector<uint8_t>& pixels() const { return pixels_; }
    ImTextureID texture() const { return texture_; }
    void setTexture(ImTextureID id) { texture_ = id; }

    const IconRegion* find(const std::string& name) const {
        auto it = regions_.find(name);
        return it == regions_.end() ? nullptr : &it->second;
    }

    // Draw an icon; resolves the name (with or without ".png") and falls
    // back to a placeholder square if unknown.
    void draw(ImDrawList* drawList, const char* name, ImVec2 pos, float size,
              ImU32 tint = IM_COL32(255, 255, 255, 255)) const;

    // Icon names in the atlas (sorted).
    std::vector<std::string> names() const;

private:
    std::map<std::string, IconRegion> regions_;
    std::vector<uint8_t> pixels_;
    int width_ = 0;
    int height_ = 0;
    int cell_ = 16;
    ImTextureID texture_ = ImTextureID_Invalid;
};

// Platform asset reader: fill `out` with the asset's bytes and return true.
using AssetReader = bool (*)(const std::string& path, std::vector<uint8_t>& out,
                             void* userData);

// Load the icon set described by the asset manifest at `manifestPath`
// (one "<icon>=<asset/path.png>" entry per line) and pack it into the atlas.
// Missing or undecodable icons are skipped and reported in `warnings`.
bool loadIconsFromAssets(IconAtlas& atlas, const std::string& manifestPath,
                         AssetReader readAsset, void* userData,
                         std::vector<std::string>& warnings);

} // namespace pdb
