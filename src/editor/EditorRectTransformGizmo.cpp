#include "EditorRectTransformGizmo.h"
#include "EditorUISnap.h"
#include "../ui/UICanvasLayout.h"
#include "../ui/UILayout.h"
#include "../scene/Scene.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace MipsyncEngine {
namespace EditorRectTransformGizmo {

namespace {

struct DragState {
    bool active = false;
    glm::vec2 startPivotLayout{ 0.0f };
    glm::vec2 startAnchored{ 0.0f };
    glm::vec2 startSizeDelta{ 100.0f };
    glm::vec3 startMatrixScale{ 1.0f };
    float startRotationZ = 0.0f;
    float scaleFactor = 1.0f;
};

DragState s_Drag;

glm::vec2 WorldToLayoutOnCanvas(const glm::mat4& planeWorld, const glm::vec3& worldPos) {
    const glm::vec4 local = glm::inverse(planeWorld) * glm::vec4(worldPos, 1.0f);
    return { local.x, local.y };
}

glm::vec3 MatrixColumnScale(const glm::mat4& matrix) {
    return {
        glm::length(glm::vec3(matrix[0])),
        glm::length(glm::vec3(matrix[1])),
        glm::length(glm::vec3(matrix[2]))
    };
}

bool ShouldPreserveAspect(Entity& entity) {
    if (const auto* image = entity.GetComponent<UIImageComponent>())
        return image->preserveAspect;
    if (const auto* button = entity.GetComponent<UIButtonComponent>())
        return button->preserveAspect;
    return false;
}

bool GetCanvasPlaneContext(Scene& scene, Entity& entity, const Camera& editorCamera, int layoutWidth,
                           int layoutHeight, float viewportAspect, Entity*& outCanvas,
                           CanvasComponent*& outCanvasComp, glm::mat4& outPlaneWorld, float& outScaleFactor,
                           UIRect& outRect) {
    outCanvas = FindCanvasAncestor(scene, &entity);
    if (!outCanvas)
        return false;

    outCanvasComp = outCanvas->GetComponent<CanvasComponent>();
    if (!outCanvasComp)
        return false;

    const int layoutW = std::max(layoutWidth, 1);
    const int layoutH = std::max(layoutHeight, 1);
    if (!TryGetEntityCanvasUIRect(scene, entity, layoutW, layoutH, outRect))
        return false;

    outScaleFactor = ComputeCanvasScaleFactor(*outCanvasComp, layoutW, layoutH);
    const float unitsPerPixel = ComputeSceneViewCanvasUnitsPerPixel(
        editorCamera, outCanvasComp->planeDistance, layoutW, layoutH, viewportAspect);
    outPlaneWorld = BuildCanvasWorldMatrix(scene, *outCanvas, *outCanvasComp, editorCamera, unitsPerPixel, true);
    return true;
}

} // namespace

bool ShouldUseRectGizmo(Scene& scene, Entity& entity) {
    if (!entity.GetComponent<RectTransformComponent>())
        return false;
    return FindCanvasAncestor(scene, &entity) != nullptr;
}

bool BuildGizmoMatrix(Scene& scene, Entity& entity, const Camera& editorCamera, int layoutWidth,
                        int layoutHeight, float viewportAspect, glm::mat4& outWorld) {
    Entity* canvasEntity = nullptr;
    CanvasComponent* canvas = nullptr;
    glm::mat4 planeWorld(1.0f);
    float scaleFactor = 1.0f;
    UIRect rect;

    if (!GetCanvasPlaneContext(scene, entity, editorCamera, layoutWidth, layoutHeight, viewportAspect,
                               canvasEntity, canvas, planeWorld, scaleFactor, rect))
        return false;

    auto* rectTransform = entity.GetComponent<RectTransformComponent>();
    if (!rectTransform)
        return false;

    ImFont* font = ImGui::GetFont();
    const auto* uiText = entity.GetComponent<UITextComponent>();
    const glm::vec2 pivotLayout =
        CalcUIGizmoPivotLayout(rect, *rectTransform, uiText, font);
    const glm::vec3 worldPivot = glm::vec3(planeWorld * glm::vec4(pivotLayout.x, pivotLayout.y, 0.0f, 1.0f));

    const glm::vec3 right = glm::normalize(glm::vec3(planeWorld[0]));
    const glm::vec3 up = glm::normalize(glm::vec3(planeWorld[1]));
    const glm::vec3 normal = glm::normalize(glm::vec3(planeWorld[2]));

    glm::mat4 basis(1.0f);
    basis[0] = glm::vec4(right, 0.0f);
    basis[1] = glm::vec4(up, 0.0f);
    basis[2] = glm::vec4(normal, 0.0f);

    const float unitsPerPixel = glm::length(glm::vec3(planeWorld[0]));
    const float handleScale =
        std::max(std::max(rect.Width(), rect.Height()) * unitsPerPixel * 0.35f, 0.4f);
    outWorld = glm::translate(glm::mat4(1.0f), worldPivot) * basis *
               glm::scale(glm::mat4(1.0f), glm::vec3(handleScale));

    return true;
}

void ApplyGizmoDrag(Scene& scene, Entity& entity, const Camera& editorCamera, int layoutWidth,
                    int layoutHeight, float viewportAspect, ImGuizmo::OPERATION operation,
                    const glm::mat4& originalWorld, const glm::mat4& manipulatedWorld,
                    bool gizmoUsing, bool gizmoStarted) {
    if (!gizmoUsing) {
        s_Drag.active = false;
        EditorUISnap::ClearActiveGuides();
        return;
    }

    auto* rectTransform = entity.GetComponent<RectTransformComponent>();
    if (!rectTransform)
        return;
    auto* transform = entity.GetComponent<TransformComponent>();

    Entity* canvasEntity = nullptr;
    CanvasComponent* canvas = nullptr;
    glm::mat4 planeWorld(1.0f);
    float scaleFactor = 1.0f;
    UIRect rect;

    if (!GetCanvasPlaneContext(scene, entity, editorCamera, layoutWidth, layoutHeight, viewportAspect,
                               canvasEntity, canvas, planeWorld, scaleFactor, rect))
        return;

    ImFont* font = ImGui::GetFont();
    const auto* uiText = entity.GetComponent<UITextComponent>();

    if (gizmoStarted || !s_Drag.active) {
        s_Drag.active = true;
        s_Drag.startAnchored = rectTransform->anchoredPosition;
        s_Drag.startSizeDelta = rectTransform->sizeDelta;
        s_Drag.startMatrixScale = MatrixColumnScale(originalWorld);
        s_Drag.startRotationZ = transform ? transform->rotation.z : 0.0f;
        s_Drag.startPivotLayout = CalcUIGizmoPivotLayout(rect, *rectTransform, uiText, font);
        s_Drag.scaleFactor = std::max(scaleFactor, 0.0001f);
    }

    if (operation == ImGuizmo::SCALE) {
        const glm::vec3 newScale = MatrixColumnScale(manipulatedWorld);
        const float safeStartX = std::max(std::abs(s_Drag.startMatrixScale.x), 0.0001f);
        const float safeStartY = std::max(std::abs(s_Drag.startMatrixScale.y), 0.0001f);
        glm::vec2 ratio{
            std::max(newScale.x / safeStartX, 0.001f),
            std::max(newScale.y / safeStartY, 0.001f)
        };
        if (ShouldPreserveAspect(entity)) {
            const float uniform = std::max(ratio.x, ratio.y);
            ratio = { uniform, uniform };
        }
        rectTransform->sizeDelta = {
            std::max(1.0f, s_Drag.startSizeDelta.x * ratio.x),
            std::max(1.0f, s_Drag.startSizeDelta.y * ratio.y)
        };
        return;
    }

    if (operation == ImGuizmo::ROTATE) {
        if (transform) {
            const glm::vec3 originalRight = glm::normalize(glm::vec3(originalWorld[0]));
            const glm::vec3 newRight = glm::normalize(glm::vec3(manipulatedWorld[0]));
            const glm::vec3 normal = glm::normalize(glm::vec3(originalWorld[2]));
            const float sinAngle = glm::dot(glm::cross(originalRight, newRight), normal);
            const float cosAngle = glm::clamp(glm::dot(originalRight, newRight), -1.0f, 1.0f);
            transform->rotation.z = s_Drag.startRotationZ + glm::degrees(std::atan2(sinAngle, cosAngle));
        }
        return;
    }

    const glm::vec3 worldPivot = glm::vec3(manipulatedWorld[3]);
    const glm::vec2 rawPivotLayout = WorldToLayoutOnCanvas(planeWorld, worldPivot);
    const glm::vec2 snappedPivot =
        EditorUISnap::SnapPivotLayout(scene, entity, rawPivotLayout, layoutWidth, layoutHeight);
    const glm::vec2 delta = snappedPivot - s_Drag.startPivotLayout;
    rectTransform->anchoredPosition = s_Drag.startAnchored + delta / s_Drag.scaleFactor;
}

} // namespace EditorRectTransformGizmo
} // namespace MipsyncEngine
