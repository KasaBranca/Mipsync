#include "GameViewSettings.h"
#include "../scene/Scene.h"
#include <algorithm>

namespace MipsyncEngine {

namespace {

struct ResolutionPreset {
    const char* label;
    int width;
    int height;
};

constexpr ResolutionPreset kPresets[] = {
    { "16:9  1920 x 1080", 1920, 1080 },
    { "16:9  1280 x 720",  1280, 720  },
    { "16:9   960 x 540",   960, 540  },
    { "16:10 1920 x 1200", 1920, 1200 },
    { "4:3  1024 x 768",   1024, 768  },
    { "Custom",               0, 0   },
};

constexpr int kPresetCount = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));

} // namespace

int GetGameViewResolutionPresetCount() { return kPresetCount; }

const char* GetGameViewResolutionPresetLabel(int index) {
    index = std::clamp(index, 0, kPresetCount - 1);
    return kPresets[index].label;
}

void ApplyGameViewResolutionPreset(GameViewSettings& settings, int presetIndex) {
    settings.resolutionPreset = std::clamp(presetIndex, 0, kPresetCount - 1);
    if (settings.resolutionPreset != kPresetCount - 1) {
        settings.renderWidth = kPresets[settings.resolutionPreset].width;
        settings.renderHeight = kPresets[settings.resolutionPreset].height;
    }
}

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
    if (!settings.syncCanvasReferenceResolution)
        return;

    const glm::vec2 ref{ static_cast<float>(std::max(settings.renderWidth, 1)),
                         static_cast<float>(std::max(settings.renderHeight, 1)) };

    for (auto& entityPtr : scene.GetEntities()) {
        if (auto* canvas = entityPtr->GetComponent<CanvasComponent>())
            canvas->referenceResolution = ref;
    }
}

} // namespace MipsyncEngine
