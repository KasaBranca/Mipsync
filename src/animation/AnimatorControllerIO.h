#pragma once

#include "AnimationTypes.h"
#include <memory>
#include <string>
#include <vector>

namespace MipsyncEngine {

std::shared_ptr<AnimatorControllerAsset> LoadAnimatorController(const std::string& absolutePath,
                                                                std::string& outError);
bool SaveAnimatorController(const std::string& absolutePath, const AnimatorControllerAsset& asset,
                            std::string& outError);
std::shared_ptr<AnimatorControllerAsset> CreateDefaultControllerForModel(
    const std::vector<std::string>& clipNames);
std::shared_ptr<AnimatorControllerAsset> CreateDefaultControllerForModel(
    const class SkeletalModelAsset& model);
std::shared_ptr<AnimatorControllerAsset> CreateEmptyController();

/// Repair clipStackIndex on states (per-state clipSourceModelPath or controller sourceModelPath).
bool SyncControllerStateClipStacks(AnimatorControllerAsset& asset);

} // namespace MipsyncEngine
