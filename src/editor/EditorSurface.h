#pragma once

#include <imgui_internal.h>
#include <algorithm>
#include <cmath>

namespace MipsyncEngine::EditorSurface {

// Shared by native buttons/foldouts/combos and custom editor surfaces. Only
// the face is shaded; labels, icons and hit rectangles stay untouched.
inline void DrawRaisedFace(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 color,
                           float rounding, ImU32 border = 0, float borderSize = 0,
                           bool pressed = false, float scale = 1.0f,
                           ImDrawFlags corners = ImDrawFlags_RoundCornersAll) {
    if (!draw || max.x <= min.x || max.y <= min.y) return;
    const ImDrawListFlags saved = draw->Flags;
    draw->Flags |= ImDrawListFlags_AntiAliasedLines;
    draw->Flags &= ~ImDrawListFlags_AntiAliasedLinesUseTex;
    draw->PushClipRect(min, max, true);
    const float halfBorder = borderSize * 0.5f;
    const ImVec2 faceMin(min.x + halfBorder, min.y + halfBorder);
    const ImVec2 faceMax(max.x - halfBorder, max.y - halfBorder);
    const float radius = std::max(0.0f, rounding - halfBorder);
    const ImVec4 face = ImGui::ColorConvertU32ToFloat4(color);
    auto shade = [&](float amount) {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(
            std::clamp(face.x + amount, 0.0f, 1.0f),
            std::clamp(face.y + amount, 0.0f, 1.0f),
            std::clamp(face.z + amount, 0.0f, 1.0f), face.w));
    };
    const int start = draw->VtxBuffer.Size;
    draw->AddRectFilled(faceMin, faceMax, color, radius, corners);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(draw, start, draw->VtxBuffer.Size,
        faceMin, ImVec2(faceMin.x, faceMax.y),
        shade(pressed ? -0.025f : 0.065f), shade(pressed ? 0.015f : -0.045f));

    // Continuous rounded contours fade into the sides. No offset rectangle or
    // detached corner strokes: all light/shadow is clipped inside the face.
    const float inset = std::max(0.75f, scale);
    const ImVec2 innerMin(faceMin.x + inset, faceMin.y + inset);
    const ImVec2 innerMax(faceMax.x - inset, faceMax.y - inset);
    if (innerMax.x > innerMin.x && innerMax.y > innerMin.y) {
        const float fadeHeight = std::max(1.0f, (faceMax.y - faceMin.y) * 0.35f);
        auto rim = [&](bool top, ImU32 tint) {
            const int rimStart = draw->VtxBuffer.Size;
            draw->AddRect(innerMin, innerMax, tint, std::max(0.0f, radius - inset),
                          corners, std::max(0.65f, scale));
            for (int i = rimStart; i < draw->VtxBuffer.Size; ++i) {
                auto& vertex = draw->VtxBuffer[i];
                const float distance = top ? vertex.pos.y - innerMin.y : innerMax.y - vertex.pos.y;
                const float opacity = (1.0f - std::clamp(distance / fadeHeight, 0.0f, 1.0f)) * face.w;
                const ImU32 alpha = ImU32(((vertex.col >> IM_COL32_A_SHIFT) & 255u) * opacity);
                vertex.col = (vertex.col & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
            }
        };
        rim(true, pressed ? IM_COL32(0, 0, 0, 45) : IM_COL32(255, 255, 255, 48));
        rim(false, pressed ? IM_COL32(255, 255, 255, 20) : IM_COL32(0, 0, 0, 58));
    }
    if (borderSize > 0 && (border & IM_COL32_A_MASK))
        draw->AddRect(faceMin, faceMax, border, radius, corners, borderSize);
    draw->PopClipRect();
    draw->Flags = saved;
}

inline ImRect CenteredReferenceIcon(const ImVec2& fieldMin, const ImVec2& fieldMax,
                                   float size = 10.0f, float rightInset = 10.0f) {
    const ImVec2 center(fieldMax.x - rightInset, (fieldMin.y + fieldMax.y) * 0.5f);
    return ImRect(ImVec2(center.x - size * 0.5f, center.y - size * 0.5f),
                  ImVec2(center.x + size * 0.5f, center.y + size * 0.5f));
}

inline bool ClearButton(const char* id, float side) {
    const bool pressed = ImGui::Button(id, ImVec2(side, side));
    const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    const float extent = std::max(2.0f, side * 0.14f);
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddLine(ImVec2(center.x - extent, center.y - extent),
                  ImVec2(center.x + extent, center.y + extent), color, 1.0f);
    draw->AddLine(ImVec2(center.x + extent, center.y - extent),
                  ImVec2(center.x - extent, center.y + extent), color, 1.0f);
    return pressed;
}

} // namespace MipsyncEngine::EditorSurface
