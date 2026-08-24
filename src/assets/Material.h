#pragma once
// ─────────────────────────────────────────────────
// Nostalty — Material asset
// .nmat JSON: { color, texture, tiling, offset }
// ─────────────────────────────────────────────────

#include <glm/glm.hpp>
#include <string>

namespace MipsyncEngine {

struct Material {
    glm::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
    std::string texturePath; // project-relative
    /// Main texture scale (Unity Tiling), default 1×1.
    glm::vec2 mainTextureTiling = { 1.0f, 1.0f };
    /// Main texture offset (Unity Offset), default 0×0.
    glm::vec2 mainTextureOffset = { 0.0f, 0.0f };

    /// Unity _MainTex_ST: (tiling.x, tiling.y, offset.x, offset.y).
    glm::vec4 MainTexST() const {
        return { mainTextureTiling.x, mainTextureTiling.y,
                 mainTextureOffset.x, mainTextureOffset.y };
    }

    static bool Save(const std::string& absPath, const Material& mat, std::string& outError);
    static bool Load(const std::string& absPath, Material& out, std::string& outError);
};

} // namespace MipsyncEngine
