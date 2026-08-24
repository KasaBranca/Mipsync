#pragma once
// ─────────────────────────────────────────────────
// Mipsync Engine — Viewport Picking & World Raycast
// ─────────────────────────────────────────────────

#include "../renderer/Camera.h"
#include "../scene/Scene.h"
#include <glm/glm.hpp>

namespace MipsyncEngine {

struct Ray {
    glm::vec3 origin;
    glm::vec3 direction;
};

struct RaycastHit {
    bool hit = false;
    float distance = 0.0f;
    Entity* entity = nullptr;
};

Entity* PickEntityAtPoint(
    Scene& scene,
    const Camera& camera,
    float mouseX, float mouseY,
    float viewportMinX, float viewportMinY,
    float viewportWidth, float viewportHeight,
    int uiLayoutWidth = 0, int uiLayoutHeight = 0);

/// Intersects a screen point with the horizontal plane y = planeY (for asset placement).
glm::vec3 PickPointOnPlane(
    const Camera& camera,
    float mouseX, float mouseY,
    float viewportMinX, float viewportMinY,
    float viewportWidth, float viewportHeight,
    float planeY = 0.0f);

/// World-space ray vs mesh AABBs (first hit along the ray).
RaycastHit RaycastWorld(Scene& scene, const glm::vec3& origin, const glm::vec3& direction,
                        float maxDistance = 1000.0f);

} // namespace MipsyncEngine
