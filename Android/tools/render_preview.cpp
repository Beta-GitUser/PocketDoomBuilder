// PocketDoomBuilder — developer tool
// render_preview.cpp — Renders the ImGui shell headlessly to a PNG.
//
// The Android shell needs a device to look at; this tool runs the exact same
// UI code on the host with a tiny software rasteriser (textured triangles,
// which is all ImGui emits) and writes the result as a PNG. Useful for
// checking the 1:1 layout against the desktop screenshots without a phone.
//
// Build and run it through Android/tests/run_tests.sh --preview, or by hand:
//   g++ -std=c++17 -O2 -I <imgui> -I <src> render_preview.cpp <src>/*.cpp
//       <imgui>/imgui*.cpp -o render_preview
//   ./render_preview out.png [width height]

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "editor.h"
#include "icons.h"
#include "imgui.h"
#include "mapio.h"
#include "theme.h"
#include "ui.h"

using namespace pdb;

namespace {

// ---------------------------------------------------------------------------
// Tiny PNG writer (8-bit RGBA, stored/uncompressed DEFLATE blocks).
// ---------------------------------------------------------------------------

uint32_t crcTable[256];
bool crcReady = false;

void initCrc() {
    if (crcReady) return;
    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crcTable[n] = c;
    }
    crcReady = true;
}

uint32_t crc32(const uint8_t* data, size_t len) {
    initCrc();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) c = crcTable[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

void putU32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((uint8_t)(x >> 24));
    v.push_back((uint8_t)(x >> 16));
    v.push_back((uint8_t)(x >> 8));
    v.push_back((uint8_t)x);
}

void pngChunk(std::vector<uint8_t>& out, const char* type,
              const std::vector<uint8_t>& data) {
    putU32(out, (uint32_t)data.size());
    std::vector<uint8_t> crcData;
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    crcData.assign(out.begin() + (long)start, out.end());
    putU32(out, crc32(crcData.data(), crcData.size()));
}

void writePng(const char* path, const std::vector<uint8_t>& rgba, int w, int h) {
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

    std::vector<uint8_t> ihdr;
    putU32(ihdr, (uint32_t)w);
    putU32(ihdr, (uint32_t)h);
    ihdr.push_back(8); // bit depth
    ihdr.push_back(6); // RGBA
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    pngChunk(out, "IHDR", ihdr);

    // Raw scanlines with filter byte 0.
    std::vector<uint8_t> raw;
    raw.reserve((size_t)(w * 4 + 1) * h);
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba.begin() + (long)y * w * 4,
                   rgba.begin() + (long)(y + 1) * w * 4);
    }

    // zlib stream made of stored (type 0) DEFLATE blocks.
    std::vector<uint8_t> z;
    z.push_back(0x78);
    z.push_back(0x01);
    size_t pos = 0;
    while (pos < raw.size()) {
        const size_t len = std::min<size_t>(65535, raw.size() - pos);
        const bool last = pos + len >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back((uint8_t)(len & 0xFF));
        z.push_back((uint8_t)(len >> 8));
        z.push_back((uint8_t)(~len & 0xFF));
        z.push_back((uint8_t)((~len >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + (long)pos, raw.begin() + (long)(pos + len));
        pos += len;
    }
    uint32_t a = 1, b = 0;
    for (uint8_t byte : raw) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    putU32(z, (b << 16) | a);
    pngChunk(out, "IDAT", z);

    std::vector<uint8_t> empty;
    pngChunk(out, "IEND", empty);

    FILE* f = std::fopen(path, "wb");
    if (f) {
        std::fwrite(out.data(), 1, out.size(), f);
        std::fclose(f);
    }
}

// ---------------------------------------------------------------------------
// Software rasteriser for ImGui draw lists
// ---------------------------------------------------------------------------

struct Texture {
    const uint8_t* pixels = nullptr;
    int width = 0;
    int height = 0;
    bool alphaOnly = false;
};

std::vector<uint8_t> g_framebuffer;
int g_fbW = 0;
int g_fbH = 0;

inline void blendPixel(int x, int y, const uint8_t* src, bool alphaOnly,
                       const ImVec4& tint) {
    if (x < 0 || y < 0 || x >= g_fbW || y >= g_fbH) return;
    uint8_t* dst = g_framebuffer.data() + ((size_t)y * g_fbW + x) * 4;
    float sr, sg, sb, sa;
    if (alphaOnly) {
        const float a = src[0] / 255.0f;
        sr = tint.x;
        sg = tint.y;
        sb = tint.z;
        sa = a * tint.w;
    } else {
        sr = src[0] / 255.0f;
        sg = src[1] / 255.0f;
        sb = src[2] / 255.0f;
        sa = (src[3] / 255.0f) * tint.w;
    }
    sr *= tint.x;
    sg *= tint.y;
    sb *= tint.z;
    if (sa <= 0.0f) return;
    const float dr = dst[0] / 255.0f, dg = dst[1] / 255.0f, db = dst[2] / 255.0f,
                da = dst[3] / 255.0f;
    const float outA = sa + da * (1.0f - sa);
    const float outR = (sr * sa + dr * da * (1.0f - sa)) / (outA > 0 ? outA : 1.0f);
    const float outG = (sg * sa + dg * da * (1.0f - sa)) / (outA > 0 ? outA : 1.0f);
    const float outB = (sb * sa + db * da * (1.0f - sa)) / (outA > 0 ? outA : 1.0f);
    dst[0] = (uint8_t)(outR * 255.0f);
    dst[1] = (uint8_t)(outG * 255.0f);
    dst[2] = (uint8_t)(outB * 255.0f);
    dst[3] = (uint8_t)(outA * 255.0f);
}

void rasterize(const ImDrawData* drawData, const Texture& fontTex,
               const Texture& iconTex) {
    for (int n = 0; n < drawData->CmdListsCount; ++n) {
        const ImDrawList* list = drawData->CmdLists[n];
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback != nullptr) continue;
            const int clipX0 = (int)cmd.ClipRect.x;
            const int clipY0 = (int)cmd.ClipRect.y;
            const int clipX1 = (int)cmd.ClipRect.z;
            const int clipY1 = (int)cmd.ClipRect.w;
            const Texture& tex = (iconTex.pixels != nullptr &&
                                  cmd.GetTexID() == (ImTextureID)(size_t)1)
                                     ? iconTex
                                     : fontTex;
            const ImDrawIdx* idx = list->IdxBuffer.Data + cmd.IdxOffset;
            const ImDrawVert* vtx = list->VtxBuffer.Data + cmd.VtxOffset;

            for (unsigned int i = 0; i + 2 < cmd.ElemCount; i += 3) {
                const ImDrawVert& v0 = vtx[idx[i + 0]];
                const ImDrawVert& v1 = vtx[idx[i + 1]];
                const ImDrawVert& v2 = vtx[idx[i + 2]];

                const float minX = std::min({v0.pos.x, v1.pos.x, v2.pos.x});
                const float maxX = std::max({v0.pos.x, v1.pos.x, v2.pos.x});
                const float minY = std::min({v0.pos.y, v1.pos.y, v2.pos.y});
                const float maxY = std::max({v0.pos.y, v1.pos.y, v2.pos.y});

                const int x0 = std::max(clipX0, (int)minX);
                const int x1 = std::min(clipX1, (int)maxX + 1);
                const int y0 = std::max(clipY0, (int)minY);
                const int y1 = std::min(clipY1, (int)maxY + 1);
                if (x0 >= x1 || y0 >= y1) continue;

                const float area = (v1.pos.x - v0.pos.x) * (v2.pos.y - v0.pos.y) -
                                   (v2.pos.x - v0.pos.x) * (v1.pos.y - v0.pos.y);
                if (area == 0.0f) continue;

                for (int y = y0; y < y1; ++y) {
                    for (int x = x0; x < x1; ++x) {
                        const float px = x + 0.5f;
                        const float py = y + 0.5f;
                        const float w0 = ((v1.pos.x - px) * (v2.pos.y - py) -
                                          (v2.pos.x - px) * (v1.pos.y - py)) / area;
                        const float w1 = ((v2.pos.x - px) * (v0.pos.y - py) -
                                          (v0.pos.x - px) * (v2.pos.y - py)) / area;
                        const float w2 = 1.0f - w0 - w1;
                        if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;

                        const float u = v0.uv.x * w0 + v1.uv.x * w1 + v2.uv.x * w2;
                        const float v = v0.uv.y * w0 + v1.uv.y * w1 + v2.uv.y * w2;
                        int tx = (int)(u * tex.width);
                        int ty = (int)(v * tex.height);
                        if (tx < 0) tx = 0;
                        if (ty < 0) ty = 0;
                        if (tx >= tex.width) tx = tex.width - 1;
                        if (ty >= tex.height) ty = tex.height - 1;
                        const int channels = tex.alphaOnly ? 1 : 4;
                        const uint8_t* src = tex.pixels +
                                             ((size_t)ty * tex.width + tx) * channels;

                        // Interpolate the vertex colours and multiply by the
                        // texture sample, the way the GL backend does.
                        const ImU32 c0 = v0.col, c1 = v1.col, c2 = v2.col;
                        auto chan = [&](int shift) {
                            const float a = ((c0 >> shift) & 0xFF) * w0;
                            const float b = ((c1 >> shift) & 0xFF) * w1;
                            const float c = ((c2 >> shift) & 0xFF) * w2;
                            return (a + b + c) / 255.0f;
                        };
                        ImVec4 tintCol(chan(0), chan(8), chan(16), chan(24));
                        blendPixel(x, y, src, tex.alphaOnly, tintCol);
                    }
                }
            }
        }
    }
}

// Read icons from the repository's asset folder so the preview shows the real
// toolbar artwork (the APK ships the same files).
bool readAssetFile(const std::string& path, std::vector<uint8_t>& out, void*) {
    const std::string full = std::string(PDB_ASSET_DIR) + "/" + path;
    FILE* f = std::fopen(full.c_str(), "rb");
    if (f == nullptr) return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize((size_t)size);
    if (size > 0 && std::fread(out.data(), 1, (size_t)size, f) != (size_t)size) {
        std::fclose(f);
        return false;
    }
    std::fclose(f);
    return true;
}

// Upload the atlas into a CPU buffer the software rasteriser can sample.
std::vector<uint8_t> g_iconPixels;
Texture g_iconTexture;

void setupIconTexture(IconAtlas& atlas) {
    if (atlas.empty()) return;
    g_iconPixels = atlas.pixels();
    g_iconTexture.pixels = g_iconPixels.data();
    g_iconTexture.width = atlas.width();
    g_iconTexture.height = atlas.height();
    g_iconTexture.alphaOnly = false;
    atlas.setTexture((ImTextureID)(size_t)1);
}

// Optional: pop a menu open so a screenshot can show its contents. Menus are
// addressed by id, so we rely on the shell's menu bar ids.
void openPreviewMenu(const char* name) {
    if (name == nullptr || name[0] == '\0') return;
    // Menu ids are derived from the label; "#" suffixes are not used by the
    // desktop's labels, so the label *is* the id.
    ImGui::OpenPopup(name);
}

// ---------------------------------------------------------------------------
// Test map (same tiny PWAD used by the unit tests)
// ---------------------------------------------------------------------------

std::vector<uint8_t> buildTestWad() {
    WadFile wad;
    wad.type = "PWAD";
    auto add = [&](const char* name, std::vector<uint8_t> data) {
        WadLump lump;
        lump.name = name;
        lump.data = std::move(data);
        wad.lumps.push_back(std::move(lump));
    };
    auto put16 = [](std::vector<uint8_t>& v, int value) {
        const uint16_t u = (uint16_t)(int16_t)value;
        v.push_back((uint8_t)(u & 0xFF));
        v.push_back((uint8_t)((u >> 8) & 0xFF));
    };
    auto putName = [](std::vector<uint8_t>& v, const char* s) {
        const size_t n = std::strlen(s);
        for (int i = 0; i < 8; ++i) v.push_back(i < (int)n ? (uint8_t)s[i] : 0);
    };

    add("MAP01", {});
    {   // Two rooms joined by a corridor, one thing in each.
        std::vector<uint8_t> t;
        int things[3][3] = {{96, 96, 1}, {352, 288, 3001}, {224, 192, 2001}};
        for (auto& th : things) {
            put16(t, th[0]); put16(t, th[1]); put16(t, 90); put16(t, th[2]);
            put16(t, 7);
        }
        add("THINGS", t);
    }
    {
        std::vector<uint8_t> v;
        // Outer rectangle 448x384 with a notch, plus interior vertices.
        int verts[8][2] = {{0, 0}, {448, 0}, {448, 384}, {0, 384},
                           {192, 64}, {256, 64}, {256, 320}, {192, 320}};
        for (auto& p : verts) { put16(v, p[0]); put16(v, p[1]); }
        add("VERTEXES", v);
    }
    {
        std::vector<uint8_t> l;
        auto line = [&](int a, int b, int side1, int side2, int flags) {
            put16(l, a); put16(l, b); put16(l, flags); put16(l, 0); put16(l, 0);
            put16(l, side1); put16(l, side2);
        };
        // Outer walls (two-sided to the outer sector), inner split (two-sided).
        line(0, 1, 0, 1, 1);
        line(1, 2, 2, 3, 1);
        line(2, 3, 4, 5, 1);
        line(3, 0, 6, 7, 1);
        line(4, 5, 8, 9, 4);
        line(5, 6, 10, 11, 4);
        line(6, 7, 12, 13, 4);
        line(7, 4, 14, 15, 4);
        add("LINEDEFS", l);
    }
    {
        std::vector<uint8_t> s;
        for (int i = 0; i < 16; ++i) {
            put16(s, 0); put16(s, 0);
            putName(s, "-");
            putName(s, "-");
            putName(s, i < 8 ? "STARTAN3" : "-");
            put16(s, i < 8 ? 0 : 1);
        }
        add("SIDEDEFS", s);
    }
    {
        std::vector<uint8_t> s;
        put16(s, 0); put16(s, 128); putName(s, "FLOOR4_8"); putName(s, "CEIL3_5");
        put16(s, 160); put16(s, 0); put16(s, 1);
        put16(s, 0); put16(s, 192); putName(s, "FLOOR7_1"); putName(s, "CEIL5_1");
        put16(s, 224); put16(s, 0); put16(s, 2);
        add("SECTORS", s);
    }
    return writeWad(wad);
}

} // namespace

// The UI layer reads manual pages and the icon manifest through this hook; on the
// host the same files live in the repository's asset folder.
namespace pdb {
bool platformReadAsset(const std::string& path, std::vector<uint8_t>& out) {
    return readAssetFile(path, out, nullptr);
}
} // namespace pdb

static ImVec4 imGuiWorkInsets(0.0f, 0.0f, 0.0f, 0.0f);

int main(int argc, char** argv) {
    const char* outPath = argc > 1 ? argv[1] : "preview.png";
    const int width = argc > 2 ? std::atoi(argv[2]) : 1600;
    const int height = argc > 3 ? std::atoi(argv[3]) : 1000;
    // Optional view selector: "file", "edit", "view", "mode", "tools", "help".
    const char* menuToOpen = argc > 4 ? argv[4] : "";

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2((float)width, (float)height);
    io.DeltaTime = 1.0f / 60.0f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->AddFontDefault();
    applyTheme(1.0f);

    // Emulate the safe-area insets the Java side reports (notch / gesture bar):
    // PDB_INSETS="top,bottom,left,right" in pixels.
    if (const char* insets = std::getenv("PDB_INSETS")) {
        int t = 0, b = 0, l = 0, r = 0;
        if (std::sscanf(insets, "%d,%d,%d,%d", &t, &b, &l, &r) == 4) {
            imGuiWorkInsets = ImVec4((float)l, (float)t, (float)r, (float)b);
            ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
            platformIo.Platform_GetWindowWorkAreaInsets = +[](ImGuiViewport*) {
                return imGuiWorkInsets;
            };
            std::printf("preview insets: top=%d bottom=%d left=%d right=%d\n", t, b, l, r);
        }
    }

    Editor& ed = editor();
    const std::vector<uint8_t> wadBytes = buildTestWad();
    ed.openWadFromMemory(wadBytes, "preview.wad", "");

    g_fbW = width;
    g_fbH = height;
    g_framebuffer.assign((size_t)width * height * 4u, 0);

    // Toolbar icons (same asset manifest the APK uses).
    {
        std::vector<std::string> warnings;
        loadIconsFromAssets(ed.icons, "icons.txt", readAssetFile, nullptr, warnings);
        for (const std::string& w : warnings) std::printf("icon: %s\n", w.c_str());
        setupIconTexture(ed.icons);
        std::printf("icons loaded: %zu\n", ed.icons.names().size());
    }

    // Optional window capture for screenshots of dialogs and the manual
    // browser: "help", "help:<page>", "shortcuts", "about".
    const char* windowToOpen = argc > 5 ? argv[5] : "";
    if (std::strcmp(windowToOpen, "help") == 0 || std::strncmp(windowToOpen, "help:", 5) == 0) {
        ed.showHelp = true;
        ed.showHelpIndex = true;
        if (std::strncmp(windowToOpen, "help:", 5) == 0) {
            ed.helpPage = windowToOpen + 5;
            ed.showHelpIndex = false;
        }
    } else if (std::strcmp(windowToOpen, "shortcuts") == 0) {
        ed.showShortcuts = true;
    } else if (std::strcmp(windowToOpen, "about") == 0) {
        ed.showAbout = true;
    }

    // The map view needs one frame to learn the viewport size before it can
    // fit the map, so run a few frames and keep the last one.
    for (int frame = 0; frame < 4; ++frame) {
        ImGui::NewFrame();
        if (frame == 3) openPreviewMenu(menuToOpen);
        drawMainWindow((float)width, (float)height);
        ImGui::Render();
    }

    unsigned char* pixels = nullptr;
    int texW = 0, texH = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &texW, &texH);
    // No graphics backend here, so hand the font atlas a texture id ourselves
    // (the real APK gets this from ImGui_ImplOpenGL3).
    io.Fonts->TexData->SetTexID((ImTextureID)(size_t)2);
    Texture tex;
    tex.pixels = pixels;
    tex.width = texW;
    tex.height = texH;
    tex.alphaOnly = false;

    // Start from the desktop's window background.
    for (size_t i = 0; i < g_framebuffer.size(); i += 4) {
        g_framebuffer[i + 0] = 240;
        g_framebuffer[i + 1] = 240;
        g_framebuffer[i + 2] = 240;
        g_framebuffer[i + 3] = 255;
    }

    // Two textures in play: the font atlas and the icon atlas. Commands carry
    // the texture id, so pick per command.
    {
        const ImDrawData* dd = ImGui::GetDrawData();
        (void)dd;
    }
    rasterize(ImGui::GetDrawData(), tex, g_iconTexture);
    writePng(outPath, g_framebuffer, width, height);

    std::printf("wrote %s (%dx%d), %d draw lists, %d vertices\n", outPath, width,
                height, ImGui::GetDrawData()->CmdListsCount,
                ImGui::GetDrawData()->TotalVtxCount);
    if (std::getenv("PDB_DEBUG_CMDS") != nullptr) {
        const ImDrawData* dd = ImGui::GetDrawData();
        for (int n = 0; n < dd->CmdListsCount; ++n) {
            const ImDrawList* list = dd->CmdLists[n];
            for (const ImDrawCmd& cmd : list->CmdBuffer) {
                if (cmd.ElemCount == 0) continue;
                float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
                for (unsigned int i = 0; i < cmd.ElemCount; ++i) {
                    const ImDrawVert& v =
                        list->VtxBuffer[list->IdxBuffer[cmd.IdxOffset + i] + cmd.VtxOffset];
                    minX = std::min(minX, v.pos.x);
                    maxX = std::max(maxX, v.pos.x);
                    minY = std::min(minY, v.pos.y);
                    maxY = std::max(maxY, v.pos.y);
                }
                std::printf("cmd list %d elems %u clip (%.0f,%.0f)-(%.0f,%.0f) vtx box (%.0f,%.0f)-(%.0f,%.0f)\n",
                            n, cmd.ElemCount, cmd.ClipRect.x, cmd.ClipRect.y,
                            cmd.ClipRect.z, cmd.ClipRect.w, minX, minY, maxX, maxY);
            }
        }
    }
    if (std::getenv("PDB_DEBUG_VIEW") != nullptr) {
        std::printf("view: zoom=%.4f offset=(%.1f,%.1f) grid=%d uiScale=%.3f\n",
                    ed.view.zoom, ed.view.offsetX, ed.view.offsetY, ed.view.gridSize,
                    currentUiScale());
        for (size_t i = 0; i < ed.map.vertices.size(); ++i) {
            const Vertex& v = ed.map.vertices[i];
            std::printf("  v%zu (%d,%d) -> local (%.1f,%.1f)\n", i, v.x, v.y,
                        ed.view.mapToScreenX(v.x), ed.view.mapToScreenY(v.y));
        }
    }
    ImGui::DestroyContext();
    return 0;
}
