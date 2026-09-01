#pragma once
// ─────────────────────────────────────────────────
// Nostalty — Asset preview thumbnails (project browser)
// ─────────────────────────────────────────────────

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace MipsyncEngine {

class Texture;
class Renderer;

class AssetThumbnail {
public:
    static AssetThumbnail& Get();

    std::shared_ptr<Texture> GetMeshThumbnail(const std::string& projectRelativePath, Renderer& renderer);
    std::shared_ptr<Texture> GetMaterialThumbnail(const std::string& projectRelativePath, Renderer& renderer);
    std::shared_ptr<Texture> GetAudioThumbnail(const std::string& projectRelativePath);
    /// Uses the first referenced model in a prefab. Returns null for model-less prefabs.
    std::shared_ptr<Texture> GetPrefabThumbnail(const std::string& projectRelativePath,
                                                Renderer& renderer);

    void DropMeshThumbnail(const std::string& projectRelativePath);
    void DropMaterialThumbnail(const std::string& projectRelativePath);
    void DropAudioThumbnail(const std::string& projectRelativePath);
    void DropPrefabThumbnail(const std::string& projectRelativePath);
    void Clear();

private:
    AssetThumbnail() = default;

    std::shared_ptr<Texture> LoadOrCreateMesh(const std::string& projectRelativePath, Renderer& renderer);
    std::shared_ptr<Texture> LoadOrCreateMaterial(const std::string& projectRelativePath, Renderer& renderer);
    std::shared_ptr<Texture> LoadOrCreateAudio(const std::string& projectRelativePath);

    std::string ThumbnailCachePath(const std::string& projectRelativePath, const char* prefix) const;
    bool IsThumbnailStale(const std::string& sourceAbs, const std::string& thumbAbs,
                          const std::vector<std::string>& extraSourceAbs = {}) const;
    bool MaterialThumbnailIsStale(const std::string& projectRelativePath) const;
    bool MeshThumbnailIsStale(const std::string& projectRelativePath) const;

    std::unordered_map<std::string, std::shared_ptr<Texture>> m_MeshCache;
    std::unordered_map<std::string, std::shared_ptr<Texture>> m_MaterialCache;
    std::unordered_map<std::string, std::shared_ptr<Texture>> m_AudioCache;
    /// Do not decode/write/load the same broken audio thumbnail every UI frame.
    std::unordered_set<std::string> m_AudioThumbnailFailed;
    struct PrefabModelCacheEntry {
        std::string modelPath;
        long long sourceStamp = 0;
    };
    std::unordered_map<std::string, PrefabModelCacheEntry> m_PrefabModelCache;
    /// Avoid re-loading huge FBX/OBJ every frame when thumbnail generation failed once.
    std::unordered_set<std::string> m_MeshThumbnailFailed;
    /// Skinned FBX: skip static mesh thumbnail path (use kind label instead).
    std::unordered_set<std::string> m_MeshThumbnailSkipped;
};

} // namespace MipsyncEngine
