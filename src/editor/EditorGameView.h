#pragma once

#include "GameViewSettings.h"

namespace MipsyncEngine {
struct RenderStatistics;
struct PSyncState;

bool DrawGameViewControls(GameViewSettings& settings, const PSyncState* pSync = nullptr);
bool BeginMaximizedGameView(const ImVec2& position, const ImVec2& size, bool focus);
void DrawGameViewStatistics(const ImVec2& panelMin, const ImVec2& panelSize,
                            const GameViewSettings& settings, const RenderStatistics& statistics,
                            size_t activeObjects, float frameRate, const PSyncState* pSync = nullptr);
} // namespace MipsyncEngine
