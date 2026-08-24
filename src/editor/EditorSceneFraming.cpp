#include "EditorSceneFraming.h"
#include "EditorSceneCamera.h"
#include "../scene/Scene.h"
#include "../renderer/Mesh.h"
#include "../physics/ColliderUtils.h"
#include "../ui/RectTransform.h"
#include "../ui/UICanvasLayout.h"
#include "../ui/UILayout.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace MipsyncEngine {

namespace {

void ExpandPoint(glm::vec3& outMin, glm::vec3& outMax, const glm::vec3& point, bool& hasBounds) {
    if (!hasBounds) {
        outMin = outMax = point;
        hasBounds = true;
        return;
    }
    outMin = glm::min(outMin, point);
    outMax = glm::max(outMax, point);
}

void ExpandLocalAabb(glm::vec3& outMin, glm::vec3& outMax, const glm::mat4& worldMatrix,
                     const glm::vec3& localMin, const glm::vec3& localMax, bool& hasBounds) {
    const glm::vec3 corners[8] = {
        { localMin.x, localMin.y, localMin.z },
        { localMax.x, localMin.y, localMin.z },
        { localMin.x, localMax.y, localMin.z },
        { localMax.x, localMax.y, localMin.z },
        { localMin.x, localMin.y, localMax.z },
        { localMax.x, localMin.y, localMax.z },
        { localMin.x, localMax.y, localMax.z },
        { localMax.x, localMax.y, localMax.z },
    };
    for (const glm::vec3& c : corners)
        ExpandPoint(outMin, outMax, glm::vec3(worldMatrix * glm::vec4(c, 1.0f)), hasBounds);
}

void ExpandUIRectOnPlane(glm::vec3& outMin, glm::vec3& outMax, const UIRect& rect,
                         const glm::mat4& planeWorld, bool& hasBounds) {
    const glm::vec3 corners[4] = {
        { rect.minX, rect.minY, 0.0f },
        { rect.maxX, rect.minY, 0.0f },
        { rect.maxX, rect.maxY, 0.0f },
        { rect.minX, rect.maxY, 0.0f },
    };
    for (const glm::vec3& c : corners)
        ExpandPoint(outMin, outMax, glm::vec3(planeWorld * glm::vec4(c, 1.0f)), hasBounds);
}

void ExpandCollider(glm::vec3& outMin, glm::vec3& outMax, Scene& scene, Entity& entity,
                    const ColliderComponent& col, bool& hasBounds) {
    const ColliderUtils::ColliderWorldPose pose = ColliderUtils::ComputeWorldPose(scene, entity, col);
    const glm::vec3 right = pose.rotation * glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 up = pose.rotation * glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 forward = pose.rotation * glm::vec3(0.0f, 0.0f, 1.0f);

    auto expandOffset = [&](const glm::vec3& offset) {
        ExpandPoint(outMin, outMax, pose.center + offset, hasBounds);
    };

    switch (col.shape) {
    case ColliderShape::Sphere:
        expandOffset(right * col.radius * pose.lossyScale.x);
        expandOffset(up * col.radius * pose.lossyScale.y);
        expandOffset(forward * col.radius * pose.lossyScale.z);
        break;
    case ColliderShape::Capsule: {
        const float halfH = col.capsuleHeight * 0.5f;
        expandOffset(up * (halfH + col.radius));
        expandOffset(-up * (halfH + col.radius));
        expandOffset(right * col.radius);
        expandOffset(forward * col.radius);
        break;
    }
    case ColliderShape::Box:
    case ColliderShape::Mesh:
    default: {
        const glm::vec3 ext = col.halfExtents * pose.lossyScale;
        for (int sx : { -1, 1 })
            for (int sy : { -1, 1 })
                for (int sz : { -1, 1 })
                    expandOffset(right * ext.x * static_cast<float>(sx) + up * ext.y * static_cast<float>(sy) +
                                 forward * ext.z * static_cast<float>(sz));
        break;
    }
    }
}

UIRect PadUIRectForFraming(const UIRect& rect, Entity& entity) {
    const glm::vec2 center = rect.Center();
    float halfW = rect.Width() * 0.5f;
    float halfH = rect.Height() * 0.5f;

    if (auto* text = entity.GetComponent<UITextComponent>()) {
        halfW = std::max(halfW, std::max(text->fontSize * 3.0f, 48.0f) * 0.5f);
        halfH = std::max(halfH, text->fontSize * 1.25f);
    } else {
        halfW = std::max(halfW, 24.0f);
        halfH = std::max(halfH, 24.0f);
    }

    return { center.x - halfW, center.y - halfH, center.x + halfW, center.y + halfH };
}

void VisitEntitySubtree3D(Scene& scene, Entity& entity, glm::vec3& outMin, glm::vec3& outMax, bool& hasBounds) {
    const glm::mat4 worldMatrix = scene.GetWorldMatrix(entity);

    if (auto* mesh = entity.GetComponent<MeshRendererComponent>(); mesh && mesh->enabled && mesh->mesh) {
        ExpandLocalAabb(outMin, outMax, worldMatrix, mesh->mesh->GetBoundsMin(), mesh->mesh->GetBoundsMax(),
                        hasBounds);
    }

    if (auto* col = entity.GetComponent<ColliderComponent>(); col && col->enabled)
        ExpandCollider(outMin, outMax, scene, entity, *col, hasBounds);

    for (uint32_t childId : entity.GetChildIDs()) {
        if (Entity* child = scene.FindEntity(childId))
            VisitEntitySubtree3D(scene, *child, outMin, outMax, hasBounds);
    }
}

} // namespace

float FocusDistanceForBounds(const glm::vec3& boundsMin, const glm::vec3& boundsMax, float fovDegrees,
                             float aspect) {
    const glm::vec3 extent = boundsMax - boundsMin;
    const float radius = std::max(glm::length(extent) * 0.5f, 0.25f);

    const float halfVert = glm::radians(fovDegrees * 0.5f);
    const float distVert = radius / std::sin(halfVert);

    const float halfHoriz = std::atan(std::tan(halfVert) * aspect);
    const float distHoriz = radius / std::sin(halfHoriz);

    return std::clamp(std::max(distVert, distHoriz) * 1.25f, 0.5f, 500.0f);
}

bool ComputeEntityWorldBounds(Scene& scene, Entity& entity, glm::vec3& outMin, glm::vec3& outMax,
                              const Camera* editorSceneCamera, float editorViewportAspect,
                              int layoutWidth, int layoutHeight) {
    bool hasBounds = false;

    VisitEntitySubtree3D(scene, entity, outMin, outMax, hasBounds);

    UIRect uiRect;
    if (editorSceneCamera && TryGetEntityCanvasUIRect(scene, entity, layoutWidth, layoutHeight, uiRect)) {
        Entity* canvasEntity = FindCanvasAncestor(scene, &entity);
        auto* canvasComp = canvasEntity ? canvasEntity->GetComponent<CanvasComponent>() : nullptr;
        if (canvasEntity && canvasComp) {
            const float layoutW = static_cast<float>(std::max(layoutWidth, 1));
            const float layoutH = static_cast<float>(std::max(layoutHeight, 1));
            const float units = ComputeSceneViewCanvasUnitsPerPixel(
                *editorSceneCamera, canvasComp->planeDistance, layoutW, layoutH, editorViewportAspect);
            const glm::mat4 canvasPlane = BuildCanvasWorldMatrix(scene, *canvasEntity, *canvasComp,
                                                               *editorSceneCamera, units, true);
            const UIRect frameRect = PadUIRectForFraming(uiRect, entity);
            ExpandUIRectOnPlane(outMin, outMax, frameRect, canvasPlane, hasBounds);
        }
    }

    if (!hasBounds) {
        const glm::vec3 pos = scene.GetWorldPosition(entity);
        ExpandPoint(outMin, outMax, pos - glm::vec3(0.5f), hasBounds);
        ExpandPoint(outMin, outMax, pos + glm::vec3(0.5f), hasBounds);
    }

    return hasBounds;
}

void FrameEntityInSceneView(Scene& scene, Entity& entity, EditorSceneCamera& sceneCamera, float viewportAspect,
                            int layoutWidth, int layoutHeight) {
    glm::vec3 boundsMin;
    glm::vec3 boundsMax;
    if (!ComputeEntityWorldBounds(scene, entity, boundsMin, boundsMax, &sceneCamera.GetCamera(), viewportAspect,
                                  layoutWidth, layoutHeight))
        return;

    const glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
    const glm::vec3 extent = boundsMax - boundsMin;
    const float maxExtent = std::max({ extent.x, extent.y, extent.z, 0.01f });

    float distance =
        FocusDistanceForBounds(boundsMin, boundsMax, sceneCamera.GetCamera().fov, viewportAspect);
    if (maxExtent < 5.0f) {
        const float halfVert = glm::radians(sceneCamera.GetCamera().fov * 0.5f);
        const float halfHoriz = std::atan(std::tan(halfVert) * viewportAspect);
        const float halfSize = std::max(extent.x, extent.y) * 0.5f;
        const float dist2D = std::max(halfSize / std::sin(halfVert), halfSize / std::sin(halfHoriz)) * 1.35f;
        distance = std::clamp(dist2D, 0.35f, 80.0f);
    }

    sceneCamera.FocusOn(center, distance);
}

} // namespace MipsyncEngine
