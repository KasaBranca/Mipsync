#pragma once

#include <imgui.h>

namespace MipsyncEngine {

/// Unity-style Game View render target (fixed resolution; panel letterboxes around it).
struct GameViewSettings {
    static constexpr int kRenderWidth = 1024;
    static constexpr int kRenderHeight = 768;

    int renderWidth = kRenderWidth;
    int renderHeight = kRenderHeight;
};

struct GameViewLetterbox {
    ImVec2 panelMin{ 0.0f, 0.0f };
    ImVec2 panelSize{ 0.0f, 0.0f };
    ImVec2 displayMin{ 0.0f, 0.0f };
    ImVec2 displaySize{ 0.0f, 0.0f };
    int renderWidth = GameViewSettings::kRenderWidth;
    int renderHeight = GameViewSettings::kRenderHeight;
};

GameViewLetterbox ComputeGameViewLetterbox(const ImVec2& panelMin, const ImVec2& panelSize,
                                          const GameViewSettings& settings);

void SyncSceneCanvasesToGameView(class Scene& scene, const GameViewSettings& settings);

} // namespace MipsyncEngine
