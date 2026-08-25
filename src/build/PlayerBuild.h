#pragma once
// Mipsync Engine — PC native standalone player packaging.

#include "../project/Project.h"
#include <string>

namespace MipsyncEngine {

struct PlayerBuildRequest {
    std::string projectPath;
    PlayerSettings settings;
    /// Folder that contains MipsyncEngine.exe (empty = running executable directory).
    std::string engineDirectory;
    /// Parent output folder; build writes `<outputParent>/<productName>/`.
    std::string outputParent;
};

struct PlayerBuildResult {
    bool success = false;
    std::string message;
    std::string outputDirectory;
    std::string executablePath;
};

namespace PlayerBuild {

/// Unity-style layout: `<output>/<Product>/<Product>.exe` + `<Product>_Data/`.
PlayerBuildResult BuildWindows(const PlayerBuildRequest& request);

/// Default: `<project>/Builds/Windows`
std::string DefaultOutputParent(const std::string& projectPath);

std::string GetRunningEngineDirectory();

} // namespace PlayerBuild

} // namespace MipsyncEngine
