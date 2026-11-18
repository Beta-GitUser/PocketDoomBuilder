// PocketDoomBuilder — Android port (Dear ImGui)
// ui_help.cpp — The Help menu's windows: the reference manual browser, the
// keyboard shortcut reference, the About box and the platform integrations.
//
// The desktop opens Help/*.chm in the Win32 help viewer; there is no such
// viewer on Android, so the same pages (shipped in assets/help) are formatted
// by helpdoc.cpp and drawn here. Navigation is the manual's own: the index
// mirrors Help/Contents.hhc, in-page and cross-page links work, and Back walks
// a page history.

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "editor.h"
#include "helpdoc.h"
#include "icons.h"
#include "imgui.h"
#include "theme.h"
#include "ui.h"
#include "ui_internal.h"

namespace pdb {
namespace ui {

namespace {

const ThemeColors& T = theme();

inline std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ') ++a;
    while (b > a && (unsigned char)s[b - 1] <= ' ') --b;
    return s.substr(a, b - a);
}

// ---------------------------------------------------------------------------
// Manual assets
// ---------------------------------------------------------------------------

bool readAsset(const std::string& path, std::vector<uint8_t>& out) {
    return platformReadAsset(path, out);
}

struct IndexEntry {
    std::string page;
    std::string title;
};

std::vector<IndexEntry>& indexEntries() {
    static std::vector<IndexEntry> entries;
    static bool loaded = false;
    if (loaded) return entries;
    loaded = true;

    std::vector<uint8_t> bytes;
    if (!readAsset("help-index.txt", bytes)) return entries;
    const std::string text(bytes.begin(), bytes.end());
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t bar = line.find('|');
        if (bar == std::string::npos) continue;
        IndexEntry entry;
        entry.page = trim(line.substr(0, bar));
        entry.title = trim(line.substr(bar + 1));
        if (!entry.page.empty()) entries.push_back(entry);
    }
    return entries;
}

// Cache of formatted pages: the parser is quick, but the manual is browsed a
// lot and ImGui draws every frame.
struct PageCache {
    std::string file;
    HelpPage page;
    std::string error;
};
PageCache& pageCache() {
    static PageCache cache;
    return cache;
}

HelpPage* loadPage(const std::string& file, std::string& error) {
    PageCache& cache = pageCache();
    if (cache.file == file) {
        error = cache.error;
        return cache.error.empty() ? &cache.page : nullptr;
    }
    cache.file = file;
    cache.error.clear();
    cache.page = HelpPage();

    std::string path = file;
    if (path.find("..") != std::string::npos) {
        cache.error = "Refusing to open '" + file + "' (outside the manual).";
        error = cache.error;
        return nullptr;
    }
    if (path.compare(0, 5, "help/") == 0) path = path.substr(5);
    std::vector<uint8_t> bytes;
    if (!readAsset("help/" + path, bytes)) {
        cache.error = "The manual page '" + file + "' is not in this build.";
        error = cache.error;
        return nullptr;
    }
    const std::string html(bytes.begin(), bytes.end());
    cache.page = parseHelpPage(html, path);
    error.clear();
    return &cache.page;
}

// Nested <ul> indents are already in the span data; the ordering is flat.
void openHelpPage(Editor& ed, const std::string& target) {
    std::string file = target;
    std::string anchor;
    const size_t hash = file.find('#');
    if (hash != std::string::npos) {
        anchor = file.substr(hash);
        file = file.substr(0, hash);
    }
    if (file.empty()) file = ed.helpPage;
    if (file == ed.helpPage) {
        ed.helpAnchor = anchor;
        return;
    }
    ed.helpHistory.push_back(ed.helpPage);
    if (ed.helpHistory.size() > 32) ed.helpHistory.erase(ed.helpHistory.begin());
    ed.helpPage = file;
    ed.helpAnchor = anchor;
    ed.helpStatus.clear();
}

void helpBack(Editor& ed) {
    if (ed.helpHistory.empty()) return;
    ed.helpPage = ed.helpHistory.back();
    ed.helpHistory.pop_back();
    ed.helpAnchor.clear();
}

// Word-level flow layout.
//
// A manual line is a sequence of styled runs (a sentence with a bold word and a
// link in it), and ImGui's own wrapping only works on one string at a time.
// Rather than fight the cursor with SameLine/NewLine sequences, the runs are laid
// out here as words with the font each one is drawn in, and emitted straight to
// the draw list. Returns true when a link word was clicked.
struct FlowAtom {
    std::string text;
    bool bold = false;
    bool code = false;
    ImU32 color = 0;
    std::string link;
};

void collectAtoms(const HelpLine& line, std::vector<FlowAtom>& atoms) {
    const ImU32 bodyColor = ImGui::ColorConvertFloat4ToU32(
        ImGui::ColorConvertU32ToFloat4(T.text));
    const ImU32 linkColor = IM_COL32(26, 90, 190, 255);
    const ImU32 codeColor = IM_COL32(64, 115, 51, 255);
    const ImU32 headColor = IM_COL32(40, 40, 40, 255);

    for (const HelpSpan& span : line.spans) {
        if (span.style == HelpStyle::Rule) continue;
        const bool bold = span.style == HelpStyle::Title ||
                          span.style == HelpStyle::Heading2 ||
                          span.style == HelpStyle::Heading3 || span.bold;
        ImU32 color = bodyColor;
        if (!span.link.empty()) color = linkColor;
        else if (span.style == HelpStyle::Code) color = codeColor;
        else if (span.style == HelpStyle::Title || span.style == HelpStyle::Heading2 ||
                 span.style == HelpStyle::Heading3) {
            color = headColor;
        }
        // Split into words, keeping trailing spaces on the word so gaps survive.
        size_t pos = 0;
        while (pos < span.text.size()) {
            size_t stop = span.text.find(' ', pos);
            if (stop == std::string::npos) stop = span.text.size();
            while (stop < span.text.size() && span.text[stop] == ' ') ++stop;
            FlowAtom atom;
            atom.text = span.text.substr(pos, stop - pos);
            atom.bold = bold;
            atom.code = span.codeFont || span.style == HelpStyle::Code;
            atom.color = color;
            atom.link = span.link;
            atoms.push_back(atom);
            pos = stop;
        }
    }
}

bool renderFlowLine(const HelpLine& line, float width) {
    std::vector<FlowAtom> atoms;
    collectAtoms(line, atoms);
    if (atoms.empty()) return false;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float startX = origin.x + line.indent;
    const float wrapX = origin.x + width;
    float x = startX;
    float y = origin.y;
    const float lineHeight = ImGui::GetTextLineHeight();
    bool firstOnRow = true;
    bool clicked = false;
    ImFont* boldFont = editor().fontBold;
    const float fontSize = ImGui::GetFontSize();

    for (const FlowAtom& atom : atoms) {
        ImFont* font = (atom.bold && boldFont != nullptr) ? boldFont : ImGui::GetFont();
        const ImVec2 size = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, atom.text.c_str());
        if (!firstOnRow && x + size.x > wrapX) {
            x = startX;
            y += lineHeight + 2.0f;
            firstOnRow = true;
        }
        // A leading space on a new row is not part of the text.
        const char* text = atom.text.c_str();
        if (firstOnRow && text[0] == ' ') ++text;
        if (*text != '\0') {
            drawList->AddText(font, fontSize, ImVec2(x, y), atom.color, text);
            const float drawWidth =
                font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text).x;
            if (!atom.link.empty()) {
                // Underline, so links read as links on a touch screen.
                drawList->AddLine(ImVec2(x, y + lineHeight - 1.0f),
                                  ImVec2(x + drawWidth, y + lineHeight - 1.0f),
                                  atom.color, 1.0f);
                const ImVec2 min(x, y);
                const ImVec2 max(x + drawWidth + 2.0f, y + lineHeight);
                if (ImGui::IsMouseHoveringRect(min, max) &&
                    ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    clicked = true;
                }
            }
            x += drawWidth;
        }
        firstOnRow = false;
    }
    // Advance the layout by the height that was actually used. SetCursorScreenPos
    // alone would trip ImGui's boundary assertion, so move with a Dummy().
    const float used = (y + lineHeight) - origin.y;
    ImGui::Dummy(ImVec2(0.0f, used));
    return clicked;
}

// An icon that opens the diagnostics panel; the desktop has no equivalent, but
// on a phone it is the only way to read the log.
void diagnosticsButton() {
    if (ImGui::Button("Diagnostics")) {
        editor().requestPlatformDiagnostics = true;
    }
}

// ---------------------------------------------------------------------------
// The manual browser
// ---------------------------------------------------------------------------

void drawIndexPane(Editor& ed, float width) {
    ImGui::BeginChild("##helpindex", ImVec2(width, 0), ImGuiChildFlags_Borders);
    ImGui::TextDisabled("Reference Manual");
    ImGui::SetNextItemWidth(-1.0f);
    char filter[64];
    std::snprintf(filter, sizeof(filter), "%s", ed.helpFilter.c_str());
    if (ImGui::InputTextWithHint("##filter", "Search pages", filter, sizeof(filter))) {
        ed.helpFilter = filter;
    }
    ImGui::Separator();

    std::string needle = ed.helpFilter;
    for (char& c : needle) c = (char)std::tolower((unsigned char)c);

    for (const IndexEntry& entry : indexEntries()) {
        std::string title = entry.title;
        if (!needle.empty()) {
            std::string haystack = title;
            for (char& c : haystack) c = (char)std::tolower((unsigned char)c);
            if (haystack.find(needle) == std::string::npos) continue;
        }
        ImGui::PushID(entry.page.c_str());
        const bool selected = entry.page == ed.helpPage;
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.10f, 0.35f, 0.75f, 1.0f));
        }
        if (ImGui::Selectable(title.c_str(), selected)) {
            if (entry.page != ed.helpPage) {
                ed.helpHistory.push_back(ed.helpPage);
                ed.helpPage = entry.page;
                ed.helpAnchor.clear();
            }
            ed.showHelpIndex = false;
        }
        ImGui::PopID();
        if (selected) ImGui::PopStyleColor();
    }
    ImGui::EndChild();
}

void drawPagePane(Editor& ed) {
    ImGui::BeginChild("##helppage", ImVec2(0, 0), ImGuiChildFlags_Borders);

    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
    if (ImGui::Button("Back") && !ed.helpHistory.empty()) {
        helpBack(ed);
    }
    ImGui::SameLine();
    if (ImGui::Button("Index")) ed.showHelpIndex = !ed.showHelpIndex;
    ImGui::PopStyleColor();
    ImGui::Separator();

    std::string error;
    HelpPage* page = loadPage(ed.helpPage, error);
    if (page == nullptr) {
        ImGui::TextWrapped("%s", error.c_str());
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("Pick another page from the index.");
        ImGui::PopStyleColor();
        ImGui::EndChild();
        return;
    }

    const float width = ImGui::GetContentRegionAvail().x;
    for (const HelpLine& line : page->lines) {
        if (line.blank) {
            ImGui::Spacing();
            continue;
        }
        // Indentation is applied by the renderer from line.indent.
        if (line.spans.size() == 1 && line.spans[0].style == HelpStyle::Rule) {
            ImGui::Separator();
        } else if (line.spans.size() == 1 &&
                   (line.spans[0].style == HelpStyle::Title ||
                    line.spans[0].style == HelpStyle::Heading2 ||
                    line.spans[0].style == HelpStyle::Heading3)) {
            if (line.spans[0].style != HelpStyle::Title) ImGui::Spacing();
            renderFlowLine(line, width);
            ImGui::Spacing();
        } else if (renderFlowLine(line, width)) {
            // A link word was clicked: find its target and navigate.
            for (const HelpSpan& span : line.spans) {
                if (span.link.empty()) continue;
                const std::string target = span.link;
                if (target.compare(0, 4, "http") == 0) {
                    ed.pendingUrl = target;
                    ed.requestOpenUrl = true;
                } else {
                    openHelpPage(ed, target);
                }
                break;
            }
        }

    }
    if (!ed.helpStatus.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", ed.helpStatus.c_str());
    }
    ImGui::EndChild();
}

} // namespace

// ---------------------------------------------------------------------------
// Help > Reference Manual
// ---------------------------------------------------------------------------

void drawHelpWindow() {
    Editor& ed = editor();
    if (!ed.showHelp) return;

    ImGui::SetNextWindowSize(ImVec2(760.0f * currentUiScale(), 480.0f * currentUiScale()),
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Reference Manual", &ed.showHelp, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    const bool wide = ImGui::GetContentRegionAvail().x > 520.0f * currentUiScale();
    if (wide && ed.showHelpIndex) {
        drawIndexPane(ed, 220.0f * currentUiScale());
        ImGui::SameLine();
    } else if (!wide) {
        if (ImGui::Button(ed.showHelpIndex ? "Hide index" : "Show index")) {
            ed.showHelpIndex = !ed.showHelpIndex;
        }
        ImGui::Separator();
    }
    drawPagePane(ed);
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Help > Keyboard Shortcuts Reference
// ---------------------------------------------------------------------------

void drawShortcutsWindow() {
    Editor& ed = editor();
    if (!ed.showShortcuts) return;

    ImGui::SetNextWindowSize(ImVec2(620.0f * currentUiScale(), 420.0f * currentUiScale()),
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Keyboard Shortcuts Reference", &ed.showShortcuts,
                      ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    // The desktop builds this table from its action list; the action system is
    // not ported yet, so the manual's own controls pages are shown here.
    ImGui::TextWrapped(
        "Controls and their default keys, from the manual. The desktop generates "
        "this table from its action list, which this build has not ported yet.");
    ImGui::Spacing();
    if (ImGui::Button("Manual: Classic Controls")) {
        ed.showHelp = true;
        ed.helpPage = "classiccontrols.html";
        ed.helpAnchor.clear();
        ed.showHelpIndex = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Editing Modes")) {
        ed.showHelp = true;
        ed.helpPage = "editingmodes.html";
        ed.helpAnchor.clear();
        ed.showHelpIndex = false;
    }
    ImGui::Separator();

    ImGui::BeginChild("##shortcuts", ImVec2(0, 0), ImGuiChildFlags_Borders);
    std::string error;
    HelpPage* page = loadPage("classiccontrols.html", error);
    if (page != nullptr) {
        for (const HelpLine& line : page->lines) {
            if (line.blank) {
                ImGui::Spacing();
                continue;
            }
            renderFlowLine(line, ImGui::GetContentRegionAvail().x);
        }
    } else {
        ImGui::TextWrapped("%s", error.c_str());
    }
    ImGui::EndChild();
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Help > Diagnostics and Log
//
// This replaces the floating overlay button the port used to draw on top of the
// GL surface: the log, the log actions and the platform settings belong in the UI,
// drawn with the same widgets as everything else.
// ---------------------------------------------------------------------------

void drawDiagnosticsWindow() {
    Editor& ed = editor();
    if (!ed.showDiagnostics) return;

    ImGui::SetNextWindowSize(ImVec2(560.0f * currentUiScale(), 420.0f * currentUiScale()),
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Diagnostics and Log", &ed.showDiagnostics,
                      ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("##diagntabs")) {
        if (ImGui::BeginTabItem("Log")) {
            if (ImGui::Button("Copy log")) ed.requestLog = true;
            ImGui::SameLine();
            if (ImGui::Button("Save to Documents")) ed.requestSaveLog = true;
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            if (ImGui::Button("Refresh")) ed.requestPlatformDiagnostics = true;
            ImGui::Separator();
            ImGui::BeginChild("##logtext", ImVec2(0, 0), ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_HorizontalScrollbar);
            if (editor().fontMono != nullptr) ImGui::PushFont(editor().fontMono);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            ImGui::TextUnformatted(ed.diagnosticsText.c_str());
            ImGui::PopStyleColor();
            if (editor().fontMono != nullptr) ImGui::PopFont();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Settings")) {
            // --- safe area ---------------------------------------------------
            bool safeArea = ed.safeAreaEnabled;
            if (ImGui::Checkbox("Respect safe area (notch, cutout, gesture bar)",
                                &safeArea)) {
                ed.safeAreaEnabled = safeArea;
                ed.pendingSafeArea = safeArea;
                ed.requestSafeAreaChange = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Keeps the interface clear of a display cutout and the gesture "
                    "bar. The insets come from the system.");
            }
            if (ed.safeAreaInsetsKnown) {
                ImGui::TextDisabled("Detected insets - top: %d px, bottom: %d px, "
                                    "left: %d px, right: %d px",
                                    ed.safeAreaTop, ed.safeAreaBottom, ed.safeAreaLeft,
                                    ed.safeAreaRight);
            } else {
                ImGui::TextDisabled("The system has not reported any insets yet.");
            }
            ImGui::Separator();
            ImGui::TextDisabled("Other settings from the desktop's Preferences "
                                "window are not ported yet.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Help > About
// ---------------------------------------------------------------------------

void drawAboutWindow() {
    Editor& ed = editor();
    if (!ed.showAbout) return;

    ImGui::SetNextWindowSize(ImVec2(460.0f * currentUiScale(), 340.0f * currentUiScale()),
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("About PocketDoomBuilder", &ed.showAbout,
                      ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    // The desktop's AboutForm has three tabs; a phone uses a tab bar too.
    if (ImGui::BeginTabBar("##abouttabs")) {
        if (ImGui::BeginTabItem("About Ultimate Doom Builder")) {
            ImGui::TextUnformatted("Ultimate Doom Builder");
            ImGui::TextDisabled("PocketDoomBuilder - interface port");
            ImGui::Separator();
            ImGui::TextWrapped(
                "A Dear ImGui re-creation of the Ultimate Doom Builder interface "
                "for Android phones and tablets.");
            ImGui::Spacing();
            ImGui::TextUnformatted("Ultimate Doom Builder contributors:");
            ImGui::BulletText("boris");
            ImGui::BulletText("ZZYZX");
            ImGui::BulletText("dpJudas (OpenGL renderer)");
            ImGui::BulletText("StrikerMan780 (64-bit version)");
            ImGui::BulletText("Talon1024 (Linux compatibility)");
            ImGui::BulletText("anotak (performance optimizations)");
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped(
                "And others; for the full list see the project's GitHub page. "
                "Background image is by Zanieon.");
            ImGui::PopStyleColor();
            ImGui::Spacing();
            if (ImGui::Button("Project page at github.com")) {
                ed.pendingUrl = "https://github.com/jewalky/UltimateDoomBuilder";
                ed.requestOpenUrl = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Official thread at ZDoom.org")) {
                ed.pendingUrl = "https://forum.zdoom.org/viewtopic.php?f=232&t=66745";
                ed.requestOpenUrl = true;
            }
            ImGui::Spacing();
            if (ImGui::Button("Copy Version")) {
                ed.helpStatus = "Version string copied to the log";
                ed.log("PocketDoomBuilder 0.1.0 (Android port of Ultimate Doom Builder)");
            }
            ImGui::SameLine();
            diagnosticsButton();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Based on...")) {
            ImGui::TextWrapped(
                "Doom Builder is designed and programmed by Pascal vd Heiden. "
                "Several game configurations were written by various members of "
                "the Doom community. See the website for a complete list of "
                "credits.");
            ImGui::Spacing();
            ImGui::TextWrapped(
                "GZDoom Builder is designed and programmed by MaxED; GZDoom "
                "Builder uses game configurations created by Gez.");
            ImGui::Spacing();
            if (ImGui::Button("www.doombuilder.com")) {
                ed.pendingUrl = "https://www.doombuilder.com";
                ed.requestOpenUrl = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("www.codeimp.com")) {
                ed.pendingUrl = "https://www.codeimp.com";
                ed.requestOpenUrl = true;
            }
            ImGui::Spacing();
            if (ImGui::Button("GZDoom Builder page at github.com")) {
                ed.pendingUrl = "https://github.com/m-x-d/GZDoom-Builder";
                ed.requestOpenUrl = true;
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("This port")) {
            ImGui::TextWrapped(
                "PocketDoomBuilder is a separate Android port: the same interface, "
                "built with Dear ImGui and a small C++ core, driven by the "
                "repository's GitHub Actions builds.");
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("Dear ImGui: MIT licence");
            ImGui::TextWrapped("Arimo font: Apache 2.0");
            ImGui::TextWrapped("Icon set: the desktop project's resources");
            ImGui::PopStyleColor();
            ImGui::Spacing();
            ImGui::TextUnformatted("The log, reports and the manual export:");
            diagnosticsButton();
            if (ImGui::Button("Save the manual to Documents")) {
                ed.requestExportManual = true;
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void drawHelpMenuWindows() {
    drawHelpWindow();
    drawDiagnosticsWindow();
    drawShortcutsWindow();
    drawAboutWindow();
}

} // namespace ui
} // namespace pdb
