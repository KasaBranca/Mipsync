#pragma once

#include <imgui.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <unordered_set>

namespace MipsyncEngine {

class Scene;
class Entity;

namespace EditorColliderGizmo {

/// Draws wireframe colliders in the Scene View (Unity-style).
void DrawSceneColliders(Scene& scene, const glm::mat4& view, const glm::mat4& proj,
                        const ImVec2& rectMin, const ImVec2& rectSize, ImDrawList* drawList,
                        const std::unordered_set<uint32_t>& selectedEntityIds);

} // namespace EditorColliderGizmo
} // namespace MipsyncEngine
