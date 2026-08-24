#pragma once

#include <imgui.h>

namespace MipsyncEngine {

/// Unity-style Game View render target (fixed resolution; panel letterboxes around it).
struct GameViewSettings {
    int renderWidth = 1024;
    int renderHeight = 768;
    int resolutionPreset = 4;
    bool syncCanvasReferenceResolution = true;
};

struct GameViewLetterbox {
    ImVec2 panelMin{ 0.0f, 0.0f };
    ImVec2 panelSize{ 0.0f, 0.0f };
    ImVec2 displayMin{ 0.0f, 0.0f };
    ImVec2 displaySize{ 0.0f, 0.0f };
    int renderWidth = 1920;
    int renderHeight = 1080;
};

GameViewLetterbox ComputeGameViewLetterbox(const ImVec2& panelMin, const ImVec2& panelSize,
                                          const GameViewSettings& settings);

int GetGameViewResolutionPresetCount();
const char* GetGameViewResolutionPresetLabel(int index);
void ApplyGameViewResolutionPreset(GameViewSettings& settings, int presetIndex);

void SyncSceneCanvasesToGameView(class Scene& scene, const GameViewSettings& settings);

} // namespace MipsyncEngine
