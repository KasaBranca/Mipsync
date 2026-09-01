#pragma once

#include "../project/Project.h"
#include <functional>
#include <string>

namespace MipsyncEngine::Ps1 {

struct Ps1BuildRequest {
    std::string projectPath;
    PlayerSettings settings;
    std::string engineRoot; // directory containing MipsyncEngine.exe (for templates)
    /// Optional build-stage callback. Fractions are monotonic in [0, 1].
    std::function<void(float, const std::string&)> progress;
};

struct Ps1BuildResult {
    bool success = false;
    std::string message;
    std::string outputDirectory;
    std::string psxExePath;   // Builds/PS1/<Product>/out/PSX.EXE (when SDK build succeeds)
    std::string discCuePath;  // optional .cue for emulator
    std::string discFolderPath; // Builds/PS1/<Product>/disc/
    size_t scriptCount = 0;   // Mipsync scripts compiled into the PS1 binary
    size_t entityCount = 0;   // entities exported from startup scene
};

std::string DefaultOutputParent(const std::string& projectPath);

/// Stage mipsync export + PSn00bSDK starter; run SDK build when PSN00BSDK is configured.
Ps1BuildResult Build(const Ps1BuildRequest& request);

/// Create a standalone directory structure (PSX.EXE, SYSTEM.CNF, etc.) for burning to CD.
Ps1BuildResult BuildDiscFolder(const Ps1BuildRequest& request);

/// Export scene/mesh/texture tables + refresh ps1_src runtime (no PSn00bSDK compile).
Ps1BuildResult ExportOnly(const Ps1BuildRequest& request);

} // namespace MipsyncEngine::Ps1
