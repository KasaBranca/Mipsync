#include "AnimatorControllerIO.h"
#include "SkeletalModel.h"
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

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

AnimatorParamType ParamTypeFromString(const std::string& s) {
    if (s == "bool") return AnimatorParamType::Bool;
    if (s == "int") return AnimatorParamType::Int;
    if (s == "trigger") return AnimatorParamType::Trigger;
    return AnimatorParamType::Float;
}

std::string ParamTypeToString(AnimatorParamType type) {
    switch (type) {
    case AnimatorParamType::Bool: return "bool";
    case AnimatorParamType::Int: return "int";
    case AnimatorParamType::Trigger: return "trigger";
    default: return "float";
    }
}

AnimatorConditionMode ConditionFromString(const std::string& s) {
    if (s == "less") return AnimatorConditionMode::Less;
    if (s == "equals") return AnimatorConditionMode::Equals;
    if (s == "notEquals") return AnimatorConditionMode::NotEquals;
    if (s == "ifTrue") return AnimatorConditionMode::IfTrue;
    if (s == "ifFalse") return AnimatorConditionMode::IfFalse;
    return AnimatorConditionMode::Greater;
}

std::string ConditionToString(AnimatorConditionMode mode) {
    switch (mode) {
    case AnimatorConditionMode::Less: return "less";
    case AnimatorConditionMode::Equals: return "equals";
    case AnimatorConditionMode::NotEquals: return "notEquals";
    case AnimatorConditionMode::IfTrue: return "ifTrue";
    case AnimatorConditionMode::IfFalse: return "ifFalse";
    default: return "greater";
    }
}

json ControllerToJson(const AnimatorControllerAsset& asset) {
    json root;
    root["version"] = 1;
    root["defaultState"] = asset.defaultState;
    if (!asset.sourceModelPath.empty())
        root["model"] = asset.sourceModelPath;

    json params = json::array();
    for (const auto& p : asset.parameters) {
        json pj;
        pj["name"] = p.name;
        pj["type"] = ParamTypeToString(p.type);
        switch (p.type) {
        case AnimatorParamType::Bool:
            pj["default"] = p.defaultBool;
            break;
        case AnimatorParamType::Int:
            pj["default"] = p.defaultInt;
            break;
        case AnimatorParamType::Float:
        default:
            pj["default"] = p.defaultFloat;
            break;
        case AnimatorParamType::Trigger:
            pj["default"] = false;
            break;
        }
        params.push_back(pj);
    }
    root["parameters"] = params;

    json states = json::array();
    for (const auto& st : asset.states) {
        json sj = {
            { "name", st.name },
            { "clip", st.clipName },
            { "speed", st.speed },
            { "startOffset", st.startOffset },
            { "loop", st.loop },
        };
        if (st.clipStackIndex >= 0)
            sj["stack"] = st.clipStackIndex;
        if (!st.clipSourceModelPath.empty())
            sj["clipModel"] = st.clipSourceModelPath;
        if (st.graphPosition.x != 0.0f || st.graphPosition.y != 0.0f)
            sj["position"] = { st.graphPosition.x, st.graphPosition.y };
        states.push_back(sj);
    }
    root["states"] = states;

    json transitions = json::array();
    for (const auto& tr : asset.transitions) {
        json tj;
        tj["from"] = tr.fromState;
        tj["to"] = tr.toState;
        tj["duration"] = tr.duration;
        tj["exitTime"] = tr.exitTime;
        tj["hasExitTime"] = tr.hasExitTime;
        json conds = json::array();
        for (const auto& c : tr.conditions) {
            conds.push_back({
                { "param", c.parameter },
                { "mode", ConditionToString(c.mode) },
                { "threshold", c.threshold },
            });
        }
        tj["conditions"] = conds;
        transitions.push_back(tj);
    }
    root["transitions"] = transitions;
    return root;
}

static void TrimInPlace(std::string& s) {
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
}

static bool StateNameExists(const AnimatorControllerAsset& asset, const std::string& name) {
    for (const auto& st : asset.states) {
        if (st.name == name)
            return true;
    }
    return false;
}

static int FindStateIndexByName(const AnimatorControllerAsset& asset, const std::string& name) {
    if (name.empty())
        return -1;
    for (size_t i = 0; i < asset.states.size(); ++i) {
        if (asset.states[i].name == name)
            return static_cast<int>(i);
    }
    return -1;
}

/// Map clip id to a state only when exactly one state uses that clip name.
static int FindStateIndexByUniqueClip(const AnimatorControllerAsset& asset, const std::string& clip) {
    if (clip.empty())
        return -1;
    int found = -1;
    for (size_t i = 0; i < asset.states.size(); ++i) {
        if (asset.states[i].clipName != clip)
            continue;
        if (found >= 0)
            return -1;
        found = static_cast<int>(i);
    }
    return found;
}

static std::string StateClipModelPath(const AnimatorControllerAsset& asset,
                                      const AnimatorStateDef& state) {
    if (!state.clipSourceModelPath.empty())
        return state.clipSourceModelPath;
    return asset.sourceModelPath;
}

static bool IsTransformAnimationClipPath(const std::string& path) {
    std::string extension = fs::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".nanim";
}

static void ValidateAnimatorController(AnimatorControllerAsset& asset) {
    for (auto& state : asset.states) {
        if (!std::isfinite(state.startOffset))
            state.startOffset = 0.0f;
        state.startOffset = std::clamp(state.startOffset, 0.0f, 1.0f);
    }

    if (asset.defaultState.empty()) {
        if (!asset.states.empty())
            asset.defaultState = asset.states[0].name;
    } else if (!StateNameExists(asset, asset.defaultState) && !asset.states.empty()) {
        asset.defaultState = asset.states[0].name;
    }

    for (auto& tr : asset.transitions) {
        TrimInPlace(tr.fromState);
        TrimInPlace(tr.toState);

        int fromIdx = FindStateIndexByName(asset, tr.fromState);
        if (fromIdx < 0)
            fromIdx = FindStateIndexByUniqueClip(asset, tr.fromState);
        int toIdx = FindStateIndexByName(asset, tr.toState);
        if (toIdx < 0)
            toIdx = FindStateIndexByUniqueClip(asset, tr.toState);
        if (fromIdx >= 0)
            tr.fromState = asset.states[static_cast<size_t>(fromIdx)].name;
        if (toIdx >= 0)
            tr.toState = asset.states[static_cast<size_t>(toIdx)].name;
    }

    asset.transitions.erase(
        std::remove_if(asset.transitions.begin(), asset.transitions.end(),
                       [&](const AnimatorTransitionDef& tr) {
                           return tr.fromState.empty() || tr.toState.empty() ||
                                  tr.fromState == tr.toState ||
                                  !StateNameExists(asset, tr.fromState) ||
                                  !StateNameExists(asset, tr.toState);
                       }),
        asset.transitions.end());
}

} // namespace

bool SyncControllerStateClipStacks(AnimatorControllerAsset& asset) {
    bool repaired = false;
    std::unordered_map<std::string, std::unordered_set<int>> usedStacksByModel;

    for (auto& st : asset.states) {
        if (st.clipSourceModelPath.empty() && !asset.sourceModelPath.empty()) {
            st.clipSourceModelPath = asset.sourceModelPath;
            repaired = true;
        }

        const std::string modelPath = StateClipModelPath(asset, st);
        if (modelPath.empty() || IsTransformAnimationClipPath(modelPath))
            continue;

        const auto model = AssetManager::Get().GetSkeletalModel(modelPath);
        if (!model)
            continue;

        auto& usedStacks = usedStacksByModel[modelPath];
        if (st.clipStackIndex >= 0) {
            const int resolved = model->ResolveClipStackIndex(st.clipName, st.clipStackIndex);
            if (resolved >= 0 && resolved != st.clipStackIndex) {
                st.clipStackIndex = resolved;
                repaired = true;
            }
            usedStacks.insert(st.clipStackIndex);
            continue;
        }

        bool assigned = false;
        for (size_t i = 0; i < model->animationNames.size(); ++i) {
            if (model->animationNames[i] != st.clipName)
                continue;
            const int stack = static_cast<int>(model->animationStackIndices[i]);
            if (usedStacks.count(stack))
                continue;
            st.clipStackIndex = stack;
            if (st.clipSourceModelPath.empty())
                st.clipSourceModelPath = modelPath;
            usedStacks.insert(stack);
            assigned = true;
            repaired = true;
            break;
        }
        if (assigned)
            continue;

        const int prev = st.clipStackIndex;
        st.clipStackIndex = model->ClipStackIndex(st.clipName);
        if (st.clipStackIndex >= 0) {
            if (st.clipSourceModelPath.empty())
                st.clipSourceModelPath = modelPath;
            usedStacks.insert(st.clipStackIndex);
            if (st.clipStackIndex != prev)
                repaired = true;
        } else {
            const int byStateName = model->FindClipIndexById(st.name);
            if (byStateName >= 0) {
                st.clipName = model->animationNames[static_cast<size_t>(byStateName)];
                st.clipStackIndex =
                    static_cast<int>(model->animationStackIndices[static_cast<size_t>(byStateName)]);
                if (st.clipSourceModelPath.empty())
                    st.clipSourceModelPath = modelPath;
                usedStacks.insert(st.clipStackIndex);
                repaired = true;
            }
        }
    }
    return repaired;
}

std::shared_ptr<AnimatorControllerAsset> LoadAnimatorController(const std::string& absolutePath,
                                                                std::string& outError) {
    try {
        std::ifstream file(absolutePath);
        if (!file.is_open()) {
            outError = "failed to open: " + absolutePath;
            return nullptr;
        }

        json root;
        file >> root;
        auto asset = std::make_shared<AnimatorControllerAsset>();
        asset->defaultState = root.value("defaultState", std::string{});
        asset->sourceModelPath = root.value("model", std::string{});

        if (root.contains("parameters") && root["parameters"].is_array()) {
            for (const auto& p : root["parameters"]) {
                AnimatorParameterDef def;
                def.name = p.value("name", "");
                def.type = ParamTypeFromString(p.value("type", "float"));
                if (p.contains("default")) {
                    if (def.type == AnimatorParamType::Bool)
                        def.defaultBool = p["default"].get<bool>();
                    else if (def.type == AnimatorParamType::Int)
                        def.defaultInt = p["default"].get<int>();
                    else
                        def.defaultFloat = p["default"].get<float>();
                }
                if (!def.name.empty())
                    asset->parameters.push_back(def);
            }
        }

        if (root.contains("states") && root["states"].is_array()) {
            for (const auto& s : root["states"]) {
                AnimatorStateDef st;
                st.name = s.value("name", "");
                st.clipName = s.value("clip", s.value("clipName", ""));
                st.clipSourceModelPath = s.value("clipModel", std::string{});
                st.clipStackIndex = s.value("stack", -1);
                st.speed = s.value("speed", 1.0f);
                st.startOffset = s.value("startOffset", 0.0f);
                st.loop = s.value("loop", true);
                if (s.contains("position") && s["position"].is_array() && s["position"].size() >= 2) {
                    st.graphPosition.x = s["position"][0].get<float>();
                    st.graphPosition.y = s["position"][1].get<float>();
                }
                if (!st.name.empty() && !st.clipName.empty())
                    asset->states.push_back(st);
            }
        }

        if (root.contains("transitions") && root["transitions"].is_array()) {
            for (const auto& t : root["transitions"]) {
                AnimatorTransitionDef tr;
                tr.fromState = t.value("from", "");
                tr.toState = t.value("to", "");
                tr.duration = t.value("duration", 0.15f);
                tr.exitTime = t.value("exitTime", 0.9f);
                tr.hasExitTime = t.value("hasExitTime", false);
                if (t.contains("conditions") && t["conditions"].is_array()) {
                    for (const auto& c : t["conditions"]) {
                        AnimatorTransitionCondition cond;
                        cond.parameter = c.value("param", c.value("parameter", ""));
                        cond.mode = ConditionFromString(c.value("mode", "greater"));
                        cond.threshold = c.value("threshold", 0.1f);
                        if (!cond.parameter.empty())
                            tr.conditions.push_back(cond);
                    }
                }
                if (!tr.fromState.empty() && !tr.toState.empty())
                    asset->transitions.push_back(tr);
            }
        }

        ValidateAnimatorController(*asset);
        SyncControllerStateClipStacks(*asset);
        return asset;
    } catch (const std::exception& ex) {
        outError = ex.what();
        return nullptr;
    }
}

bool SaveAnimatorController(const std::string& absolutePath, const AnimatorControllerAsset& asset,
                            std::string& outError) {
    try {
        AnimatorControllerAsset copy = asset;
        ValidateAnimatorController(copy);
        SyncControllerStateClipStacks(copy);
        const json root = ControllerToJson(copy);
        fs::path p(absolutePath);
        if (p.has_parent_path())
            fs::create_directories(p.parent_path());

        std::ofstream out(absolutePath);
        if (!out.is_open()) {
            outError = "failed to open: " + absolutePath;
            return false;
        }
        out << root.dump(2);
        return true;
    } catch (const std::exception& ex) {
        outError = ex.what();
        return false;
    }
}

std::shared_ptr<AnimatorControllerAsset> CreateDefaultControllerForModel(
    const SkeletalModelAsset& model) {
    auto asset = std::make_shared<AnimatorControllerAsset>();
    if (model.animationNames.empty())
        return asset;

    for (size_t i = 0; i < model.animationNames.size(); ++i) {
        AnimatorStateDef st;
        st.name = model.animationNames[i];
        st.clipName = model.animationNames[i];
        st.clipStackIndex = static_cast<int>(model.animationStackIndices[i]);
        st.speed = 1.0f;
        st.loop = true;
        st.graphPosition = glm::vec2(static_cast<float>((i % 4) * 220), static_cast<float>((i / 4) * 90));
        asset->states.push_back(st);
    }

    asset->defaultState = asset->states[0].name;

    if (asset->states.size() >= 2) {
        AnimatorTransitionDef aToB;
        aToB.fromState = asset->states[0].name;
        aToB.toState = asset->states[1].name;
        aToB.duration = 0.15f;
        asset->transitions.push_back(aToB);

        AnimatorTransitionDef bToA;
        bToA.fromState = asset->states[1].name;
        bToA.toState = asset->states[0].name;
        bToA.duration = 0.15f;
        asset->transitions.push_back(bToA);
    }

    return asset;
}

std::shared_ptr<AnimatorControllerAsset> CreateDefaultControllerForModel(
    const std::vector<std::string>& clipNames) {
    auto asset = std::make_shared<AnimatorControllerAsset>();
    if (clipNames.empty())
        return asset;

    for (size_t i = 0; i < clipNames.size(); ++i) {
        AnimatorStateDef st;
        st.name = clipNames[i];
        st.clipName = clipNames[i];
        st.clipStackIndex = -1;
        st.speed = 1.0f;
        st.loop = true;
        st.graphPosition = glm::vec2(static_cast<float>((i % 4) * 220), static_cast<float>((i / 4) * 90));
        asset->states.push_back(st);
    }

    asset->defaultState = asset->states[0].name;

    // Unconditional links between first two states (Unity-style: no conditions = always).
    if (asset->states.size() >= 2) {
        AnimatorTransitionDef aToB;
        aToB.fromState = asset->states[0].name;
        aToB.toState = asset->states[1].name;
        aToB.duration = 0.15f;
        asset->transitions.push_back(aToB);

        AnimatorTransitionDef bToA;
        bToA.fromState = asset->states[1].name;
        bToA.toState = asset->states[0].name;
        bToA.duration = 0.15f;
        asset->transitions.push_back(bToA);
    }

    return asset;
}

std::shared_ptr<AnimatorControllerAsset> CreateEmptyController() {
    auto asset = std::make_shared<AnimatorControllerAsset>();
    asset->parameters.push_back({ "Speed", AnimatorParamType::Float, 0.0f, false, 0 });
    AnimatorStateDef idle{};
    idle.name = "Idle";
    idle.clipName = "Take 001";
    asset->states.push_back(idle);
    asset->defaultState = "Idle";
    return asset;
}

} // namespace MipsyncEngine
