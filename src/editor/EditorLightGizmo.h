#pragma once

#include "../renderer/Camera.h"
#include <imgui.h>
#include <glm/glm.hpp>

namespace MipsyncEngine {

class Scene;

namespace EditorLightGizmo {

void Draw(Scene& scene, const Camera& camera, const ImVec2& imageMin, const ImVec2& imageSize);

} // namespace EditorLightGizmo
} // namespace MipsyncEngine
