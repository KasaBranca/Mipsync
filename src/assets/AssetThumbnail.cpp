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
#include "../audio/MidiFile.h"
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
#include <array>
#include <cmath>
#include <deque>

namespace fs = std::filesystem;

namespace MipsyncEngine {

namespace {

using json = nlohmann::json;

constexpr int kThumbSize = 128;
constexpr int kThumbCacheVersion = 8;

const char* AudioThumbnailPrefix(const std::string& path) {
    return IsMidiPath(path) ? "midi" : "audio";
}

std::vector<unsigned char> RenderMidiThumbnail(const MidiSequence& midi) {
    struct Note {
        uint32_t start, end;
        uint8_t key, channel, velocity;
    };
    std::vector<Note> notes;
    std::array<std::array<std::deque<size_t>, 128>, 16> held;
    std::array<std::vector<size_t>, 16> released;
    std::array<bool, 16> sustain{};
    int lowest = 127, highest = 0;
    auto release = [&](size_t index, uint32_t time) {
        auto& note = notes[index];
        if (sustain[note.channel])
            released[note.channel].push_back(index);
        else
            note.end = time;
    };
    for (const auto& event : midi.events) {
        const unsigned channel = event.status & 15;
        const unsigned type = event.status & 0xf0;
        if (type == 0x90 && event.data2) {
            held[channel][event.data1].push_back(notes.size());
            notes.push_back({event.timeUs, midi.durationUs, event.data1,
                             static_cast<uint8_t>(channel), event.data2});
            lowest = std::min(lowest, int(event.data1));
            highest = std::max(highest, int(event.data1));
        } else if (type == 0x80 || (type == 0x90 && !event.data2)) {
            auto& voices = held[channel][event.data1];
            if (!voices.empty()) {
                release(voices.front(), event.timeUs);
                voices.pop_front();
            }
        } else if (type == 0xb0) {
            if (event.data1 == 64) {
                sustain[channel] = event.data2 >= 64;
                if (!sustain[channel]) {
                    for (size_t index : released[channel]) notes[index].end = event.timeUs;
                    released[channel].clear();
                }
            } else if (event.data1 == 120 || event.data1 == 123) {
                for (auto& voices : held[channel]) {
                    for (size_t index : voices) {
                        if (event.data1 == 120) notes[index].end = event.timeUs;
                        else release(index, event.timeUs);
                    }
                    voices.clear();
                }
                if (event.data1 == 120) {
                    for (size_t index : released[channel]) notes[index].end = event.timeUs;
                    released[channel].clear();
                }
            }
        }
    }

    // Fit the played pitches, with at least two octaves for recognizable lanes.
    const int pitchCount = std::max(24, highest - lowest + 5);
    const int firstPitch = std::clamp((lowest + highest - pitchCount + 1) / 2,
                                      0, 128 - std::min(pitchCount, 128));
    const int lanes = std::min(pitchCount, 128);
    constexpr int keyWidth = 10, graphLeft = 12;
    const float laneHeight = float(kThumbSize) / lanes;
    auto blackKey = [](int pitch) {
        const int key = pitch % 12;
        return key == 1 || key == 3 || key == 6 || key == 8 || key == 10;
    };
    std::vector<unsigned char> pixels(size_t(kThumbSize) * kThumbSize * 4);
    auto put = [&](int x, int y, int r, int g, int b) {
        const size_t offset = (size_t(y) * kThumbSize + x) * 4;
        pixels[offset] = static_cast<unsigned char>(r);
        pixels[offset + 1] = static_cast<unsigned char>(g);
        pixels[offset + 2] = static_cast<unsigned char>(b);
        pixels[offset + 3] = 255;
    };
    for (int y = 0; y < kThumbSize; ++y) {
        const int lane = std::min(lanes - 1, int(y / laneHeight));
        const bool black = blackKey(firstPitch + lanes - 1 - lane);
        const bool boundary = y == int(std::ceil(lane * laneHeight));
        for (int x = 0; x < kThumbSize; ++x) {
            if (x < keyWidth) {
                const int shade = boundary ? 38 : (black && x < 7 ? 28 : 133);
                put(x, y, shade, shade, shade);
            } else if (x < graphLeft) {
                put(x, y, 21, 26, 32);
            } else {
                const bool grid = boundary || (x - graphLeft) % 16 == 0;
                put(x, y, grid ? 45 : (black ? 27 : 32),
                          grid ? 53 : (black ? 34 : 40),
                          grid ? 63 : (black ? 43 : 50));
            }
        }
    }
    for (const auto& note : notes) {
        const double duration = std::max(1u, midi.durationUs);
        const int x0 = std::clamp(graphLeft + int(note.start / duration * (kThumbSize - graphLeft)),
                                  graphLeft, kThumbSize - 1);
        const int x1 = std::clamp(graphLeft + int(std::ceil(note.end / duration * (kThumbSize - graphLeft))),
                                  x0 + 1, kThumbSize);
        const int lane = firstPitch + lanes - 1 - note.key;
        const int y0 = std::clamp(int(std::ceil(lane * laneHeight)) + (laneHeight >= 3 ? 1 : 0),
                                  0, kThumbSize - 1);
        const int y1 = std::clamp(int(std::ceil((lane + 1) * laneHeight)) - (laneHeight >= 3 ? 1 : 0),
                                  y0 + 1, kThumbSize);
        const int brightness = note.velocity * 30 / 127;
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x)
                put(x, y, 40 + brightness, 142 + brightness + (note.channel % 4) * 8, 220 + brightness);
    }
    return pixels;
}

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

    std::ofstream file(PathUtf8::FromString(thumbAbs), std::ios::binary | std::ios::trunc);
    const int written = file.is_open()
        ? stbi_write_png_to_func(
              [](void* context, void* data, int size) {
                  static_cast<std::ofstream*>(context)->write(
                      static_cast<const char*>(data), static_cast<std::streamsize>(size));
              },
              &file, kThumbSize, kThumbSize, 4, pixels.data(), kThumbSize * 4)
        : 0;
    if (!written || !file.good()) {
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
    return ext == ".fbx" || ext == ".nmodel" || ext == ".obj" || ext == ".gltf" || ext == ".glb";
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
    if (m_AudioThumbnailFailed.count(projectRelativePath))
        return nullptr;
    const std::string sourceAbs = AssetManager::Get().ToAbsolute(projectRelativePath);
    const std::string thumbAbs = ThumbnailCachePath(projectRelativePath, AudioThumbnailPrefix(projectRelativePath));
    auto it = m_AudioCache.find(projectRelativePath);
    if (it != m_AudioCache.end()) {
        if (!IsThumbnailStale(sourceAbs, thumbAbs))
            return it->second;
        DropAudioThumbnail(projectRelativePath);
    }
    auto thumbnail = LoadOrCreateAudio(projectRelativePath);
    if (!thumbnail)
        m_AudioThumbnailFailed.insert(projectRelativePath);
    return thumbnail;
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
    const std::string thumbAbs = ThumbnailCachePath(projectRelativePath, AudioThumbnailPrefix(projectRelativePath));
    if (sourceAbs.empty() || thumbAbs.empty())
        return nullptr;
    std::error_code ec;
    fs::create_directories(PathUtf8::FromString(thumbAbs).parent_path(), ec);
    if (!IsThumbnailStale(sourceAbs, thumbAbs)) {
        if (auto cached = LoadThumbnailTexture(thumbAbs, m_AudioCache, projectRelativePath))
            return cached;
    }

    std::vector<unsigned char> pixels;
    std::string error;
    if (IsMidiPath(sourceAbs)) {
        MidiSequence midi;
        if (!ReadMidi(sourceAbs, midi, error)) {
            MIPSYNC_WARN("MIDI piano-roll decode failed for {}: {}", projectRelativePath, error);
            return nullptr;
        }
        pixels = RenderMidiThumbnail(midi);
    } else {
        DecodedAudio audio;
        if (!DecodeAudioFile(sourceAbs, audio, error)) {
            MIPSYNC_WARN("Audio waveform decode failed for {}: {}", projectRelativePath, error);
            return nullptr;
        }
        pixels.resize(static_cast<size_t>(kThumbSize) * kThumbSize * 4);
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
    }
    std::ofstream file(PathUtf8::FromString(thumbAbs), std::ios::binary | std::ios::trunc);
    const int written = file.is_open()
        ? stbi_write_png_to_func(
              [](void* context, void* data, int size) {
                  static_cast<std::ofstream*>(context)->write(
                      static_cast<const char*>(data), static_cast<std::streamsize>(size));
              },
              &file, kThumbSize, kThumbSize, 4, pixels.data(), kThumbSize * 4)
        : 0;
    if (!written || !file.good())
        return nullptr;
    // Close before Texture opens the same PNG below.  Leaving the stream alive until
    // function exit made the first load race its buffered output and permanently cache
    // a false failure for this audio asset.
    file.flush();
    if (!file.good())
        return nullptr;
    file.close();
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

    const std::string ext = PathUtf8::ToString(PathUtf8::FromString(sourceAbs).extension());
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

    const bool isFbx = (ext == ".fbx" || ext == ".FBX" || ext == ".nmodel");

    // Skinned FBX must use the skeletal renderer (static Mesh::LoadFromFile is incomplete).
    std::shared_ptr<SkeletalModelAsset> skel;
    if (isFbx)
        skel = AssetManager::Get().GetSkeletalModel(projectRelativePath);

    auto mesh = ext == ".nmodel" ? std::shared_ptr<Mesh>{} : AssetManager::Get().GetMesh(projectRelativePath);
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
    m_AudioThumbnailFailed.erase(projectRelativePath);
    const std::string thumbAbs = ThumbnailCachePath(projectRelativePath, AudioThumbnailPrefix(projectRelativePath));
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
    m_AudioThumbnailFailed.clear();
    m_PrefabModelCache.clear();
    m_MeshThumbnailFailed.clear();
    m_MeshThumbnailSkipped.clear();
}

} // namespace MipsyncEngine
