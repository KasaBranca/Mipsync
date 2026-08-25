#include "BuildManifest.h"
#include "../assets/AssetManager.h"
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace MipsyncEngine {

using json = nlohmann::json;

std::string BuildManifest::StartupScenePath() const {
    if (scenesInBuild.empty())
        return {};
    const int idx = std::clamp(startupSceneIndex, 0,
                               static_cast<int>(scenesInBuild.size()) - 1);
    return scenesInBuild[static_cast<size_t>(idx)];
}

namespace BuildManifestIO {

BuildManifest FromPlayerSettings(const PlayerSettings& settings) {
    BuildManifest manifest;
    manifest.productName = settings.productName;
    manifest.companyName = settings.companyName;
    manifest.scenesInBuild = settings.scenesInBuild;
    manifest.startupSceneIndex = settings.startupSceneIndex;
    return manifest;
}

bool LoadFromFile(const std::string& absolutePath, BuildManifest& outManifest, std::string& outError) {
    try {
        std::ifstream in(PathUtf8::FromString(absolutePath), std::ios::binary);
        if (!in.is_open()) {
            outError = "Could not open boot manifest: " + absolutePath;
            return false;
        }
        json root;
        in >> root;
        outManifest.productName = root.value("productName", std::string{});
        outManifest.companyName = root.value("companyName", std::string{});
        outManifest.startupSceneIndex = root.value("startupSceneIndex", 0);
        outManifest.scenesInBuild.clear();
        if (root.contains("scenesInBuild") && root["scenesInBuild"].is_array()) {
            for (const auto& entry : root["scenesInBuild"]) {
                if (entry.is_string())
                    outManifest.scenesInBuild.push_back(entry.get<std::string>());
            }
        }
        return true;
    } catch (const std::exception& ex) {
        outError = ex.what();
        return false;
    }
}

bool SaveToFile(const std::string& absolutePath, const BuildManifest& manifest, std::string& outError) {
    try {
        json root;
        root["version"] = 1;
        root["productName"] = manifest.productName;
        root["companyName"] = manifest.companyName;
        root["startupSceneIndex"] = manifest.startupSceneIndex;
        root["scenesInBuild"] = json::array();
        for (const std::string& scene : manifest.scenesInBuild)
            root["scenesInBuild"].push_back(scene);

        const auto parent = PathUtf8::FromString(absolutePath).parent_path();
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);

        std::ofstream out(PathUtf8::FromString(absolutePath), std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            outError = "Could not write boot manifest: " + absolutePath;
            return false;
        }
        out << root.dump(2);
        return true;
    } catch (const std::exception& ex) {
        outError = ex.what();
        return false;
    }
}

} // namespace BuildManifestIO

} // namespace MipsyncEngine
