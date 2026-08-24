#include "EditorApp.h"

#include "AssetBrowserPanel.h"
#include "EditorTheme.h"
#include "../assets/AssetManager.h"
#include "../animation/AnimatorControllerIO.h"
#include "../animation/AnimationTypes.h"
#include "../animation/SkeletalModel.h"
#include "../core/Engine.h"
#include "../core/Log.h"
#include "../scene/Scene.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace MipsyncEngine {

namespace {

using json = nlohmann::json;

std::string SafeFileStem(std::string s) {
    if (s.empty())
        s = "NewAnimation";
    for (char& c : s) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (!std::isalnum(uc) && c != '_' && c != '-')
            c = '_';
    }
    return s;
}

json Vec3ToJson(const glm::vec3& v) {
    return json::array({ v.x, v.y, v.z });
}

glm::vec3 Vec3FromJson(const json& j, const glm::vec3& fallback) {
    if (!j.is_array() || j.size() < 3)
        return fallback;
    return {
        j[0].get<float>(),
        j[1].get<float>(),
        j[2].get<float>(),
    };
}

int FindKeyIndex(const std::vector<EditorApp::AnimationKeyframe>& keys, int frame) {
    for (size_t i = 0; i < keys.size(); ++i) {
        if (keys[i].frame == frame)
            return static_cast<int>(i);
    }
    return -1;
}

void SortKeys(std::vector<EditorApp::AnimationKeyframe>& keys) {
    std::sort(keys.begin(), keys.end(),
              [](const auto& a, const auto& b) { return a.frame < b.frame; });
}

int LastKeyFrame(const std::vector<EditorApp::AnimationKeyframe>& keys) {
    int last = 0;
    for (const auto& key : keys)
        last = std::max(last, key.frame);
    return last;
}

int ClipEndFrame(const EditorApp::AnimationWindowState& state) {
    return std::max(1, LastKeyFrame(state.keys));
}

int AnimationWindowPlaybackEndFrame(const EditorApp::AnimationWindowState& state) {
    return state.clipReadOnly ? std::max(1, state.lengthFrames) : ClipEndFrame(state);
}

// The dope sheet is a fixed, very large virtual canvas. Its width never
// depends on the current playhead or the last key, so the timeline does not
// visibly grow only after the user reaches its previous end.
constexpr int kTimelineVirtualEndFrame = 1'000'000;

bool NearlyEqualVec3(const glm::vec3& a, const glm::vec3& b, float epsilon = 0.0001f) {
    return std::abs(a.x - b.x) <= epsilon
        && std::abs(a.y - b.y) <= epsilon
        && std::abs(a.z - b.z) <= epsilon;
}

void CaptureRecordBaseline(EditorApp::AnimationWindowState& state,
                           uint32_t entityId,
                           const TransformComponent& transform) {
    state.recordBaselineValid = true;
    state.recordBaselineEntityId = entityId;
    state.recordBaselinePosition = transform.position;
    state.recordBaselineRotation = transform.rotation;
    state.recordBaselineScale = transform.scale;
}

bool TransformChangedSinceRecordBaseline(const EditorApp::AnimationWindowState& state,
                                         uint32_t entityId,
                                         const TransformComponent& transform) {
    if (!state.recordBaselineValid || state.recordBaselineEntityId != entityId)
        return true;
    return !NearlyEqualVec3(state.recordBaselinePosition, transform.position)
        || !NearlyEqualVec3(state.recordBaselineRotation, transform.rotation)
        || !NearlyEqualVec3(state.recordBaselineScale, transform.scale);
}

std::string ReplaceExtension(const std::string& projectRelPath, const char* ext) {
    std::filesystem::path p = PathUtf8::FromString(projectRelPath);
    p.replace_extension(ext);
    return PathUtf8::ToString(p);
}

std::string EntityDisplayName(Entity* entity) {
    if (!entity)
        return "NewAnimation";
    if (auto* tag = entity->GetComponent<TagComponent>(); tag && !tag->tag.empty())
        return tag->tag;
    return "Entity_" + std::to_string(entity->GetID());
}

void EnsureClipPath(EditorApp::AnimationWindowState& state, Entity* entity) {
    if (!state.clipPath.empty())
        return;

    std::string stem = state.clipName;
    if (stem.empty() || stem == "New Animation")
        stem = EntityDisplayName(entity) + "_Animation";
    stem = SafeFileStem(stem);
    state.clipName = stem;

    std::string rel = "assets/animations/" + stem + ".nanim";
    if (std::filesystem::exists(PathUtf8::FromString(AssetManager::Get().ToAbsolute(rel)))) {
        for (int i = 1; i < 1000; ++i) {
            rel = "assets/animations/" + stem + "_" + std::to_string(i) + ".nanim";
            if (!std::filesystem::exists(PathUtf8::FromString(AssetManager::Get().ToAbsolute(rel))))
                break;
        }
    }
    state.clipPath = rel;
}

glm::vec3 LerpVec3(const glm::vec3& a, const glm::vec3& b, float t) {
    return a + (b - a) * t;
}

struct AnimationControllerClipChoice {
    std::string label;
    std::string stateName;
    std::string clipName;
    std::string sourcePath;
    int clipStackIndex = -1;
};

std::shared_ptr<AnimatorControllerAsset> ResolveAnimationWindowController(
    AnimatorComponent* animator) {
    if (!animator)
        return {};
    if (animator->controller)
        return animator->controller;
    if (animator->controllerPath.empty())
        return {};
    std::string error;
    return AssetManager::Get().GetAnimatorController(animator->controllerPath, error);
}

std::vector<AnimationControllerClipChoice> CollectAnimationWindowClips(
    AnimatorComponent* animator,
    const std::shared_ptr<AnimatorControllerAsset>& controller) {
    std::vector<AnimationControllerClipChoice> result;
    if (!animator || !controller)
        return result;

    for (const AnimatorStateDef& animatorState : controller->states) {
        if (animatorState.clipName.empty())
            continue;

        AnimationControllerClipChoice choice;
        choice.stateName = animatorState.name;
        choice.clipName = animatorState.clipName;
        choice.clipStackIndex = animatorState.clipStackIndex;
        choice.sourcePath = animatorState.clipSourceModelPath;
        if (choice.sourcePath.empty())
            choice.sourcePath = controller->sourceModelPath;
        if (choice.sourcePath.empty())
            choice.sourcePath = animator->modelPath;

        const bool duplicate = std::any_of(result.begin(), result.end(), [&](const auto& existing) {
            return existing.clipName == choice.clipName &&
                   existing.sourcePath == choice.sourcePath &&
                   existing.clipStackIndex == choice.clipStackIndex;
        });
        if (duplicate)
            continue;

        choice.label = choice.clipName;
        if (!choice.stateName.empty() && choice.stateName != choice.clipName)
            choice.label += "  (" + choice.stateName + ")";
        result.push_back(std::move(choice));
    }
    return result;
}

} // namespace

void EditorApp::OpenAnimationWindow() {
    m_ShowAnimationWindow = true;
    FocusDockedWindow("Animation");
}

void EditorApp::OpenAnimationClipWindow(const std::string& projectRelPath) {
    if (!projectRelPath.empty())
        LoadAnimationClipForWindow(projectRelPath);
    if (Entity* selected = GetSelectedEntity())
        m_AnimationWindow.followedSelectionEntityId = selected->GetID();
    else
        m_AnimationWindow.followedSelectionEntityId = 0;
    OpenAnimationWindow();
}

bool EditorApp::LoadAnimationClipForWindow(const std::string& projectRelPath) {
    if (projectRelPath.empty())
        return false;

    const std::string abs = AssetManager::Get().ToAbsolute(projectRelPath);
    std::ifstream file(PathUtf8::FromString(abs));
    if (!file.is_open()) {
        MIPSYNC_WARN("Animation Window: failed to open {}", projectRelPath);
        return false;
    }

    json j;
    try {
        file >> j;
    } catch (const std::exception& e) {
        MIPSYNC_WARN("Animation Window: invalid .nanim {}: {}", projectRelPath, e.what());
        return false;
    }

    auto& state = m_AnimationWindow;
    state.clipPath = projectRelPath;
    state.clipSourcePath = projectRelPath;
    state.clipName = j.value("name", PathUtf8::ToString(PathUtf8::FromString(projectRelPath).stem()));
    state.controllerStateName.clear();
    state.clipReadOnly = false;
    state.fps = std::max(1, j.value("fps", 30));
    state.lengthFrames = std::max(1, j.value("lengthFrames", 60));
    state.frame = std::max(0, j.value("frame", 0));
    state.targetEntityId = j.value("targetEntityId", state.targetEntityId);
    state.recordBaselineValid = false;
    state.selectedKeyFrame = -1;
    state.keys.clear();

    if (j.contains("keys") && j["keys"].is_array()) {
        for (const json& keyJson : j["keys"]) {
            AnimationKeyframe key;
            key.frame = std::max(0, keyJson.value("frame", 0));
            key.position = Vec3FromJson(keyJson.value("position", json::array()), { 0.0f, 0.0f, 0.0f });
            key.rotation = Vec3FromJson(keyJson.value("rotation", json::array()), { 0.0f, 0.0f, 0.0f });
            key.scale = Vec3FromJson(keyJson.value("scale", json::array()), { 1.0f, 1.0f, 1.0f });
            state.keys.push_back(key);
        }
    }
    SortKeys(state.keys);
    state.lengthFrames = ClipEndFrame(state);
    state.preview = true;
    state.playing = false;
    return true;
}

bool EditorApp::SaveAnimationClipFromWindow(std::string* outError) {
    auto& state = m_AnimationWindow;
    if (state.clipReadOnly) {
        if (outError) *outError = "Imported animation clips are read-only.";
        return false;
    }
    Entity* target = m_Engine ? m_Engine->GetScene().FindEntity(state.targetEntityId) : nullptr;
    if (!target)
        target = GetSelectedEntity();
    if (target)
        state.targetEntityId = target->GetID();
    EnsureClipPath(state, target);
    if (state.clipPath.empty()) {
        if (outError) *outError = "No clip path.";
        return false;
    }

    std::filesystem::path abs = PathUtf8::FromString(AssetManager::Get().ToAbsolute(state.clipPath));
    std::error_code ec;
    std::filesystem::create_directories(abs.parent_path(), ec);

    json j;
    j["type"] = "MipsyncAnimationClip";
    j["version"] = 1;
    j["name"] = state.clipName;
    j["fps"] = state.fps;
    state.lengthFrames = ClipEndFrame(state);
    j["lengthFrames"] = state.lengthFrames;
    j["targetEntityId"] = state.targetEntityId;
    j["tracks"] = json::array({ { { "path", "" }, { "type", "Transform" } } });
    j["keys"] = json::array();
    for (const auto& key : state.keys) {
        j["keys"].push_back({
            { "frame", key.frame },
            { "position", Vec3ToJson(key.position) },
            { "rotation", Vec3ToJson(key.rotation) },
            { "scale", Vec3ToJson(key.scale) },
        });
    }

    std::ofstream file(abs, std::ios::trunc);
    if (!file.is_open()) {
        if (outError) *outError = "Failed to write " + state.clipPath;
        return false;
    }
    file << j.dump(2) << '\n';
    if (m_AssetBrowser) {
        m_AssetBrowser->Refresh();
        m_AssetBrowser->RevealAndSelectAsset(state.clipPath);
    }
    EnsureAnimatorForAnimationWindowClip();
    return true;
}

void EditorApp::EnsureAnimatorForAnimationWindowClip() {
    auto& state = m_AnimationWindow;
    if (!m_Engine || state.clipPath.empty() || state.targetEntityId == 0)
        return;

    Entity* entity = m_Engine->GetScene().FindEntity(state.targetEntityId);
    if (!entity)
        return;

    const std::string controllerRel = ReplaceExtension(state.clipPath, ".ncontroller");
    const std::string controllerAbs = AssetManager::Get().ToAbsolute(controllerRel);
    const std::string clipName = state.clipName.empty()
        ? PathUtf8::ToString(PathUtf8::FromString(state.clipPath).stem())
        : state.clipName;

    AnimatorControllerAsset ctrl;
    ctrl.defaultState = clipName;
    ctrl.sourceModelPath = state.clipPath;
    AnimatorStateDef st;
    st.name = clipName;
    st.clipName = clipName;
    st.clipSourceModelPath = state.clipPath;
    st.clipStackIndex = -1;
    st.speed = 1.0f;
    st.loop = true;
    st.graphPosition = glm::vec2(120.0f, 80.0f);
    ctrl.states.push_back(st);

    std::string error;
    if (!SaveAnimatorController(controllerAbs, ctrl, error)) {
        MIPSYNC_WARN("Animation Window: failed to save controller {}: {}", controllerRel, error);
        return;
    }
    AssetManager::Get().DropAnimatorController(controllerRel);

    AnimatorComponent* animator = entity->GetComponent<AnimatorComponent>();
    if (!animator)
        animator = &entity->AddComponent<AnimatorComponent>();
    animator->controllerPath = controllerRel;
    animator->currentState = clipName;
    animator->speed = 1.0f;
    animator->ReloadAssets();

    if (m_AssetBrowser)
        m_AssetBrowser->Refresh();
}

void EditorApp::AddAnimationWindowKeyAtCurrentFrame() {
    auto* entity = m_Engine ? m_Engine->GetScene().FindEntity(m_AnimationWindow.targetEntityId) : nullptr;
    if (!entity)
        entity = GetSelectedEntity();
    if (!entity)
        return;
    auto* tr = entity->GetComponent<TransformComponent>();
    if (!tr)
        return;

    m_AnimationWindow.targetEntityId = entity->GetID();
    EnsureClipPath(m_AnimationWindow, entity);
    AnimationKeyframe key;
    key.frame = std::max(0, m_AnimationWindow.frame);
    key.position = tr->position;
    key.rotation = tr->rotation;
    key.scale = tr->scale;

    const int existing = FindKeyIndex(m_AnimationWindow.keys, key.frame);
    if (existing >= 0)
        m_AnimationWindow.keys[static_cast<size_t>(existing)] = key;
    else
        m_AnimationWindow.keys.push_back(key);
    SortKeys(m_AnimationWindow.keys);
    m_AnimationWindow.selectedKeyFrame = key.frame;
    m_AnimationWindow.lengthFrames = ClipEndFrame(m_AnimationWindow);
    CaptureRecordBaseline(m_AnimationWindow, entity->GetID(), *tr);
    std::string err;
    if (!SaveAnimationClipFromWindow(&err) && !err.empty())
        MIPSYNC_WARN("Animation Window: {}", err);
}

void EditorApp::DeleteAnimationWindowKeyAtCurrentFrame() {
    auto& state = m_AnimationWindow;
    auto& keys = state.keys;
    const int frame = state.selectedKeyFrame >= 0 ? state.selectedKeyFrame : state.frame;
    keys.erase(std::remove_if(keys.begin(), keys.end(),
                              [frame](const AnimationKeyframe& k) { return k.frame == frame; }),
               keys.end());
    state.selectedKeyFrame = -1;
    state.lengthFrames = ClipEndFrame(state);
}

void EditorApp::SampleAnimationWindowToEntity() {
    auto& state = m_AnimationWindow;
    if (state.keys.empty() || !m_Engine)
        return;
    Entity* entity = m_Engine->GetScene().FindEntity(state.targetEntityId);
    if (!entity)
        return;
    auto* tr = entity->GetComponent<TransformComponent>();
    if (!tr)
        return;

    SortKeys(state.keys);
    const int frame = std::max(0, state.frame);
    const AnimationKeyframe* prev = &state.keys.front();
    const AnimationKeyframe* next = &state.keys.back();
    for (const auto& key : state.keys) {
        if (key.frame <= frame)
            prev = &key;
        if (key.frame >= frame) {
            next = &key;
            break;
        }
    }

    float t = 0.0f;
    if (next->frame != prev->frame)
        t = static_cast<float>(frame - prev->frame) / static_cast<float>(next->frame - prev->frame);
    tr->position = LerpVec3(prev->position, next->position, t);
    tr->rotation = LerpVec3(prev->rotation, next->rotation, t);
    tr->scale = LerpVec3(prev->scale, next->scale, t);
    SyncPhysicsAfterTransformEdit(entity);
    if (state.record)
        CaptureRecordBaseline(state, entity->GetID(), *tr);
}

void EditorApp::DrawAnimationPanel() {
    if (!m_ShowAnimationWindow)
        return;
    if (!ImGui::Begin("Animation", &m_ShowAnimationWindow)) {
        ImGui::End();
        return;
    }

    auto& state = m_AnimationWindow;
    Entity* selected = GetSelectedEntity();
    AnimatorComponent* selectedAnimator = selected
        ? selected->GetComponent<AnimatorComponent>()
        : nullptr;
    const auto selectedController = ResolveAnimationWindowController(selectedAnimator);
    const auto controllerClips = CollectAnimationWindowClips(selectedAnimator, selectedController);

    auto selectControllerClip = [&](const AnimationControllerClipChoice& choice) {
        state.playing = false;
        state.record = false;
        state.recordBaselineValid = false;
        state.frame = 0;
        state.selectedKeyFrame = -1;

        std::string extension = PathUtf8::ToString(
            PathUtf8::FromString(choice.sourcePath).extension());
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension == ".nanim" && LoadAnimationClipForWindow(choice.sourcePath)) {
            state.targetEntityId = selected ? selected->GetID() : state.targetEntityId;
            state.controllerStateName = choice.stateName;
            state.clipSourcePath = choice.sourcePath;
            state.clipReadOnly = false;
            return;
        }

        state.targetEntityId = selected ? selected->GetID() : 0;
        state.clipPath.clear();
        state.clipSourcePath = choice.sourcePath;
        state.clipName = choice.clipName;
        state.controllerStateName = choice.stateName;
        state.clipReadOnly = true;
        state.keys.clear();
        state.fps = selectedAnimator
            ? std::max(1, static_cast<int>(std::lround(selectedAnimator->animationFps)))
            : 30;
        state.lengthFrames = 60;

        if (!choice.sourcePath.empty()) {
            if (auto model = AssetManager::Get().GetSkeletalModel(choice.sourcePath)) {
                const double duration = choice.clipStackIndex >= 0
                    ? model->GetClipDurationByStackIndex(choice.clipStackIndex)
                    : model->GetClipDuration(choice.clipName);
                if (duration > 0.0)
                    state.lengthFrames = std::max(1, static_cast<int>(std::ceil(duration * state.fps)));
            }
        }
    };

    const uint32_t selectedId = selected ? selected->GetID() : 0;
    const std::string selectedControllerSignature = selectedAnimator
        ? selectedAnimator->controllerPath + "|" + selectedAnimator->modelPath
        : std::string{};
    const bool selectionChanged = state.followedSelectionEntityId != selectedId ||
                                  state.followedControllerPath != selectedControllerSignature;
    if (selectionChanged) {
        state.followedSelectionEntityId = selectedId;
        state.followedControllerPath = selectedControllerSignature;
        state.targetEntityId = selectedId;
        state.playing = false;
        state.record = false;
        state.recordBaselineValid = false;
        state.selectedKeyFrame = -1;

        if (!selected) {
            state.clipPath.clear();
            state.clipSourcePath.clear();
            state.clipName = "New Animation";
            state.controllerStateName.clear();
            state.clipReadOnly = false;
            state.keys.clear();
            state.frame = 0;
        } else if (!controllerClips.empty()) {
            std::string preferredState;
            if (selectedAnimator && !selectedAnimator->currentState.empty())
                preferredState = selectedAnimator->currentState;
            else if (selectedController)
                preferredState = selectedController->defaultState;
            auto preferred = std::find_if(controllerClips.begin(), controllerClips.end(),
                [&](const auto& choice) { return choice.stateName == preferredState; });
            selectControllerClip(preferred != controllerClips.end()
                ? *preferred
                : controllerClips.front());
        } else {
            state.clipPath.clear();
            state.clipSourcePath.clear();
            state.clipName = "New Animation";
            state.controllerStateName.clear();
            state.clipReadOnly = false;
            state.keys.clear();
            state.frame = 0;
        }
    }

    Entity* target = selected;

    const char* targetName = target && target->GetComponent<TagComponent>()
        ? target->GetComponent<TagComponent>()->tag.c_str()
        : "None";

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, 3));
    ImGui::SetNextItemWidth(220.0f);
    const char* clipPreview = state.clipName.empty() ? "<No Animation Clip>" : state.clipName.c_str();
    if (ImGui::BeginCombo("##AnimationClipDropdown", clipPreview)) {
        if (controllerClips.empty()) {
            ImGui::TextDisabled("No clips in Animator Controller");
        } else {
            for (const auto& choice : controllerClips) {
                const bool isSelected = state.controllerStateName == choice.stateName &&
                                        state.clipName == choice.clipName;
                if (ImGui::Selectable(choice.label.c_str(), isSelected))
                    selectControllerClip(choice);
                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
        if (!state.clipSourcePath.empty())
            ImGui::SetTooltip("%s", state.clipSourcePath.c_str());
        else
            ImGui::SetTooltip("Animation clips attached to the selected object's Animator");
    }
    ImGui::SameLine();
    if (ImGui::Button("Preview"))
        state.preview = !state.preview;
    if (state.preview) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.40f, 0.75f, 1.0f, 1.0f), "On");
    }
    ImGui::SameLine();
    const ImVec4 recordColor = state.record ? ImVec4(1.0f, 0.18f, 0.14f, 1.0f)
                                            : ImVec4(0.80f, 0.18f, 0.16f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, recordColor);
    ImGui::BeginDisabled(state.clipReadOnly || !target);
    if (ImGui::Button(state.record ? "●" : "○", ImVec2(28, 0))) {
        state.record = !state.record;
        if (state.record) {
            state.preview = true;
            // Recording always has an explicit initial pose at frame zero.
            // Preserve an existing frame-zero key when editing an old clip.
            if (FindKeyIndex(state.keys, 0) < 0) {
                const int previousFrame = state.frame;
                state.frame = 0;
                AddAnimationWindowKeyAtCurrentFrame();
                state.frame = previousFrame;
            } else if (target) {
                if (auto* tr = target->GetComponent<TransformComponent>())
                    CaptureRecordBaseline(state, target->GetID(), *tr);
            }
        } else {
            state.recordBaselineValid = false;
        }
    }
    ImGui::EndDisabled();
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Record");
    ImGui::SameLine();
    enum class AnimationTransportIcon { First, Previous, PlayPause, Next, Last };
    auto animationTransportButton = [&](const char* id, AnimationTransportIcon icon,
                                        bool active = false) {
        constexpr float width = 30.0f;
        constexpr float height = 22.0f;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(id, ImVec2(width, height));
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        const bool pressed = ImGui::IsItemClicked();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 min(pos.x + 1.0f, pos.y + 1.0f);
        const ImVec2 max(pos.x + width - 1.0f, pos.y + height - 1.0f);
        ImVec4 face = active ? UiTokens::Success : UiTokens::BgTertiary;
        if (held)
            face = UiTokens::BgPressed;
        else if (hovered && !active)
            face = UiTokens::BgHover;
        draw->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(face), 2.0f);
        draw->AddRect(min, max, ImGui::ColorConvertFloat4ToU32(UiTokens::BorderHighlight), 2.0f);

        const ImU32 color = ImGui::ColorConvertFloat4ToU32(UiTokens::Icon);
        const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        auto drawLeftTriangle = [&]() {
            draw->AddTriangleFilled(ImVec2(center.x + 3.0f, center.y - 4.5f),
                                    ImVec2(center.x + 3.0f, center.y + 4.5f),
                                    ImVec2(center.x - 3.0f, center.y), color);
        };
        auto drawRightTriangle = [&]() {
            draw->AddTriangleFilled(ImVec2(center.x - 3.0f, center.y - 4.5f),
                                    ImVec2(center.x - 3.0f, center.y + 4.5f),
                                    ImVec2(center.x + 3.0f, center.y), color);
        };

        switch (icon) {
        case AnimationTransportIcon::First:
            draw->AddRectFilled(ImVec2(center.x - 5.0f, center.y - 5.0f),
                                ImVec2(center.x - 3.0f, center.y + 5.0f), color);
            drawLeftTriangle();
            break;
        case AnimationTransportIcon::Previous:
            drawLeftTriangle();
            break;
        case AnimationTransportIcon::PlayPause:
            if (state.playing) {
                draw->AddRectFilled(ImVec2(center.x - 4.0f, center.y - 5.0f),
                                    ImVec2(center.x - 1.5f, center.y + 5.0f), color);
                draw->AddRectFilled(ImVec2(center.x + 1.5f, center.y - 5.0f),
                                    ImVec2(center.x + 4.0f, center.y + 5.0f), color);
            } else {
                drawRightTriangle();
            }
            break;
        case AnimationTransportIcon::Next:
            drawRightTriangle();
            break;
        case AnimationTransportIcon::Last:
            drawRightTriangle();
            draw->AddRectFilled(ImVec2(center.x + 3.0f, center.y - 5.0f),
                                ImVec2(center.x + 5.0f, center.y + 5.0f), color);
            break;
        }
        return pressed;
    };

    if (animationTransportButton("##AnimationFirst", AnimationTransportIcon::First)) {
        state.frame = 0;
        state.playing = false;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("First frame");
    ImGui::SameLine(0.0f, 3.0f);
    if (animationTransportButton("##AnimationPrevious", AnimationTransportIcon::Previous)) {
        state.frame = std::max(0, state.frame - 1);
        state.playing = false;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Previous frame");
    ImGui::SameLine(0.0f, 3.0f);
    if (animationTransportButton("##AnimationPlayPause", AnimationTransportIcon::PlayPause,
                                 state.playing))
        state.playing = !state.playing;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(state.playing ? "Pause" : "Play");
    ImGui::SameLine(0.0f, 3.0f);
    if (animationTransportButton("##AnimationNext", AnimationTransportIcon::Next)) {
        state.frame += 1;
        state.playing = false;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Next frame");
    ImGui::SameLine(0.0f, 3.0f);
    if (animationTransportButton("##AnimationLast", AnimationTransportIcon::Last)) {
        state.frame = AnimationWindowPlaybackEndFrame(state);
        state.playing = false;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Last key frame");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(72);
    ImGui::DragInt("Frame", &state.frame, 0.2f, 0, 1000000);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(58);
    ImGui::DragInt("FPS", &state.fps, 0.1f, 1, 120);
    ImGui::SameLine();
    state.frame = std::max(0, state.frame);
    if (!state.clipReadOnly)
        state.lengthFrames = ClipEndFrame(state);
    ImGui::TextDisabled("End %d", state.lengthFrames);
    ImGui::PopStyleVar();

    const double now = ImGui::GetTime();
    if (state.playing && state.preview && state.fps > 0) {
        const int clipEnd = AnimationWindowPlaybackEndFrame(state);
        if (state.lastTick <= 0.0)
            state.lastTick = now;
        const double step = 1.0 / static_cast<double>(state.fps);
        if (now - state.lastTick >= step) {
            const int advance = std::max(1, static_cast<int>((now - state.lastTick) / step));
            state.frame += advance;
            while (state.frame > clipEnd)
                state.frame -= clipEnd + 1;
            state.lastTick = now;
        }
    } else {
        state.lastTick = now;
    }

    ImGui::Separator();

    if (!target) {
        const float availW = ImGui::GetContentRegionAvail().x;
        const float y = ImGui::GetCursorPosY() + 46.0f;
        ImGui::SetCursorPosY(y);
        ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::TextSecondary);
        ImGui::SetCursorPosX(std::max(0.0f, (availW - 430.0f) * 0.5f));
        ImGui::TextUnformatted("Select a GameObject to begin animating.");
        ImGui::SetCursorPosX(std::max(0.0f, (availW - 520.0f) * 0.5f));
        ImGui::TextUnformatted("Use Create to make an Animation Clip for the selected object.");
        ImGui::PopStyleColor();
        ImGui::End();
        return;
    }

    ImGui::Text("Clip");
    ImGui::SameLine(58);
    char nameBuf[128]{};
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", state.clipName.c_str());
    ImGui::SetNextItemWidth(210);
    ImGui::BeginDisabled(state.clipReadOnly);
    if (ImGui::InputText("##clipName", nameBuf, sizeof(nameBuf)))
        state.clipName = nameBuf;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Create")) {
        const std::string stem = SafeFileStem(state.clipName.empty() ? targetName : state.clipName);
        state.clipName = stem;
        state.clipPath = "assets/animations/" + stem + ".nanim";
        state.clipSourcePath = state.clipPath;
        state.controllerStateName.clear();
        state.clipReadOnly = false;
        state.keys.clear();
        state.selectedKeyFrame = -1;
        state.frame = 0;
        if (target)
            state.targetEntityId = target->GetID();
        AddAnimationWindowKeyAtCurrentFrame();
        std::string err;
        if (!SaveAnimationClipFromWindow(&err) && !err.empty())
            MIPSYNC_WARN("Animation Window: {}", err);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(state.clipReadOnly);
    if (ImGui::Button("Save")) {
        std::string err;
        if (!SaveAnimationClipFromWindow(&err) && !err.empty())
            MIPSYNC_WARN("Animation Window: {}", err);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", state.clipReadOnly
        ? "Imported clip (read-only)"
        : (state.clipPath.empty() ? "No clip" : state.clipPath.c_str()));

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DragDrop::kAssetMove)) {
            const auto paths = DragDrop::ParseAssetMovePayload(payload->Data,
                                                               static_cast<size_t>(payload->DataSize));
            if (!paths.empty()) {
                const std::string& rel = paths.front();
                if (PathUtf8::FromString(rel).extension() == ".nanim" &&
                    LoadAnimationClipForWindow(rel)) {
                    state.targetEntityId = target->GetID();
                    state.followedSelectionEntityId = target->GetID();
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::Text("Target");
    ImGui::SameLine(58);
    ImGui::TextUnformatted(targetName);
    ImGui::SameLine();
    if (selected && ImGui::Button("Use Selection")) {
        state.targetEntityId = selected->GetID();
        state.recordBaselineValid = false;
        if (state.keys.empty())
            AddAnimationWindowKeyAtCurrentFrame();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(state.clipReadOnly);
    if (ImGui::Button("Add Key"))
        AddAnimationWindowKeyAtCurrentFrame();
    ImGui::SameLine();
    if (ImGui::Button("Delete Key")) {
        DeleteAnimationWindowKeyAtCurrentFrame();
        std::string err;
        if (!SaveAnimationClipFromWindow(&err) && !err.empty())
            MIPSYNC_WARN("Animation Window: {}", err);
    }
    ImGui::EndDisabled();

    if (state.record && target) {
        if (auto* tr = target->GetComponent<TransformComponent>()) {
            if (TransformChangedSinceRecordBaseline(state, target->GetID(), *tr))
                AddAnimationWindowKeyAtCurrentFrame();
        }
    }
    if (state.preview)
        SampleAnimationWindowToEntity();

    ImGui::Separator();

    const float leftW = 210.0f;
    const float rowH = 24.0f;
    const float pixelsPerFrame = 8.0f;
    const float timelineW = kTimelineVirtualEndFrame * pixelsPerFrame + 40.0f;

    if (ImGui::BeginChild("AnimationDopesheet", ImVec2(0, 0), true,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        // Use the child window's draw list so its inner clip rectangle keeps
        // the virtual timeline behind the ruler and both scrollbars.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float timelineX = start.x + leftW;
        ImGui::TextDisabled("Properties");
        ImGui::SameLine(leftW);
        ImGui::TextDisabled("Dopesheet");

        const float rulerY = ImGui::GetCursorScreenPos().y + 2.0f;
        const bool hasPlayableRange = state.clipReadOnly ? state.lengthFrames > 0
                                                         : !state.keys.empty();
        const int activeEndFrame = state.clipReadOnly ? state.lengthFrames
                                                       : LastKeyFrame(state.keys);
        const float activeTimelineW = hasPlayableRange
            ? (static_cast<float>(activeEndFrame) + 1.0f) * pixelsPerFrame
            : 0.0f;
        dl->AddRectFilled(ImVec2(timelineX, rulerY),
                          ImVec2(timelineX + timelineW, rulerY + 22.0f),
                          IM_COL32(58, 58, 58, 255));
        if (hasPlayableRange) {
            dl->AddRectFilled(ImVec2(timelineX, rulerY),
                              ImVec2(timelineX + activeTimelineW, rulerY + 22.0f),
                              IM_COL32(35, 35, 35, 255));
        }
        const float scrollX = ImGui::GetScrollX();
        const float visibleWidth = ImGui::GetWindowWidth();
        const int visibleFirstFrame = std::max(0, static_cast<int>(std::floor(
            (scrollX - leftW - 24.0f) / pixelsPerFrame)));
        const int visibleLastFrame = std::min(kTimelineVirtualEndFrame, static_cast<int>(std::ceil(
            (scrollX + visibleWidth - leftW + 24.0f) / pixelsPerFrame)));
        const int tickStep = std::max(1, state.fps / 2);
        const int firstTick = (visibleFirstFrame / tickStep) * tickStep;
        for (int f = firstTick; f <= visibleLastFrame; f += tickStep) {
            const float x = timelineX + f * pixelsPerFrame;
            dl->AddLine(ImVec2(x, rulerY), ImVec2(x, rulerY + 116.0f),
                        IM_COL32(80, 80, 80, 120), 1.0f);
            dl->AddText(ImVec2(x + 2.0f, rulerY), IM_COL32(170, 170, 170, 255),
                        std::to_string(f).c_str());
        }

        const ImVec2 scrubMin(timelineX, rulerY);
        const ImVec2 scrubSize(timelineW, 132.0f);

        int clickedKeyFrame = -1;
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            for (const auto& key : state.keys) {
                const float keyX = timelineX + key.frame * pixelsPerFrame;
                if (std::abs(mouse.x - keyX) > 7.0f)
                    continue;
                for (int row = 1; row <= 3; ++row) {
                    const float keyY = start.y + 28.0f + row * rowH + rowH * 0.5f;
                    if (std::abs(mouse.y - keyY) <= 8.0f) {
                        clickedKeyFrame = key.frame;
                        break;
                    }
                }
                if (clickedKeyFrame >= 0)
                    break;
            }
        }

        ImGui::SetCursorScreenPos(scrubMin);
        ImGui::InvisibleButton("##AnimationTimelineScrub", scrubSize,
                               ImGuiButtonFlags_MouseButtonLeft);
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (clickedKeyFrame >= 0) {
            state.selectedKeyFrame = clickedKeyFrame;
            state.frame = clickedKeyFrame;
            state.playing = false;
        } else if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const float localX = ImGui::GetIO().MousePos.x - timelineX;
            const int newFrame = static_cast<int>(std::round(localX / pixelsPerFrame));
            state.frame = std::clamp(newFrame, 0, kTimelineVirtualEndFrame);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                state.selectedKeyFrame = -1;
            state.playing = false;
        }

        auto drawRow = [&](const char* label, int row) {
            const float y = start.y + 28.0f + row * rowH;
            dl->AddRectFilled(ImVec2(start.x, y), ImVec2(start.x + leftW - 6, y + rowH - 2),
                              row % 2 ? IM_COL32(50, 50, 50, 255) : IM_COL32(44, 44, 44, 255));
            dl->AddText(ImVec2(start.x + 10.0f, y + 5.0f), IM_COL32(220, 220, 220, 255), label);
            dl->AddRectFilled(ImVec2(timelineX, y), ImVec2(timelineX + timelineW, y + rowH - 2),
                              row % 2 ? IM_COL32(56, 56, 56, 255) : IM_COL32(52, 52, 52, 255));
            if (hasPlayableRange) {
                dl->AddRectFilled(ImVec2(timelineX, y),
                                  ImVec2(timelineX + activeTimelineW, y + rowH - 2),
                                  row % 2 ? IM_COL32(38, 38, 38, 255)
                                          : IM_COL32(34, 34, 34, 255));
            }
        };
        drawRow("▾ Transform", 0);
        drawRow("   Position", 1);
        drawRow("   Rotation", 2);
        drawRow("   Scale", 3);

        for (const auto& key : state.keys) {
            const float x = timelineX + key.frame * pixelsPerFrame;
            for (int row = 1; row <= 3; ++row) {
                const float y = start.y + 28.0f + row * rowH + rowH * 0.5f;
                ImVec2 pts[4] = {
                    { x, y - 5.0f }, { x + 5.0f, y }, { x, y + 5.0f }, { x - 5.0f, y }
                };
                const bool selectedKey = key.frame == state.selectedKeyFrame;
                const bool onFrame = key.frame == state.frame;
                dl->AddConvexPolyFilled(pts, 4, selectedKey ? IM_COL32(255, 235, 120, 255)
                                              : onFrame ? IM_COL32(255, 215, 80, 255)
                                                        : IM_COL32(245, 180, 55, 255));
                dl->AddPolyline(pts, 4,
                                selectedKey ? IM_COL32(255, 255, 235, 255)
                                            : IM_COL32(40, 30, 10, 255),
                                ImDrawFlags_Closed, selectedKey ? 2.0f : 1.0f);
            }
        }

        // Draw the playhead last so row backgrounds and the active-range fill
        // can never cover it.
        const float playX = timelineX + state.frame * pixelsPerFrame;
        dl->AddLine(ImVec2(playX, rulerY), ImVec2(playX, rulerY + 132.0f),
                    IM_COL32(255, 80, 60, 255), 2.0f);

        ImGui::Dummy(ImVec2(leftW + timelineW, 28.0f + rowH * 5.0f));
    }
    ImGui::EndChild();

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::GetIO().WantTextInput) {
        const ImGuiIO& io = ImGui::GetIO();
        const bool shortcutModifier = io.KeyCtrl || io.KeySuper;

        if (shortcutModifier && ImGui::IsKeyPressed(ImGuiKey_C, false) &&
            state.selectedKeyFrame >= 0) {
            const int selectedIndex = FindKeyIndex(state.keys, state.selectedKeyFrame);
            if (selectedIndex >= 0) {
                state.keyClipboard = state.keys[static_cast<size_t>(selectedIndex)];
                state.keyClipboardValid = true;
            }
        }

        if (shortcutModifier && ImGui::IsKeyPressed(ImGuiKey_V, false) &&
            state.keyClipboardValid && !state.clipReadOnly) {
            AnimationKeyframe pasted = state.keyClipboard;
            pasted.frame = std::max(0, state.frame);
            const int existingIndex = FindKeyIndex(state.keys, pasted.frame);
            if (existingIndex >= 0)
                state.keys[static_cast<size_t>(existingIndex)] = pasted;
            else
                state.keys.push_back(pasted);
            SortKeys(state.keys);
            state.selectedKeyFrame = pasted.frame;
            state.lengthFrames = ClipEndFrame(state);

            std::string err;
            if (!SaveAnimationClipFromWindow(&err) && !err.empty())
                MIPSYNC_WARN("Animation Window: {}", err);
        }

        if (!state.clipReadOnly && state.selectedKeyFrame >= 0 &&
            (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))) {
            DeleteAnimationWindowKeyAtCurrentFrame();
            std::string err;
            if (!SaveAnimationClipFromWindow(&err) && !err.empty())
                MIPSYNC_WARN("Animation Window: {}", err);
        }
    }

    ImGui::End();
}

} // namespace MipsyncEngine
