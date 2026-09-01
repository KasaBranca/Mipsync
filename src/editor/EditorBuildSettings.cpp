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
#include <cwctype>
#include <cstring>
#include <filesystem>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#include <exdisp.h>
#include <shellapi.h>
#include <shlwapi.h>
#endif

namespace MipsyncEngine {

namespace fs = std::filesystem;

namespace {

std::string UnescapeBuildField(const std::string& value) {
    std::string decoded;
    decoded.reserve(value.size());
    bool escaped = false;
    for (char ch : value) {
        if (escaped) {
            decoded += ch == 'n' ? '\n' : ch;
            escaped = false;
        } else if (ch == '\\') {
            escaped = true;
        } else {
            decoded += ch;
        }
    }
    if (escaped)
        decoded += '\\';
    return decoded;
}

#ifdef _WIN32
std::wstring QuoteWindowsArgument(const std::wstring& value) {
    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'\"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted += ch;
            backslashes = 0;
            continue;
        }
        quoted.append(backslashes, L'\\');
        backslashes = 0;
        quoted += ch;
    }
    quoted.append(backslashes * 2, L'\\');
    quoted += L'\"';
    return quoted;
}

std::wstring NormalizeExplorerPath(const fs::path& path) {
    std::error_code ec;
    fs::path normalized = fs::weakly_canonical(path, ec);
    if (ec)
        normalized = path.lexically_normal();
    std::wstring value = normalized.wstring();
    while (value.size() > 3 && (value.back() == L'\\' || value.back() == L'/'))
        value.pop_back();
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return value;
}

bool FocusExplorerWindowAt(const fs::path& folder) {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize = SUCCEEDED(init);

    IShellWindows* shellWindows = nullptr;
    const HRESULT created = CoCreateInstance(
        CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER, IID_IShellWindows,
        reinterpret_cast<void**>(&shellWindows));
    if (FAILED(created) || !shellWindows) {
        if (uninitialize)
            CoUninitialize();
        return false;
    }

    const std::wstring wanted = NormalizeExplorerPath(folder);
    long count = 0;
    shellWindows->get_Count(&count);
    bool focused = false;
    for (long i = 0; i < count && !focused; ++i) {
        VARIANT index;
        VariantInit(&index);
        index.vt = VT_I4;
        index.lVal = i;
        IDispatch* dispatch = nullptr;
        if (FAILED(shellWindows->Item(index, &dispatch)) || !dispatch)
            continue;

        IWebBrowser2* browser = nullptr;
        const HRESULT queried = dispatch->QueryInterface(
            IID_IWebBrowser2, reinterpret_cast<void**>(&browser));
        dispatch->Release();
        if (FAILED(queried) || !browser)
            continue;

        BSTR locationUrl = nullptr;
        if (SUCCEEDED(browser->get_LocationURL(&locationUrl)) && locationUrl) {
            wchar_t decoded[32768]{};
            DWORD decodedLength = static_cast<DWORD>(std::size(decoded));
            if (SUCCEEDED(PathCreateFromUrlW(locationUrl, decoded, &decodedLength, 0)) &&
                NormalizeExplorerPath(fs::path(decoded)) == wanted) {
                SHANDLE_PTR nativeHandle = 0;
                if (SUCCEEDED(browser->get_HWND(&nativeHandle))) {
                    const HWND window = reinterpret_cast<HWND>(nativeHandle);
                    if (IsIconic(window))
                        ShowWindow(window, SW_RESTORE);
                    ShowWindow(window, SW_SHOW);
                    BringWindowToTop(window);
                    SetForegroundWindow(window);
                    focused = true;
                }
            }
            SysFreeString(locationUrl);
        }
        browser->Release();
    }

    shellWindows->Release();
    if (uninitialize)
        CoUninitialize();
    return focused;
}
#endif

} // namespace

EditorBuildSettings::EditorBuildSettings(Engine* engine) : m_Engine(engine) {
    ReloadFromDisk();
}

EditorBuildSettings::~EditorBuildSettings() {
    CancelBuildProcess();
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
    return StartBuildProcess(BuildJobKind::Ps1, "--build-ps1", outMessage);
}

bool EditorBuildSettings::RunPcNativeBuild(std::string& outMessage) {
    return StartBuildProcess(BuildJobKind::PcNative, "--build-pc", outMessage);
}

bool EditorBuildSettings::RunBuildAndRun(std::string& outMessage) {
    return StartBuildProcess(BuildJobKind::Ps1AndRun, "--build-ps1", outMessage);
}

bool EditorBuildSettings::RunBuildDiscFolder(std::string& outMessage) {
    return StartBuildProcess(BuildJobKind::DiscFolder, "--build-disc", outMessage);
}

bool EditorBuildSettings::StartBuildProcess(BuildJobKind kind, const char* cliFlag,
                                            std::string& outMessage) {
    if (m_BuildInProgress) {
        m_BuildDialogRequested = true;
        outMessage = "A build is already running.";
        return false;
    }

    std::string saveError;
    if (!SaveProject(saveError)) {
        outMessage = saveError;
        m_LastBuildOk = false;
        m_LastBuildMessage = outMessage;
        return false;
    }

#ifdef _WIN32
    const fs::path exePath = GetExeDirectory() / "MipsyncEngine.exe";
    std::error_code ec;
    if (!fs::is_regular_file(exePath, ec)) {
        outMessage = "MipsyncEngine.exe was not found next to the running editor.";
        return false;
    }

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE outputRead = nullptr;
    HANDLE outputWrite = nullptr;
    if (!CreatePipe(&outputRead, &outputWrite, &security, 0)) {
        outMessage = "Could not create the build output pipe.";
        return false;
    }
    SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = outputWrite;
    startup.hStdError = outputWrite;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    const std::wstring exe = exePath.wstring();
    const std::wstring flag = PathUtf8::FromString(cliFlag).wstring();
    const std::wstring project = PathUtf8::FromString(m_Project.path).wstring();
    std::wstring command = QuoteWindowsArgument(exe) + L" " + flag + L" " +
                           QuoteWindowsArgument(project);

    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                        exePath.parent_path().wstring().c_str(), &startup, &process)) {
        const DWORD error = GetLastError();
        CloseHandle(outputRead);
        CloseHandle(outputWrite);
        outMessage = "Could not start the isolated build process (Win32 error " +
                     std::to_string(error) + ").";
        return false;
    }
    CloseHandle(outputWrite);

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                     &limits, sizeof(limits)) ||
            !AssignProcessToJobObject(job, process.hProcess)) {
            CloseHandle(job);
            job = nullptr;
        }
    }
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);

    m_BuildProcessHandle = process.hProcess;
    m_BuildOutputReadHandle = outputRead;
    m_BuildJobHandle = job;
    m_BuildKind = kind;
    m_BuildInProgress = true;
    m_BuildFinished = false;
    m_BuildCancelled = false;
    m_BuildDialogRequested = true;
    m_BuildDialogCloseRequested = false;
    m_BuildProgress = 0.01f;
    m_BuildStartedAt = ImGui::GetTime();
    m_BuildStage = "Starting isolated build process";
    m_BuildLog.clear();
    m_BuildPendingOutput.clear();
    m_LastBuildMessage.clear();
    m_LastBuildOk = false;
    m_LastPs1Build = {};
    m_LastPcBuild = {};
    outMessage = "Build started.";
    return true;
#else
    (void)kind;
    (void)cliFlag;
    outMessage = "Background builds are not available on this platform.";
    m_LastBuildMessage = outMessage;
    m_LastBuildOk = false;
    return false;
#endif
}

void EditorBuildSettings::ConsumeBuildOutput(const char* bytes, size_t size) {
    m_BuildPendingOutput.append(bytes, size);
    size_t newline = 0;
    while ((newline = m_BuildPendingOutput.find('\n')) != std::string::npos) {
        std::string line = m_BuildPendingOutput.substr(0, newline);
        m_BuildPendingOutput.erase(0, newline + 1);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        ConsumeBuildLine(line);
    }
}

void EditorBuildSettings::ConsumeBuildLine(const std::string& line) {
    constexpr const char* progressPrefix = "MIPSYNC_PROGRESS\t";
    constexpr const char* resultPrefix = "MIPSYNC_RESULT_";
    if (line.rfind(progressPrefix, 0) == 0) {
        const size_t percentStart = std::strlen(progressPrefix);
        const size_t stageStart = line.find('\t', percentStart);
        if (stageStart != std::string::npos) {
            try {
                const int percent = std::stoi(line.substr(percentStart, stageStart - percentStart));
                m_BuildProgress = std::max(
                    m_BuildProgress, std::clamp(percent / 100.0f, 0.0f, 0.99f));
            } catch (...) {
            }
            m_BuildStage = line.substr(stageStart + 1);
        }
        return;
    }

    if (line.rfind(resultPrefix, 0) == 0) {
        const size_t separator = line.find('\t');
        if (separator == std::string::npos)
            return;
        const std::string key = line.substr(0, separator);
        const std::string value = line.substr(separator + 1);
        if (key == "MIPSYNC_RESULT_MESSAGE")
            m_LastBuildMessage = UnescapeBuildField(value);
        else if (key == "MIPSYNC_RESULT_OUTPUT") {
            m_LastPs1Build.outputDirectory = value;
            m_LastPcBuild.outputDirectory = value;
        } else if (key == "MIPSYNC_RESULT_PSX")
            m_LastPs1Build.psxExePath = value;
        else if (key == "MIPSYNC_RESULT_CUE")
            m_LastPs1Build.discCuePath = value;
        else if (key == "MIPSYNC_RESULT_DISC")
            m_LastPs1Build.discFolderPath = value;
        else if (key == "MIPSYNC_RESULT_EXE")
            m_LastPcBuild.executablePath = value;
        return;
    }

    if (!line.empty()) {
        m_BuildLog += line + '\n';
        constexpr size_t maxLogBytes = 64 * 1024;
        if (m_BuildLog.size() > maxLogBytes)
            m_BuildLog.erase(0, m_BuildLog.size() - maxLogBytes);
    }
}

void EditorBuildSettings::PollBuildProcess() {
#ifdef _WIN32
    if (!m_BuildInProgress || !m_BuildProcessHandle || !m_BuildOutputReadHandle)
        return;

    HANDLE process = static_cast<HANDLE>(m_BuildProcessHandle);
    HANDLE output = static_cast<HANDLE>(m_BuildOutputReadHandle);
    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(output, nullptr, 0, nullptr, &available, nullptr) || available == 0)
            break;
        char buffer[4096];
        DWORD read = 0;
        if (!ReadFile(output, buffer, std::min<DWORD>(available, sizeof(buffer)), &read, nullptr) ||
            read == 0)
            break;
        ConsumeBuildOutput(buffer, read);
    }

    DWORD exitCode = STILL_ACTIVE;
    if (GetExitCodeProcess(process, &exitCode) && exitCode != STILL_ACTIVE)
        FinishBuildProcess(exitCode);
#endif
}

void EditorBuildSettings::FinishBuildProcess(unsigned long exitCode) {
#ifdef _WIN32
    // The process can exit between the last UI-frame poll and its final
    // structured result writes. Drain the pipe once more before closing it.
    if (m_BuildOutputReadHandle) {
        HANDLE output = static_cast<HANDLE>(m_BuildOutputReadHandle);
        for (;;) {
            DWORD available = 0;
            if (!PeekNamedPipe(output, nullptr, 0, nullptr, &available, nullptr) || available == 0)
                break;
            char buffer[4096];
            DWORD read = 0;
            if (!ReadFile(output, buffer, std::min<DWORD>(available, sizeof(buffer)), &read,
                          nullptr) || read == 0)
                break;
            ConsumeBuildOutput(buffer, read);
        }
    }
    if (!m_BuildPendingOutput.empty()) {
        ConsumeBuildLine(m_BuildPendingOutput);
        m_BuildPendingOutput.clear();
    }
    if (m_BuildOutputReadHandle)
        CloseHandle(static_cast<HANDLE>(m_BuildOutputReadHandle));
    if (m_BuildProcessHandle)
        CloseHandle(static_cast<HANDLE>(m_BuildProcessHandle));
    if (m_BuildJobHandle)
        CloseHandle(static_cast<HANDLE>(m_BuildJobHandle));
#endif
    m_BuildOutputReadHandle = nullptr;
    m_BuildProcessHandle = nullptr;
    m_BuildJobHandle = nullptr;
    m_BuildInProgress = false;
    m_BuildFinished = true;
    m_BuildProgress = 1.0f;
    m_LastBuildOk = exitCode == 0 && !m_BuildCancelled;
    m_LastPs1Build.success = m_LastBuildOk;
    m_LastPcBuild.success = m_LastBuildOk;

    if (m_BuildCancelled) {
        m_BuildStage = "Build cancelled";
        m_LastBuildMessage = "Build cancelled.";
    } else if (m_LastBuildOk) {
        m_BuildStage = "Build complete";
    } else {
        m_BuildStage = "Build failed";
        if (m_LastBuildMessage.empty())
            m_LastBuildMessage = "Build process exited with code " + std::to_string(exitCode) + ".";
    }

    if (m_LastBuildOk && m_BuildKind == BuildJobKind::Ps1AndRun) {
        Ps1::LaunchRequest launch;
        launch.prefs = m_Ps1Prefs;
        launch.psxExePath = m_LastPs1Build.psxExePath;
        launch.discCuePath = m_LastPs1Build.discCuePath;
        const Ps1::LaunchResult run = Ps1::LaunchInEmulator(launch);
        m_LastBuildOk = run.success;
        m_LastPs1Build.success = run.success;
        if (!m_LastBuildMessage.empty())
            m_LastBuildMessage += '\n';
        m_LastBuildMessage += run.message;
        m_BuildStage = run.success ? "Build complete - emulator launched"
                                   : "Build complete - emulator launch failed";
    }

    if (m_LastBuildOk && m_BuildKind != BuildJobKind::Ps1AndRun)
        OpenSuccessfulBuildOutput();

    // Successful builds need no acknowledgement; errors and cancellations
    // keep the dialog open so their output remains visible.
    if (m_LastBuildOk)
        m_BuildDialogCloseRequested = true;
}

void EditorBuildSettings::CancelBuildProcess() {
#ifdef _WIN32
    if (m_BuildInProgress) {
        m_BuildCancelled = true;
        if (m_BuildJobHandle)
            TerminateJobObject(static_cast<HANDLE>(m_BuildJobHandle), ERROR_CANCELLED);
        else if (m_BuildProcessHandle)
            TerminateProcess(static_cast<HANDLE>(m_BuildProcessHandle), ERROR_CANCELLED);
        if (m_BuildProcessHandle)
            WaitForSingleObject(static_cast<HANDLE>(m_BuildProcessHandle), 500);
        FinishBuildProcess(ERROR_CANCELLED);
    }
#endif
}

void EditorBuildSettings::OpenSuccessfulBuildOutput() {
#ifdef _WIN32
    fs::path output;
    if (m_BuildKind == BuildJobKind::PcNative)
        output = PathUtf8::FromString(m_LastPcBuild.outputDirectory);
    else if (m_BuildKind == BuildJobKind::DiscFolder && !m_LastPs1Build.discFolderPath.empty())
        output = PathUtf8::FromString(m_LastPs1Build.discFolderPath);
    else if (!m_LastPs1Build.outputDirectory.empty())
        output = PathUtf8::FromString(m_LastPs1Build.outputDirectory) / "out";
    std::error_code ec;
    if (!output.empty() && fs::is_directory(output, ec) && !FocusExplorerWindowAt(output))
        ShellExecuteW(nullptr, L"explore", output.wstring().c_str(), nullptr, nullptr,
                      SW_SHOWNORMAL);
#endif
}

void EditorBuildSettings::DrawBuildProgressDialog() {
    if (m_BuildDialogRequested) {
        ImGui::OpenPopup("Build Progress");
        m_BuildDialogRequested = false;
    }

    ImGui::SetNextWindowSize(ImVec2(560.0f, 330.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Build Progress", nullptr,
                                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse))
        return;

    if (m_BuildDialogCloseRequested) {
        m_BuildDialogCloseRequested = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    const char* target = "PS1";
    if (m_BuildKind == BuildJobKind::PcNative)
        target = "PC Native";
    else if (m_BuildKind == BuildJobKind::DiscFolder)
        target = "PS1 Disc Folder";
    else if (m_BuildKind == BuildJobKind::Ps1AndRun)
        target = "PS1 + Emulator";

    ImGui::Text("Building: %s", target);
    ImGui::Spacing();
    ImGui::TextWrapped("%s", m_BuildStage.empty() ? "Preparing build..." : m_BuildStage.c_str());
    const int percent = static_cast<int>(m_BuildProgress * 100.0f + 0.5f);
    const std::string overlay = std::to_string(percent) + "%";
    ImGui::ProgressBar(m_BuildProgress, ImVec2(-1.0f, 22.0f), overlay.c_str());
    const double elapsed = std::max(0.0, ImGui::GetTime() - m_BuildStartedAt);
    ImGui::TextDisabled("Elapsed: %.1f seconds", elapsed);

    ImGui::Spacing();
    ImGui::TextDisabled("Build output");
    ImGui::BeginChild("##build_output", ImVec2(0.0f, 150.0f), true,
                      ImGuiWindowFlags_HorizontalScrollbar);
    if (m_BuildLog.empty())
        ImGui::TextDisabled("Waiting for build output...");
    else
        ImGui::TextUnformatted(m_BuildLog.c_str());
    if (m_BuildInProgress && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    if (m_BuildInProgress) {
        if (EditorTheme::AeroButton("Cancel Build", ImVec2(130.0f, EditorTheme::ButtonHeight),
                                    AeroButtonKind::Danger))
            CancelBuildProcess();
    } else {
        const ImVec4 color = m_LastBuildOk ? ImVec4(0.4f, 0.9f, 0.5f, 1.0f)
                                           : ImVec4(1.0f, 0.45f, 0.4f, 1.0f);
        ImGui::TextColored(color, "%s", m_LastBuildOk ? "Succeeded" : "Failed");
        if (ImGui::IsItemHovered() && !m_LastBuildMessage.empty())
            ImGui::SetTooltip("%s", m_LastBuildMessage.c_str());
        ImGui::SameLine();
        if (EditorTheme::AeroButton("Close", ImVec2(100.0f, EditorTheme::ButtonHeight),
                                    AeroButtonKind::Secondary))
            ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void EditorBuildSettings::Draw() {
    PollBuildProcess();
    DrawBuildProgressDialog();

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
    ImGui::BeginDisabled(m_BuildInProgress);
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
    ImGui::EndDisabled();

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
