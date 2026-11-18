// PocketDoomBuilder — Android port (Dear ImGui)
// ui_dialogs.cpp — Modal-ish windows: errors/warnings, about, preferences,
// game configurations and grid setup.

#include <cstdio>
#include <string>

#include "editor.h"
#include "imgui.h"
#include "theme.h"
#include "ui.h"
#include "ui_internal.h"

namespace pdb {
namespace ui {

namespace {

const ThemeColors& T = theme();

// Small helper for the standard dialog window chrome.
bool beginDialog(const char* title, bool* open, ImVec2 size) {
    ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    return ImGui::Begin(title, open,
                        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking);
}

void logStub(const char* what) {
    editor().log(std::string("\"") + what + "\" is not implemented in this build.", 1);
}

} // namespace

void drawDialogs() {
    Editor& ed = editor();

    // --- Errors and Warnings -------------------------------------------------
    if (ed.showErrors) {
        if (beginDialog("Errors and Warnings", &ed.showErrors, ImVec2(520, 320))) {
            if (ImGui::Button("Close")) ed.showErrors = false;
            ImGui::SameLine();
            if (ImGui::Button("Clear")) {
                ed.warnings.clear();
            }
            ImGui::Separator();
            ImGui::BeginChild("##errlist", ImVec2(0, 0), ImGuiChildFlags_Borders);
            if (ed.warnings.empty()) {
                ImGui::TextDisabled("No errors or warnings.");
            }
            for (size_t i = 0; i < ed.warnings.size(); ++i) {
                ImGui::PushID((int)i);
                ed.icons.draw(ImGui::GetWindowDrawList(), "Warning",
                              ImVec2(ImGui::GetCursorScreenPos().x,
                                     ImGui::GetCursorScreenPos().y + 1),
                              14.0f);
                ImGui::Dummy(ImVec2(16.0f, 14.0f));
                ImGui::SameLine(0.0f, 4.0f);
                ImGui::TextWrapped("%s", ed.warnings[i].c_str());
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }

    // --- About ---------------------------------------------------------------
    if (ed.showAbout) {
        if (beginDialog("About PocketDoomBuilder", &ed.showAbout, ImVec2(420, 260))) {
            ImGui::TextUnformatted("PocketDoomBuilder");
            ImGui::TextDisabled("Ultimate Doom Builder, ported to Android");
            ImGui::Separator();
            ImGui::TextWrapped(
                "A Dear ImGui re-creation of the Ultimate Doom Builder "
                "interface for Android tablets and phones.");
            ImGui::Spacing();
            ImGui::TextDisabled("Phase 1: interface shell, WAD loading (Doom / "
                                "Hexen / UDMF), 2D map rendering, pan, zoom and "
                                "selection.");
            ImGui::Spacing();
            ImGui::TextDisabled("Built on Dear ImGui (MIT) and Arimo (Apache 2.0).");
            ImGui::Spacing();
            if (ImGui::Button("OK", ImVec2(80, 0))) ed.showAbout = false;
        }
        ImGui::End();
    }

    // --- Preferences ---------------------------------------------------------
    if (ed.showPreferences) {
        if (beginDialog("Preferences", &ed.showPreferences, ImVec2(460, 320))) {
            if (ImGui::BeginTabBar("##preftabs")) {
                if (ImGui::BeginTabItem("Interface")) {
                    ImGui::Checkbox("Show grid", &ed.showGrid);
                    ImGui::Checkbox("Show vertices", &ed.showVertices);
                    ImGui::Checkbox("Show things", &ed.showThings);
                    ImGui::Checkbox("Fixed things scale", &ed.fixedThingsScale);
                    ImGui::Checkbox("Expanded info panel", &ed.infoPanelExpanded);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Editing")) {
                    ImGui::Checkbox("Snap to grid", &ed.snapToGrid);
                    ImGui::Checkbox("Dynamic grid size", &ed.snapToGrid);
                    ImGui::TextDisabled("Other editing preferences are stubbed.");
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Controls")) {
                    ImGui::TextDisabled("Touch controls arrive in the next phase.");
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Appearance")) {
                    ImGui::TextDisabled("Colours follow the desktop defaults.");
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::Separator();
            if (ImGui::Button("Close", ImVec2(90, 0))) ed.showPreferences = false;
        }
        ImGui::End();
    }

    // --- Game Configurations -------------------------------------------------
    if (ed.showGameConfigs) {
        if (beginDialog("Game Configurations", &ed.showGameConfigs, ImVec2(520, 340))) {
            ImGui::TextUnformatted("Configuration:");
            ImGui::Separator();
            ImGui::BeginChild("##configs", ImVec2(0, -36), ImGuiChildFlags_Borders);
            const std::vector<GameConfig>& configs = gameConfigs();
            for (size_t i = 0; i < configs.size(); ++i) {
                const bool active = configs[i].name == ed.configName;
                if (ImGui::Selectable(configs[i].name.c_str(), active)) {
                    ed.configName = configs[i].name;
                    ed.status = "Game configuration: " + configs[i].name;
                    logStub("Switching game configurations");
                }
            }
            ImGui::EndChild();
            if (ImGui::Button("Close", ImVec2(90, 0))) ed.showGameConfigs = false;
        }
        ImGui::End();
    }

    // --- Grid and Backdrop Setup --------------------------------------------
    if (ed.showGridSetup) {
        if (beginDialog("Grid and Backdrop Setup", &ed.showGridSetup,
                        ImVec2(360, 240))) {
            static int customSize = 32;
            static bool dynamic = false;
            ImGui::TextUnformatted("Grid size:");
            static const int kSizes[] = {1024, 512, 256, 128, 64, 32, 16, 8, 4, 1};
            for (int size : kSizes) {
                const std::string label = std::to_string(size);
                if (ImGui::RadioButton(label.c_str(), ed.view.gridSize == size)) {
                    ed.view.setGridSize(size);
                    ed.status = "Grid size: " + label + " mp";
                }
                ImGui::SameLine();
            }
            ImGui::NewLine();
            ImGui::InputInt("Custom size", &customSize);
            if (ImGui::Button("Apply")) {
                ed.view.setGridSize(customSize);
                ed.status = "Grid size: " + std::to_string(ed.view.gridSize) + " mp";
            }
            ImGui::Checkbox("Dynamic grid size", &dynamic);
            ImGui::Separator();
            if (ImGui::Button("Close", ImVec2(90, 0))) ed.showGridSetup = false;
        }
        ImGui::End();
    }

    // --- Open / Save progress ------------------------------------------------
    // The actual SAF picker is driven from the Java side; these flags tell the
    // platform layer to launch it.
    if (ed.requestOpen) {
        ed.log("Waiting for the system file picker to return a WAD...");
        ed.requestOpen = false; // consumed by the platform layer
    }
}

} // namespace ui
} // namespace pdb
