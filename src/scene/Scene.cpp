#include "Scene.h"
#include "../animation/AnimationTypes.h"
#include "../animation/AnimatorRuntime.h"
#include "../animation/SkeletalModel.h"
#include "../renderer/Renderer.h"
#include <algorithm>
#include "../mips/Bytecode.h"
#include "../assets/AssetManager.h"
#include "../assets/AssetThumbnail.h"
#include "../assets/Material.h"
#include "../mips/PS1SceneExport.h"
#include "../mips/PS1TextureExport.h"
#include "../renderer/Mesh.h"
#include "../renderer/Texture.h"
#include "../renderer/Renderer.h"
#include "../renderer/Framebuffer.h"
#include "../core/Log.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <functional>
#include <array>
#include <unordered_map>
#include <utility>
#include <cmath>
#include <filesystem>

namespace MipsyncEngine {

namespace {

constexpr uint32_t kPs1PreviewMeshVersion = 16;

void EnsurePs1PreviewMesh(MeshRendererComponent& meshRenderer) {
    if (meshRenderer.meshPrimitive != "File" || !meshRenderer.mesh)
        return;
    if (meshRenderer.ps1PreviewMesh && meshRenderer.ps1PreviewMeshVersion == kPs1PreviewMeshVersion)
        return;
    meshRenderer.ps1PreviewMesh = Mips::BuildPs1PreviewMesh(*meshRenderer.mesh, true);
    meshRenderer.ps1PreviewMeshVersion = kPs1PreviewMeshVersion;
}

void EnsurePs1RigidPreview(SkinnedMeshRendererComponent& skinned) {
    if (!skinned.mesh)
        return;
    if (skinned.ps1RigidPreviewMesh && skinned.ps1RigidPreviewVersion == kPs1PreviewMeshVersion)
        return;

    std::shared_ptr<SkeletalModelAsset> skel;
    if (!skinned.modelPath.empty())
        skel = AssetManager::Get().GetSkeletalModel(skinned.modelPath);
    if (!skel || !skel->mesh || skel->mesh->GetIndexCount() == 0)
        return;

    skinned.ps1RigidPreviewMesh = Mips::BuildPs1RigidBonePreview(
        *skel, skinned.meshPartIndex, skinned.ps1SeamFill,
        skinned.ps1VertexAnimTargetTris, skinned.ps1VertexAnimTargetVerts);
    skinned.ps1RigidPreviewVersion = kPs1PreviewMeshVersion;
}

uint8_t Expand5To8(uint16_t v) {
    v &= 0x1Fu;
    return static_cast<uint8_t>((v << 3) | (v >> 2));
}

std::shared_ptr<Texture> BuildPs1PreviewTexture(const std::string& projectRelPath) {
    if (projectRelPath.empty())
        return nullptr;

    Mips::PsxExportedTexture tex;
    std::string err;
    if (!Mips::LoadPsxTextureFromFile(AssetManager::Get().ToAbsolute(projectRelPath), 128, tex, err)) {
        MIPSYNC_WARN("PS1 preview texture skipped '{}': {}", projectRelPath, err);
        return nullptr;
    }
    if (tex.width <= 0 || tex.height <= 0 || tex.pixels565.empty())
        return nullptr;

    std::vector<unsigned char> rgba(static_cast<size_t>(tex.width) *
                                    static_cast<size_t>(tex.height) * 4u);
    for (size_t i = 0; i < tex.pixels565.size(); ++i) {
        const uint16_t px = tex.pixels565[i] & 0x7FFFu;
        const size_t off = i * 4u;
        if (px == 0) {
            rgba[off + 0] = 0;
            rgba[off + 1] = 0;
            rgba[off + 2] = 0;
            rgba[off + 3] = 0;
            continue;
        }
        rgba[off + 0] = Expand5To8(px);
        rgba[off + 1] = Expand5To8(px >> 5);
        rgba[off + 2] = Expand5To8(px >> 10);
        rgba[off + 3] = 255;
    }

    TextureParams params;
    params.nearestFilter = true;
    params.clampToEdge = false;
    params.maxSize = 0;
    return std::make_shared<Texture>(tex.width, tex.height, rgba.data(), 4, params);
}

const Texture* ResolveSceneViewTexture(MeshRendererComponent& meshRenderer, bool usePs1PreviewMeshes) {
    if (!usePs1PreviewMeshes || meshRenderer.texturePath.empty())
        return meshRenderer.texture.get();

    if (!meshRenderer.ps1PreviewTexture ||
        meshRenderer.ps1PreviewTexturePath != meshRenderer.texturePath) {
        meshRenderer.ps1PreviewTexture = BuildPs1PreviewTexture(meshRenderer.texturePath);
        meshRenderer.ps1PreviewTexturePath = meshRenderer.texturePath;
    }
    return meshRenderer.ps1PreviewTexture ? meshRenderer.ps1PreviewTexture.get()
                                          : meshRenderer.texture.get();
}

struct CachedFaceMaterial {
    Material material;
    std::shared_ptr<Texture> texture;
    std::filesystem::file_time_type writeTime{};
    bool initialized = false;
    bool valid = false;
};

const CachedFaceMaterial* ResolveProModelerFaceMaterial(const std::string& projectRelPath) {
    if (projectRelPath.empty())
        return nullptr;
    static std::unordered_map<std::string, CachedFaceMaterial> cache;
    CachedFaceMaterial& entry = cache[projectRelPath];
    const std::string absolute = AssetManager::Get().ToAbsolute(projectRelPath);
    std::error_code ec;
    const auto writeTime = std::filesystem::last_write_time(absolute, ec);
    if (!entry.initialized || (!ec && writeTime != entry.writeTime)) {
        entry.initialized = true;
        entry.writeTime = ec ? std::filesystem::file_time_type{} : writeTime;
        std::string error;
        entry.valid = Material::Load(absolute, entry.material, error);
        entry.texture = entry.valid && !entry.material.texturePath.empty()
            ? AssetManager::Get().GetTexture(entry.material.texturePath)
            : nullptr;
        if (!entry.valid)
            MIPSYNC_WARN("ProModeler face material load failed '{}': {}", projectRelPath, error);
    }
    return entry.valid ? &entry : nullptr;
}

void DrawProModelerWithFaceMaterials(Renderer& renderer, ProModelerComponent& proModeler,
                                     MeshRendererComponent& meshRenderer, const Mesh& mesh,
                                     const glm::mat4& worldMatrix, float projectionDepthClamp) {
    proModeler.EnsureFaceTopology();
    const size_t triangleCount = mesh.GetIndexCount() / 3u;
    if (triangleCount == 0)
        return;

    auto materialPathForTriangle = [&](size_t triangle) -> std::string {
        if (triangle >= proModeler.triangleFaceIds.size())
            return {};
        const auto it = proModeler.faceMaterialPaths.find(proModeler.triangleFaceIds[triangle]);
        return it == proModeler.faceMaterialPaths.end() ? std::string{} : it->second;
    };

    for (size_t first = 0; first < triangleCount; ) {
        const std::string materialPath = materialPathForTriangle(first);
        size_t end = first + 1u;
        while (end < triangleCount && materialPathForTriangle(end) == materialPath)
            ++end;

        const Texture* texture = meshRenderer.texture.get();
        glm::vec4 color = meshRenderer.color;
        glm::vec2 tiling = meshRenderer.textureTiling;
        glm::vec2 offset = meshRenderer.textureOffset;
        if (const CachedFaceMaterial* faceMaterial =
                ResolveProModelerFaceMaterial(materialPath)) {
            texture = faceMaterial->texture.get();
            color = faceMaterial->material.color;
            tiling = faceMaterial->material.mainTextureTiling;
            offset = faceMaterial->material.mainTextureOffset;
        }

        renderer.DrawMesh(mesh, worldMatrix, texture, color, tiling, offset,
                          static_cast<uint32_t>(first * 3u),
                          static_cast<uint32_t>((end - first) * 3u),
                          projectionDepthClamp, true);
        first = end;
    }
}

AnimatorComponent* FindAnimatorInAncestors(Entity& entity, const Scene& scene) {
    for (Entity* current = &entity; current; ) {
        if (auto* anim = current->GetComponent<AnimatorComponent>())
            return anim;
        const uint32_t parentId = current->GetParentID();
        if (parentId == 0)
            break;
        current = const_cast<Entity*>(scene.FindEntity(parentId));
    }
    return nullptr;
}

uint32_t FindSkeletalCharacterRootId(Entity& entity, const Scene& scene) {
    Entity* current = &entity;
    uint32_t rootId = entity.GetID();
    while (current) {
        if (current->GetComponent<AnimatorComponent>())
            rootId = current->GetID();
        const uint32_t parentId = current->GetParentID();
        if (parentId == 0)
            break;
        current = const_cast<Entity*>(scene.FindEntity(parentId));
    }
    return rootId;
}

int ResolveSkinnedMeshPartIndex(Entity& entity, const SkinnedMeshRendererComponent& skinned,
                                const SkeletalModelAsset& skel) {
    if (skinned.meshPartIndex >= 0 &&
        static_cast<size_t>(skinned.meshPartIndex) < skel.meshParts.size()) {
        return skinned.meshPartIndex;
    }

    std::string label;
    if (auto* tag = entity.GetComponent<TagComponent>())
        label = tag->tag;

    if (!label.empty()) {
        for (size_t i = 0; i < skel.meshParts.size(); ++i) {
            if (skel.meshParts[i].name == label)
                return static_cast<int>(i);
        }
    }

    if (skel.meshParts.size() == 1)
        return 0;

    // meshPartIndex -1 on the component means "draw all submeshes".
    if (skinned.meshPartIndex < 0)
        return -1;

    static bool warned = false;
    if (!warned) {
        MIPSYNC_WARN(
            "SkinnedMeshRenderer on '{}' has invalid meshPartIndex ({}); assign meshPart in "
            "inspector or re-spawn FBX",
            label.empty() ? "entity" : label.c_str(), skinned.meshPartIndex);
        warned = true;
    }
    return -2;
}

void FillSkinnedBoneMatrices(const SkinnedMeshRendererComponent& skinned, AnimatorComponent* animator,
                             bool animateCharacters, glm::mat4 bones[kMaxBones]) {
    for (int i = 0; i < kMaxBones; ++i)
        bones[i] = glm::mat4(1.0f);

    std::string modelPath = skinned.modelPath;
    if (modelPath.empty() && animator && !animator->modelPath.empty())
        modelPath = animator->modelPath;

    auto model = modelPath.empty() ? nullptr : AssetManager::Get().GetSkeletalModel(modelPath);
    if (!model)
        return;

    const bool hasAnimatorPose = animator && animator->enabled &&
                                 !animator->debugBindPoseOnly && animator->controller;
    if (hasAnimatorPose) {
        if (animateCharacters)
            SampleAnimatorBoneMatrices(*animator, bones);
        else
            SampleAnimatorDefaultStateBoneMatrices(*animator, bones);
        return;
    }

    model->EvaluateBoneMatrices({}, 0.0, bones);
}

} // namespace

// ─────────────────────────────────────────────────
// TransformComponent
// ─────────────────────────────────────────────────
void MeshRendererComponent::RebuildMesh() {
    ps1PreviewMesh.reset();
    ps1PreviewMeshVersion = 0;
    ps1PreviewTexture.reset();
    ps1PreviewTexturePath.clear();
    if (meshPrimitive == "File") {
        if (!meshPath.empty()) {
            mesh = AssetManager::Get().GetMesh(meshPath);
            if (mesh) {
                ps1PreviewMesh = Mips::BuildPs1PreviewMesh(*mesh, true);
                ps1PreviewMeshVersion = kPs1PreviewMeshVersion;
            }
        }
        return;
    }

    meshPath.clear();
    if (meshPrimitive == "Sphere")
        mesh = std::make_shared<Mesh>(Mesh::CreateSphere(meshSize * 0.5f, 16, 12));
    else if (meshPrimitive == "Plane")
        mesh = std::make_shared<Mesh>(Mesh::CreatePlane(meshSize, 10));
    else if (meshPrimitive == "Terrain") {
        constexpr int subdivisions = 32;
        const int row = subdivisions + 1;
        mesh = std::make_shared<Mesh>(Mesh::CreateTerrainFromData(
            meshSize, subdivisions,
            std::vector<float>(static_cast<size_t>(row * row), 0.0f),
            std::vector<glm::vec4>(static_cast<size_t>(row * row), glm::vec4(1.0f))));
    }
    else if (meshPrimitive == "ProModeler") {
        if (!mesh)
            mesh = std::make_shared<Mesh>(Mesh::CreateCube(meshSize));
    }
    else
        mesh = std::make_shared<Mesh>(Mesh::CreateCube(meshSize));
}

void MeshRendererComponent::SetPrimitive(const std::string& primitive, float size) {
    meshPrimitive = primitive;
    meshSize = size;
    RebuildMesh();
}

void MeshRendererComponent::SetMeshFile(const std::string& projectRelPath) {
    meshPath = projectRelPath;
    meshPrimitive = "File";
    ps1PreviewMesh.reset();
    ps1PreviewMeshVersion = 0;
    mesh = AssetManager::Get().GetMesh(projectRelPath);
    if (!mesh) {
        MIPSYNC_WARN("Failed to load mesh asset: {}", projectRelPath);
        mesh = std::make_shared<Mesh>(Mesh::CreateCube(1.0f));
    }
    ps1PreviewMesh = Mips::BuildPs1PreviewMesh(*mesh, true);
    ps1PreviewMeshVersion = kPs1PreviewMeshVersion;
}

void TerrainComponent::EnsureData() {
    subdivisions = std::clamp(subdivisions, 1, 128);
    size = std::max(size, 0.01f);
    brushRadius = std::max(0.05f, brushRadius);
    brushStrength = std::clamp(brushStrength, 0.0f, 10.0f);

    const int row = subdivisions + 1;
    const size_t expected = static_cast<size_t>(row * row);
    if (heights.size() == expected && paintColors.size() == expected)
        return;

    heights.assign(expected, 0.0f);
    paintColors.assign(expected, glm::vec4(1.0f));
}

bool TerrainComponent::ApplyBrush(const glm::vec3& localPoint) {
    EnsureData();

    const float half = size * 0.5f;
    if (localPoint.x < -half - brushRadius || localPoint.x > half + brushRadius ||
        localPoint.z < -half - brushRadius || localPoint.z > half + brushRadius) {
        return false;
    }

    const int row = subdivisions + 1;
    const float step = size / static_cast<float>(subdivisions);
    const float radius = std::max(0.05f, brushRadius);
    const float strength = std::clamp(brushStrength, 0.0f, 10.0f);
    bool touched = false;

    if (brushMode == BrushMode::Smooth) {
        std::vector<float> smoothed = heights;
        for (int z = 0; z <= subdivisions; ++z) {
            for (int x = 0; x <= subdivisions; ++x) {
                const size_t idx = static_cast<size_t>(z * row + x);
                const float px = -half + static_cast<float>(x) * step;
                const float pz = -half + static_cast<float>(z) * step;
                const float dist = glm::length(glm::vec2(px - localPoint.x, pz - localPoint.z));
                if (dist > radius)
                    continue;

                float sum = 0.0f;
                int count = 0;
                for (int dz = -1; dz <= 1; ++dz) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int sx = std::clamp(x + dx, 0, subdivisions);
                        const int sz = std::clamp(z + dz, 0, subdivisions);
                        sum += heights[static_cast<size_t>(sz * row + sx)];
                        ++count;
                    }
                }
                const float influence = std::pow(1.0f - dist / radius, 2.0f);
                smoothed[idx] = glm::mix(heights[idx], sum / static_cast<float>(count),
                                         std::clamp(influence * strength * 0.12f, 0.0f, 1.0f));
                touched = true;
            }
        }
        if (touched)
            heights.swap(smoothed);
        return touched;
    }

    for (int z = 0; z <= subdivisions; ++z) {
        for (int x = 0; x <= subdivisions; ++x) {
            const size_t idx = static_cast<size_t>(z * row + x);
            const float px = -half + static_cast<float>(x) * step;
            const float pz = -half + static_cast<float>(z) * step;
            const float dist = glm::length(glm::vec2(px - localPoint.x, pz - localPoint.z));
            if (dist > radius)
                continue;

            const float influence = std::pow(1.0f - dist / radius, 2.0f);
            if (brushMode == BrushMode::Paint) {
                paintColors[idx] = glm::mix(paintColors[idx], brushColor,
                                            std::clamp(influence * strength * 0.18f, 0.0f, 1.0f));
                paintColors[idx].a = 1.0f;
            } else {
                const float direction = brushMode == BrushMode::Lower ? -1.0f : 1.0f;
                heights[idx] += direction * influence * strength * 0.08f;
            }
            touched = true;
        }
    }
    return touched;
}

void TerrainComponent::ResetProcedural() {
    heights.clear();
    paintColors.clear();
    EnsureData();
}

void TerrainComponent::ClearPaint() {
    EnsureData();
    std::fill(paintColors.begin(), paintColors.end(), glm::vec4(1.0f));
}

void TerrainComponent::RebuildMesh(MeshRendererComponent& renderer) {
    EnsureData();
    renderer.meshPrimitive = "Terrain";
    renderer.meshSize = size;
    renderer.meshPath.clear();
    renderer.ps1PreviewMesh.reset();
    renderer.ps1PreviewMeshVersion = 0;
    renderer.ps1PreviewTexture.reset();
    renderer.ps1PreviewTexturePath.clear();
    renderer.mesh = std::make_shared<Mesh>(Mesh::CreateTerrainFromData(
        size, subdivisions, heights, paintColors));
}

namespace {

void PMPushTri(std::vector<ProModelerVertex>& vertices, std::vector<uint32_t>& indices,
               const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
               const glm::vec2& uva = { 0.0f, 0.0f },
               const glm::vec2& uvb = { 1.0f, 0.0f },
               const glm::vec2& uvc = { 1.0f, 1.0f }) {
    glm::vec3 normal = glm::normalize(glm::cross(b - a, c - a));
    if (!std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z))
        normal = { 0.0f, 1.0f, 0.0f };
    const uint32_t base = static_cast<uint32_t>(vertices.size());
    vertices.push_back({ a, normal, uva, glm::vec4(1.0f) });
    vertices.push_back({ b, normal, uvb, glm::vec4(1.0f) });
    vertices.push_back({ c, normal, uvc, glm::vec4(1.0f) });
    indices.insert(indices.end(), { base + 0u, base + 1u, base + 2u });
}

void PMPushQuad(std::vector<ProModelerVertex>& vertices, std::vector<uint32_t>& indices,
                const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d) {
    glm::vec3 normal = glm::normalize(glm::cross(b - a, c - a));
    if (!std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z))
        normal = { 0.0f, 1.0f, 0.0f };
    const uint32_t base = static_cast<uint32_t>(vertices.size());
    vertices.push_back({ a, normal, { 0.0f, 0.0f }, glm::vec4(1.0f) });
    vertices.push_back({ b, normal, { 1.0f, 0.0f }, glm::vec4(1.0f) });
    vertices.push_back({ c, normal, { 1.0f, 1.0f }, glm::vec4(1.0f) });
    vertices.push_back({ d, normal, { 0.0f, 1.0f }, glm::vec4(1.0f) });
    indices.insert(indices.end(), { base + 0u, base + 1u, base + 2u, base + 0u, base + 2u, base + 3u });
}

void PMAppendBox(std::vector<ProModelerVertex>& vertices, std::vector<uint32_t>& indices,
                 const glm::vec3& center, const glm::vec3& size) {
    const glm::vec3 h = glm::max(size * 0.5f, glm::vec3(0.001f));
    const glm::vec3 p000 = center + glm::vec3(-h.x, -h.y, -h.z);
    const glm::vec3 p001 = center + glm::vec3(-h.x, -h.y,  h.z);
    const glm::vec3 p010 = center + glm::vec3(-h.x,  h.y, -h.z);
    const glm::vec3 p011 = center + glm::vec3(-h.x,  h.y,  h.z);
    const glm::vec3 p100 = center + glm::vec3( h.x, -h.y, -h.z);
    const glm::vec3 p101 = center + glm::vec3( h.x, -h.y,  h.z);
    const glm::vec3 p110 = center + glm::vec3( h.x,  h.y, -h.z);
    const glm::vec3 p111 = center + glm::vec3( h.x,  h.y,  h.z);

    PMPushQuad(vertices, indices, p001, p101, p111, p011); // front
    PMPushQuad(vertices, indices, p100, p000, p010, p110); // back
    PMPushQuad(vertices, indices, p011, p111, p110, p010); // top
    PMPushQuad(vertices, indices, p000, p100, p101, p001); // bottom
    PMPushQuad(vertices, indices, p101, p100, p110, p111); // right
    PMPushQuad(vertices, indices, p000, p001, p011, p010); // left
}

} // namespace

void ProModelerComponent::ResetBox() {
    shape = Shape::Box;
    vertices.clear();
    indices.clear();
    faceMaterialPaths.clear();
    PMAppendBox(vertices, indices, { 0.0f, 0.0f, 0.0f }, size);
    triangleFaceIds = { 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6 };
    nextFaceId = 7;
}

void ProModelerComponent::ResetPlane() {
    shape = Shape::Plane;
    vertices.clear();
    indices.clear();
    faceMaterialPaths.clear();
    const glm::vec3 h = glm::max(size * 0.5f, glm::vec3(0.001f));
    PMPushQuad(vertices, indices,
               { -h.x, 0.0f, -h.z },
               {  h.x, 0.0f, -h.z },
               {  h.x, 0.0f,  h.z },
               { -h.x, 0.0f,  h.z });
    triangleFaceIds = { 1, 1 };
    nextFaceId = 2;
}

void ProModelerComponent::ResetRamp() {
    shape = Shape::Ramp;
    vertices.clear();
    indices.clear();
    faceMaterialPaths.clear();
    const glm::vec3 h = glm::max(size * 0.5f, glm::vec3(0.001f));
    const glm::vec3 a{ -h.x, -h.y, -h.z };
    const glm::vec3 b{  h.x, -h.y, -h.z };
    const glm::vec3 c{ -h.x, -h.y,  h.z };
    const glm::vec3 d{  h.x, -h.y,  h.z };
    const glm::vec3 e{ -h.x,  h.y,  h.z };
    const glm::vec3 f{  h.x,  h.y,  h.z };
    // Winding is outward-facing on every surface.
    PMPushQuad(vertices, indices, a, b, d, c); // bottom (-Y)
    PMPushQuad(vertices, indices, f, e, c, d); // back high (+Z)
    PMPushQuad(vertices, indices, e, f, b, a); // slope (+Y/-Z)
    PMPushTri(vertices, indices, e, a, c);     // left (-X)
    PMPushTri(vertices, indices, f, d, b);     // right (+X)
    triangleFaceIds = { 1, 1, 2, 2, 3, 3, 4, 5 };
    nextFaceId = 6;
}

void ProModelerComponent::ResetStairs() {
    shape = Shape::Stairs;
    vertices.clear();
    indices.clear();
    faceMaterialPaths.clear();
    triangleFaceIds.clear();
    nextFaceId = 1;
    const int count = std::clamp(steps, 1, 32);
    const glm::vec3 clampedSize = glm::max(size, glm::vec3(0.001f));
    const float run = clampedSize.z / static_cast<float>(count);
    const float rise = clampedSize.y / static_cast<float>(count);
    for (int i = 0; i < count; ++i) {
        const float boxHeight = rise * static_cast<float>(i + 1);
        const float zCenter = -clampedSize.z * 0.5f + run * (static_cast<float>(i) + 0.5f);
        const float yCenter = -clampedSize.y * 0.5f + boxHeight * 0.5f;
        PMAppendBox(vertices, indices, { 0.0f, yCenter, zCenter }, { clampedSize.x, boxHeight, run });
        for (int face = 0; face < 6; ++face) {
            triangleFaceIds.push_back(nextFaceId);
            triangleFaceIds.push_back(nextFaceId++);
        }
    }
}

void ProModelerComponent::EnsureFaceTopology() {
    const size_t triangleCount = indices.size() / 3u;
    if (triangleFaceIds.size() == triangleCount) {
        for (uint32_t id : triangleFaceIds)
            nextFaceId = std::max(nextFaceId, id + 1u);
        return;
    }

    triangleFaceIds.assign(triangleCount, 0u);
    nextFaceId = 1;
    auto triangleNormal = [&](size_t tri) {
        const uint32_t i0 = indices[tri * 3u + 0u];
        const uint32_t i1 = indices[tri * 3u + 1u];
        const uint32_t i2 = indices[tri * 3u + 2u];
        return glm::normalize(glm::cross(vertices[i1].position - vertices[i0].position,
                                         vertices[i2].position - vertices[i0].position));
    };
    for (size_t seed = 0; seed < triangleCount; ++seed) {
        if (triangleFaceIds[seed] != 0u)
            continue;
        const uint32_t faceId = nextFaceId++;
        const glm::vec3 seedNormal = triangleNormal(seed);
        triangleFaceIds[seed] = faceId;
        // Legacy files had no polygon metadata. Reconstruct quads by pairing
        // one coplanar adjacent triangle instead of flood-filling an entire
        // plane; this preserves loop cuts made by older editor versions.
        for (size_t candidate = seed + 1u; candidate < triangleCount; ++candidate) {
            if (triangleFaceIds[candidate] != 0u)
                continue;
            int shared = 0;
            for (int a = 0; a < 3; ++a)
                for (int b = 0; b < 3; ++b)
                    shared += indices[seed * 3u + static_cast<size_t>(a)] ==
                              indices[candidate * 3u + static_cast<size_t>(b)];
            if (shared < 2 || glm::dot(seedNormal, triangleNormal(candidate)) < 0.995f)
                continue;
            triangleFaceIds[candidate] = faceId;
            break;
        }
    }
}

void ProModelerComponent::MoveFaceVertices(const glm::vec3& direction, float amount) {
    if (vertices.empty() || amount == 0.0f)
        return;
    const glm::vec3 dir = glm::normalize(direction);
    if (!std::isfinite(dir.x) || !std::isfinite(dir.y) || !std::isfinite(dir.z))
        return;

    float best = glm::dot(vertices[0].position, dir);
    for (const auto& v : vertices)
        best = std::max(best, glm::dot(v.position, dir));
    const float epsilon = std::max(0.0005f, glm::length(size) * 0.001f);
    for (auto& v : vertices) {
        if (std::abs(glm::dot(v.position, dir) - best) <= epsilon)
            v.position += dir * amount;
    }
    shape = Shape::Custom;
    RecalculateNormals();
}

void ProModelerComponent::ExtrudeFaces(const std::vector<size_t>& faceTriangles,
                                       std::vector<uint32_t>& outSelectedVertices,
                                       std::vector<size_t>& outSelectedFaceTriangles) {
    if (faceTriangles.empty() || indices.empty() || vertices.empty())
        return;
    EnsureFaceTopology();
    std::string inheritedMaterial;
    if (!faceTriangles.empty()) {
        const size_t faceIndex = faceTriangles.front() / 3u;
        if (faceIndex < triangleFaceIds.size()) {
            const auto materialIt = faceMaterialPaths.find(triangleFaceIds[faceIndex]);
            if (materialIt != faceMaterialPaths.end())
                inheritedMaterial = materialIt->second;
        }
    }

    // 1. Gather all unique vertex indices involved in the extrusion
    std::vector<uint32_t> selectedVIs;
    for (size_t triStart : faceTriangles) {
        for (int i = 0; i < 3; ++i) {
            uint32_t vi = indices[triStart + i];
            if (std::find(selectedVIs.begin(), selectedVIs.end(), vi) == selectedVIs.end()) {
                selectedVIs.push_back(vi);
            }
        }
    }

    // 2. Identify boundary edges (edges in the selection shared by only 1 selected triangle)
    // Key: ordered pair (min, max), Value: pair of original vertex IDs in edge order (u, v) and count
    struct EdgeInfo {
        uint32_t u, v;
        int count;
    };
    std::vector<EdgeInfo> edgeInfos;
    auto addEdge = [&](uint32_t u, uint32_t v) {
        uint32_t minVal = std::min(u, v);
        uint32_t maxVal = std::max(u, v);
        for (auto& info : edgeInfos) {
            if (std::min(info.u, info.v) == minVal && std::max(info.u, info.v) == maxVal) {
                info.count++;
                return;
            }
        }
        edgeInfos.push_back({ u, v, 1 });
    };

    for (size_t triStart : faceTriangles) {
        addEdge(indices[triStart + 0], indices[triStart + 1]);
        addEdge(indices[triStart + 1], indices[triStart + 2]);
        addEdge(indices[triStart + 2], indices[triStart + 0]);
    }

    // 3. Duplicate vertices and map old to new
    std::unordered_map<uint32_t, uint32_t> oldToNew;
    for (uint32_t vi : selectedVIs) {
        uint32_t newVi = static_cast<uint32_t>(vertices.size());
        vertices.push_back(vertices[vi]); // Copy vertex attributes
        oldToNew[vi] = newVi;
    }

    // 4. Rewrite the selected face to use the duplicated vertices. The old
    // boundary ring remains connected to neighboring faces and becomes the
    // base loop of the extrusion. Do not add an internal bottom cap: that
    // produced overlapping faces and the striped/diagonal side artifacts.
    for (size_t triStart : faceTriangles) {
        indices[triStart + 0] = oldToNew[indices[triStart + 0]];
        indices[triStart + 1] = oldToNew[indices[triStart + 1]];
        indices[triStart + 2] = oldToNew[indices[triStart + 2]];
    }

    // 5. Create one hard-edged quad per boundary edge. Side vertices are
    // intentionally separate for stable planar UVs/normals; editor movement
    // welds coincident positions so the shape still deforms as one surface.
    std::vector<uint32_t> sideTopVertices;
    for (const auto& edge : edgeInfos) {
        if (edge.count == 1) { // It is a boundary edge
            uint32_t u = edge.u;
            uint32_t v = edge.v;
            const uint32_t uNew = oldToNew[u];
            const uint32_t vNew = oldToNew[v];
            const uint32_t sideBase = static_cast<uint32_t>(vertices.size());
            vertices.push_back(vertices[u]);
            vertices.push_back(vertices[v]);
            vertices.push_back(vertices[vNew]);
            vertices.push_back(vertices[uNew]);
            indices.insert(indices.end(), {
                sideBase + 0u, sideBase + 1u, sideBase + 2u,
                sideBase + 0u, sideBase + 2u, sideBase + 3u,
            });
            triangleFaceIds.push_back(nextFaceId);
            triangleFaceIds.push_back(nextFaceId);
            if (!inheritedMaterial.empty())
                faceMaterialPaths[nextFaceId] = inheritedMaterial;
            ++nextFaceId;
            sideTopVertices.push_back(sideBase + 2u);
            sideTopVertices.push_back(sideBase + 3u);
        }
    }

    // 7. Update selection states for the editor app
    outSelectedVertices.clear();
    for (uint32_t vi : selectedVIs) {
        outSelectedVertices.push_back(oldToNew[vi]);
    }
    for (uint32_t vi : sideTopVertices)
        if (std::find(outSelectedVertices.begin(), outSelectedVertices.end(), vi) ==
            outSelectedVertices.end())
            outSelectedVertices.push_back(vi);
    // faceTriangles starts remain the same in m_indices, but they now index the new top vertices
    outSelectedFaceTriangles = faceTriangles;

    shape = Shape::Custom;
    RecalculatePlanarUVs();
    RecalculateNormals();
}

bool ProModelerComponent::ConnectOppositeEdges(
    const std::pair<uint32_t, uint32_t>& first,
    const std::pair<uint32_t, uint32_t>& second,
    std::vector<uint32_t>& outSelectedVertices) {
    if (first.first >= vertices.size() || first.second >= vertices.size() ||
        second.first >= vertices.size() || second.second >= vertices.size())
        return false;
    EnsureFaceTopology();

    uint32_t ia = first.first, ib = first.second;
    uint32_t ic = second.first, id = second.second;
    const glm::vec3 a = vertices[ia].position;
    const glm::vec3 b = vertices[ib].position;
    glm::vec3 c = vertices[ic].position;
    glm::vec3 d = vertices[id].position;
    const float epsilon = std::max(0.0005f, glm::length(size) * 0.001f);
    auto same = [&](const glm::vec3& p, const glm::vec3& q) {
        return glm::length(p - q) <= epsilon;
    };
    if (same(a, b) || same(c, d) || same(a, c) || same(a, d) ||
        same(b, c) || same(b, d))
        return false;

    // Pair the second edge so a-c and b-d are the short boundary sides.
    if (glm::length(a - c) + glm::length(b - d) >
        glm::length(a - d) + glm::length(b - c)) {
        std::swap(ic, id);
        std::swap(c, d);
    }

    // Edge overlays may return the duplicate indices of an adjacent hard
    // face. Resolve both geometric edges back to one semantic quad first.
    auto faceHasEdge = [&](uint32_t faceId, const glm::vec3& p, const glm::vec3& q) {
        for (size_t ti = 0; ti + 2 < indices.size(); ti += 3) {
            if (ti / 3u >= triangleFaceIds.size() || triangleFaceIds[ti / 3u] != faceId)
                continue;
            const uint32_t tri[3] = {indices[ti], indices[ti + 1], indices[ti + 2]};
            for (int edge = 0; edge < 3; ++edge) {
                const glm::vec3& x = vertices[tri[edge]].position;
                const glm::vec3& y = vertices[tri[(edge + 1) % 3]].position;
                if ((same(x, p) && same(y, q)) || (same(x, q) && same(y, p)))
                    return true;
            }
        }
        return false;
    };

    uint32_t replacedFaceId = 0;
    for (uint32_t faceId : triangleFaceIds) {
        if (faceId != 0u && faceHasEdge(faceId, a, b) && faceHasEdge(faceId, c, d)) {
            replacedFaceId = faceId;
            break;
        }
    }
    if (replacedFaceId == 0u)
        return false;

    std::vector<size_t> replaceTriangles;
    for (size_t ti = 0; ti + 2 < indices.size(); ti += 3) {
        if (ti / 3u < triangleFaceIds.size() && triangleFaceIds[ti / 3u] == replacedFaceId)
            replaceTriangles.push_back(ti);
    }
    if (replaceTriangles.size() < 2)
        return false;

    auto indexOnFaceAt = [&](const glm::vec3& position) -> uint32_t {
        for (size_t ti : replaceTriangles) {
            for (int corner = 0; corner < 3; ++corner) {
                const uint32_t vi = indices[ti + static_cast<size_t>(corner)];
                if (same(vertices[vi].position, position))
                    return vi;
            }
        }
        return UINT32_MAX;
    };
    ia = indexOnFaceAt(a);
    ib = indexOnFaceAt(b);
    ic = indexOnFaceAt(c);
    id = indexOnFaceAt(d);
    if (ia == UINT32_MAX || ib == UINT32_MAX || ic == UINT32_MAX || id == UINT32_MAX)
        return false;

    const glm::vec3 p0 = vertices[indices[replaceTriangles.front() + 0]].position;
    const glm::vec3 p1 = vertices[indices[replaceTriangles.front() + 1]].position;
    const glm::vec3 p2 = vertices[indices[replaceTriangles.front() + 2]].position;
    const glm::vec3 faceNormal = glm::normalize(glm::cross(p1 - p0, p2 - p0));
    if (!std::isfinite(faceNormal.x) || glm::length(faceNormal) < 0.5f)
        return false;

    std::string inheritedMaterial;
    const auto materialIt = faceMaterialPaths.find(replacedFaceId);
    if (materialIt != faceMaterialPaths.end())
        inheritedMaterial = materialIt->second;

    for (auto it = replaceTriangles.rbegin(); it != replaceTriangles.rend(); ++it) {
        const size_t faceIndex = *it / 3u;
        indices.erase(indices.begin() + static_cast<ptrdiff_t>(*it),
                      indices.begin() + static_cast<ptrdiff_t>(*it + 3));
        if (faceIndex < triangleFaceIds.size())
            triangleFaceIds.erase(triangleFaceIds.begin() + static_cast<ptrdiff_t>(faceIndex));
    }

    auto midpointVertex = [](const ProModelerVertex& x, const ProModelerVertex& y) {
        ProModelerVertex out = x;
        out.position = (x.position + y.position) * 0.5f;
        out.uv = (x.uv + y.uv) * 0.5f;
        out.color = (x.color + y.color) * 0.5f;
        return out;
    };
    const uint32_t base = static_cast<uint32_t>(vertices.size());
    // Reuse the original corners and create only the two vertices belonging
    // to the new edge. Both resulting faces reference these exact same
    // midpoint indices, so lifting the edge cannot tear the surface apart.
    vertices.push_back(midpointVertex(vertices[ia], vertices[ib]));
    vertices.push_back(midpointVertex(vertices[ic], vertices[id]));
    const uint32_t firstMidpoint = base + 0u;
    const uint32_t secondMidpoint = base + 1u;

    auto addTriFacing = [&](uint32_t x, uint32_t y, uint32_t z) {
        const glm::vec3 n = glm::cross(vertices[y].position - vertices[x].position,
                                       vertices[z].position - vertices[x].position);
        if (glm::dot(n, faceNormal) < 0.0f) std::swap(y, z);
        indices.insert(indices.end(), {x, y, z});
    };
    addTriFacing(ia, firstMidpoint, secondMidpoint);
    addTriFacing(ia, secondMidpoint, ic);
    addTriFacing(firstMidpoint, ib, id);
    addTriFacing(firstMidpoint, id, secondMidpoint);
    const uint32_t firstNewFace = nextFaceId++;
    const uint32_t secondNewFace = nextFaceId++;
    triangleFaceIds.insert(triangleFaceIds.end(), {
        firstNewFace, firstNewFace, secondNewFace, secondNewFace
    });
    if (!inheritedMaterial.empty()) {
        faceMaterialPaths[firstNewFace] = inheritedMaterial;
        faceMaterialPaths[secondNewFace] = inheritedMaterial;
    }
    if (replacedFaceId != 0u)
        faceMaterialPaths.erase(replacedFaceId);

    outSelectedVertices = {firstMidpoint, secondMidpoint};
    shape = Shape::Custom;
    RecalculatePlanarUVs();
    RecalculateNormals();
    return true;
}

void ProModelerComponent::FlipFaces(const std::vector<size_t>& faceTriangles) {
    for (size_t triStart : faceTriangles) {
        if (triStart + 2 < indices.size())
            std::swap(indices[triStart + 1], indices[triStart + 2]);
    }
    shape = Shape::Custom;
    RecalculateNormals();
}

void ProModelerComponent::RecalculatePlanarUVs() {
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const uint32_t i0 = indices[i + 0];
        const uint32_t i1 = indices[i + 1];
        const uint32_t i2 = indices[i + 2];
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
            continue;

        glm::vec3 normal = glm::normalize(glm::cross(
            vertices[i1].position - vertices[i0].position,
            vertices[i2].position - vertices[i0].position));
        if (!std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z))
            normal = { 0.0f, 1.0f, 0.0f };

        const glm::vec3 absNormal = glm::abs(normal);
        auto project = [&](const glm::vec3& p) -> glm::vec2 {
            if (absNormal.x >= absNormal.y && absNormal.x >= absNormal.z)
                return { p.z, p.y };
            if (absNormal.y >= absNormal.x && absNormal.y >= absNormal.z)
                return { p.x, p.z };
            return { p.x, p.y };
        };

        vertices[i0].uv = project(vertices[i0].position);
        vertices[i1].uv = project(vertices[i1].position);
        vertices[i2].uv = project(vertices[i2].position);
    }
}

void ProModelerComponent::RecalculateNormals() {
    for (auto& v : vertices)
        v.normal = { 0.0f, 0.0f, 0.0f };
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const uint32_t i0 = indices[i + 0];
        const uint32_t i1 = indices[i + 1];
        const uint32_t i2 = indices[i + 2];
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
            continue;
        glm::vec3 normal = glm::normalize(glm::cross(
            vertices[i1].position - vertices[i0].position,
            vertices[i2].position - vertices[i0].position));
        if (!std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z))
            normal = { 0.0f, 1.0f, 0.0f };
        vertices[i0].normal += normal;
        vertices[i1].normal += normal;
        vertices[i2].normal += normal;
    }
    for (auto& v : vertices) {
        const float len = glm::length(v.normal);
        v.normal = len > 1e-6f ? (v.normal / len) : glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

void ProModelerComponent::RebuildMesh(MeshRendererComponent& renderer) {
    if (vertices.empty() || indices.empty())
        ResetBox();
    EnsureFaceTopology();
    RecalculatePlanarUVs();
    RecalculateNormals();

    std::vector<Vertex> meshVertices;
    meshVertices.reserve(vertices.size());
    for (const auto& v : vertices)
        meshVertices.push_back({ v.position, v.normal, v.uv, v.color });

    renderer.meshPrimitive = "ProModeler";
    renderer.meshPath.clear();
    renderer.ps1PreviewMesh.reset();
    renderer.ps1PreviewMeshVersion = 0;
    renderer.ps1PreviewTexture.reset();
    renderer.ps1PreviewTexturePath.clear();
    renderer.mesh = std::make_shared<Mesh>(meshVertices, indices);
}

void SkinnedMeshRendererComponent::SetModelFile(const std::string& projectRelPath) {
    modelPath = projectRelPath;
    ps1RigidPreviewMesh.reset();
    ps1RigidPreviewVersion = 0;
    AssetManager::Get().DropSkeletalModel(projectRelPath);
    AssetThumbnail::Get().DropMeshThumbnail(projectRelPath);
    auto model = AssetManager::Get().GetSkeletalModel(projectRelPath);
    mesh = model ? model->mesh : nullptr;
    if (!mesh)
        MIPSYNC_WARN("Failed to load skeletal model: {}", projectRelPath);
}

glm::mat4 TransformComponent::RotationMatrixFromEuler(const glm::vec3& eulerDegrees) {
    // FPS / camera: yaw around world Y, then pitch around local X, then roll.
    return glm::rotate(glm::mat4(1.0f), glm::radians(eulerDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f))
         * glm::rotate(glm::mat4(1.0f), glm::radians(eulerDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f))
         * glm::rotate(glm::mat4(1.0f), glm::radians(eulerDegrees.z), glm::vec3(0.0f, 0.0f, 1.0f));
}

glm::mat4 TransformComponent::GetTransform() const {
    const glm::mat4 rot = RotationMatrixFromEuler(rotation);
    return glm::translate(glm::mat4(1.0f), position) * rot * glm::scale(glm::mat4(1.0f), scale);
}

// ─────────────────────────────────────────────────
// Scene
// ─────────────────────────────────────────────────

Entity* Scene::CreateEntity(const std::string& name) {
    return CreateEntityWithId(m_NextEntityID++, name);
}

Entity* Scene::CreateEntityWithId(uint32_t id, const std::string& name) {
    if (id >= m_NextEntityID)
        m_NextEntityID = id + 1;
    auto entity = std::make_unique<Entity>(id, name);
    Entity* rawPtr = entity.get();
    m_Entities.push_back(std::move(entity));
    return rawPtr;
}

void Scene::Clear() {
    m_Entities.clear();
    m_NextEntityID = 1;
}

void Scene::DetachFromParent(Entity* child) {
    if (!child) return;
    if (Entity* oldParent = FindEntity(child->GetParentID())) {
        auto& siblings = oldParent->GetChildIDs();
        siblings.erase(std::remove(siblings.begin(), siblings.end(), child->GetID()),
                       siblings.end());
    }
    child->SetParentID(0);
}

void Scene::DestroyEntityRecursive(uint32_t id) {
    Entity* entity = FindEntity(id);
    if (!entity) return;

    // Copy ids first; recursive calls mutate the parent's child list.
    std::vector<uint32_t> children = entity->GetChildIDs();
    for (uint32_t childId : children)
        DestroyEntityRecursive(childId);

    DetachFromParent(entity);

    auto it = std::remove_if(m_Entities.begin(), m_Entities.end(),
        [id](const std::unique_ptr<Entity>& e) { return e->GetID() == id; });
    m_Entities.erase(it, m_Entities.end());
}

void Scene::DestroyEntity(Entity* entity) {
    if (!entity) return;
    DestroyEntityRecursive(entity->GetID());
}

Entity* Scene::FindEntity(uint32_t id) {
    if (id == 0) return nullptr;
    for (auto& e : m_Entities)
        if (e->GetID() == id) return e.get();
    return nullptr;
}

const Entity* Scene::FindEntity(uint32_t id) const {
    if (id == 0) return nullptr;
    for (const auto& e : m_Entities)
        if (e->GetID() == id) return e.get();
    return nullptr;
}

bool Scene::IsAncestor(uint32_t ancestorId, uint32_t descendantId) const {
    if (ancestorId == 0 || descendantId == 0) return false;
    uint32_t cursor = descendantId;
    while (cursor != 0) {
        if (cursor == ancestorId) return true;
        const Entity* node = FindEntity(cursor);
        if (!node) return false;
        cursor = node->GetParentID();
    }
    return false;
}

bool Scene::SetParent(Entity* child, Entity* parent) {
    if (!child) return false;
    if (parent == child) return false;
    if (parent && IsAncestor(child->GetID(), parent->GetID()))
        return false;

    DetachFromParent(child);

    if (parent) {
        parent->GetChildIDs().push_back(child->GetID());
        child->SetParentID(parent->GetID());
    }
    return true;
}

bool Scene::ReorderEntity(Entity* child, Entity* parent, Entity* sibling, bool insertAfter) {
    if (!child)
        return false;
    if (parent == child)
        return false;
    if (sibling == child)
        return false;
    if (parent && IsAncestor(child->GetID(), parent->GetID()))
        return false;
    if (sibling && sibling->GetParentID() != (parent ? parent->GetID() : 0))
        return false;

    const uint32_t oldParentId = child->GetParentID();
    const uint32_t newParentId = parent ? parent->GetID() : 0;

    auto removeFrom = [&](std::vector<uint32_t>& list) {
        list.erase(std::remove(list.begin(), list.end(), child->GetID()), list.end());
    };

    if (Entity* oldParent = FindEntity(oldParentId)) {
        removeFrom(oldParent->GetChildIDs());
    }

    child->SetParentID(newParentId);

    std::vector<uint32_t>* targetList = nullptr;
    if (parent) {
        targetList = &parent->GetChildIDs();
    } else {
        // Root entities are ordered by m_Entities. Move the owned entity itself.
        auto it = std::find_if(m_Entities.begin(), m_Entities.end(),
            [&](const std::unique_ptr<Entity>& e) { return e.get() == child; });
        if (it == m_Entities.end())
            return false;
        std::unique_ptr<Entity> moved = std::move(*it);
        m_Entities.erase(it);

        auto insertIt = m_Entities.end();
        if (sibling) {
            insertIt = std::find_if(m_Entities.begin(), m_Entities.end(),
                [&](const std::unique_ptr<Entity>& e) { return e.get() == sibling; });
            if (insertIt == m_Entities.end())
                insertIt = m_Entities.end();
            else if (insertAfter)
                ++insertIt;
        }
        m_Entities.insert(insertIt, std::move(moved));
        return oldParentId != newParentId || sibling != nullptr;
    }

    auto& siblings = *targetList;
    removeFrom(siblings);
    auto insertIt = siblings.end();
    if (sibling) {
        insertIt = std::find(siblings.begin(), siblings.end(), sibling->GetID());
        if (insertIt == siblings.end())
            insertIt = siblings.end();
        else if (insertAfter)
            ++insertIt;
    }
    siblings.insert(insertIt, child->GetID());
    return true;
}

glm::mat4 Scene::GetWorldMatrix(const Entity& entity) const {
    glm::mat4 world(1.0f);
    const Entity* cursor = &entity;
    uint32_t visited[64];
    size_t visitCount = 0;

    while (cursor) {
        const uint32_t id = cursor->GetID();
        for (size_t i = 0; i < visitCount; ++i) {
            if (visited[i] == id) {
                MIPSYNC_WARN("Transform parent cycle detected on entity {}", id);
                return world;
            }
        }
        if (visitCount < sizeof(visited) / sizeof(visited[0]))
            visited[visitCount++] = id;

        const auto* tr = const_cast<Entity*>(cursor)->GetComponent<TransformComponent>();
        if (!tr)
            break;
        world = tr->GetTransform() * world;
        cursor = FindEntity(cursor->GetParentID());
    }
    return world;
}

glm::mat4 Scene::GetParentWorldMatrix(const Entity& entity) const {
    const Entity* parent = FindEntity(entity.GetParentID());
    return parent ? GetWorldMatrix(*parent) : glm::mat4(1.0f);
}

glm::vec3 Scene::GetWorldPosition(const Entity& entity) const {
    const glm::mat4 world = GetWorldMatrix(entity);
    return glm::vec3(world[3]);
}

void Scene::SetWorldPosition(Entity& entity, const glm::vec3& worldPos) {
    auto* tr = entity.GetComponent<TransformComponent>();
    if (!tr)
        return;
    const glm::mat4 parentWorld = GetParentWorldMatrix(entity);
    const glm::vec4 local = glm::inverse(parentWorld) * glm::vec4(worldPos, 1.0f);
    tr->position = glm::vec3(local);
}

Entity* Scene::DuplicateEntity(Entity& source) {
  std::function<Entity*(Entity*, Entity*)> cloneRecursive =
      [&](Entity* src, Entity* newParent) -> Entity* {
    const auto* srcTag = src->GetComponent<TagComponent>();
    const std::string name = srcTag ? srcTag->tag + " (Copy)" : "Entity (Copy)";
    Entity* copy = CreateEntity(name);
    copy->SetActive(src->IsActive());
    copy->SetStatic(src->IsStatic());
    copy->SetEditorTag(src->GetEditorTag());
    copy->SetEditorLayer(src->GetEditorLayer());

    if (newParent)
      SetParent(copy, newParent);

    if (auto* srcTr = src->GetComponent<TransformComponent>()) {
      if (auto* dstTr = copy->GetComponent<TransformComponent>()) {
        dstTr->position = srcTr->position;
        dstTr->rotation = srcTr->rotation;
        dstTr->scale = srcTr->scale;
      }
    }

    if (auto* srcSk = src->GetComponent<SkinnedMeshRendererComponent>()) {
      auto& dstSk = copy->AddComponent<SkinnedMeshRendererComponent>();
      dstSk.enabled = srcSk->enabled;
      dstSk.modelPath = srcSk->modelPath;
      dstSk.meshPartIndex = srcSk->meshPartIndex;
      dstSk.texturePath = srcSk->texturePath;
      dstSk.materialPath = srcSk->materialPath;
      dstSk.textureTiling = srcSk->textureTiling;
      dstSk.textureOffset = srcSk->textureOffset;
      dstSk.color = srcSk->color;
      dstSk.ps1SeamFill = srcSk->ps1SeamFill;
      dstSk.SetModelFile(dstSk.modelPath);
      if (!dstSk.texturePath.empty())
        dstSk.texture = AssetManager::Get().GetTexture(dstSk.texturePath);
    }

    if (auto* srcAnim = src->GetComponent<AnimatorComponent>()) {
      auto& dstAnim = copy->AddComponent<AnimatorComponent>();
      dstAnim.enabled = srcAnim->enabled;
      dstAnim.modelPath = srcAnim->modelPath;
      dstAnim.controllerPath = srcAnim->controllerPath;
      dstAnim.speed = srcAnim->speed;
      dstAnim.animationFps = srcAnim->animationFps;
      dstAnim.parameters = srcAnim->parameters;
      dstAnim.ReloadAssets();
    }

    if (auto* srcMr = src->GetComponent<MeshRendererComponent>()) {
      auto& dstMr = copy->AddComponent<MeshRendererComponent>();
      dstMr.enabled = srcMr->enabled;
      dstMr.meshPrimitive = srcMr->meshPrimitive;
      dstMr.meshSize = srcMr->meshSize;
      dstMr.meshPath = srcMr->meshPath;
      dstMr.texturePath = srcMr->texturePath;
      dstMr.materialPath = srcMr->materialPath;
      dstMr.textureTiling = srcMr->textureTiling;
      dstMr.textureOffset = srcMr->textureOffset;
      dstMr.color = srcMr->color;
      dstMr.viewModel = srcMr->viewModel;
      dstMr.ps1SeamFill = srcMr->ps1SeamFill;
      dstMr.texture = srcMr->texture;
      dstMr.RebuildMesh();
        if (!dstMr.texturePath.empty())
        dstMr.texture = AssetManager::Get().GetTexture(dstMr.texturePath);
    }

    if (auto* srcPb = src->GetComponent<ProModelerComponent>()) {
      auto& dstPb = copy->AddComponent<ProModelerComponent>();
      dstPb.enabled = srcPb->enabled;
      dstPb.shape = srcPb->shape;
      dstPb.size = srcPb->size;
      dstPb.steps = srcPb->steps;
      dstPb.extrudeAmount = srcPb->extrudeAmount;
      dstPb.vertices = srcPb->vertices;
      dstPb.indices = srcPb->indices;
      dstPb.triangleFaceIds = srcPb->triangleFaceIds;
      dstPb.nextFaceId = srcPb->nextFaceId;
      dstPb.faceMaterialPaths = srcPb->faceMaterialPaths;
      if (auto* dstMr = copy->GetComponent<MeshRendererComponent>())
        dstPb.RebuildMesh(*dstMr);
    }

    if (auto* srcLight = src->GetComponent<LightComponent>()) {
      auto& dstLight = copy->AddComponent<LightComponent>();
      dstLight.enabled = srcLight->enabled;
      dstLight.type = srcLight->type;
      dstLight.color = srcLight->color;
      dstLight.intensity = srcLight->intensity;
      dstLight.range = srcLight->range;
      dstLight.spotAngle = srcLight->spotAngle;
      dstLight.spotInnerAngle = srcLight->spotInnerAngle;
    }

    if (auto* srcPost = src->GetComponent<PostProcessVolumeComponent>()) {
      auto& dstPost = copy->AddComponent<PostProcessVolumeComponent>();
      dstPost.enabled = srcPost->enabled;
      dstPost.isGlobal = srcPost->isGlobal;
      dstPost.priority = srcPost->priority;
      dstPost.fogEnabled = srcPost->fogEnabled;
      dstPost.fogColor = srcPost->fogColor;
      dstPost.fogStart = srcPost->fogStart;
      dstPost.fogEnd = srcPost->fogEnd;
      dstPost.colorGradingEnabled = srcPost->colorGradingEnabled;
      dstPost.exposure = srcPost->exposure;
      dstPost.contrast = srcPost->contrast;
      dstPost.saturation = srcPost->saturation;
      dstPost.colorFilter = srcPost->colorFilter;
      dstPost.vignetteEnabled = srcPost->vignetteEnabled;
      dstPost.vignetteColor = srcPost->vignetteColor;
      dstPost.vignetteIntensity = srcPost->vignetteIntensity;
      dstPost.vignetteSmoothness = srcPost->vignetteSmoothness;
      dstPost.skyboxEnabled = srcPost->skyboxEnabled;
      dstPost.skyboxTexturePath = srcPost->skyboxTexturePath;
      dstPost.skyboxTexture = srcPost->skyboxTexture;
      dstPost.skyboxRotationDegrees = srcPost->skyboxRotationDegrees;
      dstPost.skyboxExposure = srcPost->skyboxExposure;
      dstPost.skyboxTint = srcPost->skyboxTint;
    }

    if (auto* srcAudio = src->GetComponent<AudioSourceComponent>()) {
      auto& dstAudio = copy->AddComponent<AudioSourceComponent>();
      dstAudio.enabled = srcAudio->enabled;
      dstAudio.clipPath = srcAudio->clipPath;
      dstAudio.playOnAwake = srcAudio->playOnAwake;
      dstAudio.loop = srcAudio->loop;
      dstAudio.mute = srcAudio->mute;
      dstAudio.volume = srcAudio->volume;
    }

    if (auto* srcCam = src->GetComponent<CameraComponent>()) {
      auto& dstCam = copy->AddComponent<CameraComponent>();
      dstCam.enabled = srcCam->enabled;
      dstCam.primary = false;
      dstCam.camera = srcCam->camera;
      dstCam.prerenderedBackgroundPath = srcCam->prerenderedBackgroundPath;
      dstCam.shotTriggerEntityId = srcCam->shotTriggerEntityId;
      dstCam.shotPriority = srcCam->shotPriority;
    }

    for (auto* srcScript : src->GetComponents<MipsScriptComponent>()) {
      auto& dstScript = copy->AddComponent<MipsScriptComponent>();
      dstScript.enabled = srcScript->enabled;
      dstScript.scriptPath = srcScript->scriptPath;
      dstScript.module = srcScript->module;
      dstScript.fieldValues = srcScript->fieldValues;
      dstScript.fieldAssetPaths = srcScript->fieldAssetPaths;
    }

    if (auto* srcCol = src->GetComponent<ColliderComponent>()) {
      auto& dstCol = copy->AddComponent<ColliderComponent>();
      dstCol.enabled = srcCol->enabled;
      dstCol.shape = srcCol->shape;
      dstCol.center = srcCol->center;
      dstCol.halfExtents = srcCol->halfExtents;
      dstCol.radius = srcCol->radius;
      dstCol.capsuleHeight = srcCol->capsuleHeight;
      dstCol.isTrigger = srcCol->isTrigger;
      dstCol.cameraShotTrigger = srcCol->cameraShotTrigger;
      dstCol.cameraTargetEntityId = srcCol->cameraTargetEntityId;
    }

    if (auto* srcRb = src->GetComponent<RigidbodyComponent>()) {
      auto& dstRb = copy->AddComponent<RigidbodyComponent>();
      dstRb.enabled = srcRb->enabled;
      dstRb.bodyType = srcRb->bodyType;
      dstRb.mass = srcRb->mass;
      dstRb.useGravity = srcRb->useGravity;
      dstRb.linearDrag = srcRb->linearDrag;
      dstRb.bounciness = srcRb->bounciness;
      dstRb.freezeRotation = srcRb->freezeRotation;
    }

    if (auto* srcCanvas = src->GetComponent<CanvasComponent>()) {
      auto& dstCanvas = copy->AddComponent<CanvasComponent>();
      dstCanvas.enabled = srcCanvas->enabled;
      dstCanvas.renderMode = srcCanvas->renderMode;
      dstCanvas.scaleMode = srcCanvas->scaleMode;
      dstCanvas.sortOrder = srcCanvas->sortOrder;
      dstCanvas.eventCameraEntityId = srcCanvas->eventCameraEntityId;
      dstCanvas.referenceResolution = srcCanvas->referenceResolution;
      dstCanvas.matchWidthOrHeight = srcCanvas->matchWidthOrHeight;
      dstCanvas.planeDistance = srcCanvas->planeDistance;
    }

    if (auto* srcRect = src->GetComponent<RectTransformComponent>()) {
      auto& dstRect = copy->AddComponent<RectTransformComponent>();
      dstRect.enabled = srcRect->enabled;
      dstRect.anchorMin = srcRect->anchorMin;
      dstRect.anchorMax = srcRect->anchorMax;
      dstRect.pivot = srcRect->pivot;
      dstRect.anchoredPosition = srcRect->anchoredPosition;
      dstRect.sizeDelta = srcRect->sizeDelta;
    }

    if (auto* srcImage = src->GetComponent<UIImageComponent>()) {
      auto& dstImage = copy->AddComponent<UIImageComponent>();
      dstImage.enabled = srcImage->enabled;
      dstImage.color = srcImage->color;
      dstImage.texturePath = srcImage->texturePath;
      dstImage.texture = srcImage->texture;
      dstImage.preserveAspect = srcImage->preserveAspect;
    }

    if (auto* srcText = src->GetComponent<UITextComponent>()) {
      auto& dstText = copy->AddComponent<UITextComponent>();
      dstText.enabled = srcText->enabled;
      dstText.text = srcText->text;
      dstText.color = srcText->color;
      dstText.fontSize = srcText->fontSize;
      dstText.alignment = srcText->alignment;
    }

    if (auto* srcGroup = src->GetComponent<UIButtonGroupComponent>()) {
      auto& dstGroup = copy->AddComponent<UIButtonGroupComponent>();
      dstGroup.enabled = srcGroup->enabled;
      dstGroup.selectedIndex = srcGroup->selectedIndex;
      dstGroup.wrapNavigation = srcGroup->wrapNavigation;
      dstGroup.keyboardNavigation = srcGroup->keyboardNavigation;
      dstGroup.gamepadNavigation = srcGroup->gamepadNavigation;
      dstGroup.keyboardConfirm = srcGroup->keyboardConfirm;
      dstGroup.gamepadConfirm = srcGroup->gamepadConfirm;
      dstGroup.confirmKey = srcGroup->confirmKey;
      dstGroup.confirmButton = srcGroup->confirmButton;
      dstGroup.cursorTexturePath = srcGroup->cursorTexturePath;
      dstGroup.cursorTexture = srcGroup->cursorTexture;
      dstGroup.cursorOffset = srcGroup->cursorOffset;
      dstGroup.cursorSize = srcGroup->cursorSize;
    }

    if (auto* srcButton = src->GetComponent<UIButtonComponent>()) {
      auto& dstButton = copy->AddComponent<UIButtonComponent>();
      dstButton.enabled = srcButton->enabled;
      dstButton.label = srcButton->label;
      dstButton.backgroundTexturePath = srcButton->backgroundTexturePath;
      dstButton.backgroundTexture = srcButton->backgroundTexture;
      dstButton.preserveAspect = srcButton->preserveAspect;
      dstButton.normalColor = srcButton->normalColor;
      dstButton.selectedColor = srcButton->selectedColor;
      dstButton.pressedColor = srcButton->pressedColor;
      dstButton.textColor = srcButton->textColor;
      dstButton.fontSize = srcButton->fontSize;
    }

    if (auto* srcSpectrum = src->GetComponent<UIAudioSpectrumComponent>()) {
      auto& dstSpectrum = copy->AddComponent<UIAudioSpectrumComponent>();
      dstSpectrum.enabled = srcSpectrum->enabled;
      dstSpectrum.sourceEntityId = srcSpectrum->sourceEntityId;
      dstSpectrum.color = srcSpectrum->color;
      dstSpectrum.backgroundColor = srcSpectrum->backgroundColor;
      dstSpectrum.barCount = srcSpectrum->barCount;
      dstSpectrum.barGap = srcSpectrum->barGap;
      dstSpectrum.sensitivity = srcSpectrum->sensitivity;
      dstSpectrum.smoothing = srcSpectrum->smoothing;
    }

    for (uint32_t childId : src->GetChildIDs()) {
      if (Entity* child = FindEntity(childId))
        cloneRecursive(child, copy);
    }
    return copy;
  };

  return cloneRecursive(&source, nullptr);
}

void Scene::SyncCamerasToWorldTransforms() {
    for (auto& entityPtr : m_Entities) {
        auto* cam = entityPtr->GetComponent<CameraComponent>();
        auto* tr  = entityPtr->GetComponent<TransformComponent>();
        if (!cam || !cam->enabled || !tr) continue;

        const glm::mat4 world = GetWorldMatrix(*entityPtr);
        const glm::vec3 worldPos(world[3]);
        cam->camera.SyncFromWorldMatrix(worldPos, world);
    }
}

void Scene::Update(float deltaTime) {
    if (m_AnimateCharacters)
        AnimationSystem::Update(*this, deltaTime);
}

void Scene::SetPrimaryCamera(Entity& entity) {
    for (auto& entityPtr : m_Entities) {
        auto* cameraComp = entityPtr->GetComponent<CameraComponent>();
        if (!cameraComp)
            continue;
        cameraComp->primary = (entityPtr.get() == &entity);
    }
}

void Scene::NormalizePrimaryCameras() {
    std::vector<Entity*> primaries;
    primaries.reserve(m_Entities.size());

    for (auto& entityPtr : m_Entities) {
        auto* cameraComp = entityPtr->GetComponent<CameraComponent>();
        if (cameraComp && cameraComp->enabled && cameraComp->primary)
            primaries.push_back(entityPtr.get());
    }

    auto tagName = [](Entity* entity) -> const char* {
        if (auto* tag = entity->GetComponent<TagComponent>())
            return tag->tag.c_str();
        return "Camera";
    };

    auto isFpsCamera = [](Entity* entity) -> bool {
      for (auto* script : entity->GetComponents<MipsScriptComponent>()) {
            if (script->module && script->module->className == "FirstPersonController")
                return true;
            if (script->scriptPath.find("FirstPersonController") != std::string::npos)
                return true;
      }
        return false;
    };

    Entity* chosen = nullptr;
    if (primaries.size() > 1) {
        for (Entity* entity : primaries) {
            if (isFpsCamera(entity)) {
                chosen = entity;
                break;
            }
        }
        if (!chosen)
            chosen = primaries.front();

        MIPSYNC_WARN("Multiple primary cameras; using '{}'", tagName(chosen));
        for (auto& entityPtr : m_Entities) {
            if (auto* cameraComp = entityPtr->GetComponent<CameraComponent>())
                cameraComp->primary = (entityPtr.get() == chosen);
        }
        return;
    }

    if (!primaries.empty())
        return;

    // Do not silently promote a non-primary shot camera. The renderer can still
    // use the first enabled camera as a fallback without mutating scene data.
}

Entity* Scene::GetPrimaryCameraEntity() {
    Entity* bestPrimary = nullptr;
    for (auto& entity : m_Entities) {
        auto* cameraComp = entity->GetComponent<CameraComponent>();
        if (!cameraComp || !cameraComp->enabled || !cameraComp->primary)
            continue;
        if (!bestPrimary ||
            bestPrimary->GetID() > entity->GetID())
            bestPrimary = entity.get();
    }
    if (bestPrimary)
        return bestPrimary;
    for (auto& entity : m_Entities) {
        auto* cameraComp = entity->GetComponent<CameraComponent>();
        if (cameraComp && cameraComp->enabled)
            return entity.get();
    }
    return nullptr;
}

Camera* Scene::GetPrimaryCamera() {
    Entity* entity = GetPrimaryCameraEntity();
    if (!entity)
        return nullptr;
    auto* cameraComp = entity->GetComponent<CameraComponent>();
    return cameraComp ? &cameraComp->camera : nullptr;
}

namespace {

glm::vec3 LightForwardFromWorldMatrix(const glm::mat4& world) {
    return glm::normalize(glm::vec3(world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
}

} // namespace

void Scene::UploadLightsToRenderer(Renderer& renderer) const {
    SceneLightGpu lights[kMaxSceneLights];
    int count = 0;

    for (const auto& entityPtr : m_Entities) {
        if (!entityPtr || count >= kMaxSceneLights)
            break;
        auto* light = entityPtr->GetComponent<LightComponent>();
        auto* transform = entityPtr->GetComponent<TransformComponent>();
        if (!light || !light->enabled || !transform)
            continue;

        const glm::mat4 world = GetWorldMatrix(*entityPtr);
        const glm::vec3 worldPos = glm::vec3(world[3]);
        const glm::vec3 forward = LightForwardFromWorldMatrix(world);

        SceneLightGpu gpu{};
        gpu.colorIntensity = glm::vec4(light->color, light->intensity);
        gpu.dirRange.w = light->range;

        const float outerRad = glm::radians(std::clamp(light->spotAngle, 1.0f, 89.0f));
        const float innerRad = glm::radians(std::clamp(light->spotInnerAngle, 0.5f, light->spotAngle));
        gpu.spotParams.x = std::cos(outerRad);
        gpu.spotParams.y = std::cos(innerRad);

        switch (light->type) {
        case LightType::Directional:
            gpu.posType = glm::vec4(worldPos, 0.0f);
            gpu.dirRange = glm::vec4(forward, 0.0f);
            break;
        case LightType::Point:
            gpu.posType = glm::vec4(worldPos, 1.0f);
            gpu.dirRange = glm::vec4(0.0f, 0.0f, 0.0f, light->range);
            break;
        case LightType::Spot:
            gpu.posType = glm::vec4(worldPos, 2.0f);
            gpu.dirRange = glm::vec4(forward, light->range);
            break;
        }
        lights[count++] = gpu;
    }

    renderer.SetSceneLights(lights, count);
}

void Scene::Render(Renderer& renderer, const Camera& camera, Framebuffer* targetFBO,
                   uint32_t highlightEntityId, bool usePs1PreviewMeshes,
                   bool includeEditorOnlyMeshes, const Texture* backgroundTexture) {
    UploadLightsToRenderer(renderer);
    // Pass 1: render all meshes normally (selected mesh included).
    renderer.BeginScene(camera, targetFBO, backgroundTexture);

    Entity* highlightEntity = nullptr;
    TransformComponent* highlightTransform = nullptr;
    MeshRendererComponent* highlightMesh = nullptr;

    glm::mat4 highlightWorld(1.0f);

    // Pre-rendered backgrounds have no depth of their own. Seed the depth
    // buffer with marked Unity geometry before drawing dynamic objects.
    if (backgroundTexture && !includeEditorOnlyMeshes) {
        for (auto& entity : m_Entities) {
            if (!entity->IsActive()) continue;
            auto* transform = entity->GetComponent<TransformComponent>();
            auto* meshRenderer = entity->GetComponent<MeshRendererComponent>();
            if (!transform || !meshRenderer || !meshRenderer->enabled ||
                !meshRenderer->mesh || !meshRenderer->prerenderOccluder)
                continue;

            glm::mat4 worldMatrix = GetWorldMatrix(*entity);
            if (meshRenderer->meshPrimitive == "File" && meshRenderer->meshSize != 1.0f) {
                worldMatrix = worldMatrix * glm::scale(
                    glm::mat4(1.0f), glm::vec3(meshRenderer->meshSize));
            }
            renderer.DrawMeshDepthOnly(*meshRenderer->mesh, worldMatrix);
        }
    }

    for (auto& entity : m_Entities) {
        if (!entity->IsActive()) continue;
        auto* transform = entity->GetComponent<TransformComponent>();
        auto* meshRenderer = entity->GetComponent<MeshRendererComponent>();
        if (!transform || !meshRenderer || !meshRenderer->enabled || !meshRenderer->mesh)
            continue;
        if (meshRenderer->prerenderOccluder && backgroundTexture && !includeEditorOnlyMeshes)
            continue;
        if (meshRenderer->editorOnly && !includeEditorOnlyMeshes)
            continue;

        glm::mat4 worldMatrix = GetWorldMatrix(*entity);
        // File meshes are imported at unit extent; mesh_size matches primitives and PS1 export.
        if (meshRenderer->meshPrimitive == "File" && meshRenderer->meshSize != 1.0f) {
            worldMatrix = worldMatrix * glm::scale(glm::mat4(1.0f),
                                                   glm::vec3(meshRenderer->meshSize));
        }
        const Mesh* drawMesh = meshRenderer->mesh.get();
        float projectionDepthClamp = 0.0f;
        if (usePs1PreviewMeshes && meshRenderer->meshPrimitive == "File") {
            EnsurePs1PreviewMesh(*meshRenderer);
            if (meshRenderer->ps1PreviewMesh) {
                drawMesh = meshRenderer->ps1PreviewMesh.get();
            }
        }

        if (auto* proModeler = entity->GetComponent<ProModelerComponent>()) {
            DrawProModelerWithFaceMaterials(renderer, *proModeler, *meshRenderer, *drawMesh,
                                            worldMatrix, projectionDepthClamp);
        } else {
            renderer.DrawMesh(*drawMesh, worldMatrix,
                              ResolveSceneViewTexture(*meshRenderer, usePs1PreviewMeshes), meshRenderer->color,
                              meshRenderer->textureTiling, meshRenderer->textureOffset,
                              0, 0, projectionDepthClamp);
        }

        if (entity->GetID() == highlightEntityId) {
            highlightEntity = entity.get();
            highlightTransform = transform;
            highlightMesh = meshRenderer;
            highlightWorld = worldMatrix;
        }
    }

    struct CharacterBoneKey {
        const SkeletalModelAsset* model = nullptr;
        uint32_t rootId = 0;
        const AnimatorComponent* animator = nullptr;

        bool operator==(const CharacterBoneKey& other) const noexcept {
            return model == other.model && rootId == other.rootId && animator == other.animator;
        }
    };
    struct CharacterBoneKeyHash {
        size_t operator()(const CharacterBoneKey& k) const noexcept {
            size_t h = std::hash<const void*>{}(k.model);
            h ^= std::hash<uint32_t>{}(k.rootId) << 1;
            h ^= std::hash<const void*>{}(k.animator) << 2;
            return h;
        }
    };
    std::unordered_map<CharacterBoneKey, std::array<glm::mat4, kMaxBones>, CharacterBoneKeyHash>
        characterBones;
    characterBones.reserve(8);

    for (auto& entity : m_Entities) {
        auto* skinned = entity->GetComponent<SkinnedMeshRendererComponent>();
        if (!skinned || !skinned->enabled || !skinned->mesh)
            continue;

        std::shared_ptr<SkeletalModelAsset> skel;
        if (!skinned->modelPath.empty())
            skel = AssetManager::Get().GetSkeletalModel(skinned->modelPath);
        if (!skel || !skel->mesh || skel->mesh->GetIndexCount() == 0)
            continue;

        const uint32_t rootId = FindSkeletalCharacterRootId(*entity, *this);
        AnimatorComponent* animator = entity->GetComponent<AnimatorComponent>();
        if (!animator)
            animator = FindAnimatorInAncestors(*entity, *this);
        const CharacterBoneKey key{ skel.get(), rootId, animator };
        if (!characterBones.count(key)) {
            if (animator && !skinned->modelPath.empty()) {
                if (animator->modelPath != skinned->modelPath)
                    animator->modelPath = skinned->modelPath;
                if (!animator->model || animator->model.get() != skel.get())
                    animator->model = skel;
            }
            FillSkinnedBoneMatrices(*skinned, animator, m_AnimateCharacters, characterBones[key].data());
        }
    }

    for (auto& entity : m_Entities) {
        auto* transform = entity->GetComponent<TransformComponent>();
        auto* skinned = entity->GetComponent<SkinnedMeshRendererComponent>();
        if (!transform || !skinned || !skinned->enabled || !skinned->mesh)
            continue;

        std::shared_ptr<SkeletalModelAsset> skel;
        if (!skinned->modelPath.empty())
            skel = AssetManager::Get().GetSkeletalModel(skinned->modelPath);
        if (!skel || !skel->mesh || skel->mesh->GetIndexCount() == 0)
            continue;

        const int partIndex = ResolveSkinnedMeshPartIndex(*entity, *skinned, *skel);
        if (partIndex == -2)
            continue;

        const bool useComponentMaterial =
            !skinned->materialPath.empty() || !skinned->texturePath.empty();

        const glm::mat4 worldMatrix = GetWorldMatrix(*entity);
        const glm::mat4 partGeo = partIndex >= 0
            ? skel->meshParts[static_cast<size_t>(partIndex)].geometryToWorld
            : skel->meshGeometryToWorld;

        const uint32_t rootId = FindSkeletalCharacterRootId(*entity, *this);
        AnimatorComponent* drawAnimator = entity->GetComponent<AnimatorComponent>();
        if (!drawAnimator)
            drawAnimator = FindAnimatorInAncestors(*entity, *this);
        const CharacterBoneKey key{ skel.get(), rootId, drawAnimator };
        const auto boneIt = characterBones.find(key);
        if (boneIt == characterBones.end())
            continue;

        const int boneCount = static_cast<int>(skel->bones.size());
        const int drawPartFilter = partIndex >= 0 ? partIndex : -1;
        if (usePs1PreviewMeshes && skinned->ps1ExportMode == SkinnedMeshRendererComponent::Ps1ExportMode::RigidBones) {
            EnsurePs1RigidPreview(*skinned);
            if (skinned->ps1RigidPreviewMesh) {
                DrawSkeletalModelGpu(renderer, *skel, *skinned->ps1RigidPreviewMesh, worldMatrix, partGeo, boneIt->second.data(),
                                      boneCount, useComponentMaterial, skinned->texture.get(), skinned->color,
                                      skinned->textureTiling, skinned->textureOffset, drawPartFilter, false);
            }
        } else {
            DrawSkeletalModelGpu(renderer, *skel, *skel->mesh, worldMatrix, partGeo, boneIt->second.data(),
                                  boneCount, useComponentMaterial, skinned->texture.get(), skinned->color,
                                  skinned->textureTiling, skinned->textureOffset, drawPartFilter);
        }
    }

    renderer.EndScene(targetFBO);

    // Pass 2: post-process outline around the selected mesh (Unity-style).
    if (targetFBO && highlightEntity && highlightTransform && highlightMesh && highlightMesh->mesh) {
        static const glm::vec3 kSelectionOutlineColor{ 1.0f, 0.78f, 0.18f };
        const Mesh* outlineMesh = highlightMesh->mesh.get();
        if (usePs1PreviewMeshes && highlightMesh->meshPrimitive == "File") {
            EnsurePs1PreviewMesh(*highlightMesh);
            if (highlightMesh->ps1PreviewMesh)
                outlineMesh = highlightMesh->ps1PreviewMesh.get();
        }
        renderer.RenderSelectionOutline(*targetFBO, *outlineMesh,
                                        highlightWorld,
                                        kSelectionOutlineColor, 3.0f);
    }
}

} // namespace MipsyncEngine
