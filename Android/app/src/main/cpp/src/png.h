// PocketDoomBuilder — Android port (Dear ImGui)
// png.h — Minimal, dependency-free PNG decoder.
//
// The desktop editor's toolbar icons live as PNG files in
// Source/Core/Resources; they are shipped in the APK's assets and
// decoded at startup into an RGBA atlas. Only the PNG subset those
// files use is supported (8-bit, non-interlaced, colour types 0/2/3/6),
// which is all 185 shipped icons need.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pdb {

struct ImageRGBA {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels; // width * height * 4, RGBA

    bool valid() const {
        return width > 0 && height > 0 &&
               pixels.size() == (size_t)width * (size_t)height * 4u;
    }
};

// Decode a PNG image held in memory. Returns false on unsupported or
// corrupt data and fills `error`.
bool decodePng(const std::vector<uint8_t>& bytes, ImageRGBA& out,
               std::string& error);

// Raw DEFLATE (RFC 1951) decompression, exposed for tests.
bool inflateRaw(const uint8_t* data, size_t size, std::vector<uint8_t>& out,
                std::string& error);

} // namespace pdb
