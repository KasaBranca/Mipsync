#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace MipsyncEngine {

constexpr int kMaxBones = 128;

/// Special transition source (Unity "Any State").
inline constexpr const char* kAnimatorAnyStateName = "Any State";

enum class AnimatorParamType : uint8_t {
    Float = 0,
    Bool,
    Int,
    Trigger,
};

struct AnimatorParameterDef {
    std::string name;
    AnimatorParamType type = AnimatorParamType::Float;
    float defaultFloat = 0.0f;
    bool defaultBool = false;
    int defaultInt = 0;
};

struct AnimatorStateDef {
    std::string name;
    /// Unique clip id from SkeletalModelAsset::animationNames (Mixamo-disambiguated).
    std::string clipName;
    /// Project-relative FBX that owns clipStackIndex (Mixamo: often one anim per file).
    std::string clipSourceModelPath;
    /// ufbx anim_stacks index; -1 = resolve from clipName only.
    int clipStackIndex = -1;
    float speed = 1.0f;
    /// Normalized position within the clip used when the state begins (0..1).
    float startOffset = 0.0f;
    bool loop = true;
    /// Visual editor node position (graph space pixels).
    glm::vec2 graphPosition{ 0.0f, 0.0f };
};

enum class AnimatorConditionMode : uint8_t {
    Greater = 0,
    Less,
    Equals,
    NotEquals,
    IfTrue,
    IfFalse,
};

struct AnimatorTransitionCondition {
    std::string parameter;
    AnimatorConditionMode mode = AnimatorConditionMode::Greater;
    float threshold = 0.1f;
};

struct AnimatorTransitionDef {
    std::string fromState;
    std::string toState;
    float duration = 0.15f;
    float exitTime = 0.9f;
    bool hasExitTime = false;
    std::vector<AnimatorTransitionCondition> conditions;
};

struct AnimatorControllerAsset {
    std::string defaultState;
    /// Optional FBX path (project-relative) for clip picker in the controller editor.
    std::string sourceModelPath;
    std::vector<AnimatorParameterDef> parameters;
    std::vector<AnimatorStateDef> states;
    std::vector<AnimatorTransitionDef> transitions;
};

} // namespace MipsyncEngine
