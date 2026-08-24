#pragma once
// ─────────────────────────────────────────────────
// Nostalty — Project panel asset icons (ImDrawList)
// ─────────────────────────────────────────────────

#include <imgui.h>

#include <string>

namespace MipsyncEngine {

class Texture;

enum class AssetKind;

/// Draw texture in a cell preserving source aspect ratio (letterboxed), UV flipped for OpenGL.
void DrawTexturedImageAspectFit(ImDrawList* drawList, const Texture& tex, ImVec2 cellMin,
                                ImVec2 cellMax);

/// Bundled PNG under `resources/icons/project/` (see README there).
bool TryDrawProjectAssetIcon(ImDrawList* drawList, AssetKind kind, ImVec2 min, ImVec2 max,
                             const std::string& path = "");

/// Built-in vector icons when no PNG is present.
void DrawFolderIcon(ImDrawList* drawList, ImVec2 min, ImVec2 max);
void DrawScriptIcon(ImDrawList* drawList, ImVec2 min, ImVec2 max);

/// ProModeler custom vector icons
void DrawShapePresetIcon(ImDrawList* drawList, ImVec2 min, ImVec2 max, int shapeType);
void DrawEditModeIcon(ImDrawList* drawList, ImVec2 min, ImVec2 max, int mode);
void DrawExtrudeIcon(ImDrawList* drawList, ImVec2 min, ImVec2 max, int directionIndex);

} // namespace MipsyncEngine
