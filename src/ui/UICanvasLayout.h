#pragma once

#include "RectTransform.h"
#include <glm/glm.hpp>

namespace MipsyncEngine {

class Scene;
class Entity;
class Camera;
struct CanvasComponent;

/// Pixels → world units for runtime / game camera canvases.
constexpr float kCanvasUnitsPerPixel = 0.01f;

UIRect GetCanvasReferenceRect(const CanvasComponent& canvas, float scaleFactor);

/// Scales canvas so it fits in the scene-view frustum at planeDistance (Unity-style editor preview).
float ComputeSceneViewCanvasUnitsPerPixel(const Camera& editorCamera, float planeDistance, float layoutWidth,
                                          float layoutHeight, float viewportAspect);

/// @param editorScenePreview When true, upright plane (world +Y up, +X right). When false, uses event/primary game camera.
glm::mat4 BuildCanvasWorldMatrix(Scene& scene, Entity& canvasEntity, const CanvasComponent& canvas,
                                 const Camera& viewCamera, float unitsPerPixel = kCanvasUnitsPerPixel,
                                 bool editorScenePreview = false);

/// Walks parents to find the Canvas that owns this UI element.
Entity* FindCanvasAncestor(Scene& scene, Entity* entity);

} // namespace MipsyncEngine
