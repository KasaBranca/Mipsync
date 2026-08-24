#include "Ps1Toolchain.h"
#include "../assets/AssetManager.h"
#include "../core/Log.h"
#include <fstream>
#include <nlohmann/json.hpp>

namespace MipsyncEngine::Ps1 {
namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

fs::path HubConfigDir() {
#ifdef _WIN32
    if (const char* appdata = std::getenv("APPDATA"))
        return fs::path(appdata) / "MipsyncEngine";
#endif
    return fs::path();
}

bool IsValidSdkRoot(const fs::path& root) {
    std::error_code ec;
    return fs::is_regular_file(root / "lib" / "libpsn00b" / "cmake" / "sdk.cmake", ec);
}

std::optional<fs::path> FromEnvVar(const char* name) {
    if (const char* value = std::getenv(name)) {
        const fs::path root = PathUtf8::FromString(value);
        if (IsValidSdkRoot(root))
            return root;
    }
    return std::nullopt;
}

std::optional<fs::path> FromHubSettings() {
    const fs::path settingsPath = HubConfigDir() / "hub-settings.json";
    std::error_code ec;
    if (!fs::is_regular_file(settingsPath, ec))
        return std::nullopt;

    try {
        std::ifstream in(settingsPath);
        if (!in.is_open())
            return std::nullopt;
        json j;
        in >> j;
        if (!j.contains("psn00bsdkDir") || !j["psn00bsdkDir"].is_string())
            return std::nullopt;
        const fs::path root = PathUtf8::FromString(j["psn00bsdkDir"].get<std::string>());
        if (IsValidSdkRoot(root))
            return root;
    } catch (const std::exception& ex) {
        MIPSYNC_WARN("hub-settings.json PSn00bSDK entry invalid: {}", ex.what());
    }
    return std::nullopt;
}

std::optional<fs::path> ScanToolchainInstalls() {
    const fs::path toolchains = HubConfigDir() / "Installs" / "toolchains" / "psn00bsdk";
    std::error_code ec;
    if (!fs::is_directory(toolchains, ec))
        return std::nullopt;

    std::optional<fs::path> best;
    for (auto it = fs::recursive_directory_iterator(
             toolchains, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (!it->is_directory(ec))
            continue;
        if (!IsValidSdkRoot(it->path()))
            continue;
        best = it->path();
    }
    return best;
}

std::optional<fs::path> FromDevCache(const fs::path& engineDir) {
    const fs::path candidates[] = {
        engineDir / "third_party" / ".cache" / "psn00bsdk-build" / "sdk" /
            "PSn00bSDK-0.24-win32",
        engineDir.parent_path() / "third_party" / ".cache" / "psn00bsdk-build" / "sdk" /
            "PSn00bSDK-0.24-win32",
        engineDir.parent_path().parent_path() / "third_party" / ".cache" / "psn00bsdk-build" /
            "sdk" / "PSn00bSDK-0.24-win32",
    };
    for (const fs::path& c : candidates) {
        if (IsValidSdkRoot(c))
            return c;
    }
    return std::nullopt;
}

} // namespace

std::optional<fs::path> ResolvePsn00bsdkRoot(const fs::path& engineDir) {
    if (auto sdk = FromEnvVar("PSN00BSDK"))
        return sdk;
    if (auto sdk = FromHubSettings())
        return sdk;
    if (auto sdk = ScanToolchainInstalls())
        return sdk;
    if (auto sdk = FromDevCache(engineDir))
        return sdk;
    return std::nullopt;
}

} // namespace MipsyncEngine::Ps1
