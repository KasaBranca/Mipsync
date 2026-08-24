#pragma once

#include <imgui.h>
#include <glm/glm.hpp>

namespace MipsyncEngine {

class Scene;
class Entity;
class Camera;

namespace EditorCanvasGizmo {

/// Walks up the hierarchy to find the Canvas on or above `entity`.
Entity* FindCanvasEntity(Scene& scene, Entity* entity);

/// Draws Unity-style canvas bounds (root rect + child UI rects when selected).
/// Unity-style: canvas wireframes in 3D in front of the scene view camera (layout = game view resolution).
void DrawSceneCanvases(Scene& scene, const Camera& editorSceneCamera, const glm::mat4& view,
                       const glm::mat4& proj, const ImVec2& rectMin, const ImVec2& rectSize,
                       ImDrawList* drawList, uint32_t selectedEntityId, int layoutViewportWidth,
                       int layoutViewportHeight);

} // namespace EditorCanvasGizmo
} // namespace MipsyncEngine
