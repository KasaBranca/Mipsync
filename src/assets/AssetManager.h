#pragma once
// ─────────────────────────────────────────────────
// Nostalty — Asset Manager
// Texture cache + project-relative path resolution
// ─────────────────────────────────────────────────

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace MipsyncEngine {

class Texture;
class Mesh;
class Scene;
struct Material;
struct MeshRendererComponent;
struct SkinnedMeshRendererComponent;
struct SkeletalModelAsset;
struct AnimatorControllerAsset;

/// UTF-8 / std::filesystem::path bridge (avoids MinGW's ANSI-by-default narrow conversion).
namespace PathUtf8 {
inline std::filesystem::path FromString(const std::string& s) {
    if (s.empty()) return {};
    const auto* begin = reinterpret_cast<const char8_t*>(s.data());
    const auto* end   = begin + s.size();
    return std::filesystem::path(begin, end);
}
inline std::string ToString(const std::filesystem::path& p) {
    auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}
inline std::string FromWide(const std::wstring& w) {
    return ToString(std::filesystem::path(w));
}
} // namespace PathUtf8

class AssetManager {
public:
    static AssetManager& Get();

    void SetProjectRoot(const std::string& root);
    const std::string& GetProjectRoot() const { return m_Root; }

    /// Convert "C:/proj/assets/foo.png" → "assets/foo.png" if inside the project, else returns the path unchanged.
    std::string ToProjectRelative(const std::string& anyPath) const;
    /// "assets/foo.png" → "<projectRoot>/assets/foo.png".
    std::string ToAbsolute(const std::string& projectRelativePath) const;

    std::shared_ptr<Texture> GetTexture(const std::string& projectRelativePath);
    /// High-quality texture path for editor backgrounds / HDRI skyboxes.
    /// Unlike gameplay textures, this keeps much more resolution and uses linear filtering.
    std::shared_ptr<Texture> GetSkyboxTexture(const std::string& projectRelativePath);
    void DropTexture(const std::string& projectRelativePath);

    std::shared_ptr<Mesh> GetMesh(const std::string& projectRelativePath);
    void DropMesh(const std::string& projectRelativePath);

    std::shared_ptr<SkeletalModelAsset> GetSkeletalModel(const std::string& projectRelativePath);
    void DropSkeletalModel(const std::string& projectRelativePath);

    std::shared_ptr<AnimatorControllerAsset> GetAnimatorController(const std::string& projectRelPath,
                                                                   std::string& outError);
    void DropAnimatorController(const std::string& projectRelPath);

    void Clear();

    /// Convenience: applies a material's fields onto a MeshRenderer (color + texture).
    void ApplyMaterialToMeshRenderer(MeshRendererComponent& mr, const Material& mat,
                                      const std::string& materialPath);
    void ApplyMaterialToSkinnedMeshRenderer(SkinnedMeshRendererComponent& mr, const Material& mat,
                                            const std::string& materialPath);
    void ClearMeshRendererMaterial(MeshRendererComponent& mr);
    void ClearSkinnedMeshRendererMaterial(SkinnedMeshRendererComponent& mr);

    /// Re-apply a material asset to every MeshRenderer that references it (live edit).
    void ApplyMaterialToSceneUsers(Scene& scene, const std::string& projectRelativeMaterialPath,
                                   const Material& mat);

private:
    AssetManager() = default;
    std::string m_Root;
    std::unordered_map<std::string, std::shared_ptr<Texture>> m_Textures;
    std::unordered_map<std::string, std::shared_ptr<Texture>> m_SkyboxTextures;
    std::unordered_map<std::string, std::shared_ptr<Mesh>> m_Meshes;
    std::unordered_map<std::string, std::shared_ptr<SkeletalModelAsset>> m_SkeletalModels;
    std::unordered_map<std::string, std::shared_ptr<AnimatorControllerAsset>> m_Controllers;
    std::unordered_set<std::string> m_FailedTexturePaths;
    std::unordered_set<std::string> m_FailedSkyboxTexturePaths;
    std::unordered_set<std::string> m_FailedMeshPaths;
    std::unordered_set<std::string> m_FailedSkeletalModelPaths;
};

} // namespace MipsyncEngine
