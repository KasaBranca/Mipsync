#pragma once
// ─────────────────────────────────────────────────
// Mipsync Engine — Editor Toolbar Icons
// ─────────────────────────────────────────────────

#include <imgui.h>
#include <ImGuizmo.h>

namespace MipsyncEngine {

enum class GizmoIcon {
    Translate,
    Rotate,
    Scale
};

void DrawGizmoIcon(GizmoIcon icon, ImDrawList* drawList, ImVec2 center, float size, ImU32 color);

bool DrawGizmoIconButton(GizmoIcon icon, ImGuizmo::OPERATION operation, ImGuizmo::OPERATION current, ImGuizmo::OPERATION& selected);

} // namespace MipsyncEngine
