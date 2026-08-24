#include "RuntimePaths.h"
#include "../assets/AssetManager.h"
#include <filesystem>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace MipsyncEngine {

std::filesystem::path GetExecutablePath() {
#ifdef _WIN32
    wchar_t modulePath[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) > 0)
        return std::filesystem::path(modulePath);
#endif
    return std::filesystem::current_path() / "MipsyncEngine.exe";
}

std::filesystem::path GetExeDirectory() {
    return GetExecutablePath().parent_path();
}

std::filesystem::path DetectPlayerDataDirectory() {
    const std::filesystem::path exePath = GetExecutablePath();
    const std::filesystem::path dataDir =
        exePath.parent_path() / (exePath.stem().wstring() + L"_Data");
    std::error_code ec;
    if (std::filesystem::is_regular_file(dataDir / "boot.json", ec))
        return dataDir;
    return {};
}

std::filesystem::path GetBundledResourcesDirectory() {
    if (const std::filesystem::path dataDir = DetectPlayerDataDirectory(); !dataDir.empty()) {
        const std::filesystem::path inData = dataDir / "Resources";
        std::error_code ec;
        if (std::filesystem::is_directory(inData, ec))
            return inData;
    }

    const std::filesystem::path nextToExe = GetExeDirectory() / "resources";
    std::error_code ec;
    if (std::filesystem::is_directory(nextToExe, ec))
        return nextToExe;

    return GetExeDirectory() / "resources";
}

std::filesystem::path GetEngineSettingsDirectory() {
#ifdef _WIN32
    wchar_t appDataPath[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"APPDATA", appDataPath, MAX_PATH) > 0) {
        const std::filesystem::path dir =
            std::filesystem::path(appDataPath) / L"MipsyncEngine";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir;
    }
#endif
    // Fallback: directory next to the executable
    const std::filesystem::path dir = GetExeDirectory() / "engine_settings";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

} // namespace MipsyncEngine
