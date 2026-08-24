#include "EditorCameraGizmo.h"
#include "../renderer/Camera.h"
#include "../scene/Scene.h"
#include <array>
#include <cmath>

namespace MipsyncEngine {
namespace EditorCameraGizmo {

namespace {

glm::mat4 GetCameraRotationMatrix(const TransformComponent& transform) {
    return TransformComponent::RotationMatrixFromEuler(transform.rotation);
}

glm::vec3 TransformPoint(const glm::mat4& matrix, const glm::vec3& point) {
    return glm::vec3(matrix * glm::vec4(point, 1.0f));
}

void BuildFrustumCorners(float fovDeg, float aspect, float nearPlane, float farPlane,
                         std::array<glm::vec3, 8>& corners) {
    const auto halfExtents = [](float distance, float fov, float aspectRatio) {
        const float halfHeight = std::tan(glm::radians(fov * 0.5f)) * distance;
        return glm::vec2(halfHeight * aspectRatio, halfHeight);
    };

    const glm::vec2 nearHalf = halfExtents(nearPlane, fovDeg, aspect);
    const glm::vec2 farHalf = halfExtents(farPlane, fovDeg, aspect);

    corners[0] = { -nearHalf.x, -nearHalf.y, -nearPlane };
    corners[1] = {  nearHalf.x, -nearHalf.y, -nearPlane };
    corners[2] = {  nearHalf.x,  nearHalf.y, -nearPlane };
    corners[3] = { -nearHalf.x,  nearHalf.y, -nearPlane };
    corners[4] = { -farHalf.x, -farHalf.y, -farPlane };
    corners[5] = {  farHalf.x, -farHalf.y, -farPlane };
    corners[6] = {  farHalf.x,  farHalf.y, -farPlane };
    corners[7] = { -farHalf.x,  farHalf.y, -farPlane };
}

} // namespace

bool WorldToScreen(const glm::vec3& worldPos, const glm::mat4& view, const glm::mat4& proj,
                   const ImVec2& rectMin, const ImVec2& rectSize, ImVec2& out) {
    const glm::vec4 clipPos = proj * view * glm::vec4(worldPos, 1.0f);
    if (clipPos.w <= 0.0001f)
        return false;

    const glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
    out.x = rectMin.x + (ndc.x * 0.5f + 0.5f) * rectSize.x;
    out.y = rectMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * rectSize.y;
    return true;
}

void DrawWorldLine(ImDrawList* drawList, const glm::vec3& a, const glm::vec3& b,
                   const glm::mat4& view, const glm::mat4& proj,
                   const ImVec2& rectMin, const ImVec2& rectSize, ImU32 color, float thickness) {
    ImVec2 sa, sb;
    if (WorldToScreen(a, view, proj, rectMin, rectSize, sa) &&
        WorldToScreen(b, view, proj, rectMin, rectSize, sb)) {
        drawList->AddLine(sa, sb, color, thickness);
    }
}

void DrawFrustum(const Camera& camera, const TransformComponent& transform, float aspect,
                 const glm::mat4& view, const glm::mat4& proj,
                 const ImVec2& rectMin, const ImVec2& rectSize, ImDrawList* drawList) {
    std::array<glm::vec3, 8> localCorners{};
    BuildFrustumCorners(camera.fov, aspect, camera.nearClip, camera.farClip, localCorners);

    const glm::mat4 worldMatrix = glm::translate(glm::mat4(1.0f), transform.position) * GetCameraRotationMatrix(transform);
    std::array<glm::vec3, 8> worldCorners{};
    for (size_t i = 0; i < worldCorners.size(); ++i)
        worldCorners[i] = TransformPoint(worldMatrix, localCorners[i]);

    const glm::vec3 eye = transform.position;
    const ImU32 bodyColor = IM_COL32(255, 255, 255, 210);
    const ImU32 nearColor = IM_COL32(255, 220, 80, 230);
    const ImU32 farColor = IM_COL32(160, 200, 255, 160);

    for (int i = 0; i < 4; ++i)
        DrawWorldLine(drawList, eye, worldCorners[i], view, proj, rectMin, rectSize, bodyColor, 1.5f);

    for (int i = 0; i < 4; ++i) {
        const int next = (i + 1) % 4;
        DrawWorldLine(drawList, worldCorners[i], worldCorners[next], view, proj, rectMin, rectSize, nearColor, 2.0f);
        DrawWorldLine(drawList, worldCorners[i + 4], worldCorners[next + 4], view, proj, rectMin, rectSize, farColor, 1.0f);
        DrawWorldLine(drawList, worldCorners[i], worldCorners[i + 4], view, proj, rectMin, rectSize, bodyColor, 1.0f);
    }
}

} // namespace EditorCameraGizmo
} // namespace MipsyncEngine
