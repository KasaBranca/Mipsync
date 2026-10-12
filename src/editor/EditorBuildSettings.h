#pragma once

#include "../build/PlayerBuild.h"
#include "../project/Project.h"
#include "../ps1/Ps1Build.h"
#include "../ps1/Ps1EditorPrefs.h"
#include <string>

namespace MipsyncEngine {

class Engine;

class EditorBuildSettings {
public:
    explicit EditorBuildSettings(Engine* engine);
    ~EditorBuildSettings();

    void SetOpen(bool open) { m_Open = open; }
    bool IsOpen() const { return m_Open; }

    ProjectInfo& Project() { return m_Project; }
    const ProjectInfo& Project() const { return m_Project; }

    void ReloadFromDisk();
    bool SaveProject(std::string& outError);

    void Draw();
    bool RunPcNativeBuild(std::string& outMessage, bool revealOutput = true, bool showDialog = true);
    bool RunBuild(std::string& outMessage, bool revealOutput = true, bool showDialog = true);
    bool RunBuildAndRun(std::string& outMessage);
    bool RunBuildDiscFolder(std::string& outMessage, bool revealOutput = true, bool showDialog = true);
    bool IsBuildInProgress() const { return m_BuildInProgress; }
    bool HasBuildFinished() const { return m_BuildFinished; }
    bool WasBuildSuccessful() const { return m_LastBuildOk; }
    float GetBuildProgress() const { return m_BuildProgress; }
    const std::string& GetBuildMessage() const { return m_LastBuildMessage; }
    const std::string& GetBuildStage() const { return m_BuildStage; }
    const std::string& GetBuildLog() const { return m_BuildLog; }
    bool WasBuildCancelled() const { return m_BuildCancelled; }
    unsigned long GetBuildExitCode() const { return m_BuildExitCode; }
    bool HasBuildDialog() const { return m_BuildDialogRequested || m_BuildDialogVisible; }
    void UpdateBuildProcess() { PollBuildProcess(); }
    void CommandCancelBuild() { CancelBuildProcess(); }
    bool CommandCloseBuildDialog(std::string& outError);
    const Ps1::Ps1BuildResult& GetPs1BuildResult() const { return m_LastPs1Build; }
    const PlayerBuildResult& GetPcBuildResult() const { return m_LastPcBuild; }

private:
    enum class BuildJobKind { Ps1, Ps1AndRun, PcNative, DiscFolder };

    void DrawPs1BuildSettings();
    void DrawPs1EmulatorSettings();
    void DrawScenesInBuild();
    void ScanScenesFolder();
    void AddScenePath(const std::string& projectRelative);
    bool SaveEditorPrefs(std::string& outError);
    void AutoSaveProject();
    bool StartBuildProcess(BuildJobKind kind, const char* cliFlag, std::string& outMessage, bool revealOutput = true, bool showDialog = true);
    void PollBuildProcess();
    void ConsumeBuildOutput(const char* bytes, size_t size);
    void ConsumeBuildLine(const std::string& line);
    void FinishBuildProcess(unsigned long exitCode);
    void CancelBuildProcess();
    void DrawBuildProgressDialog();
    void OpenSuccessfulBuildOutput();

    Engine* m_Engine = nullptr;
    ProjectInfo m_Project;
    Ps1::EditorPrefs m_Ps1Prefs;
    bool m_Open = false;
    std::string m_LastBuildMessage;
    bool m_LastBuildOk = false;
    Ps1::Ps1BuildResult m_LastPs1Build;
    PlayerBuildResult m_LastPcBuild;

    BuildJobKind m_BuildKind = BuildJobKind::Ps1;
    bool m_BuildInProgress = false;
    bool m_RevealOutput = true;
    bool m_BuildFinished = false;
    bool m_BuildCancelled = false;
    bool m_BuildDialogRequested = false;
    bool m_BuildDialogCloseRequested = false;
    bool m_BuildDialogVisible = false;
    unsigned long m_BuildExitCode = 0;
    float m_BuildProgress = 0.0f;
    double m_BuildStartedAt = 0.0;
    std::string m_BuildStage;
    std::string m_BuildLog;
    std::string m_BuildPendingOutput;
    void* m_BuildProcessHandle = nullptr;
    void* m_BuildOutputReadHandle = nullptr;
    void* m_BuildJobHandle = nullptr;
};

} // namespace MipsyncEngine
