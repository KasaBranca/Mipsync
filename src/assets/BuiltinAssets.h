#pragma once
// ─────────────────────────────────────────────────
// Built-in resources (Unity "Built-in Resources" equivalent)
//
// Read-only assets shipped with the engine in <resources>/builtin and
// referenced from projects through the reserved project-relative root
// "builtin/". They are never copied into a project, cannot be edited,
// renamed, moved or deleted, and resolve to the same file in the editor,
// the protected player package and PS1 export.
// ─────────────────────────────────────────────────

#include <glm/glm.hpp>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace MipsyncEngine::BuiltinAssets {

/// Reserved first path component of every built-in reference.
inline constexpr const char* kRoot = "builtin";
inline constexpr const char* kDisplayName = "Built-in Resources";
/// Material every new MeshRenderer starts with (checkerboard).
inline constexpr const char* kDefaultMaterial = "builtin/default.nmat";
inline constexpr const char* kDefaultTexture = "builtin/default.png";

/// True for "builtin" itself and anything below it ('/' or '\\' separated).
bool IsBuiltinPath(std::string_view projectRelPath);

/// Folder holding the built-in files: <bundled resources>/builtin.
std::filesystem::path Directory();

/// Absolute file for a built-in reference; empty when the path is not
/// built-in or tries to leave the built-in folder.
std::filesystem::path Resolve(std::string_view projectRelPath);

/// Absolute path for any project asset reference: built-in references map
/// to the engine resources, everything else is joined to projectRoot.
std::filesystem::path ResolveAssetPath(const std::filesystem::path& projectRoot,
                                       std::string_view projectRelPath);

/// Reverse of Resolve: "builtin/<name>" for a file inside Directory(),
/// otherwise empty.
std::string ToBuiltinPath(const std::filesystem::path& absolute);

/// Built-in files as project-relative references, sorted by name.
std::vector<std::string> List();

/// A serialized MeshRenderer without a "material" key predates material
/// references. Old scenes (and the original project template) wrote
/// renderer-local appearance keys even when they only restated defaults,
/// e.g. "color": [1,1,1,1]. Only appearance that differs from those defaults
/// needs a migrated material; everything else uses the built-in default
/// material. Shared by scene loading and PS1 export so both agree.
inline bool LegacyAppearanceNeedsMaterial(const std::string& texturePath, const glm::vec4& color,
                                          const glm::vec2& tiling, const glm::vec2& offset) {
    return !texturePath.empty() || color != glm::vec4(1.0f) ||
           tiling != glm::vec2(1.0f) || offset != glm::vec2(0.0f);
}

} // namespace MipsyncEngine::BuiltinAssets
