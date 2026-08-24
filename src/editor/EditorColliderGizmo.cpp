#include "EditorColliderGizmo.h"
#include "EditorCameraGizmo.h"
#include "../physics/ColliderUtils.h"
#include "../scene/Scene.h"
#include <cmath>

namespace MipsyncEngine {
namespace EditorColliderGizmo {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr int kCircleSegments = 32;

glm::vec3 Axis(const ColliderUtils::ColliderWorldPose& pose, int axis) {
    const glm::vec3 local = axis == 0 ? glm::vec3(1, 0, 0) : axis == 1 ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
    return pose.rotation * local;
}

void DrawLine(ImDrawList* drawList, const glm::vec3& a, const glm::vec3& b,
              const glm::mat4& view, const glm::mat4& proj,
              const ImVec2& rectMin, const ImVec2& rectSize, ImU32 color, float thickness) {
    EditorCameraGizmo::DrawWorldLine(drawList, a, b, view, proj, rectMin, rectSize, color, thickness);
}

void DrawCircle(ImDrawList* drawList, const glm::vec3& center, const glm::vec3& axisU,
                const glm::vec3& axisV, const glm::mat4& view, const glm::mat4& proj,
                const ImVec2& rectMin, const ImVec2& rectSize, ImU32 color, float thickness) {
    glm::vec3 prev{};
    bool hasPrev = false;
    for (int i = 0; i <= kCircleSegments; ++i) {
        const float t = (static_cast<float>(i) / kCircleSegments) * 2.0f * kPi;
        const glm::vec3 point = center + std::cos(t) * axisU + std::sin(t) * axisV;
        if (hasPrev)
            DrawLine(drawList, prev, point, view, proj, rectMin, rectSize, color, thickness);
        prev = point;
        hasPrev = true;
    }
}

void DrawArc(ImDrawList* drawList, const glm::vec3& center, const glm::vec3& axisU,
             const glm::vec3& axisV, float startAngle, float endAngle,
             const glm::mat4& view, const glm::mat4& proj,
             const ImVec2& rectMin, const ImVec2& rectSize, ImU32 color, float thickness) {
    const int segments = kCircleSegments / 2;
    glm::vec3 prev{};
    bool hasPrev = false;
    for (int i = 0; i <= segments; ++i) {
        const float t = glm::mix(startAngle, endAngle, static_cast<float>(i) / segments);
        const glm::vec3 point = center + std::cos(t) * axisU + std::sin(t) * axisV;
        if (hasPrev)
            DrawLine(drawList, prev, point, view, proj, rectMin, rectSize, color, thickness);
        prev = point;
        hasPrev = true;
    }
}

ImU32 ColliderColor(const ColliderComponent& col, bool selected) {
    if (col.isTrigger)
        return selected ? IM_COL32(255, 200, 60, 255) : IM_COL32(255, 200, 60, 120);
    return selected ? IM_COL32(80, 220, 90, 255) : IM_COL32(80, 220, 90, 110);
}

float ColliderThickness(bool selected) {
    return selected ? 2.0f : 1.0f;
}

void DrawBox(const ColliderComponent& col, const ColliderUtils::ColliderWorldPose& pose,
             ImDrawList* drawList, const glm::mat4& view, const glm::mat4& proj,
             const ImVec2& rectMin, const ImVec2& rectSize, ImU32 color, float thickness) {
    const glm::vec3 he = col.halfExtents * pose.lossyScale;
    const glm::vec3 ax = Axis(pose, 0) * he.x;
    const glm::vec3 ay = Axis(pose, 1) * he.y;
    const glm::vec3 az = Axis(pose, 2) * he.z;

    glm::vec3 corners[8];
    int idx = 0;
    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sy = -1; sy <= 1; sy += 2) {
            for (int sz = -1; sz <= 1; sz += 2)
                corners[idx++] = pose.center + ax * static_cast<float>(sx)
                               + ay * static_cast<float>(sy)
                               + az * static_cast<float>(sz);
        }
    }

    static constexpr int kEdges[12][2] = {
        {0, 1}, {1, 3}, {3, 2}, {2, 0},
        {4, 5}, {5, 7}, {7, 6}, {6, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7},
    };
    for (const auto& edge : kEdges)
        DrawLine(drawList, corners[edge[0]], corners[edge[1]], view, proj, rectMin, rectSize, color, thickness);
}

void DrawSphere(const ColliderComponent& col, const ColliderUtils::ColliderWorldPose& pose,
                ImDrawList* drawList, const glm::mat4& view, const glm::mat4& proj,
                const ImVec2& rectMin, const ImVec2& rectSize, ImU32 color, float thickness) {
    const float scale = std::max(pose.lossyScale.x, std::max(pose.lossyScale.y, pose.lossyScale.z));
    const float r = col.radius * scale;
    const glm::vec3 ax = Axis(pose, 0) * r;
    const glm::vec3 ay = Axis(pose, 1) * r;
    const glm::vec3 az = Axis(pose, 2) * r;
    DrawCircle(drawList, pose.center, ax, ay, view, proj, rectMin, rectSize, color, thickness);
    DrawCircle(drawList, pose.center, ax, az, view, proj, rectMin, rectSize, color, thickness);
    DrawCircle(drawList, pose.center, ay, az, view, proj, rectMin, rectSize, color, thickness);
}

void DrawCapsule(const ColliderComponent& col, const ColliderUtils::ColliderWorldPose& pose,
                 ImDrawList* drawList, const glm::mat4& view, const glm::mat4& proj,
                 const ImVec2& rectMin, const ImVec2& rectSize, ImU32 color, float thickness) {
    const glm::vec3 up = Axis(pose, 1);
    const glm::vec3 right = Axis(pose, 0);
    const glm::vec3 forward = Axis(pose, 2);

    const float r = col.radius * std::max(pose.lossyScale.x, pose.lossyScale.z);
    const float halfH = (col.capsuleHeight * 0.5f) * pose.lossyScale.y;

    const glm::vec3 topCenter = pose.center + up * halfH;
    const glm::vec3 bottomCenter = pose.center - up * halfH;

    DrawCircle(drawList, topCenter, right * r, forward * r, view, proj, rectMin, rectSize, color, thickness);
    DrawCircle(drawList, bottomCenter, right * r, forward * r, view, proj, rectMin, rectSize, color, thickness);

    for (int i = 0; i < 4; ++i) {
        const float angle = static_cast<float>(i) * kPi * 0.5f;
        const glm::vec3 offset = (right * std::cos(angle) + forward * std::sin(angle)) * r;
        DrawLine(drawList, topCenter + offset, bottomCenter + offset, view, proj, rectMin, rectSize, color, thickness);
    }

    // cos(t) controls the capsule's up axis.  The top cap therefore needs
    // cos(t) >= 0 and the bottom cap cos(t) <= 0.  Using 0..pi here draws a
    // sideways half-circle and makes both spherical ends appear cut away.
    DrawArc(drawList, topCenter, up * r, right * r, -0.5f * kPi, 0.5f * kPi,
            view, proj, rectMin, rectSize, color, thickness);
    DrawArc(drawList, topCenter, up * r, forward * r, -0.5f * kPi, 0.5f * kPi,
            view, proj, rectMin, rectSize, color, thickness);
    DrawArc(drawList, bottomCenter, up * r, right * r, 0.5f * kPi, 1.5f * kPi,
            view, proj, rectMin, rectSize, color, thickness);
    DrawArc(drawList, bottomCenter, up * r, forward * r, 0.5f * kPi, 1.5f * kPi,
            view, proj, rectMin, rectSize, color, thickness);
}

void DrawColliderWireframe(const ColliderComponent& col, const ColliderUtils::ColliderWorldPose& pose,
                           ImDrawList* drawList, const glm::mat4& view, const glm::mat4& proj,
                           const ImVec2& rectMin, const ImVec2& rectSize, ImU32 color, float thickness) {
    switch (col.shape) {
    case ColliderShape::Sphere:
        DrawSphere(col, pose, drawList, view, proj, rectMin, rectSize, color, thickness);
        break;
    case ColliderShape::Capsule:
        DrawCapsule(col, pose, drawList, view, proj, rectMin, rectSize, color, thickness);
        break;
    case ColliderShape::Mesh:
        DrawBox(col, pose, drawList, view, proj, rectMin, rectSize,
                IM_COL32(120, 200, 255, static_cast<int>((color >> 24) & 0xFF)), thickness);
        break;
    case ColliderShape::Box:
    default:
        DrawBox(col, pose, drawList, view, proj, rectMin, rectSize, color, thickness);
        break;
    }
}

} // namespace

void DrawSceneColliders(Scene& scene, const glm::mat4& view, const glm::mat4& proj,
                        const ImVec2& rectMin, const ImVec2& rectSize, ImDrawList* drawList,
                        const std::unordered_set<uint32_t>& selectedEntityIds) {
    for (const auto& entityPtr : scene.GetEntities()) {
        Entity* entity = entityPtr.get();
        if (selectedEntityIds.count(entity->GetID()) == 0)
            continue;
        auto* collider = entity->GetComponent<ColliderComponent>();
        if (!collider || !collider->enabled)
            continue;

        const bool selected = true;
        const auto* rb = entity->GetComponent<RigidbodyComponent>();
        const ColliderUtils::ColliderWorldPose pose =
            (rb && rb->characterController)
                ? ColliderUtils::ComputePhysicsWorldPose(scene, *entity, *collider, rb)
                : ColliderUtils::ComputeWorldPose(scene, *entity, *collider);
        DrawColliderWireframe(
            *collider, pose, drawList, view, proj, rectMin, rectSize,
            ColliderColor(*collider, selected), ColliderThickness(selected));
    }
}

} // namespace EditorColliderGizmo
} // namespace MipsyncEngine
