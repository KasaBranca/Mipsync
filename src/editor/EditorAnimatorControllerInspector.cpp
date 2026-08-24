#include "EditorAnimatorControllerInspector.h"
#include "EditorAnimatorGraph.h"
#include "EditorApp.h"
#include "../core/Engine.h"
#include "EditorTheme.h"
#include "AssetBrowserPanel.h"
#include "../animation/AnimatorControllerIO.h"
#include "../animation/AnimatorRuntime.h"
#include "../animation/SkeletalModel.h"
#include "../scene/Scene.h"
#include "../renderer/Texture.h"
#include "../assets/AssetManager.h"
#include "../core/Log.h"
#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_set>

namespace MipsyncEngine {

namespace {

std::string DefaultControllerRelForModel(const std::string& modelPath) {
    const size_t slash = modelPath.find_last_of('/');
    std::string base = slash == std::string::npos ? modelPath : modelPath.substr(slash + 1);
    const size_t dot = base.find_last_of('.');
    if (dot != std::string::npos)
        base = base.substr(0, dot);
    if (base.empty())
        base = "Controller";
    return "assets/animations/" + base + ".ncontroller";
}

} // namespace

void CommitAnimatorController(const std::string& projectRelPath, AnimatorControllerAsset& asset,
                              Scene* scene) {
    if (asset.defaultState.empty() && !asset.states.empty())
        asset.defaultState = asset.states[0].name;
    std::string err;
    if (SaveAnimatorController(AssetManager::Get().ToAbsolute(projectRelPath), asset, err)) {
        AssetManager::Get().DropAnimatorController(projectRelPath);
        if (scene)
            ReloadAnimatorsUsingController(*scene, projectRelPath);
        MIPSYNC_INFO("Animator controller saved: {}", projectRelPath);
    } else {
        MIPSYNC_WARN("Animator controller save failed: {}", err);
    }
}

std::vector<std::string> LoadAnimationClipNames(const std::string& modelProjectPath) {
    if (modelProjectPath.empty())
        return {};
    auto model = AssetManager::Get().GetSkeletalModel(modelProjectPath);
    if (!model)
        return {};
    return model->animationNames;
}

bool AnimatorStateUsesClip(const AnimatorControllerAsset& asset, const std::string& clipName) {
    for (const auto& st : asset.states) {
        if (st.clipName == clipName)
            return true;
    }
    return false;
}

std::string MakeUniqueAnimatorStateName(const AnimatorControllerAsset& asset,
                                        const std::string& preferredIn) {
    const std::string preferred = preferredIn;
    auto nameTaken = [&](const std::string& candidate) {
        for (const auto& st : asset.states) {
            if (st.name == candidate)
                return true;
        }
        return false;
    };

    const std::string base = preferred.empty() ? "New State" : preferred;
    if (!nameTaken(base))
        return base;

    for (int suffix = 2; suffix < 10000; ++suffix) {
        const std::string candidate = base + " (" + std::to_string(suffix) + ")";
        if (!nameTaken(candidate))
            return candidate;
    }
    return base + "_" + std::to_string(asset.states.size());
}

bool StateUsesAnimStack(const AnimatorControllerAsset& asset, const std::string& modelPath,
                        int stackIndex) {
    if (stackIndex < 0 || modelPath.empty())
        return false;
    for (const auto& st : asset.states) {
        if (st.clipStackIndex != stackIndex)
            continue;
        const std::string stModel =
            st.clipSourceModelPath.empty() ? asset.sourceModelPath : st.clipSourceModelPath;
        if (stModel == modelPath)
            return true;
    }
    return false;
}

void AddAnimatorStateForClip(AnimatorControllerAsset& asset, const std::string& clipName,
                              glm::vec2 graphPosition, int clipStackIndex) {
    if (clipName.empty())
        return;

    AnimatorStateDef st{};
    st.name = MakeUniqueAnimatorStateName(asset, clipName);
    st.clipName = clipName;
    st.clipSourceModelPath = asset.sourceModelPath;
    st.clipStackIndex = clipStackIndex;
    if (st.clipStackIndex < 0 && !asset.sourceModelPath.empty()) {
        if (const auto model = AssetManager::Get().GetSkeletalModel(asset.sourceModelPath))
            st.clipStackIndex = model->ClipStackIndex(clipName);
    }
    if (graphPosition.x >= 0.0f && graphPosition.y >= 0.0f) {
        st.graphPosition = graphPosition;
    } else {
        st.graphPosition = glm::vec2(40.0f + static_cast<float>(asset.states.size()) * 40.0f,
                                     40.0f + static_cast<float>(asset.states.size()) * 20.0f);
    }
    asset.states.push_back(st);
    if (asset.defaultState.empty())
        asset.defaultState = st.name;
}

void ImportAllAnimatorClipsAsStatesAt(AnimatorControllerAsset& asset,
                                      const std::vector<std::string>& clips, glm::vec2 origin,
                                      bool& changed) {
    const size_t before = asset.states.size();
    float x = 0.0f;
    float y = 0.0f;
    const auto model = asset.sourceModelPath.empty()
        ? nullptr
        : AssetManager::Get().GetSkeletalModel(asset.sourceModelPath);

    for (const std::string& clip : clips) {
        const int stackIdx = model ? model->ClipStackIndex(clip) : -1;
        if (stackIdx >= 0 && StateUsesAnimStack(asset, asset.sourceModelPath, stackIdx))
            continue;
        if (stackIdx < 0 && AnimatorStateUsesClip(asset, clip))
            continue;
        AddAnimatorStateForClip(asset, clip, origin + glm::vec2(x, y), stackIdx);
        x += 220.0f;
        if (x > 660.0f) {
            x = 0.0f;
            y += 90.0f;
        }
    }
    if (asset.states.size() != before)
        changed = true;
}

void ImportAllAnimatorClipsAsStates(AnimatorControllerAsset& asset,
                                    const std::vector<std::string>& clips, bool& changed) {
    ImportAllAnimatorClipsAsStatesAt(asset, clips, glm::vec2{ 40.0f, 40.0f }, changed);
}

bool ApplyAnimatorModelToController(AnimatorControllerAsset& asset,
                                    const std::string& modelProjectPath,
                                    std::vector<std::string>& outClips, glm::vec2 graphOrigin,
                                    bool& changed) {
    if (modelProjectPath.empty())
        return false;

    asset.sourceModelPath = modelProjectPath;
    outClips = LoadAnimationClipNames(modelProjectPath);
    if (outClips.empty())
        return false;

    ImportAllAnimatorClipsAsStatesAt(asset, outClips, graphOrigin, changed);
    changed = true;
    return true;
}

bool DrawAnimatorClipField(const char* label, std::string& clipName, int& clipStackIndex,
                           std::string* clipSourceModelPath, const std::string& modelProjectPath,
                           const std::shared_ptr<SkeletalModelAsset>& model,
                           const std::vector<std::string>& clips, bool& changed) {
    if (model && !model->animationNames.empty()) {
        std::string preview = clipName.empty() ? "<select clip>" : clipName;
        if (clipStackIndex >= 0)
            preview += " [stack " + std::to_string(clipStackIndex) + "]";
        if (ImGui::BeginCombo(label, preview.c_str())) {
            for (size_t i = 0; i < model->animationNames.size(); ++i) {
                const std::string& clip = model->animationNames[i];
                const int stack = static_cast<int>(model->animationStackIndices[i]);
                const bool selected = clip == clipName && clipStackIndex == stack;
                const std::string row = clip + "  [stack " + std::to_string(stack) + "]";
                if (ImGui::Selectable(row.c_str(), selected)) {
                    clipName = clip;
                    clipStackIndex = stack;
                    if (clipSourceModelPath && !modelProjectPath.empty())
                        *clipSourceModelPath = modelProjectPath;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
    } else if (clips.empty()) {
        char clipBuf[256] = {};
        std::strncpy(clipBuf, clipName.c_str(), sizeof(clipBuf) - 1);
        if (ImGui::InputText(label, clipBuf, sizeof(clipBuf))) {
            clipName = clipBuf;
            clipStackIndex = -1;
            if (clipSourceModelPath)
                clipSourceModelPath->clear();
            changed = true;
            return true;
        }
        return false;
    } else {
        const char* preview = clipName.empty() ? "<select clip>" : clipName.c_str();
        if (ImGui::BeginCombo(label, preview)) {
            for (const std::string& clip : clips) {
                if (ImGui::Selectable(clip.c_str(), clip == clipName)) {
                    clipName = clip;
                    clipStackIndex = -1;
                    if (clipSourceModelPath)
                        clipSourceModelPath->clear();
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
    }

    char clipBuf[256] = {};
    std::strncpy(clipBuf, clipName.c_str(), sizeof(clipBuf) - 1);
    if (ImGui::InputText("Custom Clip Name", clipBuf, sizeof(clipBuf))) {
        clipName = clipBuf;
        changed = true;
    }
    return changed;
}

void DrawAnimatorParametersSidebar(AnimatorControllerAsset& asset, bool& changed) {
    static int selectedParameter = -1;
    selectedParameter = std::clamp(selectedParameter, -1,
                                   static_cast<int>(asset.parameters.size()) - 1);

    ImGui::TextColored(EditorTheme::TextSecondary, "Parameters");
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - 24.0f);
    if (ImGui::SmallButton("+##add_parameter"))
        ImGui::OpenPopup("Add Animator Parameter");

    auto uniqueParameterName = [&](const char* preferred) {
        auto used = [&](const std::string& candidate) {
            return std::any_of(asset.parameters.begin(), asset.parameters.end(),
                               [&](const AnimatorParameterDef& p) { return p.name == candidate; });
        };
        std::string base = preferred;
        if (!used(base))
            return base;
        for (int suffix = 1; suffix < 10000; ++suffix) {
            std::string candidate = base + " " + std::to_string(suffix);
            if (!used(candidate))
                return candidate;
        }
        return base + " New";
    };

    if (ImGui::BeginPopup("Add Animator Parameter")) {
        const struct { const char* label; AnimatorParamType type; } types[] = {
            { "Float", AnimatorParamType::Float }, { "Int", AnimatorParamType::Int },
            { "Bool", AnimatorParamType::Bool }, { "Trigger", AnimatorParamType::Trigger },
        };
        for (const auto& entry : types) {
            if (ImGui::MenuItem(entry.label)) {
                AnimatorParameterDef parameter{};
                parameter.name = uniqueParameterName(entry.label);
                parameter.type = entry.type;
                asset.parameters.push_back(std::move(parameter));
                selectedParameter = static_cast<int>(asset.parameters.size()) - 1;
                changed = true;
            }
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();

    auto typeGlyph = [](AnimatorParamType t) -> const char* {
        switch (t) {
        case AnimatorParamType::Float: return "F";
        case AnimatorParamType::Bool: return "B";
        case AnimatorParamType::Int: return "I";
        case AnimatorParamType::Trigger: return "T";
        }
        return "?";
    };

    int paramRemove = -1;
    for (size_t i = 0; i < asset.parameters.size(); ++i) {
        auto& p = asset.parameters[i];
        ImGui::PushID(static_cast<int>(i));
        const bool selected = selectedParameter == static_cast<int>(i);
        if (ImGui::Selectable("##parameter_row", selected, ImGuiSelectableFlags_AllowDoubleClick,
                              ImVec2(0.0f, 24.0f)))
            selectedParameter = static_cast<int>(i);
        const ImVec2 rowMin = ImGui::GetItemRectMin();
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(rowMin.x + 5.0f, rowMin.y + 4.0f),
            ImGui::ColorConvertFloat4ToU32(EditorTheme::PsAccent),
            typeGlyph(p.type));
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(rowMin.x + 25.0f, rowMin.y + 4.0f),
            ImGui::ColorConvertFloat4ToU32(EditorTheme::TextPrimary), p.name.c_str());
        ImGui::PopID();
    }

    if (asset.parameters.empty())
        ImGui::TextColored(EditorTheme::TextMuted, "Use + to add a parameter.");

    if (selectedParameter >= 0 && selectedParameter < static_cast<int>(asset.parameters.size())) {
        ImGui::Separator();
        auto& p = asset.parameters[static_cast<size_t>(selectedParameter)];
        char nameBuf[96] = {};
        std::strncpy(nameBuf, p.name.c_str(), sizeof(nameBuf) - 1);
        if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
            const std::string next = nameBuf;
            if (!next.empty() && next != p.name) {
                const std::string old = p.name;
                p.name = next;
                for (auto& transition : asset.transitions)
                    for (auto& condition : transition.conditions)
                        if (condition.parameter == old)
                            condition.parameter = next;
                changed = true;
            }
        }
        int typeIdx = static_cast<int>(p.type);
        if (ImGui::Combo("Type", &typeIdx, "Float\0Bool\0Int\0Trigger\0")) {
            p.type = static_cast<AnimatorParamType>(typeIdx);
            changed = true;
        }
        if (p.type == AnimatorParamType::Float)
            changed |= ImGui::DragFloat("Default", &p.defaultFloat, 0.01f);
        else if (p.type == AnimatorParamType::Int)
            changed |= ImGui::DragInt("Default", &p.defaultInt);
        else if (p.type == AnimatorParamType::Bool)
            changed |= EditorTheme::Checkbox("Default", &p.defaultBool);
        else
            ImGui::TextColored(EditorTheme::TextMuted, "Trigger resets after it is consumed.");

        if (ImGui::Button("Delete Parameter", ImVec2(-1.0f, 0.0f)))
            paramRemove = selectedParameter;
    }

    if (paramRemove >= 0 && paramRemove < static_cast<int>(asset.parameters.size())) {
        const std::string removedName = asset.parameters[static_cast<size_t>(paramRemove)].name;
        asset.parameters.erase(asset.parameters.begin() + paramRemove);
        for (auto& transition : asset.transitions) {
            transition.conditions.erase(
                std::remove_if(transition.conditions.begin(), transition.conditions.end(),
                               [&](const AnimatorTransitionCondition& condition) {
                                   return condition.parameter == removedName;
                               }),
                transition.conditions.end());
        }
        selectedParameter = std::min(paramRemove,
                                     static_cast<int>(asset.parameters.size()) - 1);
        changed = true;
    }
}

namespace {

std::string ResolveControllerModelPath(EditorApp* editor, const std::string& controllerRelPath,
                                       const AnimatorControllerAsset& asset) {
    if (!asset.sourceModelPath.empty())
        return asset.sourceModelPath;
    if (editor)
        return editor->FindModelPathForController(controllerRelPath);
    return {};
}

void DrawAnimatorClipSourceHint(EditorApp* editor, const std::string& controllerRelPath,
                                const AnimatorControllerAsset& asset,
                                std::vector<std::string>& outClips) {
    const std::string modelPath = ResolveControllerModelPath(editor, controllerRelPath, asset);
    if (!modelPath.empty()) {
        ImGui::TextColored(EditorTheme::TextMuted, "Model: %s", modelPath.c_str());
        outClips = LoadAnimationClipNames(modelPath);
    }
}

void DrawAnimatorControllerListEditor(AnimatorControllerAsset& asset,
                                      const std::shared_ptr<SkeletalModelAsset>& model,
                                      const std::vector<std::string>& clips, bool& changed) {
    if (ImGui::BeginCombo("Default State", asset.defaultState.empty() ? "<none>" : asset.defaultState.c_str())) {
        for (const auto& st : asset.states) {
            if (ImGui::Selectable(st.name.c_str(), st.name == asset.defaultState)) {
                asset.defaultState = st.name;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();
    ImGui::TextColored(EditorTheme::TextSecondary, "Parameters");
    int paramRemove = -1;
    for (size_t i = 0; i < asset.parameters.size(); ++i) {
        auto& p = asset.parameters[i];
        ImGui::PushID(static_cast<int>(i));
        char nameBuf[128] = {};
        std::strncpy(nameBuf, p.name.c_str(), sizeof(nameBuf) - 1);
        if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
            p.name = nameBuf;
            changed = true;
        }
        int typeIdx = static_cast<int>(p.type);
        if (ImGui::Combo("Type", &typeIdx, "float\0bool\0int\0trigger\0")) {
            p.type = static_cast<AnimatorParamType>(typeIdx);
            changed = true;
        }
        if (p.type == AnimatorParamType::Float) {
            if (ImGui::DragFloat("Default", &p.defaultFloat, 0.01f))
                changed = true;
        } else if (p.type == AnimatorParamType::Bool) {
            if (EditorTheme::Checkbox("Default", &p.defaultBool))
                changed = true;
        } else if (p.type == AnimatorParamType::Int) {
            if (ImGui::DragInt("Default", &p.defaultInt))
                changed = true;
        }
        if (ImGui::SmallButton("Remove"))
            paramRemove = static_cast<int>(i);
        ImGui::Separator();
        ImGui::PopID();
    }
    if (paramRemove >= 0) {
        asset.parameters.erase(asset.parameters.begin() + paramRemove);
        changed = true;
    }
    if (ImGui::SmallButton("+ Parameter")) {
        asset.parameters.push_back({ "NewParam", AnimatorParamType::Float, 0.0f, false, 0 });
        changed = true;
    }

    ImGui::Spacing();
    ImGui::TextColored(EditorTheme::TextSecondary, "States");
    int stateRemove = -1;
    for (size_t i = 0; i < asset.states.size(); ++i) {
        auto& st = asset.states[i];
        ImGui::PushID(static_cast<int>(1000 + i));
        char nameBuf[128] = {};
        std::strncpy(nameBuf, st.name.c_str(), sizeof(nameBuf) - 1);
        if (ImGui::InputText("State", nameBuf, sizeof(nameBuf))) {
            st.name = nameBuf;
            changed = true;
        }
        DrawAnimatorClipField("Clip", st.clipName, st.clipStackIndex, &st.clipSourceModelPath,
                              asset.sourceModelPath, model, clips, changed);
        if (ImGui::DragFloat("Speed", &st.speed, 0.01f, 0.0f, 10.0f))
            changed = true;
        if (EditorTheme::Checkbox("Loop", &st.loop))
            changed = true;
        if (ImGui::SmallButton("Remove"))
            stateRemove = static_cast<int>(i);
        ImGui::Separator();
        ImGui::PopID();
    }
    if (stateRemove >= 0) {
        asset.states.erase(asset.states.begin() + stateRemove);
        changed = true;
    }
    if (ImGui::SmallButton("+ State")) {
        AnimatorStateDef st{};
        st.name = "NewState";
        st.clipName = "Take 001";
        st.graphPosition = glm::vec2(40.0f, 40.0f);
        asset.states.push_back(st);
        changed = true;
    }

    ImGui::Spacing();
    ImGui::TextColored(EditorTheme::TextSecondary, "Transitions");
    int trRemove = -1;
    for (size_t i = 0; i < asset.transitions.size(); ++i) {
        auto& tr = asset.transitions[i];
        ImGui::PushID(static_cast<int>(2000 + i));
        char fromBuf[128] = {};
        char toBuf[128] = {};
        std::strncpy(fromBuf, tr.fromState.c_str(), sizeof(fromBuf) - 1);
        std::strncpy(toBuf, tr.toState.c_str(), sizeof(toBuf) - 1);
        if (ImGui::InputText("From", fromBuf, sizeof(fromBuf))) {
            tr.fromState = fromBuf;
            changed = true;
        }
        if (ImGui::InputText("To", toBuf, sizeof(toBuf))) {
            tr.toState = toBuf;
            changed = true;
        }
        if (ImGui::DragFloat("Duration", &tr.duration, 0.01f, 0.0f, 5.0f))
            changed = true;
        if (EditorTheme::Checkbox("Has Exit Time", &tr.hasExitTime))
            changed = true;
        if (tr.hasExitTime && ImGui::DragFloat("Exit Time", &tr.exitTime, 0.01f, 0.0f, 1.0f))
            changed = true;

        for (size_t ci = 0; ci < tr.conditions.size(); ++ci) {
            auto& c = tr.conditions[ci];
            ImGui::PushID(static_cast<int>(3000 + ci));
            char paramBuf[128] = {};
            std::strncpy(paramBuf, c.parameter.c_str(), sizeof(paramBuf) - 1);
            if (ImGui::InputText("Param", paramBuf, sizeof(paramBuf))) {
                c.parameter = paramBuf;
                changed = true;
            }
            int modeIdx = static_cast<int>(c.mode);
            if (ImGui::Combo("Mode", &modeIdx,
                             "greater\0less\0equals\0notEquals\0ifTrue\0ifFalse\0")) {
                c.mode = static_cast<AnimatorConditionMode>(modeIdx);
                changed = true;
            }
            if (ImGui::DragFloat("Threshold", &c.threshold, 0.01f))
                changed = true;
            if (ImGui::SmallButton("X")) {
                tr.conditions.erase(tr.conditions.begin() + static_cast<std::ptrdiff_t>(ci));
                changed = true;
            }
            ImGui::PopID();
        }
        if (!tr.conditions.empty() && ImGui::SmallButton("Clear All Conditions")) {
            tr.conditions.clear();
            changed = true;
        }
        if (ImGui::SmallButton("+ Condition")) {
            tr.conditions.push_back({ "Speed", AnimatorConditionMode::Greater, 0.1f });
            changed = true;
        }

        if (ImGui::SmallButton("Remove Transition"))
            trRemove = static_cast<int>(i);
        ImGui::Separator();
        ImGui::PopID();
    }
    if (trRemove >= 0) {
        asset.transitions.erase(asset.transitions.begin() + trRemove);
        changed = true;
    }
    if (ImGui::SmallButton("+ Transition")) {
        std::string a = asset.states.empty() ? "A" : asset.states[0].name;
        std::string b = asset.states.size() > 1 ? asset.states[1].name : a;
        asset.transitions.push_back({ a, b, 0.15f, 0.9f, false, {} });
        changed = true;
    }
}

} // namespace

bool CreateAndAssignControllerFromModel(EditorApp* editor, AnimatorComponent& animator,
                                        const std::string& modelProjectPath,
                                        SkinnedMeshRendererComponent* skinned) {
    if (modelProjectPath.empty())
        return false;

    if (skinned) {
        skinned->SetModelFile(modelProjectPath);
        if (!skinned->texture)
            skinned->texture = std::make_shared<Texture>(Texture::CreateCheckerboard(128, 16));
    }

    auto model = AssetManager::Get().GetSkeletalModel(modelProjectPath);
    if (!model) {
        MIPSYNC_WARN("Create controller: model not loaded: {}", modelProjectPath);
        return false;
    }
    if (model->animationNames.empty()) {
        MIPSYNC_WARN("Create controller: no animation clips on {}", modelProjectPath);
        return false;
    }

    auto asset = CreateDefaultControllerForModel(*model);
    asset->sourceModelPath = modelProjectPath;
    for (auto& st : asset->states)
        st.clipSourceModelPath = modelProjectPath;
    SyncControllerStateClipStacks(*asset);
    const std::string rel = DefaultControllerRelForModel(modelProjectPath);
    std::string err;
    if (!SaveAnimatorController(AssetManager::Get().ToAbsolute(rel), *asset, err)) {
        MIPSYNC_WARN("Create controller save failed: {}", err);
        return false;
    }

    AssetManager::Get().DropAnimatorController(rel);
    animator.modelPath = modelProjectPath;
    animator.controllerPath = rel;
    animator.ReloadAssets();
    if (editor)
        editor->RecordUndoSnapshot();
    if (editor)
        editor->OpenAnimatorControllerWindow(rel);
    MIPSYNC_INFO("Created animator controller: {}", rel);
    return true;
}

void DrawAnimatorControllerEditor(EditorApp* editor, const std::string& projectRelPath) {
    static std::string loadedPath;
    static std::shared_ptr<AnimatorControllerAsset> asset;
    static bool loadFailed = false;

    if (loadedPath != projectRelPath) {
        loadedPath = projectRelPath;
        loadFailed = false;
        asset.reset();
        std::string err;
        asset = LoadAnimatorController(AssetManager::Get().ToAbsolute(projectRelPath), err);
        if (!asset) {
            MIPSYNC_WARN("Animator controller load failed: {}", err);
            loadFailed = true;
        } else if (SyncControllerStateClipStacks(*asset)) {
            // Persist clip-stack repairs once when opening (not every inspector frame).
            Scene* scene = editor ? &editor->GetEngine()->GetScene() : nullptr;
            CommitAnimatorController(projectRelPath, *asset, scene);
        }
    }

    if (loadFailed || !asset) {
        ImGui::TextColored(EditorTheme::Error, "Failed to load animator controller.");
        return;
    }

    const size_t slash = projectRelPath.find_last_of('/');
    const char* fileName = slash == std::string::npos
        ? projectRelPath.c_str()
        : projectRelPath.c_str() + slash + 1;

    static bool showParameters = true;
    ImGui::TextColored(EditorTheme::TextSecondary, "Base Layer");
    ImGui::SameLine();
    ImGui::TextColored(EditorTheme::TextMuted, ">  %s", fileName);
    ImGui::SameLine();
    const char* parameterToggle = showParameters ? "Hide Parameters" : "Show Parameters";
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
                                 ImGui::GetWindowContentRegionMax().x -
                                     ImGui::CalcTextSize(parameterToggle).x - 18.0f));
    if (ImGui::SmallButton(parameterToggle))
        showParameters = !showParameters;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s", projectRelPath.c_str());
    ImGui::Separator();

    SyncControllerStateClipStacks(*asset);
    bool userChanged = false;
    std::vector<std::string> availableClips;
    DrawAnimatorClipSourceHint(editor, projectRelPath, *asset, availableClips);
    std::shared_ptr<SkeletalModelAsset> sourceModel;
    if (!asset->sourceModelPath.empty())
        sourceModel = AssetManager::Get().GetSkeletalModel(asset->sourceModelPath);
    const float bodyH = std::max(360.0f, ImGui::GetContentRegionAvail().y);
    constexpr float kParamSidebarW = 188.0f;

    if (showParameters) {
        ImGui::BeginChild("AnimatorParamRail", ImVec2(kParamSidebarW, bodyH), true);
        DrawAnimatorParametersSidebar(*asset, userChanged);
        ImGui::EndChild();
        ImGui::SameLine();
    }
    ImGui::BeginChild("AnimatorMainRail", ImVec2(0.0f, bodyH), false);
    DrawAnimatorControllerGraphEditor(editor, projectRelPath, asset, availableClips, userChanged);
    ImGui::EndChild();

    Scene* scene = editor ? &editor->GetEngine()->GetScene() : nullptr;
    if (userChanged)
        CommitAnimatorController(projectRelPath, *asset, scene);
}

} // namespace MipsyncEngine
