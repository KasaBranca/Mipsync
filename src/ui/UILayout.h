#pragma once

#include "RectTransform.h"
#include <imgui.h>
#include <functional>

namespace MipsyncEngine {

class Scene;
class Entity;
class Camera;
struct CanvasComponent;
struct UITextComponent;
enum class UITextAlignment : uint8_t;

struct UILayoutContext {
    float viewportWidth = 1920.0f;
    float viewportHeight = 1080.0f;
    float scaleFactor = 1.0f;
    bool pixelSpace = true;
};

/// Depth-first UI layout under a canvas (includes components on the canvas entity).
void VisitCanvasUI(Scene& scene, Entity& canvasEntity, const CanvasComponent& canvas,
                   const UILayoutContext& ctx,
                   const std::function<void(Entity&, const UIRect&)>& visitor);

/// Map a UI rect (bottom-left pixel origin) to ImGui screen coordinates over a scene/game view image.
void UIRectToScreenOverlay(const UIRect& rect, float layoutW, float layoutH, const ImVec2& imageMin,
                           const ImVec2& imageSize, ImVec2& outMin, ImVec2& outMax);

/// Project a world point to Scene View panel screen coordinates (ImGui Y-down).
bool WorldPointToSceneViewScreen(const glm::vec3& world, const Camera& camera, const ImVec2& imageMin,
                                 const ImVec2& imageSize, ImVec2& out);

bool ProjectCanvasLayoutPoint(const glm::mat4& canvasWorld, float layoutX, float layoutY, const Camera& camera,
                              const ImVec2& imageMin, const ImVec2& imageSize, ImVec2& out);

/// Scene View: ImGui text aligned to the canvas plane (readable size, correct orientation).
void DrawCanvasAlignedText(ImDrawList* drawList, ImFont* font, const char* text, float layoutFontSize,
                           float layoutX, float layoutY, const glm::mat4& canvasWorld, const Camera& camera,
                           const ImVec2& imageMin, const ImVec2& imageSize, ImU32 color);

/// Layout rect for a single entity under its ancestor canvas (full parent chain).
bool TryGetEntityCanvasUIRect(Scene& scene, Entity& entity, int layoutWidth, int layoutHeight, UIRect& outRect);

float MeasureUITextWidth(ImFont* font, float fontSize, const char* text);

/// Unity-style: Center = horizontal + vertical middle of the rect; top-left origin for glyph layout.
void CalcUITextDrawLayout(const UIRect& rect, UITextAlignment alignment, ImFont* font, float fontSize,
                          const char* text, float& outDrawX, float& outLayoutYTop);

/// Gizmo / tool handle position in canvas layout space (+Y up).
glm::vec2 CalcUIGizmoPivotLayout(const UIRect& rect, const RectTransformComponent& rectTransform,
                                 const UITextComponent* text, ImFont* font);

} // namespace MipsyncEngine
