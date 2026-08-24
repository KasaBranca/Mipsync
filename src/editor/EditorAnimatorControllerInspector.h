#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace MipsyncEngine {

struct AnimatorControllerAsset;
class EditorApp;

inline constexpr const char* kAnimatorControllerWindowTitle = "Animator Controller";

std::string MakeUniqueAnimatorStateName(const AnimatorControllerAsset& asset,
                                        const std::string& preferred = {});

/// Unity-style left column (name, type, default value).
void DrawAnimatorParametersSidebar(AnimatorControllerAsset& asset, bool& changed);

std::vector<std::string> LoadAnimationClipNames(const std::string& modelProjectPath);
bool DrawAnimatorClipField(const char* label, std::string& clipName, int& clipStackIndex,
                           std::string* clipSourceModelPath, const std::string& modelProjectPath,
                           const std::shared_ptr<class SkeletalModelAsset>& model,
                           const std::vector<std::string>& clips, bool& changed);
bool SyncControllerStateClipStacks(AnimatorControllerAsset& asset);
bool AnimatorStateUsesClip(const AnimatorControllerAsset& asset, const std::string& clipName);
bool StateUsesAnimStack(const AnimatorControllerAsset& asset, const std::string& modelPath,
                        int stackIndex);
void AddAnimatorStateForClip(AnimatorControllerAsset& asset, const std::string& clipName,
                              glm::vec2 graphPosition = glm::vec2{ -1.0f, -1.0f },
                              int clipStackIndex = -1);
void ImportAllAnimatorClipsAsStates(AnimatorControllerAsset& asset,
                                    const std::vector<std::string>& clips, bool& changed);
void ImportAllAnimatorClipsAsStatesAt(AnimatorControllerAsset& asset,
                                      const std::vector<std::string>& clips, glm::vec2 origin,
                                      bool& changed);
bool ApplyAnimatorModelToController(AnimatorControllerAsset& asset,
                                      const std::string& modelProjectPath,
                                      std::vector<std::string>& outClips, glm::vec2 graphOrigin,
                                      bool& changed);

struct Scene;
void CommitAnimatorController(const std::string& projectRelPath, AnimatorControllerAsset& asset,
                              Scene* scene = nullptr);

/// Full editor UI (graph/list, save). Caller must not wrap in ImGui::Begin.
void DrawAnimatorControllerEditor(EditorApp* editor, const std::string& projectRelPath);
/// Creates `assets/animations/<modelBase>.ncontroller` from FBX clip names; assigns to `animator`.
bool CreateAndAssignControllerFromModel(EditorApp* editor, class AnimatorComponent& animator,
                                        const std::string& modelProjectPath,
                                        class SkinnedMeshRendererComponent* skinned = nullptr);

} // namespace MipsyncEngine
