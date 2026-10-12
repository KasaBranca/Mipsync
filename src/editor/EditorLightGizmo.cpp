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

// Screen-space silhouettes stay upright and readable at every camera angle.
// Draw the dark border first, then the complete white silhouette, so joins
// between the bulb, neck and base have no internal outline or seams.
void DrawLightIcon(ImDrawList* drawList, const ImVec2& center, float size, LightType type) {
    const auto point = [&](float x, float y) {
        return ImVec2(center.x + x * size, center.y + y * size);
    };
    for (int pass = 0; pass < 2; ++pass) {
        const bool border = pass == 0;
        const ImU32 color = border ? IM_COL32(20, 22, 26, 170) : IM_COL32(255, 255, 255, 255);
        const float outline = border ? std::max(1.0f, size * 0.022f) : 0.0f;
        const auto capsule = [&](ImVec2 a, ImVec2 b, float width) {
            width += outline * 2.0f;
            drawList->AddLine(a, b, color, width);
            drawList->AddCircleFilled(a, width * 0.5f, color, 12);
            drawList->AddCircleFilled(b, width * 0.5f, color, 12);
        };
        if (type == LightType::Point) {
            drawList->AddCircleFilled(point(0, -0.055f), size * 0.205f + outline, color, 32);
            const ImVec2 neck[] = {
                point(-0.15f - outline / size, 0.07f), point(0.15f + outline / size, 0.07f),
                point(0.10f + outline / size, 0.24f), point(-0.10f - outline / size, 0.24f)
            };
            drawList->AddConvexPolyFilled(neck, 4, color);
            drawList->AddRectFilled(
                {center.x - size * 0.10f - outline, center.y + size * 0.17f - outline},
                {center.x + size * 0.10f + outline, center.y + size * 0.35f + outline},
                color, size * 0.055f + outline);
            for (int i = 0; i < 8; ++i) {
                if (i == 2) continue; // The base occupies the downward ray.
                const float angle = i * kPi / 4.0f;
                const float x = std::cos(angle), y = std::sin(angle);
                capsule(point(x * 0.325f, -0.055f + y * 0.325f),
                        point(x * 0.455f, -0.055f + y * 0.455f), size * 0.065f);
            }
        } else {
            drawList->AddCircleFilled(point(0, -0.15f), size * 0.18f + outline, color, 32);
            drawList->AddRectFilled(
                {center.x - size * 0.18f - outline, center.y - size * 0.15f - outline},
                {center.x + size * 0.18f + outline, center.y + size * 0.065f + outline},
                color, size * 0.02f + outline, ImDrawFlags_RoundCornersBottom);
            capsule(point(-0.11f, 0.20f), point(-0.29f, 0.38f), size * 0.065f);
            capsule(point(0, 0.20f), point(0, 0.43f), size * 0.065f);
            capsule(point(0.11f, 0.20f), point(0.29f, 0.38f), size * 0.065f);
        }
    }
}

bool ProjectLightIcon(const glm::vec3& pos, const glm::mat4& view, const glm::mat4& proj,
                      const ImVec2& imageMin, const ImVec2& imageSize, float size, ImVec2& out) {
    const glm::vec4 clip = proj * view * glm::vec4(pos, 1.0f);
    for (int i = 0; i < 4; ++i)
        if (!std::isfinite(clip[i])) return false;
    if (clip.w <= 0.0001f || clip.z < -clip.w || clip.z > clip.w) return false;
    out = {imageMin.x + (clip.x / clip.w * 0.5f + 0.5f) * imageSize.x,
           imageMin.y + (0.5f - clip.y / clip.w * 0.5f) * imageSize.y};
    const float margin = size * 0.55f;
    return out.x + margin >= imageMin.x && out.x - margin <= imageMin.x + imageSize.x &&
           out.y + margin >= imageMin.y && out.y - margin <= imageMin.y + imageSize.y;
}

glm::vec3 LightForward(const glm::mat4& world) {
    const glm::vec3 forward = glm::vec3(world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
    const float length = glm::length(forward);
    return std::isfinite(length) && length > 0.0001f ? forward / length : glm::vec3(0, 0, -1);
}

glm::vec3 LightRight(const glm::vec3& forward) {
    const glm::vec3 reference = std::abs(forward.y) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    return glm::normalize(glm::cross(forward, reference));
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

    const glm::vec3 right = LightRight(forward);
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
    if (imageSize.x <= 0 || imageSize.y <= 0) return;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const glm::mat4 view = camera.GetViewMatrix();
    const glm::mat4 proj = camera.GetProjectionMatrix();
    const float iconSize = ImGui::GetFontSize() * 4.0f;
    drawList->PushClipRect(imageMin, {imageMin.x + imageSize.x, imageMin.y + imageSize.y}, true);

    for (const auto& entityPtr : scene.GetEntities()) {
        if (!entityPtr || !scene.IsActiveInHierarchy(*entityPtr)) continue;
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

        const ImU32 color = IM_COL32(235, 220, 165, 100);
        const float r = std::max(light->range, 0.25f);
        if (light->type == LightType::Point) {
            DrawCircle(drawList, pos, glm::vec3(r, 0, 0), glm::vec3(0, 0, r), view, proj, imageMin, imageSize,
                       color, 1.0f);
            DrawCircle(drawList, pos, glm::vec3(r, 0, 0), glm::vec3(0, r, 0), view, proj, imageMin, imageSize,
                       color, 1.0f);
            DrawCircle(drawList, pos, glm::vec3(0, r, 0), glm::vec3(0, 0, r), view, proj, imageMin, imageSize,
                       color, 1.0f);
        } else if (light->type == LightType::Spot) {
            const float outerRad = glm::radians(std::clamp(light->spotAngle, 1.0f, 89.0f));
            const float coneLen = r;
            const glm::vec3 end = pos + forward * coneLen;
            const glm::vec3 right = LightRight(forward);
            const glm::vec3 up = glm::normalize(glm::cross(right, forward));
            const float tanOuter = std::tan(outerRad);
            DrawCircle(drawList, end, right * coneLen * tanOuter, up * coneLen * tanOuter,
                       view, proj, imageMin, imageSize, color, 1.0f);
            EditorCameraGizmo::DrawWorldLine(drawList, pos, end + right * coneLen * tanOuter, view, proj,
                                             imageMin, imageSize, color, 1.0f);
            EditorCameraGizmo::DrawWorldLine(drawList, pos, end - right * coneLen * tanOuter, view, proj,
                                             imageMin, imageSize, color, 1.0f);
            EditorCameraGizmo::DrawWorldLine(drawList, pos, end + up * coneLen * tanOuter, view, proj,
                                             imageMin, imageSize, color, 1.0f);
            EditorCameraGizmo::DrawWorldLine(drawList, pos, end - up * coneLen * tanOuter, view, proj,
                                             imageMin, imageSize, color, 1.0f);
        }
    }
    // Overlay sprites after every range/cone, so another light's wireframe
    // cannot cut through a source icon. Only the position is world-space.
    for (const auto& entityPtr : scene.GetEntities()) {
        if (!entityPtr || !scene.IsActiveInHierarchy(*entityPtr)) continue;
        const auto* light = entityPtr->GetComponent<LightComponent>();
        if (!light || !light->enabled || light->type == LightType::Directional ||
            !entityPtr->GetComponent<TransformComponent>()) continue;
        ImVec2 center;
        if (ProjectLightIcon(glm::vec3(scene.GetWorldMatrix(*entityPtr)[3]), view, proj,
                             imageMin, imageSize, iconSize, center))
            DrawLightIcon(drawList, center, iconSize, light->type);
    }
    drawList->PopClipRect();
}

} // namespace EditorLightGizmo
} // namespace MipsyncEngine
