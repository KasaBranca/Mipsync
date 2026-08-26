#pragma once

#include "AnimationTypes.h"
#include "SkeletalModel.h"
#include <memory>
#include <string>
#include <vector>

namespace MipsyncEngine {

class Scene;
struct AnimatorComponent;

struct AnimatorTransitionProbe {
    std::string label;
    bool wouldFire = false;
    std::string detail;
};

/// Why outgoing transitions from the current state do or do not fire (Play-mode diagnostics).
std::vector<AnimatorTransitionProbe> ProbeOutgoingTransitions(const AnimatorComponent& animator);
/// Jump to a state immediately (no blend); for editor clip/transition testing.
void ForceAnimatorState(AnimatorComponent& animator, const std::string& stateName);

void SampleAnimatorBoneMatrices(const AnimatorComponent& animator, glm::mat4 out[kMaxBones]);
/// Sample the controller default state's configured starting pose for edit-mode preview.
void SampleAnimatorDefaultStateBoneMatrices(AnimatorComponent& animator,
                                            glm::mat4 out[kMaxBones]);

/// FBX path used to evaluate a state's clip (may differ from Skinned Mesh modelPath).
std::string ResolveStateClipModelPath(const AnimatorComponent& animator,
                                      const AnimatorStateDef& state);
std::shared_ptr<SkeletalModelAsset> ResolveStateClipModel(const AnimatorComponent& animator,
                                                          const AnimatorStateDef& state);

/// Reload AnimatorComponents that use this .ncontroller (after editor save).
void ReloadAnimatorsUsingController(Scene& scene, const std::string& controllerRelPath);
/// Reload every Animator in the scene (call when entering Play).
void ReloadAllSceneAnimators(Scene& scene);

class AnimationSystem {
public:
    static void Update(Scene& scene, float deltaTime);
};

} // namespace MipsyncEngine
