#include "EditorAnimatorDebug.h"
#include "EditorTheme.h"
#include "../animation/AnimatorRuntime.h"
#include "../animation/AnimationTypes.h"
#include "../animation/SkeletalModel.h"
#include "../scene/Scene.h"
#include <imgui.h>
#include <cmath>
#include <unordered_set>

namespace MipsyncEngine {

void DrawAnimatorRuntimeDiagnostics(AnimatorComponent& animator, bool isPlaying) {
    ImGui::Separator();
    ImGui::TextColored(EditorTheme::TextSecondary, "Play Diagnostics");

    if (!isPlaying) {
        ImGui::TextWrapped(
            "Press PLAY, then use this section to tell whether clips are distinct or "
            "transitions are blocked.");
        return;
    }

    if (!animator.controller) {
        ImGui::TextColored(EditorTheme::TextMuted, "Need controller assigned.");
        return;
    }

    const AnimatorStateDef* cur = nullptr;
    for (const auto& st : animator.controller->states) {
        if (st.name == animator.currentState) {
            cur = &st;
            break;
        }
    }

    ImGui::Text("Skinned mesh model: %s",
                animator.modelPath.empty() ? "<none>" : animator.modelPath.c_str());
    if (animator.controller && !animator.controller->sourceModelPath.empty())
        ImGui::Text("Controller source model: %s", animator.controller->sourceModelPath.c_str());

    ImGui::Text("Current state: %s", animator.currentState.empty() ? "<none>"
                                                                   : animator.currentState.c_str());
    if (cur) {
        const auto clipModel = ResolveStateClipModel(animator, *cur);
        const int stackIdx =
            clipModel ? clipModel->ResolveClipStackIndex(cur->clipName, cur->clipStackIndex) : -1;
        const double dur = clipModel && stackIdx >= 0
            ? clipModel->GetClipDurationByStackIndex(stackIdx)
            : (clipModel ? clipModel->GetClipDuration(cur->clipName) : 0.0);
        ImGui::Text("Playing clip id: %s", cur->clipName.c_str());
        ImGui::Text("Eval FBX: %s", ResolveStateClipModelPath(animator, *cur).c_str());
        ImGui::Text("anim_stack #%d  |  duration %.2fs", stackIdx, dur);
        if (!cur->clipName.empty() && cur->clipStackIndex < 0)
            ImGui::TextColored(EditorTheme::Error,
                               "State has no stack index — re-save controller after picking clips.");
        if (!ResolveStateClipModelPath(animator, *cur).empty() &&
            !animator.modelPath.empty() &&
            ResolveStateClipModelPath(animator, *cur) != animator.modelPath) {
            ImGui::TextColored(EditorTheme::PsAccent,
                               "Clip FBX differs from skinned mesh (Mixamo: this is expected).");
        }
    }
    if (animator.inTransition) {
        const AnimatorStateDef* toSt = nullptr;
        for (const auto& st : animator.controller->states) {
            if (st.name == animator.nextState) {
                toSt = &st;
                break;
            }
        }
        const float t = animator.transitionDuration > 0.0f
            ? animator.transitionTime / animator.transitionDuration
            : 1.0f;
        ImGui::TextColored(EditorTheme::LinkCyan, "Transition -> %s (%.0f%%)",
                           animator.nextState.c_str(), t * 100.0f);
        if (toSt) {
            const auto toModel = ResolveStateClipModel(animator, *toSt);
            const int toStack = toModel
                ? toModel->ResolveClipStackIndex(toSt->clipName, toSt->clipStackIndex)
                : -1;
            ImGui::Text("Target clip: %s (stack #%d)", toSt->clipName.c_str(), toStack);
            ImGui::Text("Target FBX: %s", ResolveStateClipModelPath(animator, *toSt).c_str());
        }
    } else {
        ImGui::TextColored(EditorTheme::TextMuted, "Not in transition");
    }

    ImGui::Spacing();
    ImGui::TextColored(EditorTheme::TextSecondary, "Clip map (state vs eval FBX + stack)");
    if (ImGui::BeginTable("##animdiagclips", 5,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("State");
        ImGui::TableSetupColumn("Clip id");
        ImGui::TableSetupColumn("Eval FBX");
        ImGui::TableSetupColumn("Stack#");
        ImGui::TableSetupColumn("Dur");
        ImGui::TableHeadersRow();

        std::unordered_set<std::string> stackKeysUsed;
        bool duplicateStacks = false;
        for (const auto& st : animator.controller->states) {
            const auto clipModel = ResolveStateClipModel(animator, st);
            const int stackIdx =
                clipModel ? clipModel->ResolveClipStackIndex(st.clipName, st.clipStackIndex) : -1;
            const std::string evalPath = ResolveStateClipModelPath(animator, st);
            if (stackIdx >= 0) {
                const std::string key = evalPath + "#" + std::to_string(stackIdx);
                if (!stackKeysUsed.insert(key).second)
                    duplicateStacks = true;
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const bool isCur = st.name == animator.currentState;
            if (isCur)
                ImGui::TextColored(EditorTheme::SuccessLabel, "%s", st.name.c_str());
            else
                ImGui::TextUnformatted(st.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(st.clipName.c_str());
            ImGui::TableNextColumn();
            const char* evalName = evalPath.empty() ? "<mesh model>" : evalPath.c_str();
            ImGui::TextUnformatted(evalName);
            ImGui::TableNextColumn();
            if (st.clipStackIndex >= 0)
                ImGui::Text("%d", stackIdx);
            else
                ImGui::TextColored(EditorTheme::TextMuted, "%d (auto)", stackIdx);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", stackIdx >= 0 && clipModel
                                    ? clipModel->GetClipDurationByStackIndex(stackIdx)
                                    : (clipModel ? clipModel->GetClipDuration(st.clipName) : 0.0));
        }
        ImGui::EndTable();

        if (animator.controller->states.size() >= 2) {
            if (duplicateStacks) {
                ImGui::TextColored(
                    EditorTheme::Error,
                    "Same FBX+stack on multiple states — poses will look identical. "
                    "Re-pick clips in the controller graph (+ Add Animation) and save.");
            } else {
                ImGui::TextColored(
                    EditorTheme::SuccessLabel,
                    "Each state uses a distinct clip source. If motion still fails, check "
                    "transitions below.");
            }
        }
    }

    ImGui::Spacing();
    ImGui::TextColored(EditorTheme::TextSecondary, "Force state (bypass transitions)");
    for (const auto& st : animator.controller->states) {
        ImGui::PushID(st.name.c_str());
        if (ImGui::SmallButton(st.name.c_str()))
            ForceAnimatorState(animator, st.name);
        ImGui::PopID();
        ImGui::SameLine();
    }
    ImGui::NewLine();

    ImGui::Spacing();
    ImGui::TextColored(EditorTheme::TextSecondary, "Outgoing transitions");
    ImGui::TextColored(EditorTheme::TextMuted, "Controller has %zu transition(s), current state '%s'",
                       animator.controller->transitions.size(),
                       animator.currentState.empty() ? "<none>" : animator.currentState.c_str());
    const auto probes = ProbeOutgoingTransitions(animator);
    if (probes.empty()) {
        if (animator.controller->transitions.empty()) {
            ImGui::TextColored(EditorTheme::Error,
                               "No transitions in controller — connect states in the graph editor "
                               "and save.");
        } else {
            size_t outgoingFromCurrent = 0;
            bool brokenFromRef = false;
            for (const auto& tr : animator.controller->transitions) {
                if (tr.fromState == animator.currentState)
                    ++outgoingFromCurrent;
                else {
                    bool fromExists = false;
                    for (const auto& st : animator.controller->states) {
                        if (st.name == tr.fromState) {
                            fromExists = true;
                            break;
                        }
                    }
                    if (!fromExists)
                        brokenFromRef = true;
                }
            }

            if (outgoingFromCurrent == 0 && !brokenFromRef) {
                ImGui::TextColored(
                    EditorTheme::PsAccent,
                    "Current state '%s' has no outgoing transitions (this is normal for a "
                    "one-way link).",
                    animator.currentState.c_str());
                ImGui::TextWrapped(
                    "To switch again, add a transition FROM '%s' in the Animator Controller "
                    "graph (e.g. back to another state), save, and press PLAY.",
                    animator.currentState.c_str());
            } else {
                ImGui::TextColored(EditorTheme::Error,
                                   "No transitions from current state — a transition 'from' name "
                                   "may not match any state. Reopen the controller and save.");
            }

            ImGui::TextColored(EditorTheme::TextMuted, "All transitions in this controller:");
            if (ImGui::BeginTable("##animdiagalltr", 2,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("From");
                ImGui::TableSetupColumn("To");
                ImGui::TableHeadersRow();
                for (const auto& tr : animator.controller->transitions) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    const bool fromCur = tr.fromState == animator.currentState;
                    if (fromCur)
                        ImGui::TextColored(EditorTheme::SuccessLabel, "%s", tr.fromState.c_str());
                    else
                        ImGui::TextUnformatted(tr.fromState.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(tr.toState.c_str());
                }
                ImGui::EndTable();
            }
        }
    } else {
        for (const auto& p : probes) {
            const ImVec4 col =
                p.wouldFire ? EditorTheme::SuccessLabel : EditorTheme::TextSecondary;
            ImGui::BulletText("%s", p.label.c_str());
            ImGui::SameLine();
            ImGui::TextColored(col, "%s", p.detail.c_str());
        }
    }
}

} // namespace MipsyncEngine
