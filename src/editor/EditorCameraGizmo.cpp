#include "EditorCameraGizmo.h"
#include "../renderer/Camera.h"
#include "../scene/Scene.h"
#include <array>
#include <cmath>
#include <cstdio>

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
                 const ImVec2& rectMin, const ImVec2& rectSize, ImDrawList* drawList,
                 bool isSelected) {
    std::array<glm::vec3, 8> localCorners{};
    BuildFrustumCorners(camera.fov, aspect, camera.nearClip, camera.farClip, localCorners);

    const glm::mat4 worldMatrix = glm::translate(glm::mat4(1.0f), transform.position) * GetCameraRotationMatrix(transform);
    std::array<glm::vec3, 8> worldCorners{};
    for (size_t i = 0; i < worldCorners.size(); ++i)
        worldCorners[i] = TransformPoint(worldMatrix, localCorners[i]);

    const glm::vec3 eye = transform.position;

    // Distinctive color scheme for PS1 Camera Frustum & Far Clip
    const ImU32 bodyColor = isSelected ? IM_COL32(255, 255, 255, 180) : IM_COL32(180, 180, 180, 80);
    const ImU32 nearColor = isSelected ? IM_COL32(255, 220, 80, 240)  : IM_COL32(200, 180, 80, 100);
    const ImU32 farColor  = isSelected ? IM_COL32(60, 210, 255, 255)  : IM_COL32(60, 160, 220, 100);
    const ImU32 farFillColor = isSelected ? IM_COL32(40, 180, 255, 30) : IM_COL32(40, 140, 200, 12);
    const ImU32 nearFillColor = isSelected ? IM_COL32(255, 220, 80, 35) : IM_COL32(200, 180, 80, 12);
    const float lineThick = isSelected ? 1.5f : 1.0f;
    const float farThick  = isSelected ? 2.5f : 1.2f;

    // Frustum connecting rays from camera origin to far corners
    for (int i = 0; i < 4; ++i) {
        DrawWorldLine(drawList, eye, worldCorners[i], view, proj, rectMin, rectSize, bodyColor, lineThick);
        DrawWorldLine(drawList, worldCorners[i], worldCorners[i + 4], view, proj, rectMin, rectSize, bodyColor, lineThick);
    }

    // Near plane rectangle & fill
    ImVec2 sn[4];
    bool allNearVisible = true;
    for (int i = 0; i < 4; ++i) {
        const int next = (i + 1) % 4;
        DrawWorldLine(drawList, worldCorners[i], worldCorners[next], view, proj, rectMin, rectSize, nearColor, isSelected ? 2.0f : 1.0f);
        if (!WorldToScreen(worldCorners[i], view, proj, rectMin, rectSize, sn[i]))
            allNearVisible = false;
    }
    if (allNearVisible) {
        drawList->AddQuadFilled(sn[0], sn[1], sn[2], sn[3], nearFillColor);
    }

    // Far plane rectangle, diagonal cross X, and fill
    ImVec2 sf[4];
    bool allFarVisible = true;
    for (int i = 0; i < 4; ++i) {
        const int next = (i + 1) % 4;
        DrawWorldLine(drawList, worldCorners[i + 4], worldCorners[next + 4], view, proj, rectMin, rectSize, farColor, farThick);
        if (!WorldToScreen(worldCorners[i + 4], view, proj, rectMin, rectSize, sf[i]))
            allFarVisible = false;
    }

    if (isSelected) {
        // Far clip diagonal cross (shows cutoff plane)
        DrawWorldLine(drawList, worldCorners[4], worldCorners[6], view, proj, rectMin, rectSize, IM_COL32(60, 210, 255, 140), 1.2f);
        DrawWorldLine(drawList, worldCorners[5], worldCorners[7], view, proj, rectMin, rectSize, IM_COL32(60, 210, 255, 140), 1.2f);
    }

    if (allFarVisible) {
        drawList->AddQuadFilled(sf[0], sf[1], sf[2], sf[3], farFillColor);
    }

    // Far clip text badge on screen when selected
    if (isSelected) {
        glm::vec3 farCenter = (worldCorners[4] + worldCorners[5] + worldCorners[6] + worldCorners[7]) * 0.25f;
        farCenter += glm::vec3(worldMatrix * glm::vec4(0.0f, localCorners[6].y * 0.15f, 0.0f, 0.0f));
        ImVec2 textPos;
        if (WorldToScreen(farCenter, view, proj, rectMin, rectSize, textPos)) {
            char label[64];
            snprintf(label, sizeof(label), "Far Clip: %.1f m", camera.farClip);
            const ImVec2 textSize = ImGui::CalcTextSize(label);
            const ImVec2 pad(6.0f, 3.0f);
            const ImVec2 boxMin(textPos.x - textSize.x * 0.5f - pad.x, textPos.y - textSize.y * 0.5f - pad.y);
            const ImVec2 boxMax(textPos.x + textSize.x * 0.5f + pad.x, textPos.y + textSize.y * 0.5f + pad.y);
            drawList->AddRectFilled(boxMin, boxMax, IM_COL32(20, 30, 45, 210), 4.0f);
            drawList->AddRect(boxMin, boxMax, IM_COL32(60, 210, 255, 220), 4.0f, 0, 1.0f);
            drawList->AddText(ImVec2(boxMin.x + pad.x, boxMin.y + pad.y), IM_COL32(180, 235, 255, 255), label);
        }
    }
}

void DrawDistanceCullGizmo(const glm::vec3& center, float radius,
                           const glm::mat4& view, const glm::mat4& proj,
                           const ImVec2& rectMin, const ImVec2& rectSize, ImDrawList* drawList,
                           bool isSelected) {
    if (radius <= 0.01f)
        return;

    const int segments = 36;
    const float step = (2.0f * 3.14159265358979323846f) / segments;
    const ImU32 ringColor = isSelected ? IM_COL32(255, 145, 30, 240) : IM_COL32(230, 140, 30, 90);
    const float thickness = isSelected ? 2.0f : 1.2f;

    // Ring 1: XZ plane (Horizontal ring)
    for (int i = 0; i < segments; ++i) {
        const float t0 = i * step;
        const float t1 = (i + 1) * step;
        const glm::vec3 p0 = center + glm::vec3(std::cos(t0) * radius, 0.0f, std::sin(t0) * radius);
        const glm::vec3 p1 = center + glm::vec3(std::cos(t1) * radius, 0.0f, std::sin(t1) * radius);
        DrawWorldLine(drawList, p0, p1, view, proj, rectMin, rectSize, ringColor, thickness);
    }

    // Ring 2: XY plane (Vertical ring 1)
    for (int i = 0; i < segments; ++i) {
        const float t0 = i * step;
        const float t1 = (i + 1) * step;
        const glm::vec3 p0 = center + glm::vec3(std::cos(t0) * radius, std::sin(t0) * radius, 0.0f);
        const glm::vec3 p1 = center + glm::vec3(std::cos(t1) * radius, std::sin(t1) * radius, 0.0f);
        DrawWorldLine(drawList, p0, p1, view, proj, rectMin, rectSize, ringColor, thickness);
    }

    // Ring 3: YZ plane (Vertical ring 2)
    for (int i = 0; i < segments; ++i) {
        const float t0 = i * step;
        const float t1 = (i + 1) * step;
        const glm::vec3 p0 = center + glm::vec3(0.0f, std::sin(t0) * radius, std::cos(t0) * radius);
        const glm::vec3 p1 = center + glm::vec3(0.0f, std::sin(t1) * radius, std::cos(t1) * radius);
        DrawWorldLine(drawList, p0, p1, view, proj, rectMin, rectSize, ringColor, thickness);
    }

    // Distance Label Badge on top
    if (isSelected) {
        const glm::vec3 topPoint = center + glm::vec3(0.0f, radius * 1.02f, 0.0f);
        ImVec2 textPos;
        if (WorldToScreen(topPoint, view, proj, rectMin, rectSize, textPos)) {
            char label[64];
            snprintf(label, sizeof(label), "Cull Distance: %.1f m", radius);
            const ImVec2 textSize = ImGui::CalcTextSize(label);
            const ImVec2 pad(6.0f, 3.0f);
            const ImVec2 boxMin(textPos.x - textSize.x * 0.5f - pad.x, textPos.y - textSize.y - pad.y * 2.0f);
            const ImVec2 boxMax(textPos.x + textSize.x * 0.5f + pad.x, textPos.y);
            drawList->AddRectFilled(boxMin, boxMax, IM_COL32(35, 22, 12, 220), 4.0f);
            drawList->AddRect(boxMin, boxMax, IM_COL32(255, 145, 30, 240), 4.0f, 0, 1.2f);
            drawList->AddText(ImVec2(boxMin.x + pad.x, boxMin.y + pad.y), IM_COL32(255, 210, 150, 255), label);
        }
    }
}

} // namespace EditorCameraGizmo
} // namespace MipsyncEngine
