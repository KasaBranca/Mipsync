#include "GameViewSettings.h"
#include "../scene/Scene.h"
#include <algorithm>

namespace MipsyncEngine {

GameViewLetterbox ComputeGameViewLetterbox(const ImVec2& panelMin, const ImVec2& panelSize,
                                           const GameViewSettings& settings) {
    GameViewLetterbox result;
    result.panelMin = panelMin;
    result.panelSize = panelSize;
    result.renderWidth = std::max(settings.renderWidth, 1);
    result.renderHeight = std::max(settings.renderHeight, 1);

    const float targetAspect = static_cast<float>(result.renderWidth) / static_cast<float>(result.renderHeight);
    const float panelAspect =
        panelSize.y > 0.0f ? (panelSize.x / panelSize.y) : targetAspect;

    if (panelAspect > targetAspect) {
        result.displaySize.y = panelSize.y;
        result.displaySize.x = panelSize.y * targetAspect;
    } else {
        result.displaySize.x = panelSize.x;
        result.displaySize.y = panelSize.x / targetAspect;
    }

    result.displayMin.x = panelMin.x + (panelSize.x - result.displaySize.x) * 0.5f;
    result.displayMin.y = panelMin.y + (panelSize.y - result.displaySize.y) * 0.5f;
    return result;
}

void SyncSceneCanvasesToGameView(Scene& scene, const GameViewSettings& settings) {
    const glm::vec2 ref{ static_cast<float>(std::max(settings.renderWidth, 1)),
                         static_cast<float>(std::max(settings.renderHeight, 1)) };

    for (auto& entityPtr : scene.GetEntities()) {
        if (auto* canvas = entityPtr->GetComponent<CanvasComponent>())
            canvas->referenceResolution = ref;
    }
}

} // namespace MipsyncEngine
