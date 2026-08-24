#include "EditorLightGizmo.h"
#include "EditorCameraGizmo.h"
#include "../scene/Scene.h"
#include <algorithm>
#include <cmath>

namespace MipsyncEngine {
namespace EditorLightGizmo {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr int kCircleSegments = 24;
constexpr int kDirectionalRayCount = 8;

glm::vec3 LightForward(const glm::mat4& world) {
    return glm::normalize(glm::vec3(world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
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
            EditorCameraGizmo::DrawWorldLine(drawList, prev, point, view, proj, rectMin, rectSize, color,
                                             thickness);
        prev = point;
        hasPrev = true;
    }
}

void DrawDirectionalLightGizmo(ImDrawList* drawList, const glm::vec3& pos, const glm::vec3& forward,
                               const glm::mat4& view, const glm::mat4& proj, const ImVec2& imageMin,
                               const ImVec2& imageSize) {
    const ImU32 shaftColor = IM_COL32(255, 235, 120, 255);
    const ImU32 headColor = IM_COL32(255, 200, 60, 255);
    const ImU32 rayColor = IM_COL32(255, 245, 180, 200);

    glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    if (glm::length(right) < 0.01f)
        right = glm::normalize(glm::cross(forward, glm::vec3(1.0f, 0.0f, 0.0f)));
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));

    // Local -Z = irradiance direction; arrow runs from gizmo origin into the scene (+forward).
    const float shaftLen = 3.5f;
    const glm::vec3 head = pos;
    const glm::vec3 tail = pos + forward * shaftLen;

    EditorCameraGizmo::DrawWorldLine(drawList, head, tail, view, proj, imageMin, imageSize, shaftColor, 4.0f);

    constexpr float diskRadius = 0.35f;
    constexpr float rayLength = 0.25f;
    DrawCircle(drawList, head, right * diskRadius, up * diskRadius,
               view, proj, imageMin, imageSize, headColor, 2.5f);

    // Directional-light rays are short radial marks around the source disk.
    // Long parallel world-space lines overwhelm the scene and do not read as
    // part of the light icon when viewed in perspective.
    for (int i = 0; i < kDirectionalRayCount; ++i) {
        const float angle = (static_cast<float>(i) / kDirectionalRayCount) * 2.0f * kPi;
        const glm::vec3 radial = std::cos(angle) * right + std::sin(angle) * up;
        const glm::vec3 rayStart = head + radial * diskRadius;
        const glm::vec3 rayEnd = head + radial * (diskRadius + rayLength);
        EditorCameraGizmo::DrawWorldLine(drawList, rayStart, rayEnd, view, proj, imageMin, imageSize,
                                         rayColor, 2.0f);
    }
}

} // namespace

void Draw(Scene& scene, const Camera& camera, const ImVec2& imageMin, const ImVec2& imageSize) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const glm::mat4 view = camera.GetViewMatrix();
    const glm::mat4 proj = camera.GetProjectionMatrix();

    for (const auto& entityPtr : scene.GetEntities()) {
        if (!entityPtr) continue;
        auto* light = entityPtr->GetComponent<LightComponent>();
        if (!light || !light->enabled)
            continue;
        auto* transform = entityPtr->GetComponent<TransformComponent>();
        if (!transform) continue;

        const glm::mat4 world = scene.GetWorldMatrix(*entityPtr);
        const glm::vec3 pos = glm::vec3(world[3]);
        const glm::vec3 forward = LightForward(world);

        if (light->type == LightType::Directional) {
            DrawDirectionalLightGizmo(drawList, pos, forward, view, proj, imageMin, imageSize);
            continue;
        }

        ImU32 color = IM_COL32(255, 220, 80, 220);
        if (light->type == LightType::Spot)
            color = IM_COL32(120, 200, 255, 220);

        EditorCameraGizmo::DrawWorldLine(drawList, pos, pos + forward * 0.75f, view, proj, imageMin, imageSize,
                                         color, 2.0f);

        const float r = std::max(light->range, 0.25f);
        DrawCircle(drawList, pos, glm::vec3(r, 0, 0), glm::vec3(0, 0, r), view, proj, imageMin, imageSize,
                   color, 1.5f);
        DrawCircle(drawList, pos, glm::vec3(r, 0, 0), glm::vec3(0, r, 0), view, proj, imageMin, imageSize,
                   color, 1.5f);

        if (light->type == LightType::Spot) {
            const float outerRad = glm::radians(std::clamp(light->spotAngle, 1.0f, 89.0f));
            const float coneLen = std::min(light->range, 4.0f);
            const glm::vec3 end = pos + forward * coneLen;
            glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
            if (glm::length(right) < 0.01f)
                right = glm::normalize(glm::cross(forward, glm::vec3(1, 0, 0)));
            const glm::vec3 up = glm::normalize(glm::cross(right, forward));
            const float tanOuter = std::tan(outerRad);
            EditorCameraGizmo::DrawWorldLine(drawList, pos, end + right * coneLen * tanOuter, view, proj,
                                             imageMin, imageSize, color, 1.5f);
            EditorCameraGizmo::DrawWorldLine(drawList, pos, end - right * coneLen * tanOuter, view, proj,
                                             imageMin, imageSize, color, 1.5f);
            EditorCameraGizmo::DrawWorldLine(drawList, pos, end + up * coneLen * tanOuter, view, proj,
                                             imageMin, imageSize, color, 1.5f);
            EditorCameraGizmo::DrawWorldLine(drawList, pos, end - up * coneLen * tanOuter, view, proj,
                                             imageMin, imageSize, color, 1.5f);
        }
    }
}

} // namespace EditorLightGizmo
} // namespace MipsyncEngine
