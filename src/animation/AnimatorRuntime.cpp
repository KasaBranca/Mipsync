#include "AnimatorRuntime.h"
#include "AnimationTypes.h"
#include "AnimatorControllerIO.h"
#include "SkeletalModel.h"
#include "../scene/Scene.h"
#include "../assets/AssetManager.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace MipsyncEngine {

namespace {

using json = nlohmann::json;

const AnimatorStateDef* FindState(const AnimatorControllerAsset& ctrl, const std::string& name) {
    for (const auto& st : ctrl.states) {
        if (st.name == name)
            return &st;
    }
    return nullptr;
}

float GetParamFloat(const AnimatorComponent& anim, const std::string& name, float fallback) {
    const auto it = anim.parameters.floats.find(name);
    return it != anim.parameters.floats.end() ? it->second : fallback;
}

bool GetParamBool(const AnimatorComponent& anim, const std::string& name, bool fallback) {
    const auto it = anim.parameters.bools.find(name);
    return it != anim.parameters.bools.end() ? it->second : fallback;
}

bool ConditionPasses(const AnimatorComponent& anim, const AnimatorTransitionCondition& cond) {
    const auto floatIt = anim.parameters.floats.find(cond.parameter);
    const auto boolIt = anim.parameters.bools.find(cond.parameter);
    const auto intIt = anim.parameters.ints.find(cond.parameter);

    switch (cond.mode) {
    case AnimatorConditionMode::IfTrue:
        if (const auto trigIt = anim.parameters.triggers.find(cond.parameter);
            trigIt != anim.parameters.triggers.end())
            return trigIt->second;
        return boolIt != anim.parameters.bools.end() && boolIt->second;
    case AnimatorConditionMode::IfFalse:
        if (anim.heldTriggers.count(cond.parameter))
            return false;
        if (const auto trigIt = anim.parameters.triggers.find(cond.parameter);
            trigIt != anim.parameters.triggers.end())
            return !trigIt->second;
        return boolIt != anim.parameters.bools.end() && !boolIt->second;
    case AnimatorConditionMode::Greater:
        if (floatIt != anim.parameters.floats.end())
            return floatIt->second > cond.threshold;
        if (intIt != anim.parameters.ints.end())
            return static_cast<float>(intIt->second) > cond.threshold;
        return false;
    case AnimatorConditionMode::Less:
        if (floatIt != anim.parameters.floats.end())
            return floatIt->second < cond.threshold;
        if (intIt != anim.parameters.ints.end())
            return static_cast<float>(intIt->second) < cond.threshold;
        return false;
    case AnimatorConditionMode::Equals:
        if (floatIt != anim.parameters.floats.end())
            return std::abs(floatIt->second - cond.threshold) < 1e-4f;
        if (intIt != anim.parameters.ints.end())
            return intIt->second == static_cast<int>(cond.threshold);
        return false;
    case AnimatorConditionMode::NotEquals:
        if (floatIt != anim.parameters.floats.end())
            return std::abs(floatIt->second - cond.threshold) >= 1e-4f;
        if (intIt != anim.parameters.ints.end())
            return intIt->second != static_cast<int>(cond.threshold);
        return false;
    }
    return false;
}

struct TransformClipKey {
    int frame = 0;
    glm::vec3 position{ 0.0f };
    glm::vec3 rotation{ 0.0f };
    glm::vec3 scale{ 1.0f };
};

struct TransformClipAsset {
    bool valid = false;
    int fps = 30;
    int lengthFrames = 1;
    std::filesystem::file_time_type writeTime{};
    std::vector<TransformClipKey> keys;
};

std::unordered_map<std::string, TransformClipAsset> g_TransformClipCache;

std::string ToLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool IsTransformClipPath(const std::string& path) {
    return ToLowerCopy(PathUtf8::ToString(PathUtf8::FromString(path).extension())) == ".nanim";
}

glm::vec3 JsonVec3(const json& j, const glm::vec3& fallback) {
    if (!j.is_array() || j.size() < 3)
        return fallback;
    return {
        j[0].get<float>(),
        j[1].get<float>(),
        j[2].get<float>(),
    };
}

const TransformClipAsset* LoadTransformClip(const std::string& projectRelPath) {
    if (projectRelPath.empty() || !IsTransformClipPath(projectRelPath))
        return nullptr;
    TransformClipAsset clip{};
    const std::string abs = AssetManager::Get().ToAbsolute(projectRelPath);
    const std::filesystem::path absPath = PathUtf8::FromString(abs);
    std::error_code ec;
    clip.writeTime = std::filesystem::last_write_time(absPath, ec);
    if (const auto it = g_TransformClipCache.find(projectRelPath); it != g_TransformClipCache.end() &&
        it->second.writeTime == clip.writeTime) {
        return it->second.valid ? &it->second : nullptr;
    }

    std::ifstream file(absPath);
    if (!file.is_open()) {
        g_TransformClipCache[projectRelPath] = clip;
        return nullptr;
    }

    json j;
    try {
        file >> j;
    } catch (...) {
        g_TransformClipCache[projectRelPath] = clip;
        return nullptr;
    }

    clip.fps = std::max(1, j.value("fps", 30));
    clip.lengthFrames = std::max(1, j.value("lengthFrames", 1));
    if (j.contains("keys") && j["keys"].is_array()) {
        for (const json& keyJson : j["keys"]) {
            TransformClipKey key;
            key.frame = std::max(0, keyJson.value("frame", 0));
            key.position = JsonVec3(keyJson.value("position", json::array()), { 0.0f, 0.0f, 0.0f });
            key.rotation = JsonVec3(keyJson.value("rotation", json::array()), { 0.0f, 0.0f, 0.0f });
            key.scale = JsonVec3(keyJson.value("scale", json::array()), { 1.0f, 1.0f, 1.0f });
            clip.keys.push_back(key);
            clip.lengthFrames = std::max(clip.lengthFrames, key.frame);
        }
    }
    std::sort(clip.keys.begin(), clip.keys.end(),
              [](const TransformClipKey& a, const TransformClipKey& b) { return a.frame < b.frame; });
    clip.valid = !clip.keys.empty();
    const auto [it, _] = g_TransformClipCache.emplace(projectRelPath, std::move(clip));
    return it->second.valid ? &it->second : nullptr;
}

std::string ResolveStateClipPath(const AnimatorComponent& anim, const AnimatorStateDef& state) {
    if (!state.clipSourceModelPath.empty())
        return state.clipSourceModelPath;
    if (anim.controller && !anim.controller->sourceModelPath.empty())
        return anim.controller->sourceModelPath;
    return anim.modelPath;
}

const TransformClipAsset* ResolveStateTransformClip(const AnimatorComponent& anim,
                                                    const AnimatorStateDef& state) {
    return LoadTransformClip(ResolveStateClipPath(anim, state));
}

float NormalizedStartOffset(const AnimatorStateDef& state) {
    return std::isfinite(state.startOffset)
        ? std::clamp(state.startOffset, 0.0f, 1.0f)
        : 0.0f;
}

float TransformClipFrame(const TransformClipAsset& clip, const AnimatorStateDef& state,
                         float stateTime) {
    const float end = static_cast<float>(std::max(1, clip.lengthFrames));
    const float raw = stateTime * std::max(0.0f, state.speed) * static_cast<float>(clip.fps) +
                      NormalizedStartOffset(state) * end;
    if (state.loop && end > 0.0f)
        return std::fmod(raw, end);
    return std::clamp(raw, 0.0f, end);
}

float TransformClipElapsedNormalizedTime(const TransformClipAsset& clip,
                                         const AnimatorStateDef& state,
                                         float stateTime) {
    const float end = static_cast<float>(std::max(1, clip.lengthFrames));
    if (end <= 0.0f)
        return 0.0f;

    // Exit Time measures time elapsed since entering the state. Start Offset
    // only selects the sampled pose, and looping must not wrap this clock.
    return std::max(0.0f, stateTime) * std::max(0.0f, state.speed) *
           static_cast<float>(clip.fps) / end;
}

void SampleTransformClipToEntity(const TransformClipAsset& clip, const AnimatorStateDef& state,
                                 float localTime, Entity& entity) {
    auto* tr = entity.GetComponent<TransformComponent>();
    if (!tr || clip.keys.empty())
        return;

    const float frame = TransformClipFrame(clip, state, localTime);
    const TransformClipKey* prev = &clip.keys.front();
    const TransformClipKey* next = &clip.keys.back();
    for (const auto& key : clip.keys) {
        if (static_cast<float>(key.frame) <= frame)
            prev = &key;
        if (static_cast<float>(key.frame) >= frame) {
            next = &key;
            break;
        }
    }

    float t = 0.0f;
    if (next->frame != prev->frame)
        t = (frame - static_cast<float>(prev->frame)) /
            static_cast<float>(next->frame - prev->frame);
    tr->position = prev->position + (next->position - prev->position) * t;
    tr->rotation = prev->rotation + (next->rotation - prev->rotation) * t;
    tr->scale = prev->scale + (next->scale - prev->scale) * t;
}

bool SampleStateTransformClipToEntity(const AnimatorComponent& anim,
                                      const AnimatorStateDef& state,
                                      float localTime,
                                      Entity& entity) {
    const TransformClipAsset* clip = ResolveStateTransformClip(anim, state);
    if (!clip)
        return false;
    SampleTransformClipToEntity(*clip, state, localTime, entity);
    return true;
}

} // namespace

std::string ResolveStateClipModelPath(const AnimatorComponent& anim, const AnimatorStateDef& state) {
    if (!state.clipSourceModelPath.empty())
        return state.clipSourceModelPath;
    if (anim.controller && !anim.controller->sourceModelPath.empty())
        return anim.controller->sourceModelPath;
    return anim.modelPath;
}

std::shared_ptr<SkeletalModelAsset> ResolveStateClipModel(const AnimatorComponent& anim,
                                                          const AnimatorStateDef& state) {
    const std::string path = ResolveStateClipModelPath(anim, state);
    if (path.empty())
        return anim.model;
    if (anim.model && anim.modelPath == path)
        return anim.model;
    return AssetManager::Get().GetSkeletalModel(path);
}

namespace {

double ClipDurationForState(const SkeletalModelAsset& model, const AnimatorStateDef& state) {
    if (state.clipName.empty() && state.clipStackIndex < 0)
        return 0.0;
    const int stackIdx = model.ResolveClipStackIndex(state.clipName, state.clipStackIndex);
    if (stackIdx >= 0)
        return model.GetClipDurationByStackIndex(stackIdx);
    return model.GetClipDuration(state.clipName);
}

double ClipTimeSeconds(const SkeletalModelAsset& model, const AnimatorStateDef& state, float stateTime) {
    const double duration = ClipDurationForState(model, state);
    const double raw = static_cast<double>(stateTime) * state.speed +
                       static_cast<double>(NormalizedStartOffset(state)) * duration;
    if (state.loop && duration > 0.0)
        return std::fmod(raw, duration);
    return std::clamp(raw, 0.0, duration);
}

void BlendBoneMatrices(const glm::mat4 a[kMaxBones], const glm::mat4 b[kMaxBones], float t,
                       glm::mat4 out[kMaxBones]) {
    for (int i = 0; i < kMaxBones; ++i) {
        for (int c = 0; c < 4; ++c)
            out[i][c] = glm::mix(a[i][c], b[i][c], t);
    }
}

void EvaluateStatePose(AnimatorComponent& anim, const AnimatorStateDef& state, float localTime,
                       glm::mat4 out[kMaxBones]) {
    const auto clipModel = ResolveStateClipModel(anim, state);
    if (!clipModel) {
        if (anim.model)
            anim.model->EvaluateBoneMatrices({}, 0.0, out);
        return;
    }

    if (state.clipName.empty() && state.clipStackIndex < 0) {
        if (anim.model && clipModel.get() != anim.model.get()) {
            glm::mat4 clipPose[kMaxBones];
            clipModel->EvaluateBoneMatrices({}, 0.0, clipPose);
            RetargetBoneMatrices(*anim.model, *clipModel, clipPose, out);
        } else {
            clipModel->EvaluateBoneMatrices({}, 0.0, out);
        }
        return;
    }

    const double t = ClipTimeSeconds(*clipModel, state, localTime);
    const int stackIdx = clipModel->ResolveClipStackIndex(state.clipName, state.clipStackIndex);
    if (stackIdx >= 0) {
        if (anim.model)
            EvaluateRetargetedBoneMatrices(*anim.model, *clipModel, stackIdx, t, out);
        else
            clipModel->EvaluateBoneMatricesByStackIndex(stackIdx, t, out);
    } else {
        glm::mat4 clipPose[kMaxBones];
        clipModel->EvaluateBoneMatrices(state.clipName, t, clipPose);
        if (anim.model && clipModel.get() != anim.model.get())
            RetargetBoneMatrices(*anim.model, *clipModel, clipPose, out);
        else
            std::copy(std::begin(clipPose), std::end(clipPose), out);
    }
}

double ElapsedNormalizedStateTime(const SkeletalModelAsset& model,
                                  const AnimatorStateDef& state,
                                  float stateTime) {
    const double dur = ClipDurationForState(model, state);
    if (dur <= 0.0)
        return 0.0;

    // Keep transition timing independent from pose sampling. In particular,
    // do not add Start Offset or wrap looped clips back to normalized time 0.
    return static_cast<double>(std::max(0.0f, stateTime)) *
           static_cast<double>(std::max(0.0f, state.speed)) / dur;
}

bool TransitionExitTimeReached(const SkeletalModelAsset& model, const AnimatorStateDef& state,
                               float stateTime, const AnimatorTransitionDef& tr) {
    if (!tr.hasExitTime)
        return true;
    const double dur = ClipDurationForState(model, state);
    if (dur <= 0.0)
        return true;
    return ElapsedNormalizedStateTime(model, state, stateTime) >=
           static_cast<double>(tr.exitTime);
}

bool AnimatorTransitionExitTimeReached(const AnimatorComponent& anim,
                                       const AnimatorStateDef& state,
                                       float stateTime,
                                       const AnimatorTransitionDef& tr) {
    if (!tr.hasExitTime)
        return true;
    if (const TransformClipAsset* clip = ResolveStateTransformClip(anim, state))
        return TransformClipElapsedNormalizedTime(*clip, state, stateTime) >= tr.exitTime;
    if (const auto model = ResolveStateClipModel(anim, state))
        return TransitionExitTimeReached(*model, state, stateTime, tr);
    return true;
}

bool AnimatorStateTimeAtNormalizedExit(const AnimatorComponent& anim,
                                       const AnimatorStateDef& state,
                                       float normalizedExitTime,
                                       float& outStateTime) {
    if (!std::isfinite(normalizedExitTime) || normalizedExitTime < 0.0f)
        return false;

    const float stateSpeed = std::max(0.0f, state.speed);
    if (stateSpeed <= 0.0f)
        return false;

    if (const TransformClipAsset* clip = ResolveStateTransformClip(anim, state)) {
        const float fps = static_cast<float>(std::max(1, clip->fps));
        const float frames = static_cast<float>(std::max(1, clip->lengthFrames));
        outStateTime = normalizedExitTime * frames / (fps * stateSpeed);
        return std::isfinite(outStateTime);
    }

    if (const auto model = ResolveStateClipModel(anim, state)) {
        const double duration = ClipDurationForState(*model, state);
        if (duration <= 0.0)
            return false;
        outStateTime = static_cast<float>(
            static_cast<double>(normalizedExitTime) * duration /
            static_cast<double>(stateSpeed));
        return std::isfinite(outStateTime);
    }
    return false;
}

const char* ConditionModeLabel(AnimatorConditionMode mode) {
    switch (mode) {
    case AnimatorConditionMode::Greater: return ">";
    case AnimatorConditionMode::Less: return "<";
    case AnimatorConditionMode::Equals: return "==";
    case AnimatorConditionMode::NotEquals: return "!=";
    case AnimatorConditionMode::IfTrue: return "is true";
    case AnimatorConditionMode::IfFalse: return "is false";
    }
    return "?";
}

std::string FormatConditionFailure(const AnimatorComponent& anim,
                                   const AnimatorTransitionCondition& cond) {
    std::string out = cond.parameter + " " + ConditionModeLabel(cond.mode);
    if (cond.mode != AnimatorConditionMode::IfTrue && cond.mode != AnimatorConditionMode::IfFalse)
        out += " " + std::to_string(cond.threshold);

    const auto floatIt = anim.parameters.floats.find(cond.parameter);
    const auto boolIt = anim.parameters.bools.find(cond.parameter);
    const auto intIt = anim.parameters.ints.find(cond.parameter);

    out += " (current: ";
    if (floatIt != anim.parameters.floats.end())
        out += std::to_string(floatIt->second);
    else if (boolIt != anim.parameters.bools.end())
        out += boolIt->second ? "true" : "false";
    else if (intIt != anim.parameters.ints.end())
        out += std::to_string(intIt->second);
    else
        out += "not set — add parameter or remove condition";
    out += ")";
    return out;
}

} // namespace

std::vector<AnimatorTransitionProbe> ProbeOutgoingTransitions(const AnimatorComponent& animator) {
    std::vector<AnimatorTransitionProbe> probes;
    if (!animator.controller)
        return probes;

    const AnimatorStateDef* curState = FindState(*animator.controller, animator.currentState);

    for (const auto& tr : animator.controller->transitions) {
        if (tr.fromState != animator.currentState && tr.fromState != kAnimatorAnyStateName)
            continue;

        AnimatorTransitionProbe probe{};
        probe.label = tr.fromState + " -> " + tr.toState;
        if (!tr.conditions.empty())
            probe.label += " [" + std::to_string(tr.conditions.size()) + " condition(s)]";

        if (!FindState(*animator.controller, tr.toState)) {
            probe.detail = "toState not found in controller";
            probes.push_back(probe);
            continue;
        }
        if (animator.inTransition) {
            probe.detail = "already in transition";
            probes.push_back(probe);
            continue;
        }
        if (curState && !AnimatorTransitionExitTimeReached(animator, *curState, animator.stateTime, tr)) {
            float normalizedTime = 0.0f;
            if (const TransformClipAsset* clip = ResolveStateTransformClip(animator, *curState))
                normalizedTime = TransformClipElapsedNormalizedTime(
                    *clip, *curState, animator.stateTime);
            else if (const auto curModel = ResolveStateClipModel(animator, *curState))
                normalizedTime = static_cast<float>(ElapsedNormalizedStateTime(
                    *curModel, *curState, animator.stateTime));
            probe.detail = "exit time: norm " + std::to_string(normalizedTime) + " < " +
                           std::to_string(tr.exitTime);
            probes.push_back(probe);
            continue;
        }
        if (tr.conditions.empty()) {
            probe.wouldFire = true;
            probe.detail = "no conditions (always)";
            probes.push_back(probe);
            continue;
        }

        bool allPass = true;
        std::string failed;
        for (const auto& cond : tr.conditions) {
            if (ConditionPasses(animator, cond))
                continue;
            allPass = false;
            failed = FormatConditionFailure(animator, cond);
            break;
        }
        if (allPass) {
            probe.wouldFire = true;
            probe.detail = "all conditions pass";
        } else {
            probe.detail = "needs " + failed;
        }
        probes.push_back(probe);
    }
    return probes;
}

void ForceAnimatorState(AnimatorComponent& animator, const std::string& stateName) {
    if (!animator.controller || !FindState(*animator.controller, stateName))
        return;
    animator.currentState = stateName;
    animator.nextState.clear();
    animator.inTransition = false;
    animator.stateTime = 0.0f;
    animator.transitionTime = 0.0f;
    animator.transitionDuration = 0.0f;

    if (const AnimatorStateDef* st = FindState(*animator.controller, stateName))
        EvaluateStatePose(animator, *st, 0.0f, animator.boneMatrices);
}

void AnimatorComponent::ReloadAssets() {
    AssetManager& assets = AssetManager::Get();
    if (!modelPath.empty()) {
        assets.DropSkeletalModel(modelPath);
        model = assets.GetSkeletalModel(modelPath);
    }

    controller.reset();
    if (!controllerPath.empty()) {
        assets.DropAnimatorController(controllerPath);
        std::string err;
        controller = assets.GetAnimatorController(controllerPath, err);
    }
    if (!controller && model && !model->animationNames.empty()) {
        controller = CreateDefaultControllerForModel(*model);
        controller->sourceModelPath = modelPath;
        for (auto& st : controller->states)
            st.clipSourceModelPath = modelPath;
    }

    if (controller)
        SyncControllerStateClipStacks(*controller);

    if (controller) {
        for (const auto& p : controller->parameters) {
            switch (p.type) {
            case AnimatorParamType::Float:
                if (!parameters.floats.count(p.name))
                    parameters.floats[p.name] = p.defaultFloat;
                break;
            case AnimatorParamType::Bool:
                if (!parameters.bools.count(p.name))
                    parameters.bools[p.name] = p.defaultBool;
                break;
            case AnimatorParamType::Int:
                if (!parameters.ints.count(p.name))
                    parameters.ints[p.name] = p.defaultInt;
                break;
            case AnimatorParamType::Trigger:
                parameters.triggers[p.name] = false;
                break;
            }
        }
    }

    ResetToDefaultState();
}

void AnimatorComponent::ResetToDefaultState() {
    currentState.clear();
    nextState.clear();
    stateTime = 0.0f;
    transitionDuration = 0.0f;
    transitionTime = 0.0f;
    inTransition = false;
    heldTriggers.clear();

    if (controller && !controller->defaultState.empty() &&
        FindState(*controller, controller->defaultState)) {
        currentState = controller->defaultState;
    } else if (controller && !controller->states.empty()) {
        currentState = controller->states[0].name;
    }

    for (int i = 0; i < kMaxBones; ++i)
        boneMatrices[i] = glm::mat4(1.0f);

    if (model && !model->bones.empty() && controller && !currentState.empty()) {
        if (const AnimatorStateDef* st = FindState(*controller, currentState))
            EvaluateStatePose(*this, *st, 0.0f, boneMatrices);
        else
            model->EvaluateBoneMatrices({}, 0.0, boneMatrices);
    }
}

void ReloadAnimatorsUsingController(Scene& scene, const std::string& controllerRelPath) {
    if (controllerRelPath.empty())
        return;
    for (auto& entityPtr : scene.GetEntities()) {
        if (!entityPtr)
            continue;
        auto* animator = entityPtr->GetComponent<AnimatorComponent>();
        if (animator && animator->controllerPath == controllerRelPath)
            animator->ReloadAssets();
    }
}

void ReloadAllSceneAnimators(Scene& scene) {
    std::unordered_set<std::string> controllerPaths;
    for (auto& entityPtr : scene.GetEntities()) {
        if (!entityPtr)
            continue;
        if (auto* animator = entityPtr->GetComponent<AnimatorComponent>()) {
            if (!animator->controllerPath.empty())
                controllerPaths.insert(animator->controllerPath);
        }
    }
    AssetManager& assets = AssetManager::Get();
    for (const std::string& path : controllerPaths)
        assets.DropAnimatorController(path);

    for (auto& entityPtr : scene.GetEntities()) {
        if (!entityPtr)
            continue;
        if (auto* animator = entityPtr->GetComponent<AnimatorComponent>())
            animator->ReloadAssets();
    }
}

void SampleAnimatorBoneMatrices(const AnimatorComponent& animator, glm::mat4 out[kMaxBones]) {
    for (int i = 0; i < kMaxBones; ++i)
        out[i] = glm::mat4(1.0f);

    if (!animator.model || animator.model->bones.empty())
        return;

    if (animator.debugBindPoseOnly) {
        animator.model->EvaluateBoneMatrices({}, 0.0, out);
        return;
    }

    if (!animator.controller) {
        animator.model->EvaluateBoneMatrices({}, 0.0, out);
        return;
    }

    for (int i = 0; i < kMaxBones; ++i)
        out[i] = animator.boneMatrices[i];
}

void SampleAnimatorDefaultStateBoneMatrices(AnimatorComponent& animator,
                                            glm::mat4 out[kMaxBones]) {
    for (int i = 0; i < kMaxBones; ++i)
        out[i] = glm::mat4(1.0f);

    if (!animator.model || animator.model->bones.empty())
        return;
    if (animator.debugBindPoseOnly || !animator.controller) {
        animator.model->EvaluateBoneMatrices({}, 0.0, out);
        return;
    }

    const AnimatorStateDef* state = nullptr;
    if (!animator.controller->defaultState.empty())
        state = FindState(*animator.controller, animator.controller->defaultState);
    if (!state && !animator.controller->states.empty())
        state = &animator.controller->states.front();

    if (state)
        EvaluateStatePose(animator, *state, 0.0f, out);
    else
        animator.model->EvaluateBoneMatrices({}, 0.0, out);
}

void AnimationSystem::Update(Scene& scene, float deltaTime) {
    for (auto& entityPtr : scene.GetEntities()) {
        if (!entityPtr)
            continue;
        auto* animator = entityPtr->GetComponent<AnimatorComponent>();
        if (!animator || !animator->enabled)
            continue;

        if (auto* skinned = entityPtr->GetComponent<SkinnedMeshRendererComponent>()) {
            if (!skinned->modelPath.empty()) {
                if (animator->modelPath != skinned->modelPath)
                    animator->modelPath = skinned->modelPath;
                if (!animator->model || animator->modelPath != skinned->modelPath)
                    animator->model = AssetManager::Get().GetSkeletalModel(skinned->modelPath);
            }
        }
        if (!animator->controller && !animator->model)
            continue;

        if (!animator->controller) {
            animator->model->EvaluateBoneMatrices({}, 0.0, animator->boneMatrices);
            continue;
        }

        if (animator->currentState.empty())
            animator->ResetToDefaultState();

        const float timeScale = std::max(0.0f, animator->speed);
        const float transitionTimeScale = timeScale > 0.0f ? timeScale : 1.0f;
        animator->stateTime += deltaTime * timeScale;

        // Consume triggers after this frame's transition checks.
        std::vector<std::string> firedTriggers;
        for (const auto& [name, fired] : animator->parameters.triggers) {
            if (fired)
                firedTriggers.push_back(name);
        }

        if (!animator->inTransition && animator->controller) {
            const AnimatorStateDef* curState = FindState(*animator->controller, animator->currentState);
            float exitHoldStateTime = 0.0f;
            bool shouldHoldAtExit = false;
            bool transitionChosen = false;

            for (const auto& tr : animator->controller->transitions) {
                if (tr.fromState != animator->currentState &&
                    tr.fromState != kAnimatorAnyStateName)
                    continue;
                if (!FindState(*animator->controller, tr.toState))
                    continue;
                if (curState &&
                    !AnimatorTransitionExitTimeReached(*animator, *curState, animator->stateTime, tr))
                    continue;

                bool allPass = true;
                for (const auto& cond : tr.conditions) {
                    if (!ConditionPasses(*animator, cond)) {
                        allPass = false;
                        break;
                    }
                }
                if (!allPass) {
                    // Match the PS1 runtime: once an outgoing transition's
                    // Exit Time has been reached, hold that pose until its
                    // conditions pass. Any State transitions must not freeze
                    // every state in the controller.
                    if (curState && tr.hasExitTime &&
                        tr.fromState == animator->currentState) {
                        float candidateStateTime = 0.0f;
                        if (AnimatorStateTimeAtNormalizedExit(
                                *animator, *curState, tr.exitTime,
                                candidateStateTime) &&
                            (!shouldHoldAtExit || candidateStateTime < exitHoldStateTime)) {
                            exitHoldStateTime = candidateStateTime;
                            shouldHoldAtExit = true;
                        }
                    }
                    continue;
                }

                animator->nextState = tr.toState;
                animator->transitionDuration = std::max(0.0f, tr.duration);
                animator->transitionTime = 0.0f;
                animator->inTransition = animator->transitionDuration > 0.0f;
                if (!animator->inTransition) {
                    animator->currentState = animator->nextState;
                    animator->stateTime = 0.0f;
                }
                transitionChosen = true;
                break;
            }

            if (!transitionChosen && shouldHoldAtExit)
                animator->stateTime = std::min(animator->stateTime, exitHoldStateTime);
        }

        if (animator->inTransition) {
            animator->transitionTime += deltaTime * transitionTimeScale;
            const float t = animator->transitionDuration > 0.0f
                ? std::clamp(animator->transitionTime / animator->transitionDuration, 0.0f, 1.0f)
                : 1.0f;

            const AnimatorStateDef* fromSt =
                FindState(*animator->controller, animator->currentState);
            const AnimatorStateDef* toSt = FindState(*animator->controller, animator->nextState);

            glm::mat4 fromMats[kMaxBones];
            glm::mat4 toMats[kMaxBones];
            for (int i = 0; i < kMaxBones; ++i) {
                fromMats[i] = glm::mat4(1.0f);
                toMats[i] = glm::mat4(1.0f);
            }
            if (fromSt) {
                if (!SampleStateTransformClipToEntity(*animator, *fromSt, animator->stateTime, *entityPtr))
                    EvaluateStatePose(*animator, *fromSt, animator->stateTime, fromMats);
            }
            if (toSt) {
                if (!SampleStateTransformClipToEntity(*animator, *toSt, 0.0f, *entityPtr))
                    EvaluateStatePose(*animator, *toSt, 0.0f, toMats);
            }
            if (animator->model)
                BlendBoneMatrices(fromMats, toMats, t, animator->boneMatrices);

            if (t >= 1.0f) {
                animator->currentState = animator->nextState;
                animator->inTransition = false;
                animator->stateTime = 0.0f;
            }
        } else {
            const AnimatorStateDef* cur = FindState(*animator->controller, animator->currentState);
            if (cur && !SampleStateTransformClipToEntity(*animator, *cur, animator->stateTime, *entityPtr))
                EvaluateStatePose(*animator, *cur, animator->stateTime, animator->boneMatrices);
        }

        /* Held triggers are still edge-triggered on entry. The heldTriggers
         * set, not a perpetually true trigger bit, blocks an outgoing IfFalse
         * condition until Mips# calls ReleaseTrigger. This prevents Any State
         * transitions from restarting the held state every frame. */
        for (const std::string& trig : firedTriggers)
            animator->parameters.triggers[trig] = false;
    }
}

} // namespace MipsyncEngine
