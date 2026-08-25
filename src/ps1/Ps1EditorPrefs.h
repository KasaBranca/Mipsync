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
///   prefs.emulatorPath → MIPSYNC_PS1_EMULATOR → bundled PCSX-Redux → common DuckStation paths.
std::string ResolveEmulatorPath(const EditorPrefs& prefs);

/// Returns the path to the PCSX-Redux executable bundled next to the engine,
/// or an empty string if no bundled runtime is present.
std::string FindBundledEmulator();

/// Returns the path to the OpenBIOS image bundled next to the engine
/// (ps1_runtime/bios/openbios.bin), or an empty string if not present.
std::string FindBundledOpenBios();

} // namespace MipsyncEngine::Ps1
