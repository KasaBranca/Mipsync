#include "EditorIcons.h"
#include "EditorTheme.h"
#include <cmath>

namespace MipsyncEngine {

static void DrawArrowHead(ImDrawList* dl, ImVec2 tip, ImVec2 dir, float length, ImU32 color) {
    float len = sqrtf(dir.x * dir.x + dir.y * dir.y);
    if (len < 0.001f) return;
    dir.x /= len;
    dir.y /= len;

    ImVec2 side{ -dir.y, dir.x };
    ImVec2 base{ tip.x - dir.x * length, tip.y - dir.y * length };
    dl->AddTriangleFilled(tip, { base.x + side.x * length * 0.45f, base.y + side.y * length * 0.45f },
                          { base.x - side.x * length * 0.45f, base.y - side.y * length * 0.45f }, color);
}

void DrawGizmoIcon(GizmoIcon icon, ImDrawList* drawList, ImVec2 center, float size, ImU32 color) {
    const float s = size * 0.5f;
    const float thick = 1.8f;

    switch (icon) {
    case GizmoIcon::Translate: {
        // Unity-style 4-way move arrows
        drawList->AddLine({ center.x - s, center.y }, { center.x + s, center.y }, color, thick);
        drawList->AddLine({ center.x, center.y - s }, { center.x, center.y + s }, color, thick);
        DrawArrowHead(drawList, { center.x + s, center.y }, { 1, 0 }, s * 0.35f, color);
        DrawArrowHead(drawList, { center.x - s, center.y }, { -1, 0 }, s * 0.35f, color);
        DrawArrowHead(drawList, { center.x, center.y - s }, { 0, -1 }, s * 0.35f, color);
        DrawArrowHead(drawList, { center.x, center.y + s }, { 0, 1 }, s * 0.35f, color);
        break;
    }
    case GizmoIcon::Rotate: {
        // Circular rotate arrow
        const float r = s * 0.82f;
        drawList->PathClear();
        drawList->PathArcTo(center, r, -0.2f, 5.2f, 24);
        drawList->PathStroke(color, 0, thick);

        float ax = center.x + r * cosf(5.2f);
        float ay = center.y + r * sinf(5.2f);
        float tx = -sinf(5.2f);
        float ty = cosf(5.2f);
        DrawArrowHead(drawList, { ax, ay }, { tx, ty }, s * 0.38f, color);
        break;
    }
    case GizmoIcon::Scale: {
        // Box with corner scale handles
        ImVec2 tl{ center.x - s * 0.65f, center.y - s * 0.65f };
        ImVec2 br{ center.x + s * 0.65f, center.y + s * 0.65f };
        drawList->AddRect(tl, br, color, 0.0f, 0, thick);

        auto corner = [&](float cx, float cy, float dx, float dy) {
            drawList->AddLine({ cx, cy }, { cx + dx * s * 0.55f, cy + dy * s * 0.55f }, color, thick);
            DrawArrowHead(drawList, { cx + dx * s * 0.55f, cy + dy * s * 0.55f }, { dx, dy }, s * 0.28f, color);
        };
        corner(tl.x, tl.y, -1, -1);
        corner(br.x, tl.y, 1, -1);
        corner(tl.x, br.y, -1, 1);
        corner(br.x, br.y, 1, 1);
        break;
    }
    }
}

bool DrawGizmoIconButton(GizmoIcon icon, ImGuizmo::OPERATION operation, ImGuizmo::OPERATION current, ImGuizmo::OPERATION& selected) {
    const ImVec2 buttonSize{ 32.0f, 28.0f };
    const bool active = current == operation;

    ImGui::PushID(static_cast<int>(operation));

    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.28f, 0.28f, 0.30f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.32f, 0.32f, 0.34f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.35f, 0.35f, 0.37f, 1.0f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.26f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.30f, 0.31f, 0.9f));
    }

    const bool pressed = ImGui::Button("##gizmo", buttonSize);

    ImVec2 rectMin = ImGui::GetItemRectMin();
    ImVec2 rectMax = ImGui::GetItemRectMax();
    ImVec2 center{ (rectMin.x + rectMax.x) * 0.5f, (rectMin.y + rectMax.y) * 0.5f };

    ImU32 iconColor = active ? ImGui::ColorConvertFloat4ToU32(EditorTheme::Accent)
                             : ImGui::ColorConvertFloat4ToU32(EditorTheme::TextPrimary);
    DrawGizmoIcon(icon, ImGui::GetWindowDrawList(), center, 14.0f, iconColor);

    ImGui::PopStyleColor(3);
    ImGui::PopID();

    if (pressed)
        selected = operation;

    ImGui::SameLine();
    return pressed;
}

} // namespace MipsyncEngine
