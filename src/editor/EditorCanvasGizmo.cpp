#include "EditorCanvasGizmo.h"
#include "EditorCameraGizmo.h"
#include "../scene/Scene.h"
#include "../ui/RectTransform.h"
#include "../ui/UICanvasLayout.h"
#include "../ui/UILayout.h"
#include <algorithm>

namespace MipsyncEngine {
namespace EditorCanvasGizmo {

namespace {

const ImU32 kCanvasColor = IM_COL32(0, 200, 255, 220);
const ImU32 kChildRectColor = IM_COL32(0, 200, 255, 120);

void DrawRectWireframe3D(const UIRect& rect, const glm::mat4& planeWorld, const glm::mat4& view,
                         const glm::mat4& proj, const ImVec2& rectMin, const ImVec2& rectSize,
                         ImDrawList* drawList, ImU32 color, float thickness) {
    const glm::vec3 corners[4] = {
        { rect.minX, rect.minY, 0.0f },
        { rect.maxX, rect.minY, 0.0f },
        { rect.maxX, rect.maxY, 0.0f },
        { rect.minX, rect.maxY, 0.0f },
    };

    glm::vec3 world[4];
    for (int i = 0; i < 4; ++i)
        world[i] = glm::vec3(planeWorld * glm::vec4(corners[i], 1.0f));

    for (int i = 0; i < 4; ++i)
        EditorCameraGizmo::DrawWorldLine(drawList, world[i], world[(i + 1) % 4], view, proj, rectMin, rectSize,
                                         color, thickness);
}

} // namespace

Entity* FindCanvasEntity(Scene& scene, Entity* entity) {
    while (entity) {
        if (entity->GetComponent<CanvasComponent>())
            return entity;
        if (entity->GetParentID() == 0)
            break;
        entity = scene.FindEntity(entity->GetParentID());
    }
    return nullptr;
}

void DrawSceneCanvases(Scene& scene, const Camera& editorSceneCamera, const glm::mat4& view,
                       const glm::mat4& proj, const ImVec2& rectMin, const ImVec2& rectSize,
                       ImDrawList* drawList, uint32_t selectedEntityId, int layoutViewportWidth,
                       int layoutViewportHeight) {
    const float layoutW = static_cast<float>(std::max(layoutViewportWidth, 1));
    const float layoutH = static_cast<float>(std::max(layoutViewportHeight, 1));

    Entity* selected = nullptr;
    if (selectedEntityId != 0)
        selected = scene.FindEntity(selectedEntityId);

    Entity* targetCanvas = selected ? FindCanvasEntity(scene, selected) : nullptr;

    for (auto& entityPtr : scene.GetEntities()) {
        auto* canvas = entityPtr->GetComponent<CanvasComponent>();
        if (!canvas || !canvas->enabled)
            continue;

        const bool isTarget = targetCanvas && targetCanvas->GetID() == entityPtr->GetID();
        if (targetCanvas && !isTarget)
            continue;

        const float scale = ComputeCanvasScaleFactor(*canvas, layoutViewportWidth, layoutViewportHeight);
        const float aspect =
            rectSize.y > 0.0f ? (rectSize.x / rectSize.y) : 1.0f;
        const float unitsPerPixel = ComputeSceneViewCanvasUnitsPerPixel(
            editorSceneCamera, canvas->planeDistance, layoutViewportWidth, layoutViewportHeight, aspect);
        const glm::mat4 planeWorld = BuildCanvasWorldMatrix(scene, *entityPtr, *canvas, editorSceneCamera,
                                                            unitsPerPixel, true);

        UILayoutContext ctx;
        ctx.viewportWidth = layoutW;
        ctx.viewportHeight = layoutH;
        ctx.scaleFactor = scale;
        ctx.pixelSpace = false;

        bool drewRoot = false;
        VisitCanvasUI(scene, *entityPtr, *canvas, ctx, [&](Entity& entity, const UIRect& rect) {
            const bool isRoot = entity.GetID() == entityPtr->GetID();
            const ImU32 color = isRoot ? kCanvasColor : kChildRectColor;
            const float thickness = isRoot ? 2.0f : 1.0f;
            DrawRectWireframe3D(rect, planeWorld, view, proj, rectMin, rectSize, drawList, color, thickness);
            drewRoot = drewRoot || isRoot;
        });

        if (!drewRoot) {
            UIRect full{ 0.0f, 0.0f, layoutW, layoutH };
            DrawRectWireframe3D(full, planeWorld, view, proj, rectMin, rectSize, drawList, kCanvasColor, 2.0f);
        }
    }
}

} // namespace EditorCanvasGizmo
} // namespace MipsyncEngine
