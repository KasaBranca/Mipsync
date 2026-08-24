#include "UICanvasLayout.h"
#include "../scene/Scene.h"
#include <algorithm>
#include <cmath>

namespace MipsyncEngine {

float ComputeSceneViewCanvasUnitsPerPixel(const Camera& editorCamera, float planeDistance, float layoutWidth,
                                          float layoutHeight, float viewportAspect) {
    const float dist = std::max(planeDistance, 0.1f);
    const float layoutW = std::max(layoutWidth, 1.0f);
    const float layoutH = std::max(layoutHeight, 1.0f);
    const float aspect = std::max(viewportAspect, 0.01f);

    const float tanHalfV = std::tan(glm::radians(editorCamera.fov * 0.5f));
    const float visibleH = 2.0f * dist * tanHalfV;
    const float visibleW = visibleH * aspect;

    // Keep the full canvas rect inside most of the scene view (Unity-style preview size).
    constexpr float kFill = 0.82f;
    return std::min((visibleW * kFill) / layoutW, (visibleH * kFill) / layoutH);
}

UIRect GetCanvasReferenceRect(const CanvasComponent& canvas, float scaleFactor) {
    const float w = std::max(canvas.referenceResolution.x, 1.0f) * scaleFactor;
    const float h = std::max(canvas.referenceResolution.y, 1.0f) * scaleFactor;
    return { 0.0f, 0.0f, w, h };
}

glm::mat4 BuildCanvasWorldMatrix(Scene& scene, Entity& canvasEntity, const CanvasComponent& canvas,
                                 const Camera& viewCamera, float unitsPerPixel, bool editorScenePreview) {
    if (canvas.renderMode == UICanvasRenderMode::WorldSpace)
        return scene.GetWorldMatrix(canvasEntity) * glm::scale(glm::mat4(1.0f), glm::vec3(unitsPerPixel));

    const float dist = canvas.planeDistance > 0.0f ? canvas.planeDistance : 1.0f;

    glm::vec3 right{ 1.0f, 0.0f, 0.0f };
    glm::vec3 up{ 0.0f, 1.0f, 0.0f };
    glm::vec3 planeNormal{ 0.0f, 0.0f, -1.0f };
    glm::vec3 pos{ 0.0f, 0.0f, 0.0f };

    if (editorScenePreview) {
        // Scene View: fixed upright wall at world origin (perpendicular to ground). Not camera-attached.
        (void)viewCamera;
        (void)dist;
        right = glm::vec3(1.0f, 0.0f, 0.0f);
        up = glm::vec3(0.0f, 1.0f, 0.0f);
        planeNormal = glm::vec3(0.0f, 0.0f, -1.0f);
        pos = glm::vec3(0.0f, 0.0f, 0.0f);
    } else {
        Entity* cameraEntity = nullptr;
        if (canvas.eventCameraEntityId != 0)
            cameraEntity = scene.FindEntity(canvas.eventCameraEntityId);
        if (!cameraEntity)
            cameraEntity = scene.GetPrimaryCameraEntity();

        if (cameraEntity) {
            const glm::mat4 camWorld = scene.GetWorldMatrix(*cameraEntity);
            const glm::vec3 viewDir = glm::normalize(glm::vec3(camWorld * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
            up = glm::normalize(glm::vec3(camWorld * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)));
            right = glm::normalize(glm::vec3(camWorld * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)));
            planeNormal = -viewDir;
            pos = glm::vec3(camWorld[3]) + viewDir * dist;
        } else {
            const glm::vec3 viewDir = glm::normalize(viewCamera.GetForward());
            right = viewCamera.GetRight();
            up = viewCamera.GetUp();
            planeNormal = -viewDir;
            pos = viewCamera.GetPosition() + viewDir * dist;
        }
    }

    // UI layout: +X right, +Y up on the plane; local +Z is the plane normal (toward the viewer).
    glm::mat4 basis(1.0f);
    basis[0] = glm::vec4(right, 0.0f);
    basis[1] = glm::vec4(up, 0.0f);
    basis[2] = glm::vec4(planeNormal, 0.0f);
    basis[3] = glm::vec4(pos, 1.0f);
    return basis * glm::scale(glm::mat4(1.0f), glm::vec3(unitsPerPixel));
}

Entity* FindCanvasAncestor(Scene& scene, Entity* entity) {
    while (entity) {
        if (entity->GetComponent<CanvasComponent>())
            return entity;
        if (entity->GetParentID() == 0)
            break;
        entity = scene.FindEntity(entity->GetParentID());
    }
    return nullptr;
}

} // namespace MipsyncEngine
