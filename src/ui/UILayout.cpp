#include "UILayout.h"
#include "UICanvasLayout.h"
#include "../scene/Scene.h"
#include "../renderer/Camera.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <functional>

namespace MipsyncEngine {

void UIRectToScreenOverlay(const UIRect& rect, float layoutW, float layoutH, const ImVec2& imageMin,
                           const ImVec2& imageSize, ImVec2& outMin, ImVec2& outMax) {
    const float safeW = std::max(layoutW, 1.0f);
    const float safeH = std::max(layoutH, 1.0f);
    outMin.x = imageMin.x + (rect.minX / safeW) * imageSize.x;
    outMax.x = imageMin.x + (rect.maxX / safeW) * imageSize.x;
    outMin.y = imageMin.y + (1.0f - rect.maxY / safeH) * imageSize.y;
    outMax.y = imageMin.y + (1.0f - rect.minY / safeH) * imageSize.y;
}

namespace {

void TransformDrawListVertsToCanvasPlane(ImDrawList* drawList, int vtxStart, const ImVec2& origin,
                                         const ImVec2& axisX, const ImVec2& axisY, float pixelsPerLayoutX,
                                         float pixelsPerLayoutY) {
    if (pixelsPerLayoutX < 0.001f || pixelsPerLayoutY < 0.001f)
        return;

    for (int i = vtxStart; i < drawList->VtxBuffer.Size; ++i) {
        ImDrawVert& v = drawList->VtxBuffer[i];
        const float dx = v.pos.x - origin.x;
        const float dy = v.pos.y - origin.y;
        v.pos.x = origin.x + (dx / pixelsPerLayoutX) * axisX.x - (dy / pixelsPerLayoutY) * axisY.x;
        v.pos.y = origin.y + (dx / pixelsPerLayoutX) * axisX.y - (dy / pixelsPerLayoutY) * axisY.y;
    }
}

} // namespace

bool WorldPointToSceneViewScreen(const glm::vec3& world, const Camera& camera, const ImVec2& imageMin,
                                 const ImVec2& imageSize, ImVec2& out) {
    if (imageSize.x <= 1.0f || imageSize.y <= 1.0f)
        return false;

    const glm::vec4 clip = camera.GetProjectionMatrix() * camera.GetViewMatrix() * glm::vec4(world, 1.0f);
    if (clip.w <= 0.0001f)
        return false;

    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;
    out.x = imageMin.x + (ndcX * 0.5f + 0.5f) * imageSize.x;
    out.y = imageMin.y + (1.0f - (ndcY * 0.5f + 0.5f)) * imageSize.y;
    return true;
}

bool ProjectCanvasLayoutPoint(const glm::mat4& canvasWorld, float layoutX, float layoutY, const Camera& camera,
                              const ImVec2& imageMin, const ImVec2& imageSize, ImVec2& out) {
    const glm::vec3 world = glm::vec3(canvasWorld * glm::vec4(layoutX, layoutY, 0.0f, 1.0f));
    return WorldPointToSceneViewScreen(world, camera, imageMin, imageSize, out);
}

void DrawCanvasAlignedText(ImDrawList* drawList, ImFont* font, const char* text, float layoutFontSize,
                           float layoutX, float layoutY, const glm::mat4& canvasWorld, const Camera& camera,
                           const ImVec2& imageMin, const ImVec2& imageSize, ImU32 color) {
    if (!drawList || !font || !text || !*text)
        return;

    ImVec2 origin;
    ImVec2 axisX;
    ImVec2 axisY;
    if (!ProjectCanvasLayoutPoint(canvasWorld, layoutX, layoutY, camera, imageMin, imageSize, origin))
        return;
    if (!ProjectCanvasLayoutPoint(canvasWorld, layoutX + 1.0f, layoutY, camera, imageMin, imageSize, axisX))
        return;
    if (!ProjectCanvasLayoutPoint(canvasWorld, layoutX, layoutY + 1.0f, camera, imageMin, imageSize, axisY))
        return;

    axisX.x -= origin.x;
    axisX.y -= origin.y;
    axisY.x -= origin.x;
    axisY.y -= origin.y;

    const float pixelsPerLayoutX = glm::length(glm::vec2(axisX.x, axisX.y));
    const float pixelsPerLayoutY = glm::length(glm::vec2(axisY.x, axisY.y));
    if (pixelsPerLayoutX < 0.25f && pixelsPerLayoutY < 0.25f)
        return;

    const float screenFontSize = std::max(layoutFontSize * pixelsPerLayoutY, 8.0f);

    const int vtxStart = drawList->VtxBuffer.Size;
    drawList->AddText(font, screenFontSize, origin, color, text);
    TransformDrawListVertsToCanvasPlane(drawList, vtxStart, origin, axisX, axisY, pixelsPerLayoutX,
                                        pixelsPerLayoutY);
}

namespace {

void VisitEntityRecursive(Scene& scene, Entity& entity, const UIRect& parentRect, float scaleFactor,
                          const std::function<void(Entity&, const UIRect&)>& visitor) {
    UIRect nodeRect = parentRect;
    if (auto* rect = entity.GetComponent<RectTransformComponent>()) {
        nodeRect = CalcRectInParent(parentRect, *rect, scaleFactor);
        visitor(entity, nodeRect);
    }

    for (uint32_t childId : entity.GetChildIDs()) {
        if (Entity* child = scene.FindEntity(childId))
            VisitEntityRecursive(scene, *child, nodeRect, scaleFactor, visitor);
    }
}

} // namespace

void VisitCanvasUI(Scene& scene, Entity& canvasEntity, const CanvasComponent& canvas,
                   const UILayoutContext& ctx,
                   const std::function<void(Entity&, const UIRect&)>& visitor) {
    UIRect layoutParent = ctx.pixelSpace
                              ? UIRect{ 0.0f, 0.0f, ctx.viewportWidth, ctx.viewportHeight }
                              : GetCanvasReferenceRect(canvas, ctx.scaleFactor);

    if (auto* rootRt = canvasEntity.GetComponent<RectTransformComponent>())
        layoutParent = CalcRectInParent(layoutParent, *rootRt, ctx.scaleFactor);

    VisitEntityRecursive(scene, canvasEntity, layoutParent, ctx.scaleFactor, visitor);
}

float MeasureUITextWidth(ImFont* font, float fontSize, const char* text) {
    if (!font || !text || !*text)
        return 0.0f;
    return font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text).x;
}

namespace {

/// Baseline Y in layout space (+Y up) so glyph bounds are vertically centered in @p rect.
float LayoutBaselineForVerticalCenter(const UIRect& rect, ImFont* font, float fontSize) {
    const float centerY = rect.Center().y;
    if (!font)
        return centerY + fontSize * 0.5f;

    ImFontBaked* baked = font->GetFontBaked(fontSize);
    if (!baked || baked->Size <= 0.0f)
        return centerY + fontSize * 0.5f;

    const float scale = fontSize / baked->Size;
    const float ascent = baked->Ascent * scale;
    const float descent = baked->Descent * scale;
    return centerY + (ascent + descent) * 0.5f;
}

} // namespace

void CalcUITextDrawLayout(const UIRect& rect, UITextAlignment alignment, ImFont* font, float fontSize,
                          const char* text, float& outDrawX, float& outLayoutYTop) {
    const float textW = MeasureUITextWidth(font, fontSize, text);

    if (alignment == UITextAlignment::Center) {
        outDrawX = rect.minX + (rect.Width() - textW) * 0.5f;
        outLayoutYTop = LayoutBaselineForVerticalCenter(rect, font, fontSize);
    } else if (alignment == UITextAlignment::Right) {
        outDrawX = rect.maxX - textW;
        outLayoutYTop = rect.maxY;
    } else {
        outDrawX = rect.minX;
        outLayoutYTop = rect.maxY;
    }
}

glm::vec2 CalcUIGizmoPivotLayout(const UIRect& rect, const RectTransformComponent& rectTransform,
                               const UITextComponent* text, ImFont* font) {
    if (text && text->alignment == UITextAlignment::Center) {
        const float textW = MeasureUITextWidth(font, text->fontSize, text->text.c_str());
        float drawX = 0.0f;
        float layoutYTop = 0.0f;
        CalcUITextDrawLayout(rect, text->alignment, font, text->fontSize, text->text.c_str(), drawX,
                             layoutYTop);
        return { drawX + textW * 0.5f, rect.Center().y };
    }

    const glm::vec2 size = rect.Size();
    return { rect.minX + size.x * rectTransform.pivot.x, rect.minY + size.y * rectTransform.pivot.y };
}

bool TryGetEntityCanvasUIRect(Scene& scene, Entity& entity, int layoutWidth, int layoutHeight, UIRect& outRect) {
    Entity* canvasEntity = FindCanvasAncestor(scene, &entity);
    if (!canvasEntity)
        return false;

    auto* canvas = canvasEntity->GetComponent<CanvasComponent>();
    if (!canvas)
        return false;

    const int layoutW = std::max(layoutWidth, 1);
    const int layoutH = std::max(layoutHeight, 1);

    UILayoutContext ctx;
    ctx.viewportWidth = static_cast<float>(layoutW);
    ctx.viewportHeight = static_cast<float>(layoutH);
    ctx.scaleFactor = ComputeCanvasScaleFactor(*canvas, layoutW, layoutH);
    ctx.pixelSpace = false;

    bool found = false;
    VisitCanvasUI(scene, *canvasEntity, *canvas, ctx, [&](Entity& e, const UIRect& rect) {
        if (e.GetID() == entity.GetID()) {
            outRect = rect;
            found = true;
        }
    });
    return found;
}

} // namespace MipsyncEngine
