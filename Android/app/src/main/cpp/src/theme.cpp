// PocketDoomBuilder — Android port (Dear ImGui)
// theme.cpp — Colour tables + ImGui style.

#include "theme.h"

#include <string>

namespace pdb {

namespace {

// .NET known colours used by ColorCollection for thing colours.
struct NamedColor {
    const char* name;
    int r, g, b;
};

const NamedColor kSystemColors[] = {
    {"DimGray", 105, 105, 105},
    {"RoyalBlue", 65, 105, 225},
    {"ForestGreen", 34, 139, 34},
    {"LightSeaGreen", 32, 178, 170},
    {"Firebrick", 178, 34, 34},
    {"DarkViolet", 148, 0, 211},
    {"DarkGoldenrod", 184, 134, 11},
    {"Silver", 192, 192, 192},
    {"Gray", 128, 128, 128},
    {"DeepSkyBlue", 0, 191, 255},
    {"LimeGreen", 50, 205, 50},
    {"PaleTurquoise", 175, 238, 238},
    {"Tomato", 255, 99, 71},
    {"Violet", 238, 130, 238},
    {"Yellow", 255, 255, 0},
    {"WhiteSmoke", 245, 245, 245},
    {"LightPink", 255, 182, 193},
    {"DarkOrange", 255, 140, 0},
    {"DarkKhaki", 189, 183, 107},
    {"Goldenrod", 218, 165, 32},
    {"ControlDark", 172, 168, 153},
    {"ControlLightLight", 255, 255, 255},
};

ImU32 namedColor(const char* name, int alpha = 255) {
    for (const NamedColor& c : kSystemColors) {
        if (std::string(c.name) == name) {
            return IM_COL32(c.r, c.g, c.b, alpha);
        }
    }
    return IM_COL32(255, 0, 255, alpha);
}

// The order of Color.THINGCOLOR00..19 in ColorCollection.cs.
const char* const kThingColorNames[20] = {
    "DimGray",      "RoyalBlue",  "ForestGreen",    "LightSeaGreen",
    "Firebrick",    "DarkViolet", "DarkGoldenrod",  "Silver",
    "Gray",         "DeepSkyBlue","LimeGreen",      "PaleTurquoise",
    "Tomato",       "Violet",     "Yellow",         "WhiteSmoke",
    "LightPink",    "DarkOrange", "DarkKhaki",      "Goldenrod",
};

} // namespace

ImU32 argbToImU32(int argb) { return (ImU32)(uint32_t)argb; }

MapColors::MapColors() {
    for (int i = 0; i < 53; ++i) colors[i] = IM_COL32(0, 0, 0, 255);

    // Defaults copied from ColorCollection's constructor (ARGB ints).
    colors[COLOR_BACKGROUND] = argbToImU32(-16777216);   // black
    colors[COLOR_VERTICES] = argbToImU32(-11425537);
    colors[COLOR_LINEDEFS] = argbToImU32(-1);            // white
    colors[COLOR_MODELWIRE] = argbToImU32(-4259937);
    colors[COLOR_INFOLINE] = argbToImU32(-3750145);
    colors[COLOR_HIGHLIGHT] = argbToImU32(-21504);       // orange
    colors[COLOR_SELECTION] = argbToImU32(-49152);       // light blue
    colors[COLOR_INDICATION] = argbToImU32(-128);
    colors[COLOR_GRID] = argbToImU32(-12171706);
    colors[COLOR_GRID64] = argbToImU32(-13018769);
    colors[COLOR_CROSSHAIR3D] = argbToImU32(-16711681);
    colors[COLOR_HIGHLIGHT3D] = argbToImU32(-24576);
    colors[COLOR_SELECTION3D] = argbToImU32(-49152);
    colors[COLOR_SCRIPTBACKGROUND] = argbToImU32(-1);
    colors[COLOR_LINENUMBERS] = argbToImU32(-13921873);
    colors[COLOR_PLAINTEXT] = argbToImU32(-16777216);
    colors[COLOR_COMMENTS] = argbToImU32(-16744448);
    colors[COLOR_KEYWORDS] = argbToImU32(-16741493);
    colors[COLOR_LITERALS] = argbToImU32(-16776999);
    colors[COLOR_CONSTANTS] = argbToImU32(-8372160);
    colors[COLOR_THREEDFLOOR] = argbToImU32(-65536);

    for (int i = 0; i < 20; ++i) {
        colors[COLOR_THINGCOLOR00 + i] = namedColor(kThingColorNames[i]);
    }
    colors[COLOR_GUIDELINE] = argbToImU32(-256);
}

ImU32 thingColor(const MapColors& colors, int thingType) {
    // Renderer2D assigns thing colours round-robin by type index.
    int idx = thingType % 20;
    if (idx < 0) idx += 20;
    return colors[COLOR_THINGCOLOR00 + idx];
}

// ---------------------------------------------------------------------------
// Chrome theme
// ---------------------------------------------------------------------------

const ThemeColors& theme() {
    // WinForms (Visual Styles) defaults the desktop app inherits.
    static const ThemeColors t = {
        /*windowBg*/       IM_COL32(240, 240, 240, 255), // Control
        /*childBg*/        IM_COL32(240, 240, 240, 255),
        /*menuBg*/         IM_COL32(240, 240, 240, 255),
        /*toolbarBg*/      IM_COL32(240, 240, 240, 255),
        /*statusBg*/       IM_COL32(240, 240, 240, 255),
        /*border*/         IM_COL32(172, 168, 153, 255), // SystemColors.ControlDark
        /*text*/           IM_COL32(0, 0, 0, 255),
        /*textDisabled*/   IM_COL32(109, 109, 109, 255), // SystemColors.GrayText
        /*highlight*/      IM_COL32(204, 228, 247, 255), // visual-styles hover
        /*buttonHover*/    IM_COL32(229, 241, 251, 255),
        /*canvasBorder*/   IM_COL32(100, 100, 100, 255), // Fixed3D
        /*selectionBg*/    IM_COL32(0, 120, 215, 255),
    };
    return t;
}

void applyTheme(float scale) {
    ImGuiStyle& style = ImGui::GetStyle();
    const ThemeColors& t = theme();

    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 0.0f;
    style.PopupRounding = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding = 0.0f;
    style.TabRounding = 0.0f;

    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;

    style.WindowPadding = ImVec2(0, 0);
    style.FramePadding = ImVec2(4.0f * scale, 3.0f * scale);
    style.ItemSpacing = ImVec2(4.0f * scale, 3.0f * scale);
    style.ItemInnerSpacing = ImVec2(4.0f * scale, 3.0f * scale);
    style.ScrollbarSize = 16.0f * scale;
    style.GrabMinSize = 10.0f * scale;
    style.IndentSpacing = 16.0f * scale;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(t.text);
    c[ImGuiCol_TextDisabled] = ImGui::ColorConvertU32ToFloat4(t.textDisabled);
    c[ImGuiCol_WindowBg] = ImGui::ColorConvertU32ToFloat4(t.windowBg);
    c[ImGuiCol_ChildBg] = ImGui::ColorConvertU32ToFloat4(t.childBg);
    c[ImGuiCol_PopupBg] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    c[ImGuiCol_Border] = ImGui::ColorConvertU32ToFloat4(t.border);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.95f, 0.97f, 1.0f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.90f, 0.94f, 0.99f, 1.0f);
    c[ImGuiCol_TitleBg] = ImVec4(0.94f, 0.94f, 0.94f, 1.0f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.90f, 0.90f, 0.90f, 1.0f);
    c[ImGuiCol_MenuBarBg] = ImGui::ColorConvertU32ToFloat4(t.menuBg);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.94f, 0.94f, 0.94f, 1.0f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.80f, 0.80f, 0.78f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.72f, 0.72f, 0.70f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.60f, 0.60f, 0.58f, 1.0f);
    c[ImGuiCol_CheckMark] = ImVec4(0.10f, 0.40f, 0.75f, 1.0f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.60f, 0.75f, 0.90f, 1.0f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.40f, 0.60f, 0.85f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.94f, 0.94f, 0.94f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImGui::ColorConvertU32ToFloat4(t.buttonHover);
    c[ImGuiCol_ButtonActive] = ImGui::ColorConvertU32ToFloat4(t.highlight);
    c[ImGuiCol_Header] = ImGui::ColorConvertU32ToFloat4(t.highlight);
    c[ImGuiCol_HeaderHovered] = ImGui::ColorConvertU32ToFloat4(t.buttonHover);
    c[ImGuiCol_HeaderActive] = ImGui::ColorConvertU32ToFloat4(t.highlight);
    c[ImGuiCol_Separator] = ImGui::ColorConvertU32ToFloat4(t.border);
    c[ImGuiCol_SeparatorHovered] = ImVec4(0.60f, 0.70f, 0.85f, 1.0f);
    c[ImGuiCol_SeparatorActive] = ImVec4(0.40f, 0.60f, 0.85f, 1.0f);
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TextSelectedBg] = ImGui::ColorConvertU32ToFloat4(t.selectionBg);
    c[ImGuiCol_NavCursor] = ImVec4(0.20f, 0.40f, 0.70f, 1.0f);
    c[ImGuiCol_Tab] = ImVec4(0.88f, 0.88f, 0.88f, 1.0f);
    c[ImGuiCol_TabHovered] = ImVec4(0.78f, 0.88f, 0.97f, 1.0f);
    c[ImGuiCol_TabSelected] = ImVec4(0.96f, 0.96f, 0.96f, 1.0f);
}

} // namespace pdb
