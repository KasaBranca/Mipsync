#include "EditorUISnap.h"
#include "../ui/UICanvasLayout.h"
#include "../ui/UILayout.h"
#include "../scene/Scene.h"
#include <algorithm>
#include <cmath>

namespace MipsyncEngine {
namespace EditorUISnap {

namespace {

constexpr float kSnapThresholdLayout = 10.0f;
const ImU32 kGuideColor = IM_COL32(255, 80, 200, 230);

std::vector<GuideLine> s_ActiveGuides;

void AddRectSnapLines(const UIRect& rect, std::vector<float>& xs, std::vector<float>& ys) {
    xs.push_back(rect.minX);
    xs.push_back(rect.maxX);
    xs.push_back(rect.Center().x);
    ys.push_back(rect.minY);
    ys.push_back(rect.maxY);
    ys.push_back(rect.Center().y);
}

float SnapScalar(float value, const std::vector<float>& lines, float threshold, float* outMatchedLine) {
    float best = value;
    float bestDist = threshold;
    float matched = value;
    bool hit = false;

    for (float line : lines) {
        const float dist = std::fabs(value - line);
        if (dist <= bestDist) {
            bestDist = dist;
            best = line;
            matched = line;
            hit = true;
        }
    }

    if (outMatchedLine && hit)
        *outMatchedLine = matched;
    return best;
}

void CollectSnapTargets(Scene& scene, Entity& entity, int layoutWidth, int layoutHeight,
                        std::vector<float>& outX, std::vector<float>& outY) {
    Entity* canvasEntity = FindCanvasAncestor(scene, &entity);
    if (!canvasEntity)
        return;

    auto* canvas = canvasEntity->GetComponent<CanvasComponent>();
    if (!canvas)
        return;

    const int layoutW = std::max(layoutWidth, 1);
    const int layoutH = std::max(layoutHeight, 1);
    const float scale = ComputeCanvasScaleFactor(*canvas, layoutW, layoutH);

    const UIRect canvasRef = GetCanvasReferenceRect(*canvas, scale);
    AddRectSnapLines(canvasRef, outX, outY);

    UIRect canvasLayoutRect;
    if (TryGetEntityCanvasUIRect(scene, *canvasEntity, layoutW, layoutH, canvasLayoutRect))
        AddRectSnapLines(canvasLayoutRect, outX, outY);

    const uint32_t parentId = entity.GetParentID();
    if (Entity* parent = scene.FindEntity(parentId)) {
        UIRect parentRect;
        if (TryGetEntityCanvasUIRect(scene, *parent, layoutW, layoutH, parentRect))
            AddRectSnapLines(parentRect, outX, outY);
    }

    for (const auto& entityPtr : scene.GetEntities()) {
        if (!entityPtr || entityPtr->GetParentID() != parentId)
            continue;
        if (entityPtr->GetID() == entity.GetID())
            continue;
        if (!entityPtr->GetComponent<RectTransformComponent>())
            continue;
        if (FindCanvasAncestor(scene, entityPtr.get()) != canvasEntity)
            continue;

        UIRect siblingRect;
        if (TryGetEntityCanvasUIRect(scene, *entityPtr, layoutW, layoutH, siblingRect))
            AddRectSnapLines(siblingRect, outX, outY);
    }
}

bool GetCanvasPlaneWorld(Scene& scene, Entity& entity, const Camera& editorCamera, int layoutWidth,
                         int layoutHeight, float viewportAspect, glm::mat4& outPlaneWorld) {
    Entity* canvasEntity = FindCanvasAncestor(scene, &entity);
    if (!canvasEntity)
        return false;

    auto* canvas = canvasEntity->GetComponent<CanvasComponent>();
    if (!canvas)
        return false;

    const float unitsPerPixel = ComputeSceneViewCanvasUnitsPerPixel(
        editorCamera, canvas->planeDistance, layoutWidth, layoutHeight, viewportAspect);
    outPlaneWorld = BuildCanvasWorldMatrix(scene, *canvasEntity, *canvas, editorCamera, unitsPerPixel, true);
    return true;
}

void DrawGuideSegment(const GuideLine& guide, const glm::mat4& planeWorld, const Camera& camera,
                      const ImVec2& imageMin, const ImVec2& imageSize, float layoutW, float layoutH,
                      ImDrawList* drawList) {
    ImVec2 a;
    ImVec2 b;
    if (guide.vertical) {
        if (!ProjectCanvasLayoutPoint(planeWorld, guide.layoutCoord, 0.0f, camera, imageMin, imageSize, a))
            return;
        if (!ProjectCanvasLayoutPoint(planeWorld, guide.layoutCoord, layoutH, camera, imageMin, imageSize, b))
            return;
    } else {
        if (!ProjectCanvasLayoutPoint(planeWorld, 0.0f, guide.layoutCoord, camera, imageMin, imageSize, a))
            return;
        if (!ProjectCanvasLayoutPoint(planeWorld, layoutW, guide.layoutCoord, camera, imageMin, imageSize, b))
            return;
    }
    drawList->AddLine(a, b, kGuideColor, 1.0f);
}

} // namespace

glm::vec2 SnapPivotLayout(Scene& scene, Entity& entity, glm::vec2 desiredPivotLayout, int layoutWidth,
                          int layoutHeight) {
    s_ActiveGuides.clear();

    std::vector<float> xs;
    std::vector<float> ys;
    CollectSnapTargets(scene, entity, layoutWidth, layoutHeight, xs, ys);

    float matchedX = 0.0f;
    float matchedY = 0.0f;
    const float snappedX = SnapScalar(desiredPivotLayout.x, xs, kSnapThresholdLayout, &matchedX);
    const float snappedY = SnapScalar(desiredPivotLayout.y, ys, kSnapThresholdLayout, &matchedY);

    if (std::fabs(snappedX - desiredPivotLayout.x) > 0.001f)
        s_ActiveGuides.push_back({ true, matchedX });
    if (std::fabs(snappedY - desiredPivotLayout.y) > 0.001f)
        s_ActiveGuides.push_back({ false, matchedY });

    return { snappedX, snappedY };
}

const std::vector<GuideLine>& GetActiveGuides() {
    return s_ActiveGuides;
}

void ClearActiveGuides() {
    s_ActiveGuides.clear();
}

void DrawActiveGuides(Scene& scene, Entity& entity, const Camera& editorCamera, int layoutWidth,
                      int layoutHeight, float viewportAspect, const ImVec2& imageMin,
                      const ImVec2& imageSize, ImDrawList* drawList) {
    if (!drawList || s_ActiveGuides.empty())
        return;

    glm::mat4 planeWorld(1.0f);
    if (!GetCanvasPlaneWorld(scene, entity, editorCamera, layoutWidth, layoutHeight, viewportAspect,
                             planeWorld))
        return;

    const float layoutW = static_cast<float>(std::max(layoutWidth, 1));
    const float layoutH = static_cast<float>(std::max(layoutHeight, 1));

    for (const GuideLine& guide : s_ActiveGuides)
        DrawGuideSegment(guide, planeWorld, editorCamera, imageMin, imageSize, layoutW, layoutH, drawList);
}

} // namespace EditorUISnap
} // namespace MipsyncEngine
