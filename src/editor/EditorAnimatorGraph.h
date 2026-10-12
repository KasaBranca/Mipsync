#pragma once

#include <memory>
#include <string>
#include <vector>

namespace MipsyncEngine {

struct AnimatorControllerAsset;
class EditorApp;

/// Mipsync state machine graph (pan/zoom, nodes, transition arrows).
bool DrawAnimatorControllerGraphEditor(EditorApp* editor, const std::string& projectRelPath,
                                       std::shared_ptr<AnimatorControllerAsset>& asset,
                                       std::vector<std::string>& availableClips,
                                       bool& outChanged);

void AutoLayoutAnimatorStates(AnimatorControllerAsset& asset);

/// Draw the selected graph item in the editor's shared Inspector. Returns false if none is selected.
bool DrawAnimatorGraphSelectionInspector(EditorApp* editor, const std::string& projectRelPath);
void ClearAnimatorGraphSelection();

} // namespace MipsyncEngine
