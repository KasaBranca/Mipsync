#include "AssetManager.h"
#include "Material.h"
#include "../renderer/Texture.h"
#include "../renderer/Mesh.h"
#include "../animation/SkeletalModel.h"
#include "../animation/AnimatorControllerIO.h"
#include "AssetThumbnail.h"
#include "../scene/Scene.h"
#include "../core/Log.h"
#include <filesystem>

namespace MipsyncEngine {

namespace fs = std::filesystem;

AssetManager& AssetManager::Get() {
    static AssetManager s_Instance;
    return s_Instance;
}

void AssetManager::SetProjectRoot(const std::string& root) {
    std::error_code ec;
    fs::path absRoot = fs::absolute(PathUtf8::FromString(root), ec);
    const std::string newRoot = ec ? root : PathUtf8::ToString(absRoot);
    if (newRoot == m_Root)
        return;
    m_Root = newRoot;
    Clear();
}

std::string AssetManager::ToProjectRelative(const std::string& anyPath) const {
    if (anyPath.empty() || m_Root.empty())
        return anyPath;
    std::error_code ec;
    fs::path full = fs::absolute(PathUtf8::FromString(anyPath), ec);
    if (ec) return anyPath;
    fs::path rel = fs::relative(full, PathUtf8::FromString(m_Root), ec);
    if (ec) return anyPath;
    if (!rel.empty()) {
        std::string s = PathUtf8::ToString(rel.generic_u8string().empty() ? rel : rel);
        // Use generic (forward-slash) path for project-relative storage.
        std::u8string g = rel.generic_u8string();
        std::string out(reinterpret_cast<const char*>(g.data()), g.size());
        if (out.rfind("..", 0) == 0) return anyPath;
        return out;
    }
    return anyPath;
}

std::string AssetManager::ToAbsolute(const std::string& projectRelativePath) const {
    if (projectRelativePath.empty()) return {};
    fs::path p = PathUtf8::FromString(projectRelativePath);
    if (p.is_absolute()) return PathUtf8::ToString(p);
    if (m_Root.empty()) return PathUtf8::ToString(p);
    return PathUtf8::ToString(PathUtf8::FromString(m_Root) / p);
}

std::shared_ptr<Texture> AssetManager::GetTexture(const std::string& projectRelativePath) {
    if (projectRelativePath.empty())
        return nullptr;

    if (m_FailedTexturePaths.count(projectRelativePath))
        return nullptr;

    auto it = m_Textures.find(projectRelativePath);
    if (it != m_Textures.end())
        return it->second;

    const std::string abs = ToAbsolute(projectRelativePath);
    std::error_code ec;
    if (!fs::exists(PathUtf8::FromString(abs), ec)) {
        MIPSYNC_WARN("Texture not found: {}", abs);
        m_FailedTexturePaths.insert(projectRelativePath);
        return nullptr;
    }

    TextureParams params;
    params.nearestFilter = true;
    params.maxSize = 256;
    auto tex = std::make_shared<Texture>(abs, params);
    if (tex->GetID() == 0) {
        MIPSYNC_WARN("Texture failed to upload: {}", abs);
        m_FailedTexturePaths.insert(projectRelativePath);
        return nullptr;
    }
    m_Textures.emplace(projectRelativePath, tex);
    return tex;
}

std::shared_ptr<Texture> AssetManager::GetSkyboxTexture(const std::string& projectRelativePath) {
    if (projectRelativePath.empty())
        return nullptr;

    if (m_FailedSkyboxTexturePaths.count(projectRelativePath))
        return nullptr;

    auto it = m_SkyboxTextures.find(projectRelativePath);
    if (it != m_SkyboxTextures.end())
        return it->second;

    const std::string abs = ToAbsolute(projectRelativePath);
    std::error_code ec;
    if (!fs::exists(PathUtf8::FromString(abs), ec)) {
        MIPSYNC_WARN("Skybox texture not found: {}", abs);
        m_FailedSkyboxTexturePaths.insert(projectRelativePath);
        return nullptr;
    }

    TextureParams params;
    params.nearestFilter = false;
    params.clampToEdge = true;
    params.maxSize = 4096;
    // Equirectangular skyboxes use shader-generated UVs where v=0 is the top
    // of the panorama. Do not apply the authored-texture flip used by regular
    // material textures, or the sky will appear upside-down.
    params.flipVerticallyOnLoad = false;
    auto tex = std::make_shared<Texture>(abs, params);
    if (tex->GetID() == 0) {
        MIPSYNC_WARN("Skybox texture failed to upload: {}", abs);
        m_FailedSkyboxTexturePaths.insert(projectRelativePath);
        return nullptr;
    }
    m_SkyboxTextures.emplace(projectRelativePath, tex);
    return tex;
}

void AssetManager::DropTexture(const std::string& projectRelativePath) {
    m_Textures.erase(projectRelativePath);
    m_SkyboxTextures.erase(projectRelativePath);
    m_FailedTexturePaths.erase(projectRelativePath);
    m_FailedSkyboxTexturePaths.erase(projectRelativePath);
}

std::shared_ptr<Mesh> AssetManager::GetMesh(const std::string& projectRelativePath) {
    if (projectRelativePath.empty())
        return nullptr;

    if (m_FailedMeshPaths.count(projectRelativePath))
        return nullptr;

    auto it = m_Meshes.find(projectRelativePath);
    if (it != m_Meshes.end())
        return it->second;

    const std::string abs = ToAbsolute(projectRelativePath);
    std::error_code ec;
    if (!fs::exists(PathUtf8::FromString(abs), ec)) {
        MIPSYNC_WARN("Mesh not found: {}", abs);
        m_FailedMeshPaths.insert(projectRelativePath);
        return nullptr;
    }

    auto loaded = std::make_shared<Mesh>(Mesh::LoadFromFile(abs));
    if (loaded->GetVertexCount() == 0) {
        MIPSYNC_WARN("Mesh load produced no vertices: {}", abs);
        m_FailedMeshPaths.insert(projectRelativePath);
        return nullptr;
    }

    m_Meshes.emplace(projectRelativePath, loaded);
    return loaded;
}

void AssetManager::DropMesh(const std::string& projectRelativePath) {
    m_Meshes.erase(projectRelativePath);
    m_FailedMeshPaths.erase(projectRelativePath);
    AssetThumbnail::Get().DropMeshThumbnail(projectRelativePath);
}

std::shared_ptr<SkeletalModelAsset> AssetManager::GetSkeletalModel(const std::string& projectRelativePath) {
    if (projectRelativePath.empty())
        return nullptr;

    if (m_FailedSkeletalModelPaths.count(projectRelativePath))
        return nullptr;

    auto it = m_SkeletalModels.find(projectRelativePath);
    if (it != m_SkeletalModels.end())
        return it->second;

    const std::string abs = ToAbsolute(projectRelativePath);
    std::error_code ec;
    if (!fs::exists(PathUtf8::FromString(abs), ec)) {
        MIPSYNC_WARN("Skeletal model not found: {}", abs);
        m_FailedSkeletalModelPaths.insert(projectRelativePath);
        return nullptr;
    }

    auto loaded = LoadSkeletalModelFromFile(abs);
    if (!loaded) {
        m_FailedSkeletalModelPaths.insert(projectRelativePath);
        return nullptr;
    }

    m_SkeletalModels.emplace(projectRelativePath, loaded);
    return loaded;
}

void AssetManager::DropSkeletalModel(const std::string& projectRelativePath) {
    m_SkeletalModels.erase(projectRelativePath);
    m_FailedSkeletalModelPaths.erase(projectRelativePath);
}

std::shared_ptr<AnimatorControllerAsset> AssetManager::GetAnimatorController(
    const std::string& projectRelPath, std::string& outError) {
    outError.clear();
    if (projectRelPath.empty())
        return nullptr;

    auto it = m_Controllers.find(projectRelPath);
    if (it != m_Controllers.end())
        return it->second;

    const std::string abs = ToAbsolute(projectRelPath);
    std::error_code ec;
    if (!fs::exists(PathUtf8::FromString(abs), ec)) {
        outError = "controller not found: " + abs;
        return nullptr;
    }

    auto loaded = LoadAnimatorController(abs, outError);
    if (!loaded)
        return nullptr;

    m_Controllers.emplace(projectRelPath, loaded);
    return loaded;
}

void AssetManager::DropAnimatorController(const std::string& projectRelPath) {
    m_Controllers.erase(projectRelPath);
}

void AssetManager::Clear() {
    m_Textures.clear();
    m_SkyboxTextures.clear();
    m_Meshes.clear();
    m_SkeletalModels.clear();
    m_Controllers.clear();
    m_FailedTexturePaths.clear();
    m_FailedSkyboxTexturePaths.clear();
    m_FailedMeshPaths.clear();
    m_FailedSkeletalModelPaths.clear();
    AssetThumbnail::Get().Clear();
}

void AssetManager::ApplyMaterialToMeshRenderer(MeshRendererComponent& mr, const Material& mat,
                                                const std::string& materialPath) {
    mr.materialPath = materialPath;
    mr.color = mat.color;
    mr.texturePath = mat.texturePath;
    mr.textureTiling = mat.mainTextureTiling;
    mr.textureOffset = mat.mainTextureOffset;
    mr.texture = mat.texturePath.empty() ? nullptr : GetTexture(mat.texturePath);
    mr.ps1PreviewTexture.reset();
    mr.ps1PreviewTexturePath.clear();
    if (!mr.texture)
        mr.texture = std::make_shared<Texture>(Texture::CreateCheckerboard(128, 16));
}

void AssetManager::ClearMeshRendererMaterial(MeshRendererComponent& mr) {
    mr.materialPath.clear();
    mr.color = { 1.0f, 1.0f, 1.0f, 1.0f };
    mr.texturePath.clear();
    mr.textureTiling = { 1.0f, 1.0f };
    mr.textureOffset = { 0.0f, 0.0f };
    mr.texture = nullptr;
    mr.ps1PreviewTexture.reset();
    mr.ps1PreviewTexturePath.clear();
}

void AssetManager::ApplyMaterialToSkinnedMeshRenderer(SkinnedMeshRendererComponent& mr, const Material& mat,
                                                      const std::string& materialPath) {
    mr.materialPath = materialPath;
    mr.color = mat.color;
    mr.texturePath = mat.texturePath;
    mr.textureTiling = mat.mainTextureTiling;
    mr.textureOffset = mat.mainTextureOffset;
    mr.texture = mat.texturePath.empty() ? nullptr : GetTexture(mat.texturePath);
    if (!mr.texture)
        mr.texture = std::make_shared<Texture>(Texture::CreateCheckerboard(128, 16));
}

void AssetManager::ClearSkinnedMeshRendererMaterial(SkinnedMeshRendererComponent& mr) {
    mr.materialPath.clear();
    mr.texturePath.clear();
    mr.textureTiling = { 1.0f, 1.0f };
    mr.textureOffset = { 0.0f, 0.0f };
    mr.texture = nullptr;
}

void AssetManager::ApplyMaterialToSceneUsers(Scene& scene,
                                              const std::string& projectRelativeMaterialPath,
                                              const Material& mat) {
    if (projectRelativeMaterialPath.empty())
        return;

    for (const auto& entity : scene.GetEntities()) {
        if (!entity) continue;
        auto* mr = entity->GetComponent<MeshRendererComponent>();
        if (mr && mr->materialPath == projectRelativeMaterialPath)
            ApplyMaterialToMeshRenderer(*mr, mat, projectRelativeMaterialPath);

        auto* sk = entity->GetComponent<SkinnedMeshRendererComponent>();
        if (sk && sk->materialPath == projectRelativeMaterialPath)
            ApplyMaterialToSkinnedMeshRenderer(*sk, mat, projectRelativeMaterialPath);
    }
}

} // namespace MipsyncEngine
