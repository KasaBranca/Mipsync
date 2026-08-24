#pragma once

#include <memory>
#include <string>
#include <vector>

namespace MipsyncEngine {

struct AnimatorControllerAsset;
class EditorApp;

/// Unity-style state machine graph (pan/zoom, nodes, transition arrows).
bool DrawAnimatorControllerGraphEditor(EditorApp* editor, const std::string& projectRelPath,
                                       std::shared_ptr<AnimatorControllerAsset>& asset,
                                       std::vector<std::string>& availableClips,
                                       bool& outChanged);

void AutoLayoutAnimatorStates(AnimatorControllerAsset& asset);

} // namespace MipsyncEngine
