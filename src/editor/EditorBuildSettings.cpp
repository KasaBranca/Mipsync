#include "EditorBuildSettings.h"
#include "../build/PlayerBuild.h"
#include "../core/Engine.h"
#include "../core/Log.h"
#include "../core/RuntimePaths.h"
#include "../ps1/Ps1Build.h"
#include "../ps1/Ps1Runner.h"
#include "../assets/AssetManager.h"
#include "EditorTheme.h"
#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <filesystem>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#endif

namespace MipsyncEngine {

namespace fs = std::filesystem;

EditorBuildSettings::EditorBuildSettings(Engine* engine) : m_Engine(engine) {
    ReloadFromDisk();
}

void EditorBuildSettings::ReloadFromDisk() {
    m_Project = ProjectInfo{};
    m_Ps1Prefs = {};
    std::string err;
    Ps1::LoadEditorPrefs(m_Ps1Prefs, err);
    if (!err.empty())
        MIPSYNC_WARN("Editor prefs: {}", err);

    if (!m_Engine)
        return;
    m_Project.path = m_Engine->GetProjectPath();
    if (!Project::LoadFromDir(m_Project.path, m_Project, err))
        MIPSYNC_WARN("Build settings: could not load project: {}", err);
}

bool EditorBuildSettings::SaveProject(std::string& outError) {
    if (m_Project.path.empty()) {
        outError = "No project path.";
        return false;
    }
    if (m_Project.player.productName.empty())
        m_Project.player.productName = m_Project.name;
    if (m_Project.player.scenesInBuild.empty())
        m_Project.player.scenesInBuild.push_back(m_Project.defaultScene);
    m_Project.player.startupSceneIndex = std::clamp(
        m_Project.player.startupSceneIndex, 0,
        std::max(0, static_cast<int>(m_Project.player.scenesInBuild.size()) - 1));
    return Project::SaveToDir(m_Project, outError);
}

bool EditorBuildSettings::SaveEditorPrefs(std::string& outError) {
    return Ps1::SaveEditorPrefs(m_Ps1Prefs, outError);
}

void EditorBuildSettings::ScanScenesFolder() {
    if (m_Project.path.empty())
        return;
    const fs::path scenesDir = PathUtf8::FromString(m_Project.path) / "scenes";
    std::error_code ec;
    if (!fs::is_directory(scenesDir, ec))
        return;

    for (const auto& entry : fs::directory_iterator(scenesDir, ec)) {
        if (!entry.is_regular_file(ec))
            continue;
        if (entry.path().extension() != ".nscene")
            continue;
        const std::string rel =
            PathUtf8::ToString(fs::path("scenes") / entry.path().filename());
        AddScenePath(rel);
    }
}

void EditorBuildSettings::AddScenePath(const std::string& projectRelative) {
    if (projectRelative.empty())
        return;
    if (std::find(m_Project.player.scenesInBuild.begin(), m_Project.player.scenesInBuild.end(),
                  projectRelative) != m_Project.player.scenesInBuild.end())
        return;
    m_Project.player.scenesInBuild.push_back(projectRelative);
    AutoSaveProject();
}

void EditorBuildSettings::DrawPs1BuildSettings() {
    ImGui::TextUnformatted("Build Targets");
    ImGui::Separator();
    ImGui::TextDisabled("PC Native: Builds/Windows/<Product>/<Product>.exe");
    ImGui::TextDisabled("PS1: Builds/PS1/<Product>/out/PSX.EXE");

    char productName[256]{};
    std::strncpy(productName, m_Project.player.productName.c_str(), sizeof(productName) - 1);
    if (ImGui::InputText("Product Name", productName, sizeof(productName))) {
        m_Project.player.productName = productName;
        AutoSaveProject();
    }

    char company[256]{};
    std::strncpy(company, m_Project.player.companyName.c_str(), sizeof(company) - 1);
    if (ImGui::InputText("Company Name", company, sizeof(company))) {
        m_Project.player.companyName = company;
        AutoSaveProject();
    }

    ImGui::TextDisabled("Output: Builds/PS1/<Product>/out/PSX.EXE");
}

void EditorBuildSettings::DrawPs1EmulatorSettings() {
    ImGui::Spacing();
    ImGui::TextUnformatted("Editor: PS1 Emulator (preview only)");
    ImGui::Separator();
    ImGui::TextDisabled(
        "BIOS defaults to OpenBIOS (configured in the Hub, or built into PCSX-Redux).");
    ImGui::TextDisabled("Set a path below only to override with a retail BIOS dump.");

    char emuPath[512]{};
    std::strncpy(emuPath, m_Ps1Prefs.emulatorPath.c_str(), sizeof(emuPath) - 1);
    if (ImGui::InputText("Emulator (.exe)", emuPath, sizeof(emuPath)))
        m_Ps1Prefs.emulatorPath = emuPath;

    char biosPath[512]{};
    std::strncpy(biosPath, m_Ps1Prefs.biosPath.c_str(), sizeof(biosPath) - 1);
    if (ImGui::InputText("BIOS override (optional)", biosPath, sizeof(biosPath)))
        m_Ps1Prefs.biosPath = biosPath;

    char extra[256]{};
    std::strncpy(extra, m_Ps1Prefs.extraArgs.c_str(), sizeof(extra) - 1);
    if (ImGui::InputText("Extra args", extra, sizeof(extra)))
        m_Ps1Prefs.extraArgs = extra;

    if (ImGui::Button("Save emulator paths")) {
        std::string err;
        if (SaveEditorPrefs(err))
            MIPSYNC_INFO("Saved editor PS1 prefs.");
        else
            MIPSYNC_WARN("{}", err);
    }
}

void EditorBuildSettings::DrawScenesInBuild() {
    ImGui::Spacing();
    ImGui::TextUnformatted("Scenes In Build");
    ImGui::Separator();

    if (ImGui::Button("Add Open Scene")) {
        const std::string openScene = m_Engine->GetEditor().GetSceneFilePath();
        if (!openScene.empty()) {
            AssetManager& assets = AssetManager::Get();
            AddScenePath(assets.ToProjectRelative(openScene));
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Scan scenes/"))
        ScanScenesFolder();

    ImGui::BeginChild("ScenesInBuildList", ImVec2(0, 160), true);
    int removeIndex = -1;
    if (ImGui::BeginTable("##scenes_in_build_table", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Scene Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        
        for (int i = 0; i < static_cast<int>(m_Project.player.scenesInBuild.size()); ++i) {
            ImGui::TableNextRow();
            
            // Column 0: Scene Name (Selectable)
            ImGui::TableNextColumn();
            ImGui::PushID(i);
            const bool isStartup = (i == m_Project.player.startupSceneIndex);
            if (ImGui::Selectable(m_Project.player.scenesInBuild[static_cast<size_t>(i)].c_str(),
                                  isStartup)) {
                if (m_Project.player.startupSceneIndex != i) {
                    m_Project.player.startupSceneIndex = i;
                    AutoSaveProject();
                }
            }
            
            // Column 1: Buttons
            ImGui::TableNextColumn();
            
            // Up button
            if (i > 0) {
                if (ImGui::SmallButton("Up")) {
                    std::swap(m_Project.player.scenesInBuild[static_cast<size_t>(i)],
                              m_Project.player.scenesInBuild[static_cast<size_t>(i - 1)]);
                    if (m_Project.player.startupSceneIndex == i)
                        --m_Project.player.startupSceneIndex;
                    else if (m_Project.player.startupSceneIndex == i - 1)
                        ++m_Project.player.startupSceneIndex;
                    AutoSaveProject();
                }
            } else {
                ImGui::BeginDisabled();
                ImGui::SmallButton("Up");
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            
            // Down button
            if (i + 1 < static_cast<int>(m_Project.player.scenesInBuild.size())) {
                if (ImGui::SmallButton("Down")) {
                    std::swap(m_Project.player.scenesInBuild[static_cast<size_t>(i)],
                              m_Project.player.scenesInBuild[static_cast<size_t>(i + 1)]);
                    if (m_Project.player.startupSceneIndex == i)
                        ++m_Project.player.startupSceneIndex;
                    else if (m_Project.player.startupSceneIndex == i + 1)
                        --m_Project.player.startupSceneIndex;
                    AutoSaveProject();
                }
            } else {
                ImGui::BeginDisabled();
                ImGui::SmallButton("Down");
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            
            if (ImGui::SmallButton("Remove"))
                removeIndex = i;
                
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    if (removeIndex >= 0) {
        m_Project.player.scenesInBuild.erase(
            m_Project.player.scenesInBuild.begin() + removeIndex);
        m_Project.player.startupSceneIndex = std::clamp(
            m_Project.player.startupSceneIndex, 0,
            std::max(0, static_cast<int>(m_Project.player.scenesInBuild.size()) - 1));
        AutoSaveProject();
    }
}

bool EditorBuildSettings::RunBuild(std::string& outMessage) {
    std::string saveErr;
    if (!SaveProject(saveErr)) {
        outMessage = saveErr;
        m_LastBuildOk = false;
        m_LastBuildMessage = outMessage;
        return false;
    }

    Ps1::Ps1BuildRequest request;
    request.projectPath = m_Project.path;
    request.settings = m_Project.player;
    request.engineRoot = PathUtf8::ToString(GetExeDirectory());

    m_LastPs1Build = Ps1::Build(request);
    outMessage = m_LastPs1Build.message;
    m_LastBuildOk = m_LastPs1Build.success;
    m_LastBuildMessage = outMessage;

    // Open output folder in Explorer on successful build
    if (m_LastPs1Build.success && !m_LastPs1Build.outputDirectory.empty()) {
#ifdef _WIN32
        const fs::path outDir = fs::path(PathUtf8::FromString(m_LastPs1Build.outputDirectory)) / "out";
        std::error_code ec;
        if (fs::is_directory(outDir, ec)) {
            ShellExecuteW(nullptr, L"explore", outDir.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
#endif
    }

    return m_LastPs1Build.success;
}

bool EditorBuildSettings::RunPcNativeBuild(std::string& outMessage) {
    std::string saveErr;
    if (!SaveProject(saveErr)) {
        outMessage = saveErr;
        m_LastBuildOk = false;
        m_LastBuildMessage = outMessage;
        return false;
    }

    PlayerBuildRequest request;
    request.projectPath = m_Project.path;
    request.settings = m_Project.player;
    request.engineDirectory = PathUtf8::ToString(GetExeDirectory());
    request.outputParent = PlayerBuild::DefaultOutputParent(m_Project.path);

    m_LastPcBuild = PlayerBuild::BuildWindows(request);
    outMessage = m_LastPcBuild.message;
    m_LastBuildOk = m_LastPcBuild.success;
    m_LastBuildMessage = outMessage;

    if (m_LastPcBuild.success && !m_LastPcBuild.outputDirectory.empty()) {
#ifdef _WIN32
        const fs::path outDir = PathUtf8::FromString(m_LastPcBuild.outputDirectory);
        std::error_code ec;
        if (fs::is_directory(outDir, ec))
            ShellExecuteW(nullptr, L"explore", outDir.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
    }

    return m_LastPcBuild.success;
}

bool EditorBuildSettings::RunBuildAndRun(std::string& outMessage) {
    if (!RunBuild(outMessage))
        return false;

    Ps1::LaunchRequest launch;
    launch.prefs = m_Ps1Prefs;
    launch.psxExePath = m_LastPs1Build.psxExePath;
    launch.discCuePath = m_LastPs1Build.discCuePath;

    const Ps1::LaunchResult run = Ps1::LaunchInEmulator(launch);
    if (!run.success) {
        outMessage += "\n" + run.message;
        m_LastBuildMessage = outMessage;
        return false;
    }
    outMessage += "\n" + run.message;
    m_LastBuildMessage = outMessage;
    return true;
}

bool EditorBuildSettings::RunBuildDiscFolder(std::string& outMessage) {
    std::string saveErr;
    if (!SaveProject(saveErr)) {
        outMessage = saveErr;
        m_LastBuildOk = false;
        m_LastBuildMessage = outMessage;
        return false;
    }

    Ps1::Ps1BuildRequest request;
    request.projectPath = m_Project.path;
    request.settings = m_Project.player;
    request.engineRoot = PathUtf8::ToString(GetExeDirectory());

    m_LastPs1Build = Ps1::BuildDiscFolder(request);
    outMessage = m_LastPs1Build.message;
    m_LastBuildOk = m_LastPs1Build.success;
    m_LastBuildMessage = outMessage;

    // Open output folder in Explorer on successful build
    if (m_LastPs1Build.success && !m_LastPs1Build.outputDirectory.empty()) {
#ifdef _WIN32
        const fs::path outDir = fs::path(PathUtf8::FromString(m_LastPs1Build.outputDirectory)) / "out";
        std::error_code ec;
        if (fs::is_directory(outDir, ec)) {
            ShellExecuteW(nullptr, L"explore", outDir.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
#endif
    }

    return m_LastPs1Build.success;
}

void EditorBuildSettings::Draw() {
    if (!m_Open)
        return;

    ImGui::SetNextWindowSize(ImVec2(680, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Build Settings", &m_Open)) {
        ImGui::End();
        return;
    }

    DrawPs1BuildSettings();
    DrawPs1EmulatorSettings();
    DrawScenesInBuild();

    ImGui::Spacing();
    ImGui::Separator();

    std::string saveErr;
    if (EditorTheme::AeroButton("Save Settings", ImVec2(140, EditorTheme::ButtonHeight),
                                AeroButtonKind::Secondary)) {
        if (SaveProject(saveErr))
            MIPSYNC_INFO("Build settings saved.");
        else
            MIPSYNC_WARN("Save failed: {}", saveErr);
    }
    ImGui::SameLine();
    if (EditorTheme::AeroButton("Build PS1", ImVec2(120, EditorTheme::ButtonHeight),
                                AeroButtonKind::Success)) {
        std::string msg;
        RunBuild(msg);
    }
    ImGui::SameLine();
    if (EditorTheme::AeroButton("Build && Run in Emulator", ImVec2(200, EditorTheme::ButtonHeight),
                                AeroButtonKind::Primary)) {
        std::string msg;
        RunBuildAndRun(msg);
    }
    ImGui::SameLine();
    if (EditorTheme::AeroButton("Build PC Native", ImVec2(160, EditorTheme::ButtonHeight),
                                AeroButtonKind::Primary)) {
        std::string msg;
        RunPcNativeBuild(msg);
    }

    if (!m_LastBuildMessage.empty()) {
        ImGui::Spacing();
        const ImVec4 color = m_LastBuildOk ? ImVec4(0.4f, 0.9f, 0.5f, 1.0f)
                                           : ImVec4(1.0f, 0.45f, 0.4f, 1.0f);
        ImGui::TextColored(color, "%s", m_LastBuildMessage.c_str());
    }

    ImGui::End();
}

void EditorBuildSettings::AutoSaveProject() {
    std::string outErr;
    if (!SaveProject(outErr)) {
        MIPSYNC_WARN("Failed to auto-save project settings: {}", outErr);
    }
}

} // namespace MipsyncEngine
