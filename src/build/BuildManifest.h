#pragma once

#include "../project/Project.h"
#include <string>
#include <vector>

namespace MipsyncEngine {

/// Runtime boot manifest (Unity-style: sits next to exe in `<Product>_Data/boot.json`).
struct BuildManifest {
    std::string productName;
    std::string companyName;
    std::vector<std::string> scenesInBuild;
    int startupSceneIndex = 0;

    std::string StartupScenePath() const;
};

namespace BuildManifestIO {

bool LoadFromFile(const std::string& absolutePath, BuildManifest& outManifest, std::string& outError);
bool SaveToFile(const std::string& absolutePath, const BuildManifest& manifest, std::string& outError);

BuildManifest FromPlayerSettings(const PlayerSettings& settings);

} // namespace BuildManifestIO

} // namespace MipsyncEngine
