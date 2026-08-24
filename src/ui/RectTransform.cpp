#include "RectTransform.h"
#include "../scene/Scene.h"
#include <cmath>

namespace MipsyncEngine {

float ComputeCanvasScaleFactor(const CanvasComponent& canvas, int screenW, int screenH) {
    switch (canvas.scaleMode) {
    case UICanvasScaleMode::ConstantPixelSize:
        return 1.0f;
    case UICanvasScaleMode::ScaleWithScreenSize:
    default:
        if (canvas.referenceResolution.x <= 0.0f || canvas.referenceResolution.y <= 0.0f)
            return 1.0f;
        const float sx = static_cast<float>(screenW) / canvas.referenceResolution.x;
        const float sy = static_cast<float>(screenH) / canvas.referenceResolution.y;
        const float logSx = std::log(sx);
        const float logSy = std::log(sy);
        const float logBlend = glm::mix(logSx, logSy, glm::clamp(canvas.matchWidthOrHeight, 0.0f, 1.0f));
        return std::exp(logBlend);
    }
}

UIRect CalcRectInParent(const UIRect& parent, const RectTransformComponent& rect, float scaleFactor) {
    const glm::vec2 parentSize = parent.Size();
    const glm::vec2 anchorMinPos = { parent.minX + parentSize.x * rect.anchorMin.x,
                                     parent.minY + parentSize.y * rect.anchorMin.y };
    const glm::vec2 anchorMaxPos = { parent.minX + parentSize.x * rect.anchorMax.x,
                                     parent.minY + parentSize.y * rect.anchorMax.y };
    const glm::vec2 anchorCenter = (anchorMinPos + anchorMaxPos) * 0.5f;

    const glm::vec2 size = {
        parentSize.x * (rect.anchorMax.x - rect.anchorMin.x) + rect.sizeDelta.x * scaleFactor,
        parentSize.y * (rect.anchorMax.y - rect.anchorMin.y) + rect.sizeDelta.y * scaleFactor,
    };

    const glm::vec2 anchored = rect.anchoredPosition * scaleFactor;
    const glm::vec2 pivotPos = anchorCenter + anchored;
    const glm::vec2 rectMin = pivotPos - glm::vec2(size.x * rect.pivot.x, size.y * rect.pivot.y);

    return { rectMin.x, rectMin.y, rectMin.x + size.x, rectMin.y + size.y };
}

} // namespace MipsyncEngine
