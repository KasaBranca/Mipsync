#pragma once

#include "Ps1EditorPrefs.h"
#include <string>

namespace MipsyncEngine::Ps1 {

struct LaunchRequest {
    std::string psxExePath;  // PSX.EXE or boot executable
    std::string discCuePath; // optional .cue (preferred when present)
    EditorPrefs prefs;
};

struct LaunchResult {
    bool success = false;
    std::string message;
};

/// Start external PS1 emulator (DuckStation-compatible CLI). Non-blocking.
LaunchResult LaunchInEmulator(const LaunchRequest& request);

} // namespace MipsyncEngine::Ps1
