#include "AssetThumbnail.h"
#include "AssetManager.h"
#include "Material.h"
#include "../animation/SkeletalModel.h"
#include "../renderer/Renderer.h"
#include "../renderer/Mesh.h"
#include "../renderer/Texture.h"
#include "../renderer/Camera.h"
#include "../renderer/Framebuffer.h"
#include "../core/Log.h"
#include "../audio/AudioDecoder.h"
#include <stb_image_write.h>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <vector>
#include <cstring>

namespace fs = std::filesystem;

namespace MipsyncEngine {

namespace {

using json = nlohmann::json;

constexpr int kThumbSize = 128;
constexpr int kThumbCacheVersion = 8;

std::string SanitizeCacheName(std::string projectRel) {
    for (char& c : projectRel) {
        if (c == '/' || c == '\\')
            c = '_';
    }
    return projectRel;
}

const Texture& WhiteTexture() {
    static Texture white = Texture::CreateSolid(255, 255, 255, 255);
    return white;
}

const Mesh& PreviewSphere() {
    static Mesh sphere = Mesh::CreateSphere(0.5f, 32, 24);
    return sphere;
}

glm::mat4 ThumbnailModelMatrix(const Mesh& mesh) {
    const glm::vec3 extent = mesh.GetBoundsMax() - mesh.GetBoundsMin();
    const float maxExtent = std::max({ extent.x, extent.y, extent.z, 0.001f });
    const float scale = 1.6f / maxExtent;
    return glm::scale(glm::rotate(glm::mat4(1.0f), glm::radians(-28.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                      glm::vec3(scale));
}

void FrameCameraForMesh(const Mesh& mesh, Camera& camera) {
    const glm::vec3 center = (mesh.GetBoundsMin() + mesh.GetBoundsMax()) * 0.5f;
    const glm::vec3 extent = mesh.GetBoundsMax() - mesh.GetBoundsMin();
    const float radius = glm::length(extent) * 0.5f;

    const glm::vec3 eye = center + glm::normalize(glm::vec3(1.1f, 0.75f, 1.3f)) * (radius * 2.8f + 0.5f);
    camera.SetPerspective(28.0f, 1.0f, 0.05f, 100.0f);
    camera.SetPosition(eye);
    camera.LookAt(center);
}

void FrameCameraForSphere(Camera& camera) {
    camera.SetPerspective(32.0f, 1.0f, 0.05f, 50.0f);
    camera.SetPosition(glm::vec3(1.15f, 0.55f, 1.35f));
    camera.LookAt(glm::vec3(0.0f));
}

struct ThumbnailRenderScope {
    Renderer& renderer;
    PS1Settings savedPsx;
    RenderMode savedMode;
    glm::vec3 savedFogColor;

    explicit ThumbnailRenderScope(Renderer& r) : renderer(r) {
        savedPsx = renderer.GetPS1Settings();
        savedMode = renderer.GetRenderMode();
        savedFogColor = savedPsx.fogColor;

        PS1Settings thumbPsx = savedPsx;
        thumbPsx.vertexJitter = 0.0f;
        thumbPsx.affineMapping = false;
        thumbPsx.colorDepthLimit = false;
        thumbPsx.ditheringEnabled = false;
        thumbPsx.fogColor = glm::vec3(0.22f, 0.22f, 0.24f);
        renderer.GetPS1Settings() = thumbPsx;
        renderer.SetRenderMode(RenderMode::Shaded);
    }

    ~ThumbnailRenderScope() {
        renderer.GetPS1Settings() = savedPsx;
        renderer.GetPS1Settings().fogColor = savedFogColor;
        renderer.SetRenderMode(savedMode);
    }
};

bool CaptureFboToPng(Framebuffer& fbo, const std::string& thumbAbs) {
    fbo.Bind();
    std::vector<unsigned char> pixels(static_cast<size_t>(kThumbSize) * kThumbSize * 4);
    // Keep GL bottom-left row order in the PNG; ImGui UV + no stbi flip on reload = upright.
    glReadPixels(0, 0, kThumbSize, kThumbSize, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    fbo.Unbind();

    if (!stbi_write_png(thumbAbs.c_str(), kThumbSize, kThumbSize, 4, pixels.data(), kThumbSize * 4)) {
        MIPSYNC_WARN("Failed to write thumbnail: {}", thumbAbs);
        return false;
    }
    return true;
}

std::shared_ptr<Texture> LoadThumbnailTexture(const std::string& thumbAbs,
                                              std::unordered_map<std::string, std::shared_ptr<Texture>>& cache,
                                              const std::string& cacheKey) {
    TextureParams params;
    params.nearestFilter = false;
    params.maxSize = kThumbSize;
    params.flipVerticallyOnLoad = false; // GL-captured PNG; do not stbi-flip (would invert in UI)
    auto tex = std::make_shared<Texture>(thumbAbs, params);
    if (tex->GetID() == 0)
        return nullptr;
    cache[cacheKey] = tex;
    return tex;
}

bool IsModelAssetPath(const std::string& path) {
    std::string ext = PathUtf8::ToString(PathUtf8::FromString(path).extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb";
}

std::string FindPrefabModelPath(const json& entity) {
    if (!entity.is_object())
        return {};

    if (entity.contains("skinnedMeshRenderer")) {
        const json& skinned = entity["skinnedMeshRenderer"];
        if (skinned.is_object()) {
            const std::string model = skinned.value("model", std::string{});
            if (IsModelAssetPath(model))
                return model;
        }
    }
    if (entity.contains("meshRenderer")) {
        const json& mesh = entity["meshRenderer"];
        if (mesh.is_object()) {
            const std::string model = mesh.value("mesh", std::string{});
            if (IsModelAssetPath(model))
                return model;
        }
    }
    if (entity.contains("children") && entity["children"].is_array()) {
        for (const json& child : entity["children"]) {
            const std::string model = FindPrefabModelPath(child);
            if (!model.empty())
                return model;
        }
    }
    return {};
}

} // namespace

AssetThumbnail& AssetThumbnail::Get() {
    static AssetThumbnail instance;
    return instance;
}

std::string AssetThumbnail::ThumbnailCachePath(const std::string& projectRelativePath,
                                             const char* prefix) const {
    const std::string& root = AssetManager::Get().GetProjectRoot();
    if (root.empty())
        return {};

    const std::string name = std::string(prefix) + "_v" + std::to_string(kThumbCacheVersion) + "_" +
                             SanitizeCacheName(projectRelativePath) + ".png";
    fs::path dir = PathUtf8::FromString(root) / ".nostalty" / "thumbnails";
    return PathUtf8::ToString(dir / PathUtf8::FromString(name));
}

bool AssetThumbnail::MaterialThumbnailIsStale(const std::string& projectRelativePath) const {
    const std::string sourceAbs = AssetManager::Get().ToAbsolute(projectRelativePath);
    const std::string thumbAbs = ThumbnailCachePath(projectRelativePath, "mat");
    if (thumbAbs.empty() || sourceAbs.empty())
        return true;

    Material mat;
    std::string err;
    if (!Material::Load(sourceAbs, mat, err))
        return true;

    std::vector<std::string> deps;
    if (!mat.texturePath.empty())
        deps.push_back(AssetManager::Get().ToAbsolute(mat.texturePath));
    return IsThumbnailStale(sourceAbs, thumbAbs, deps);
}

bool AssetThumbnail::MeshThumbnailIsStale(const std::string& projectRelativePath) const {
    const std::string sourceAbs = AssetManager::Get().ToAbsolute(projectRelativePath);
    const std::string thumbAbs = ThumbnailCachePath(projectRelativePath, "mesh");
    if (thumbAbs.empty() || sourceAbs.empty())
        return true;
    return IsThumbnailStale(sourceAbs, thumbAbs);
}

bool AssetThumbnail::IsThumbnailStale(const std::string& sourceAbs, const std::string& thumbAbs,
                                    const std::vector<std::string>& extraSourceAbs) const {
    std::error_code ec;
    if (!fs::exists(PathUtf8::FromString(thumbAbs), ec))
        return true;
    if (!fs::exists(PathUtf8::FromString(sourceAbs), ec))
        return true;

    auto newest = fs::last_write_time(PathUtf8::FromString(sourceAbs), ec);
    if (ec)
        return true;

    for (const std::string& dep : extraSourceAbs) {
        if (dep.empty())
            continue;
        if (!fs::exists(PathUtf8::FromString(dep), ec))
            continue;
        const auto t = fs::last_write_time(PathUtf8::FromString(dep), ec);
        if (!ec && t > newest)
            newest = t;
    }

    const auto thumbTime = fs::last_write_time(PathUtf8::FromString(thumbAbs), ec);
    if (ec)
        return true;
    return thumbTime < newest;
}

std::shared_ptr<Texture> AssetThumbnail::GetMeshThumbnail(const std::string& projectRelativePath,
                                                          Renderer& renderer) {
    if (projectRelativePath.empty())
        return nullptr;

    // If we already failed to generate a thumbnail for this mesh, do not retry every frame.
    // Retry only when the source file changes (stale) or when the user explicitly drops thumbnails.
    if (m_MeshThumbnailFailed.count(projectRelativePath)) {
        if (!MeshThumbnailIsStale(projectRelativePath))
            return nullptr;
        m_MeshThumbnailFailed.erase(projectRelativePath);
    }

    // Allow retry next frame until we successfully cache a thumbnail.
    // (Avoid permanent "script icon" fallback after a transient load/render failure.)

    auto it = m_MeshCache.find(projectRelativePath);
    if (it != m_MeshCache.end()) {
        if (!MeshThumbnailIsStale(projectRelativePath))
            return it->second;
        DropMeshThumbnail(projectRelativePath);
    }

    return LoadOrCreateMesh(projectRelativePath, renderer);
}

std::shared_ptr<Texture> AssetThumbnail::GetMaterialThumbnail(const std::string& projectRelativePath,
                                                              Renderer& renderer) {
    if (projectRelativePath.empty())
        return nullptr;

    auto it = m_MaterialCache.find(projectRelativePath);
    if (it != m_MaterialCache.end()) {
        if (!MaterialThumbnailIsStale(projectRelativePath))
            return it->second;
        DropMaterialThumbnail(projectRelativePath);
    }

    return LoadOrCreateMaterial(projectRelativePath, renderer);
}

std::shared_ptr<Texture> AssetThumbnail::GetAudioThumbnail(const std::string& projectRelativePath) {
    if (projectRelativePath.empty())
        return nullptr;
    const std::string sourceAbs = AssetManager::Get().ToAbsolute(projectRelativePath);
    const std::string thumbAbs = ThumbnailCachePath(projectRelativePath, "audio");
    auto it = m_AudioCache.find(projectRelativePath);
    if (it != m_AudioCache.end()) {
        if (!IsThumbnailStale(sourceAbs, thumbAbs))
            return it->second;
        DropAudioThumbnail(projectRelativePath);
    }
    return LoadOrCreateAudio(projectRelativePath);
}

std::shared_ptr<Texture> AssetThumbnail::GetPrefabThumbnail(
    const std::string& projectRelativePath, Renderer& renderer) {
    const std::string sourceAbs = AssetManager::Get().ToAbsolute(projectRelativePath);
    std::error_code ec;
    const auto writeTime = fs::last_write_time(PathUtf8::FromString(sourceAbs), ec);
    if (ec)
        return nullptr;
    const long long stamp = static_cast<long long>(writeTime.time_since_epoch().count());

    auto cached = m_PrefabModelCache.find(projectRelativePath);
    if (cached == m_PrefabModelCache.end() || cached->second.sourceStamp != stamp) {
        PrefabModelCacheEntry resolved;
        resolved.sourceStamp = stamp;
        try {
            std::ifstream file(PathUtf8::FromString(sourceAbs));
            json root;
            if (file.is_open()) {
                file >> root;
                if (root.contains("entity"))
                    resolved.modelPath = FindPrefabModelPath(root["entity"]);
                if (!resolved.modelPath.empty())
                    resolved.modelPath = AssetManager::Get().ToProjectRelative(resolved.modelPath);
            }
        } catch (const std::exception& ex) {
            MIPSYNC_WARN("Prefab thumbnail parse failed for {}: {}", projectRelativePath, ex.what());
        }
        cached = m_PrefabModelCache.insert_or_assign(projectRelativePath, std::move(resolved)).first;
    }

    if (cached->second.modelPath.empty())
        return nullptr;
    return GetMeshThumbnail(cached->second.modelPath, renderer);
}

std::shared_ptr<Texture> AssetThumbnail::LoadOrCreateAudio(const std::string& projectRelativePath) {
    const std::string sourceAbs = AssetManager::Get().ToAbsolute(projectRelativePath);
    const std::string thumbAbs = ThumbnailCachePath(projectRelativePath, "audio");
    if (sourceAbs.empty() || thumbAbs.empty())
        return nullptr;
    std::error_code ec;
    fs::create_directories(PathUtf8::FromString(thumbAbs).parent_path(), ec);
    if (!IsThumbnailStale(sourceAbs, thumbAbs)) {
        if (auto cached = LoadThumbnailTexture(thumbAbs, m_AudioCache, projectRelativePath))
            return cached;
    }

    DecodedAudio audio;
    std::string error;
    if (!DecodeAudioFile(sourceAbs, audio, error)) {
        MIPSYNC_WARN("Audio waveform decode failed for {}: {}", projectRelativePath, error);
        return nullptr;
    }

    std::vector<unsigned char> pixels(static_cast<size_t>(kThumbSize) * kThumbSize * 4);
    for (int y = 0; y < kThumbSize; ++y) {
        for (int x = 0; x < kThumbSize; ++x) {
            const size_t p = (static_cast<size_t>(y) * kThumbSize + x) * 4;
            const bool grid = (x % 16 == 0) || (y == kThumbSize / 2);
            pixels[p + 0] = grid ? 39 : 31;
            pixels[p + 1] = grid ? 50 : 39;
            pixels[p + 2] = grid ? 59 : 47;
            pixels[p + 3] = 255;
        }
    }
    for (int x = 0; x < kThumbSize; ++x) {
        const size_t begin = (audio.monoSamples.size() * static_cast<size_t>(x)) / kThumbSize;
        const size_t end = std::max(begin + 1,
            (audio.monoSamples.size() * static_cast<size_t>(x + 1)) / kThumbSize);
        float lo = 0.0f, hi = 0.0f;
        for (size_t i = begin; i < std::min(end, audio.monoSamples.size()); ++i) {
            lo = std::min(lo, audio.monoSamples[i]);
            hi = std::max(hi, audio.monoSamples[i]);
        }
        int y0 = std::clamp(static_cast<int>((0.5f - hi * 0.45f) * kThumbSize), 1, kThumbSize - 2);
        int y1 = std::clamp(static_cast<int>((0.5f - lo * 0.45f) * kThumbSize), 1, kThumbSize - 2);
        if (y1 < y0) std::swap(y0, y1);
        if (y1 == y0) ++y1;
        for (int y = y0; y <= y1; ++y) {
            const size_t p = (static_cast<size_t>(y) * kThumbSize + x) * 4;
            pixels[p + 0] = 72; pixels[p + 1] = 196; pixels[p + 2] = 211; pixels[p + 3] = 255;
        }
    }
    if (!stbi_write_png(thumbAbs.c_str(), kThumbSize, kThumbSize, 4,
                        pixels.data(), kThumbSize * 4))
        return nullptr;
    return LoadThumbnailTexture(thumbAbs, m_AudioCache, projectRelativePath);
}

std::shared_ptr<Texture> AssetThumbnail::LoadOrCreateMesh(const std::string& projectRelativePath,
                                                          Renderer& renderer) {
    const std::string sourceAbs = AssetManager::Get().ToAbsolute(projectRelativePath);
    const std::string thumbAbs = ThumbnailCachePath(projectRelativePath, "mesh");
    if (thumbAbs.empty()) {
        m_MeshThumbnailFailed.insert(projectRelativePath);
        return nullptr;
    }

    const std::string ext = sourceAbs.size() >= 4 ? sourceAbs.substr(sourceAbs.size() - 4) : std::string{};
    if (ext == ".fbx" || ext == ".FBX") {
        // Previously we skipped skinned FBX thumbnails (fallback icon only).
        // This made many FBX files look like "non-model" assets in the Project panel.
        // Try generating a thumbnail anyway; if mesh extraction fails we'll fall back naturally.
    }

    std::error_code ec;
    fs::create_directories(fs::path(PathUtf8::FromString(thumbAbs)).parent_path(), ec);

    if (!IsThumbnailStale(sourceAbs, thumbAbs)) {
        if (auto cached = LoadThumbnailTexture(thumbAbs, m_MeshCache, projectRelativePath))
            return cached;
    }

    const bool isFbx = (ext == ".fbx" || ext == ".FBX");

    // Skinned FBX must use the skeletal renderer (static Mesh::LoadFromFile is incomplete).
    std::shared_ptr<SkeletalModelAsset> skel;
    if (isFbx)
        skel = AssetManager::Get().GetSkeletalModel(projectRelativePath);

    auto mesh = AssetManager::Get().GetMesh(projectRelativePath);
    const bool hasStaticMesh = mesh && mesh->GetVertexCount() > 0;
    const bool hasSkelMesh = skel && skel->mesh && !skel->bones.empty();

    Framebuffer fbo(kThumbSize, kThumbSize);
    ThumbnailRenderScope scope(renderer);
    Camera camera;

    if (hasSkelMesh) {
        // Skinned FBX: bake rest pose to CPU mesh, then use the standard mesh thumbnail path
        // (GPU skinned draw in an offscreen FBO is unreliable on some drivers).
        MIPSYNC_INFO("Generating skeletal mesh thumbnail: {}", projectRelativePath);

        glm::mat4 bones[kMaxBones];
        const int stackIndex =
            !skel->animationStackIndices.empty() ? static_cast<int>(skel->animationStackIndices[0]) : 0;
        skel->EvaluateBoneMatricesByStackIndex(stackIndex, 0.0, bones);
        skel->UpdateCpuDisplayMesh(bones);

        if (!skel->cpuDisplayMesh || skel->cpuDisplayMesh->GetVertexCount() == 0) {
            MIPSYNC_WARN("Skeletal mesh thumbnail: no CPU display geometry for {}", projectRelativePath);
            m_MeshThumbnailFailed.insert(projectRelativePath);
            return nullptr;
        }

        mesh = skel->cpuDisplayMesh;
        FrameCameraForMesh(*mesh, camera);

        renderer.BeginScene(camera, &fbo);
        renderer.DrawMesh(*mesh, ThumbnailModelMatrix(*mesh), &WhiteTexture(),
                          glm::vec4(0.92f, 0.92f, 0.94f, 1.0f));
        renderer.EndScene(&fbo);
    } else if (hasStaticMesh) {
        FrameCameraForMesh(*mesh, camera);

        renderer.BeginScene(camera, &fbo);
        renderer.DrawMesh(*mesh, ThumbnailModelMatrix(*mesh), &WhiteTexture(),
                          glm::vec4(0.92f, 0.92f, 0.94f, 1.0f));
        renderer.EndScene(&fbo);
    } else {
        MIPSYNC_WARN("Mesh thumbnail skipped (no renderable geometry): {}", projectRelativePath);
        m_MeshThumbnailFailed.insert(projectRelativePath);
        return nullptr;
    }

    if (!CaptureFboToPng(fbo, thumbAbs)) {
        MIPSYNC_WARN("Mesh thumbnail capture failed: {}", projectRelativePath);
        m_MeshThumbnailFailed.insert(projectRelativePath);
        return nullptr;
    }

    MIPSYNC_INFO("Generated mesh thumbnail: {}", projectRelativePath);
    if (auto tex = LoadThumbnailTexture(thumbAbs, m_MeshCache, projectRelativePath))
        return tex;
    m_MeshThumbnailFailed.insert(projectRelativePath);
    return nullptr;
}

std::shared_ptr<Texture> AssetThumbnail::LoadOrCreateMaterial(const std::string& projectRelativePath,
                                                              Renderer& renderer) {
    const std::string sourceAbs = AssetManager::Get().ToAbsolute(projectRelativePath);
    const std::string thumbAbs = ThumbnailCachePath(projectRelativePath, "mat");
    if (thumbAbs.empty())
        return nullptr;

    Material mat;
    std::string err;
    if (!Material::Load(sourceAbs, mat, err))
        return nullptr;

    std::vector<std::string> deps;
    if (!mat.texturePath.empty())
        deps.push_back(AssetManager::Get().ToAbsolute(mat.texturePath));

    std::error_code ec;
    fs::create_directories(fs::path(PathUtf8::FromString(thumbAbs)).parent_path(), ec);

    if (!IsThumbnailStale(sourceAbs, thumbAbs, deps)) {
        if (auto cached = LoadThumbnailTexture(thumbAbs, m_MaterialCache, projectRelativePath))
            return cached;
    }

    const Texture* albedo = &WhiteTexture();
    if (!mat.texturePath.empty()) {
        if (auto tex = AssetManager::Get().GetTexture(mat.texturePath))
            if (tex->GetID() != 0)
                albedo = tex.get();
    }

    Framebuffer fbo(kThumbSize, kThumbSize);
    ThumbnailRenderScope scope(renderer);

    Camera camera;
    FrameCameraForSphere(camera);

    renderer.BeginScene(camera, &fbo);
    renderer.DrawMesh(PreviewSphere(), glm::mat4(1.0f), albedo, mat.color,
                      mat.mainTextureTiling, mat.mainTextureOffset);
    renderer.EndScene(&fbo);

    if (!CaptureFboToPng(fbo, thumbAbs))
        return nullptr;

    MIPSYNC_INFO("Generated material thumbnail: {}", projectRelativePath);
    return LoadThumbnailTexture(thumbAbs, m_MaterialCache, projectRelativePath);
}

void AssetThumbnail::DropMeshThumbnail(const std::string& projectRelativePath) {
    m_MeshCache.erase(projectRelativePath);
    m_MeshThumbnailFailed.erase(projectRelativePath);
    m_MeshThumbnailSkipped.erase(projectRelativePath);
    const std::string thumbAbs = ThumbnailCachePath(projectRelativePath, "mesh");
    if (!thumbAbs.empty()) {
        std::error_code ec;
        fs::remove(PathUtf8::FromString(thumbAbs), ec);
    }
}

void AssetThumbnail::DropMaterialThumbnail(const std::string& projectRelativePath) {
    m_MaterialCache.erase(projectRelativePath);
    const std::string thumbAbs = ThumbnailCachePath(projectRelativePath, "mat");
    if (!thumbAbs.empty()) {
        std::error_code ec;
        fs::remove(PathUtf8::FromString(thumbAbs), ec);
    }
}

void AssetThumbnail::DropAudioThumbnail(const std::string& projectRelativePath) {
    m_AudioCache.erase(projectRelativePath);
    const std::string thumbAbs = ThumbnailCachePath(projectRelativePath, "audio");
    if (!thumbAbs.empty()) {
        std::error_code ec;
        fs::remove(PathUtf8::FromString(thumbAbs), ec);
    }
}

void AssetThumbnail::DropPrefabThumbnail(const std::string& projectRelativePath) {
    m_PrefabModelCache.erase(projectRelativePath);
}

void AssetThumbnail::Clear() {
    m_MeshCache.clear();
    m_MaterialCache.clear();
    m_AudioCache.clear();
    m_PrefabModelCache.clear();
    m_MeshThumbnailFailed.clear();
    m_MeshThumbnailSkipped.clear();
}

} // namespace MipsyncEngine
