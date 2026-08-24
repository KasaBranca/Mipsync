#pragma once

#include <glm/glm.hpp>

namespace MipsyncEngine {

struct RectTransformComponent;
struct CanvasComponent;

/// Axis-aligned rectangle in parent space (origin bottom-left, Y up — same as OpenGL UI space).
struct UIRect {
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;

    float Width() const { return maxX - minX; }
    float Height() const { return maxY - minY; }
    glm::vec2 Size() const { return { Width(), Height() }; }
    glm::vec2 Center() const { return { (minX + maxX) * 0.5f, (minY + maxY) * 0.5f }; }
};

/// Unity-style rect layout from anchors, pivot, anchoredPosition, and sizeDelta.
UIRect CalcRectInParent(const UIRect& parent, const RectTransformComponent& rect, float scaleFactor = 1.0f);

/// Canvas Scaler: Scale With Screen Size / Constant Pixel Size.
float ComputeCanvasScaleFactor(const CanvasComponent& canvas, int screenW, int screenH);

} // namespace MipsyncEngine
