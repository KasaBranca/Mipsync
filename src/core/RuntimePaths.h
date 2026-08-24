#pragma once

#include <filesystem>
#include <string>

namespace MipsyncEngine {

/// Full path to the running executable.
std::filesystem::path GetExecutablePath();

/// Directory containing the running executable.
std::filesystem::path GetExeDirectory();

/// ``<exeStem>_Data`` next to the exe when ``boot.json`` exists (Unity-style player build).
std::filesystem::path DetectPlayerDataDirectory();

/// Engine fonts/icons: ``<exe>/resources`` or ``<Product>_Data/Resources``.
std::filesystem::path GetBundledResourcesDirectory();

/// Persistent engine-wide settings directory that survives version upgrades.
/// Windows: ``%APPDATA%\MipsyncEngine``  (creates it if needed).
std::filesystem::path GetEngineSettingsDirectory();

} // namespace MipsyncEngine
