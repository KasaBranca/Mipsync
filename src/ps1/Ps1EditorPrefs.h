#pragma once

#include <string>

namespace MipsyncEngine::Ps1 {

/// Editor-only paths (not shipped). Stored under %APPDATA%/MipsyncEngine/editor.json
struct EditorPrefs {
    std::string emulatorPath;  // e.g. DuckStation-qt-x64-Release.exe
    std::string biosPath;      // user-provided; never bundled
    std::string extraArgs;     // appended to emulator command line
};

bool LoadEditorPrefs(EditorPrefs& out, std::string& outError);
bool SaveEditorPrefs(const EditorPrefs& prefs, std::string& outError);

/// Resolve emulator:
///   prefs.emulatorPath → MIPSYNC_PS1_EMULATOR → common DuckStation paths.
std::string ResolveEmulatorPath(const EditorPrefs& prefs);

} // namespace MipsyncEngine::Ps1
