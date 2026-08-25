#include "PlayerBuild.h"
#include "BuildManifest.h"
#include "../assets/AssetManager.h"
#include "../core/Log.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace MipsyncEngine {
namespace {

namespace fs = std::filesystem;

bool ShouldSkipProjectEntry(const fs::path& relative) {
    const std::string rel = PathUtf8::ToString(relative);
    if (rel.empty())
        return false;
    if (rel == "Builds" || rel.rfind("Builds/", 0) == 0 || rel.rfind("Builds\\", 0) == 0)
        return true;
    if (rel == ".nostalty" || rel.rfind(".nostalty/", 0) == 0 ||
        rel.rfind(".nostalty\\", 0) == 0)
        return true;
    if (rel == ".git" || rel.rfind(".git/", 0) == 0 || rel.rfind(".git\\", 0) == 0)
        return true;
    if (rel == "editor_layout.ini")
        return true;
    return false;
}

bool IsMissingFileError(const std::error_code& ec) {
    if (!ec)
        return false;
    if (ec == std::make_error_code(std::errc::no_such_file_or_directory))
        return true;
#ifdef _WIN32
    return ec.value() == ERROR_FILE_NOT_FOUND || ec.value() == ERROR_PATH_NOT_FOUND;
#else
    return false;
#endif
}

void CopyProjectTree(const fs::path& sourceRoot, const fs::path& destRoot, std::string& outError) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(
             sourceRoot, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        const fs::path rel = fs::relative(it->path(), sourceRoot, ec);
        if (ec || rel.empty())
            continue;
        if (ShouldSkipProjectEntry(rel)) {
            if (it->is_directory(ec))
                it.disable_recursion_pending();
            continue;
        }

        const fs::path destPath = destRoot / rel;
        if (it->is_directory(ec)) {
            fs::create_directories(destPath, ec);
            continue;
        }
        if (!it->is_regular_file(ec))
            continue;

        std::error_code existsEc;
        if (!fs::exists(it->path(), existsEc)) {
            MIPSYNC_WARN("Player build skipped missing project file: {}",
                         PathUtf8::ToString(it->path()));
            continue;
        }

        fs::create_directories(destPath.parent_path(), ec);
        fs::copy_file(it->path(), destPath, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            if (IsMissingFileError(ec)) {
                MIPSYNC_WARN("Player build skipped missing project file: {}",
                             PathUtf8::ToString(it->path()));
                ec.clear();
                continue;
            }
            outError = "Copy failed: " + PathUtf8::ToString(it->path()) + " — " + ec.message();
            return;
        }
    }
}

void CopyIfExists(const fs::path& src, const fs::path& dst, bool recursive, std::string& outError) {
    std::error_code ec;
    if (!fs::exists(src, ec))
        return;
    if (recursive)
        fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    else
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec)
        outError = "Copy failed: " + PathUtf8::ToString(src) + " — " + ec.message();
}

std::string SanitizeProductName(std::string name) {
    if (name.empty())
        name = "Game";
    for (char& c : name) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
            c == '>' || c == '|')
            c = '_';
    }
    return name;
}

} // namespace

std::string PlayerBuild::DefaultOutputParent(const std::string& projectPath) {
    return PathUtf8::ToString(PathUtf8::FromString(projectPath) / "Builds" / "Windows");
}

std::string PlayerBuild::GetRunningEngineDirectory() {
#ifdef _WIN32
    wchar_t modulePath[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) == 0)
        return {};
    return PathUtf8::ToString(fs::path(modulePath).parent_path());
#else
    return PathUtf8::ToString(fs::current_path());
#endif
}

PlayerBuildResult PlayerBuild::BuildWindows(const PlayerBuildRequest& request) {
    PlayerBuildResult result;
    PlayerSettings settings = request.settings;
    settings.productName = SanitizeProductName(settings.productName);
    if (settings.scenesInBuild.empty()) {
        result.message = "No scenes in build list. Add scenes in Build Settings.";
        return result;
    }

    const fs::path projectRoot = PathUtf8::FromString(request.projectPath);
    std::error_code ec;
    if (!fs::is_directory(projectRoot, ec)) {
        result.message = "Invalid project path.";
        return result;
    }

    const fs::path engineDir = request.engineDirectory.empty()
        ? PathUtf8::FromString(GetRunningEngineDirectory())
        : PathUtf8::FromString(request.engineDirectory);
    const fs::path engineExe = engineDir / "MipsyncEngine.exe";
    if (!fs::is_regular_file(engineExe, ec)) {
        result.message = "MipsyncEngine.exe not found in: " + PathUtf8::ToString(engineDir);
        return result;
    }

    const fs::path outputParent = request.outputParent.empty()
        ? PathUtf8::FromString(DefaultOutputParent(request.projectPath))
        : PathUtf8::FromString(request.outputParent);
    const fs::path buildRoot = outputParent / settings.productName;
    const fs::path dataDir = buildRoot / (settings.productName + "_Data");
    const fs::path projectCopy = dataDir / "Project";
    const fs::path resourcesDir = dataDir / "Resources";

    std::string error;
    try {
        fs::create_directories(buildRoot, ec);
        fs::create_directories(dataDir, ec);
        fs::create_directories(projectCopy, ec);
        fs::create_directories(resourcesDir, ec);

        CopyProjectTree(projectRoot, projectCopy, error);
        if (!error.empty()) {
            result.message = error;
            return result;
        }

        const BuildManifest manifest = BuildManifestIO::FromPlayerSettings(settings);
        const fs::path bootPath = dataDir / "boot.json";
        if (!BuildManifestIO::SaveToFile(PathUtf8::ToString(bootPath), manifest, error)) {
            result.message = error;
            return result;
        }

        const fs::path builtExe = buildRoot / (settings.productName + ".exe");
        fs::copy_file(engineExe, builtExe, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            result.message = "Failed to copy executable: " + ec.message();
            return result;
        }

        for (const auto& entry : fs::directory_iterator(engineDir, ec)) {
            if (!entry.is_regular_file(ec))
                continue;
            const std::string ext = PathUtf8::ToString(entry.path().extension());
            if (ext != ".dll" && ext != ".DLL")
                continue;
            fs::copy_file(entry.path(), buildRoot / entry.path().filename(),
                          fs::copy_options::overwrite_existing, ec);
        }

        CopyIfExists(engineDir / "resources", resourcesDir, true, error);
        if (!error.empty()) {
            result.message = error;
            return result;
        }
        CopyIfExists(engineDir / "resources", buildRoot / "resources", true, error);
        if (!error.empty()) {
            result.message = error;
            return result;
        }

        const std::string bat = "@echo off\r\ncd /d \"%~dp0\"\r\n\"" +
                                settings.productName + ".exe\" --player --data \"" +
                                settings.productName + "_Data\"\r\n";
        std::ofstream playBat(buildRoot / "Play.bat", std::ios::binary);
        if (playBat)
            playBat << bat;

        result.success = true;
        result.outputDirectory = PathUtf8::ToString(buildRoot);
        result.executablePath = PathUtf8::ToString(builtExe);
        result.message = "Build succeeded: " + result.outputDirectory;
        MIPSYNC_INFO("Player build: {}", result.message);
    } catch (const std::exception& ex) {
        result.message = ex.what();
    }
    return result;
}

} // namespace MipsyncEngine
