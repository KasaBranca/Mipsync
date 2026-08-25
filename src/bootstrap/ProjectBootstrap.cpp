#include "ProjectBootstrap.h"
#include "../assets/AssetManager.h"
#include "../build/BuildManifest.h"
#include "../core/Log.h"
#include <algorithm>
#include <filesystem>

namespace MipsyncEngine {

ProjectBootstrapResult ProjectBootstrap::Resolve(const std::string& requestedProjectPath,
                                                 const std::string& playerDataDirectory,
                                                 bool playerMode) {
    namespace fs = std::filesystem;
    ProjectBootstrapResult result;
    std::string projectError;

    if (!playerDataDirectory.empty()) {
        const fs::path dataDir = fs::absolute(PathUtf8::FromString(playerDataDirectory));
        result.playerDataDirectory = PathUtf8::ToString(dataDir);
        result.projectPath = PathUtf8::ToString(dataDir / "Project");
        std::error_code ec;
        fs::current_path(PathUtf8::FromString(result.projectPath), ec);

        BuildManifest manifest;
        std::string bootError;
        if (BuildManifestIO::LoadFromFile(PathUtf8::ToString(dataDir / "boot.json"),
                                          manifest, bootError)) {
            result.projectName = manifest.productName.empty() ? "Game" : manifest.productName;
            result.buildScenes = manifest.scenesInBuild;
            result.startupSceneRelativePath = manifest.StartupScenePath();
            MIPSYNC_INFO("Player build: {} ({} scenes)", result.projectName,
                         result.buildScenes.size());
        } else {
            MIPSYNC_WARN("boot.json load failed: {}", bootError);
            result.projectName = PathUtf8::ToString(dataDir.filename());
        }
    } else if (!requestedProjectPath.empty()) {
        std::error_code ec;
        const fs::path requested = PathUtf8::FromString(requestedProjectPath);
        fs::current_path(requested, ec);
        result.projectPath = PathUtf8::ToString(fs::absolute(requested));
    } else {
        result.projectPath = PathUtf8::ToString(fs::current_path());
    }

    if (result.playerDataDirectory.empty()) {
        if (Project::LoadFromDir(result.projectPath, result.projectInfo, projectError)) {
            result.projectName = playerMode && !result.projectInfo.player.productName.empty()
                ? result.projectInfo.player.productName : result.projectInfo.name;
            result.buildScenes = result.projectInfo.player.scenesInBuild;
            if (playerMode) {
                result.startupSceneRelativePath = result.projectInfo.player.scenesInBuild.empty()
                    ? result.projectInfo.defaultScene
                    : result.projectInfo.player.scenesInBuild[static_cast<size_t>(std::clamp(
                        result.projectInfo.player.startupSceneIndex, 0,
                        static_cast<int>(result.projectInfo.player.scenesInBuild.size()) - 1))];
            }
            MIPSYNC_INFO("Project: {} ({})", result.projectInfo.name, result.projectPath);
        } else {
            result.projectName = PathUtf8::ToString(
                PathUtf8::FromString(result.projectPath).filename());
            if (result.projectName.empty())
                result.projectName = "Untitled";
        }
    } else if (Project::LoadFromDir(result.projectPath, result.projectInfo, projectError)) {
        if (result.buildScenes.empty())
            result.buildScenes = result.projectInfo.player.scenesInBuild;
    }

    return result;
}

void ProjectBootstrap::EnsureProjectDirectories(const std::string& projectRoot) {
    namespace fs = std::filesystem;
    const fs::path root = PathUtf8::FromString(projectRoot);
    for (const char* relative : { "assets/textures", "assets/materials", "assets/prefabs",
                                  "assets/models", "assets/scripts", "assets/animations",
                                  "assets/audio", "saves" }) {
        std::error_code ec;
        fs::create_directories(root / relative, ec);
        if (ec)
            MIPSYNC_WARN("Failed to create project directory '{}': {}", relative, ec.message());
    }
}

} // namespace MipsyncEngine
