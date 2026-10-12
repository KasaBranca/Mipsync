#pragma once
#include "EditorSurface.h"

namespace MipsyncEngine::EditorSurface {
enum class ReferenceAction { None, Select, Pick };

// One hit rectangle remains available for drag/drop and the context menu.
// The existing reference is selected through its label; the right-hand icon
// always opens the picker. Empty fields open the picker over their entire face.
inline ReferenceAction ObjectReferenceButton(const char* display, bool hasReference,
                                              float width = -1.0f, ImTextureID icon = 0,
                                              ImU32 tint = 0) {
    const bool pressed = ImGui::Button("##object_reference", ImVec2(width, 0.0f));
    const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    ImGui::RenderTextClipped(ImVec2(min.x + 5.0f, min.y), ImVec2(max.x - 20.0f, max.y),
                             display, nullptr, nullptr, ImVec2(0.0f, 0.5f));
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 center(max.x - 10.0f, (min.y + max.y) * 0.5f);
    const ImU32 color = tint ? tint : ImGui::GetColorU32(ImGuiCol_Text);
    if (icon) {
        const ImRect rect = CenteredReferenceIcon(min, max);
        // Crop the existing object-reference PNG by UV, preserving the file.
        constexpr ImVec2 uv0(10.0f / 258.0f, 1.0f - 64.0f / 324.0f);
        constexpr ImVec2 uv1(247.0f / 258.0f, 1.0f - 301.0f / 324.0f);
        draw->AddImage(icon, rect.Min, rect.Max, uv0, uv1, color);
    } else {
        draw->AddCircle(center, 4.0f, color, 12, 1.0f);
        draw->AddLine(ImVec2(center.x - 3.0f, center.y), ImVec2(center.x + 3.0f, center.y), color);
        draw->AddLine(ImVec2(center.x, center.y - 3.0f), ImVec2(center.x, center.y + 3.0f), color);
    }
    if (!pressed) return ReferenceAction::None;
    const bool clickedSelector = ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
        ImGui::GetIO().MouseClickedPos[ImGuiMouseButton_Left].x >= max.x - 20.0f;
    return !hasReference || clickedSelector ? ReferenceAction::Pick : ReferenceAction::Select;
}
} // namespace MipsyncEngine::EditorSurface
