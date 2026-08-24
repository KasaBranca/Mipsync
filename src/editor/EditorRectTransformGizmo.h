#pragma once

#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/glm.hpp>

namespace MipsyncEngine {

class Scene;
class Entity;
class Camera;

namespace EditorRectTransformGizmo {

/// Unity-style: move handles on the canvas plane (RectTransform), not at world Transform origin.
bool ShouldUseRectGizmo(Scene& scene, Entity& entity);

bool BuildGizmoMatrix(Scene& scene, Entity& entity, const Camera& editorCamera, int layoutWidth,
                        int layoutHeight, float viewportAspect, glm::mat4& outWorld);

void ApplyGizmoDrag(Scene& scene, Entity& entity, const Camera& editorCamera, int layoutWidth,
                    int layoutHeight, float viewportAspect, ImGuizmo::OPERATION operation,
                    const glm::mat4& originalWorld, const glm::mat4& manipulatedWorld,
                    bool gizmoUsing, bool gizmoStarted);

} // namespace EditorRectTransformGizmo
} // namespace MipsyncEngine
