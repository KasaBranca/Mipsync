#pragma once

#include <imgui.h>
#include <glm/glm.hpp>

namespace MipsyncEngine {

class Camera;
class TransformComponent;

namespace EditorCameraGizmo {

bool WorldToScreen(const glm::vec3& worldPos, const glm::mat4& view, const glm::mat4& proj,
                   const ImVec2& rectMin, const ImVec2& rectSize, ImVec2& out);

void DrawWorldLine(ImDrawList* drawList, const glm::vec3& a, const glm::vec3& b,
                   const glm::mat4& view, const glm::mat4& proj,
                   const ImVec2& rectMin, const ImVec2& rectSize, ImU32 color, float thickness = 1.5f);

void DrawFrustum(const Camera& camera, const TransformComponent& transform, float aspect,
                 const glm::mat4& view, const glm::mat4& proj,
                 const ImVec2& rectMin, const ImVec2& rectSize, ImDrawList* drawList);

} // namespace EditorCameraGizmo
} // namespace MipsyncEngine
