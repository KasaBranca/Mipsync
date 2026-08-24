#pragma once

#include <glm/glm.hpp>
#include <imgui.h>
#include <vector>

namespace MipsyncEngine {

class Scene;
class Entity;
class Camera;

namespace EditorUISnap {

struct GuideLine {
    bool vertical = true; // true = constant layout X, false = constant layout Y
    float layoutCoord = 0.0f;
};

/// Snap pivot in canvas layout space; fills active guides for the current drag.
glm::vec2 SnapPivotLayout(Scene& scene, Entity& entity, glm::vec2 desiredPivotLayout, int layoutWidth,
                          int layoutHeight);

const std::vector<GuideLine>& GetActiveGuides();
void ClearActiveGuides();

void DrawActiveGuides(Scene& scene, Entity& entity, const Camera& editorCamera, int layoutWidth,
                      int layoutHeight, float viewportAspect, const ImVec2& imageMin,
                      const ImVec2& imageSize, ImDrawList* drawList);

} // namespace EditorUISnap
} // namespace MipsyncEngine
