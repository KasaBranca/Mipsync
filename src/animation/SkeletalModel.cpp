#include "SkeletalModel.h"
#include "UfbxMath.h"
#include "../assets/AssetManager.h"
#include "../core/Log.h"
#include "../renderer/Mesh.h"
#include "../renderer/Renderer.h"
#include "../renderer/Texture.h"
#include "../scene/Scene.h"
#include <string>
#include <ufbx.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace MipsyncEngine {

namespace fs = std::filesystem;

static SkinnedVertex MakeSkinnedVertex(const glm::vec3& pos, const glm::vec3& normal, const glm::vec2& uv) {
    SkinnedVertex v{};
    v.position = pos;
    v.normal = glm::length(normal) > 1e-6f ? glm::normalize(normal) : glm::vec3(0.0f, 1.0f, 0.0f);
    v.uv = uv;
    v.color = glm::vec4(1.0f);
    return v;
}

static const ufbx_node* FindNodeByTypedId(const ufbx_scene* scene, uint32_t typedId) {
    if (!scene || typedId >= scene->nodes.count)
        return nullptr;
    return scene->nodes.data[typedId];
}

/// World matrix of `node` at `time` (ufbx official viewer uses per-node evaluation, not evaluate_scene).
static glm::mat4 NodeToWorldAtAnimTime(const ufbx_anim* anim, const ufbx_node* node, double time) {
    if (!anim || !node)
        return glm::mat4(1.0f);
    const ufbx_transform local = ufbx_evaluate_transform(anim, node, time);
    const glm::mat4 localM = UfbxTransformToGlm(local);
    if (!node->parent)
        return localM;
    return NodeToWorldAtAnimTime(anim, node->parent, time) * localM;
}

static const ufbx_anim* ResolveAnimDescriptor(const ufbx_scene* scene, const ufbx_anim_stack* stack) {
    if (!scene || !stack)
        return nullptr;
    if (stack->anim)
        return stack->anim;
    if (scene->anim)
        return scene->anim;
    return nullptr;
}

static int FindBoneIndex(const std::vector<SkeletalBone>& bones, const ufbx_node* node) {
    for (size_t i = 0; i < bones.size(); ++i) {
        if (bones[i].node == node)
            return static_cast<int>(i);
    }
    return -1;
}

static void QuantizeWeights(const float weights[4], int indices[4], glm::vec4& outIdx, glm::vec4& outW) {
    float sum = 0.0f;
    for (int i = 0; i < 4; ++i)
        sum += weights[i];

    if (sum < 1e-6f) {
        outIdx = glm::vec4(0.0f);
        outW = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    int order[4] = { 0, 1, 2, 3 };
    for (int i = 0; i < 3; ++i) {
        for (int j = i + 1; j < 4; ++j) {
            if (weights[order[j]] > weights[order[i]])
                std::swap(order[i], order[j]);
        }
    }

    float qSum = 0.0f;
    for (int i = 0; i < 4; ++i) {
        outIdx[i] = static_cast<float>(indices[order[i]]);
        const float w = weights[order[i]] / sum;
        outW[i] = w;
        qSum += w;
    }
    if (qSum > 1e-6f)
        outW /= qSum;
}

glm::vec4 SkinPositionInGeometry(const SkinnedVertex& sv, const glm::mat4 bones[kMaxBones],
                                 int maxBoneIndex) {
    glm::vec4 pos(0.0f);
    for (int i = 0; i < 4; ++i) {
        const float w = sv.boneWeights[i];
        if (w < 1e-6f)
            continue;
        const int bi = glm::clamp(static_cast<int>(sv.boneIndices[i] + 0.5f), 0, maxBoneIndex);
        pos += w * (bones[bi] * glm::vec4(sv.position, 1.0f));
    }
    return pos;
}

glm::vec3 SkinNormalInGeometry(const SkinnedVertex& sv, const glm::mat4 bones[kMaxBones],
                               int maxBoneIndex) {
    glm::vec3 nrm(0.0f);
    for (int i = 0; i < 4; ++i) {
        const float w = sv.boneWeights[i];
        if (w < 1e-6f)
            continue;
        const int bi = glm::clamp(static_cast<int>(sv.boneIndices[i] + 0.5f), 0, maxBoneIndex);
        const glm::mat3 normalMat = glm::mat3(glm::transpose(glm::inverse(bones[bi])));
        nrm += w * (normalMat * sv.normal);
    }
    const float len = glm::length(nrm);
    return len > 1e-6f ? nrm / len : glm::vec3(0.0f, 1.0f, 0.0f);
}

static glm::vec3 SkinnedWorldPosition(const SkinnedVertex& sv, const glm::mat4& geometryToWorld,
                                    const glm::mat4 bones[kMaxBones], int maxBoneIndex) {
    float weightSum = sv.boneWeights.x + sv.boneWeights.y + sv.boneWeights.z + sv.boneWeights.w;
    const glm::vec3 geoPos = weightSum > 1e-6f
        ? glm::vec3(SkinPositionInGeometry(sv, bones, maxBoneIndex))
        : sv.position;
    return glm::vec3(geometryToWorld * glm::vec4(geoPos, 1.0f));
}

static void ComputeDisplayFit(SkeletalModelAsset& asset, const glm::mat4 bones[kMaxBones]) {
    if (asset.sourceVertices.empty())
        return;

    const int maxBoneIndex = static_cast<int>(asset.bones.size()) - 1;

    glm::vec3 worldMin = glm::vec3(0.0f);
    glm::vec3 worldMax = glm::vec3(0.0f);
    bool first = true;

    for (size_t vi = 0; vi < asset.sourceVertices.size(); ++vi) {
        const glm::mat4 geo =
            vi < asset.vertexGeometryToWorld.size()
            ? asset.vertexGeometryToWorld[vi]
            : asset.meshGeometryToWorld;
        const glm::vec3 worldPos =
            SkinnedWorldPosition(asset.sourceVertices[vi], geo, bones, maxBoneIndex);
        if (first) {
            worldMin = worldMax = worldPos;
            first = false;
        } else {
            worldMin = glm::min(worldMin, worldPos);
            worldMax = glm::max(worldMax, worldPos);
        }
    }

    asset.displayCenter = (worldMin + worldMax) * 0.5f;
    const glm::vec3 extent = worldMax - worldMin;
    const float maxExtent = std::max({ extent.x, extent.y, extent.z, 1e-6f });
    asset.displayScale = 2.0f / maxExtent;
    asset.meshCenter = asset.displayCenter;
    asset.vertexBakeScale = asset.displayScale;
    asset.recommendedUniformScale = asset.displayScale;

    const glm::vec3 half = extent * 0.5f * asset.displayScale;
    asset.boundsMin = -half;
    asset.boundsMax = half;
}

static int FindOrAddBone(std::vector<SkeletalBone>& bones, const ufbx_skin_cluster* cluster,
                         const std::string& path) {
    if (!cluster || !cluster->bone_node)
        return -1;

    for (size_t i = 0; i < bones.size(); ++i) {
        if (bones[i].node == cluster->bone_node)
            return static_cast<int>(i);
    }

    if (bones.size() >= static_cast<size_t>(kMaxBones)) {
        MIPSYNC_WARN("FBX exceeds {} bones; extra clusters skipped: {}", kMaxBones, path);
        return -1;
    }

    SkeletalBone bone{};
    bone.name = cluster->bone_node->name.data ? cluster->bone_node->name.data : "Bone";
    bone.node = cluster->bone_node;
    bone.nodeTypedId = cluster->bone_node->typed_id;
    bone.bindGeometryToBone = UfbxMatrixToGlm(cluster->geometry_to_bone);
    bone.bindPoseMatrix =
        UfbxMatrixToGlm(cluster->bone_node->node_to_world) * bone.bindGeometryToBone;
    bones.push_back(bone);
    return static_cast<int>(bones.size()) - 1;
}

static void FinalizeSubmeshRange(std::vector<SkeletalModelSubmesh>& submeshes, uint32_t indexStart,
                                 uint32_t indexEnd, int materialIndex) {
    if (indexEnd <= indexStart || materialIndex < 0)
        return;
    if (!submeshes.empty()) {
        const SkeletalModelSubmesh& prev = submeshes.back();
        if (prev.materialIndex == static_cast<uint32_t>(materialIndex) &&
            prev.indexOffset + prev.indexCount == indexStart) {
            submeshes.back().indexCount += indexEnd - indexStart;
            return;
        }
    }
    SkeletalModelSubmesh sub{};
    sub.indexOffset = indexStart;
    sub.indexCount = indexEnd - indexStart;
    sub.materialIndex = static_cast<uint32_t>(materialIndex);
    submeshes.push_back(sub);
}

static const ufbx_texture* ResolveFileTexture(const ufbx_texture* tex) {
    if (!tex)
        return nullptr;
    const ufbx_texture* current = tex;
    for (int guard = 0; guard < 8 && current->type == UFBX_TEXTURE_LAYERED &&
                            current->layers.count > 0;
         ++guard) {
        current = current->layers.data[0].texture;
        if (!current)
            return nullptr;
    }
    return current;
}

static glm::vec4 ExtractMaterialColor(const ufbx_material* mat) {
    glm::vec3 rgb(0.8f, 0.8f, 0.8f);
    if (!mat)
        return glm::vec4(rgb, 1.0f);

    const ufbx_material_map& diffuse = mat->fbx.diffuse_color;
    if (diffuse.has_value && diffuse.value_components >= 3)
        rgb = UfbxToGlm(diffuse.value_vec3);
    else if (mat->pbr.base_color.has_value && mat->pbr.base_color.value_components >= 3)
        rgb = UfbxToGlm(mat->pbr.base_color.value_vec3);

    return glm::vec4(glm::clamp(rgb, glm::vec3(0.0f), glm::vec3(1.0f)), 1.0f);
}

static const ufbx_texture* ExtractDiffuseTexture(const ufbx_material* mat) {
    if (!mat)
        return nullptr;
    if (mat->fbx.diffuse_color.texture_enabled && mat->fbx.diffuse_color.texture)
        return ResolveFileTexture(mat->fbx.diffuse_color.texture);
    if (mat->pbr.base_color.texture_enabled && mat->pbr.base_color.texture)
        return ResolveFileTexture(mat->pbr.base_color.texture);
    for (size_t i = 0; i < mat->textures.count; ++i) {
        const ufbx_material_texture& slot = mat->textures.data[i];
        if (slot.texture)
            return ResolveFileTexture(slot.texture);
    }
    return nullptr;
}

static bool WriteEmbeddedTextureFile(const ufbx_texture* tex, const fs::path& outPath) {
    if (!tex || tex->content.size == 0)
        return false;
    std::error_code ec;
    fs::create_directories(outPath.parent_path(), ec);
    std::ofstream out(outPath, std::ios::binary);
    if (!out)
        return false;
    out.write(reinterpret_cast<const char*>(tex->content.data),
              static_cast<std::streamsize>(tex->content.size));
    return out.good();
}

static std::string PickExistingTexturePath(const ufbx_texture* tex, const fs::path& fbxDir) {
    if (!tex)
        return {};

    auto tryPath = [&](const ufbx_string& str) -> std::string {
        if (!str.data || str.length == 0)
            return {};
        fs::path p = PathUtf8::FromString(std::string(str.data, str.length));
        if (p.empty())
            return {};
        if (!p.is_absolute())
            p = fbxDir / p;
        std::error_code ec;
        if (fs::exists(p, ec))
            return PathUtf8::ToString(fs::absolute(p, ec));
        return {};
    };

    std::string abs = tryPath(tex->filename);
    if (!abs.empty())
        return abs;
    abs = tryPath(tex->relative_filename);
    if (!abs.empty())
        return abs;
    return tryPath(tex->absolute_filename);
}

static std::string ResolveTexturePath(const ufbx_material* mat, const std::string& fbxAbsPath) {
    const ufbx_texture* tex = ExtractDiffuseTexture(mat);
    if (!tex)
        return {};

    const fs::path fbxPath = PathUtf8::FromString(fbxAbsPath);
    const fs::path fbxDir = fbxPath.parent_path();
    const std::string existing = PickExistingTexturePath(tex, fbxDir);
    if (!existing.empty())
        return AssetManager::Get().ToProjectRelative(existing);

    const ufbx_texture* fileTex = ResolveFileTexture(tex);
    if (!fileTex || fileTex->content.size == 0)
        return {};

    std::string fileName = fileTex->name.data ? fileTex->name.data : "texture";
    if (fileName.find('.') == std::string::npos)
        fileName += ".png";

    const fs::path outDir =
        fbxDir / (PathUtf8::ToString(fbxPath.stem()) + "_textures");
    const fs::path outPath = outDir / PathUtf8::FromString(fileName);
    std::error_code ec;
    if (!fs::exists(outPath, ec)) {
        if (!WriteEmbeddedTextureFile(fileTex, outPath))
            return {};
        MIPSYNC_INFO("Extracted embedded FBX texture: {}", PathUtf8::ToString(outPath));
    }
    return AssetManager::Get().ToProjectRelative(PathUtf8::ToString(outPath));
}

struct FbxMaterialTable {
    std::vector<SkeletalModelMaterial> materials;
    std::unordered_map<uint32_t, int> byElementId;

    int Resolve(const ufbx_material* mat, const std::string& fbxAbsPath) {
        if (!mat)
            return EnsureDefault();
        const auto it = byElementId.find(mat->element_id);
        if (it != byElementId.end())
            return it->second;

        SkeletalModelMaterial slot{};
        slot.name = mat->name.data ? mat->name.data : "Material";
        slot.color = ExtractMaterialColor(mat);
        slot.texturePath = ResolveTexturePath(mat, fbxAbsPath);
        const int index = static_cast<int>(materials.size());
        materials.push_back(slot);
        byElementId.emplace(mat->element_id, index);
        if (!slot.texturePath.empty())
            MIPSYNC_INFO("  material '{}' -> {}", slot.name, slot.texturePath);
        return index;
    }

    int EnsureDefault() {
        if (!materials.empty())
            return 0;
        materials.push_back(SkeletalModelMaterial{});
        return 0;
    }
};

static const ufbx_material* GetFaceMaterial(const ufbx_mesh* mesh, const ufbx_node* instanceNode,
                                          size_t faceIndex) {
    if (!mesh)
        return nullptr;

    uint32_t slot = 0;
    if (faceIndex < mesh->face_material.count)
        slot = mesh->face_material.data[faceIndex];

    if (instanceNode && slot < instanceNode->materials.count)
        return instanceNode->materials.data[slot];
    if (slot < mesh->materials.count)
        return mesh->materials.data[slot];
    return nullptr;
}

static void BuildBonesFromSkin(const ufbx_skin_deformer* skin, const std::string& path,
                               std::vector<SkeletalBone>& outBones,
                               std::vector<int>& outClusterToBone) {
    outClusterToBone.clear();
    if (!skin)
        return;

    outClusterToBone.resize(skin->clusters.count, -1);
    for (size_t ci = 0; ci < skin->clusters.count; ++ci) {
        const ufbx_skin_cluster* cluster = skin->clusters.data[ci];
        outClusterToBone[ci] = FindOrAddBone(outBones, cluster, path);
    }
}

static void AppendMeshTriangles(const ufbx_mesh* mesh, const ufbx_node* instanceNode,
                                const glm::mat4& geometryToWorld, const ufbx_skin_deformer* skin,
                                const std::vector<int>& clusterToBone, FbxMaterialTable& materialTable,
                                const std::string& fbxAbsPath,
                                std::vector<SkeletalModelSubmesh>& submeshes, int& activeMaterialIndex,
                                uint32_t& submeshStart, std::vector<SkinnedVertex>& outVerts,
                                std::vector<glm::mat4>& outGeoToWorld,
                                std::vector<uint32_t>& outIndices) {
    if (!mesh)
        return;

    const ufbx_vec2 defaultUv{ 0.0f, 0.0f };
    const bool hasSkin = skin != nullptr;

    std::vector<SkinnedVertex> meshSkinVerts;
    if (hasSkin) {
        meshSkinVerts.resize(mesh->num_vertices);
        for (size_t vi = 0; vi < mesh->num_vertices; ++vi) {
            float weights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            int boneIdx[4] = { 0, 0, 0, 0 };
            int count = 0;
            float totalWeight = 0.0f;

            if (vi < skin->vertices.count) {
                const ufbx_skin_vertex vertexWeights = skin->vertices.data[vi];
                for (uint32_t wi = 0; wi < vertexWeights.num_weights && count < 4; ++wi) {
                    const ufbx_skin_weight weight =
                        skin->weights.data[vertexWeights.weight_begin + wi];
                    if (weight.cluster_index >= clusterToBone.size())
                        continue;
                    const int mapped = clusterToBone[weight.cluster_index];
                    if (mapped < 0)
                        continue;
                    boneIdx[count] = mapped;
                    weights[count] = static_cast<float>(weight.weight);
                    totalWeight += weights[count];
                    ++count;
                }
            }

            if (totalWeight > 1e-6f) {
                for (int i = 0; i < count; ++i)
                    weights[i] /= totalWeight;
            } else {
                weights[0] = 1.0f;
                count = 1;
            }

            QuantizeWeights(weights, boneIdx, meshSkinVerts[vi].boneIndices,
                            meshSkinVerts[vi].boneWeights);
        }
    }

    std::vector<uint32_t> triIndices(mesh->max_face_triangles * 3);
    if (triIndices.empty())
        triIndices.resize(3);

    for (size_t fi = 0; fi < mesh->num_faces; ++fi) {
        const ufbx_face face = mesh->faces.data[fi];
        if (face.num_indices < 3)
            continue;

        const int faceMaterial =
            materialTable.Resolve(GetFaceMaterial(mesh, instanceNode, fi), fbxAbsPath);
        if (faceMaterial != activeMaterialIndex) {
            if (activeMaterialIndex >= 0) {
                const uint32_t indexEnd = static_cast<uint32_t>(outIndices.size());
                FinalizeSubmeshRange(submeshes, submeshStart, indexEnd, activeMaterialIndex);
            }
            submeshStart = static_cast<uint32_t>(outIndices.size());
            activeMaterialIndex = faceMaterial;
        } else if (activeMaterialIndex < 0) {
            submeshStart = static_cast<uint32_t>(outIndices.size());
            activeMaterialIndex = faceMaterial;
        }

        const size_t numTris =
            ufbx_triangulate_face(triIndices.data(), triIndices.size(), mesh, face);
        for (size_t ti = 0; ti < numTris * 3; ++ti) {
            const uint32_t ix = triIndices[ti];
            const ufbx_vec3 pos = ufbx_get_vertex_vec3(&mesh->vertex_position, ix);
            const ufbx_vec3 nrm = ufbx_get_vertex_vec3(&mesh->vertex_normal, ix);
            const ufbx_vec2 uv = mesh->vertex_uv.exists
                ? ufbx_get_vertex_vec2(&mesh->vertex_uv, ix)
                : defaultUv;

            SkinnedVertex v = MakeSkinnedVertex(UfbxToGlm(pos), UfbxToGlm(nrm), { uv.x, uv.y });
            if (hasSkin) {
                const size_t vertIndex = mesh->vertex_indices.data[ix];
                if (vertIndex < meshSkinVerts.size()) {
                    v.boneIndices = meshSkinVerts[vertIndex].boneIndices;
                    v.boneWeights = meshSkinVerts[vertIndex].boneWeights;
                }
            } else {
                v.boneIndices = glm::vec4(0.0f);
                v.boneWeights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            }

            outVerts.push_back(v);
            outGeoToWorld.push_back(geometryToWorld);
            outIndices.push_back(static_cast<uint32_t>(outVerts.size() - 1));
        }
    }
}

static uint64_t HashBoneMatrices(const glm::mat4 bones[kMaxBones], size_t boneCount) {
    uint64_t hash = 14695981039346656037ULL;
    const size_t count = std::min(boneCount, static_cast<size_t>(kMaxBones));
    const auto* bytes = reinterpret_cast<const uint8_t*>(bones);
    const size_t numBytes = count * sizeof(glm::mat4);
    for (size_t i = 0; i < numBytes; ++i)
        hash = (hash ^ static_cast<uint64_t>(bytes[i])) * 1099511628211ULL;
    return hash;
}

static void BakeVertexNormalTransforms(SkeletalModelAsset& asset) {
    asset.vertexNormalGeo.resize(asset.vertexGeometryToWorld.size());
    for (size_t i = 0; i < asset.vertexGeometryToWorld.size(); ++i)
        asset.vertexNormalGeo[i] =
            glm::transpose(glm::inverse(glm::mat3(asset.vertexGeometryToWorld[i])));
}

static bool SubmeshBelongsToPart(const SkeletalModelSubmesh& sub, const SkeletalModelMeshPart& part) {
    return sub.indexOffset >= part.indexOffset &&
           sub.indexOffset + sub.indexCount <= part.indexOffset + part.indexCount;
}

static bool SubmeshPassesPartFilter(const SkeletalModelAsset& model, const SkeletalModelSubmesh& sub,
                                    int meshPartIndex) {
    if (meshPartIndex < 0)
        return true;
    if (meshPartIndex >= static_cast<int>(model.meshParts.size()))
        return true;
    return SubmeshBelongsToPart(sub, model.meshParts[static_cast<size_t>(meshPartIndex)]);
}

void DrawSkeletalModelGpu(Renderer& renderer, const SkeletalModelAsset& model, const SkinnedMesh& mesh,
                          const glm::mat4& entityWorld, const glm::mat4& partGeometryToWorld,
                          const glm::mat4 bones[kMaxBones], int boneCount, bool useComponentMaterial,
                          const Texture* overrideTexture, const glm::vec4& overrideColor,
                          const glm::vec2& overrideTiling, const glm::vec2& overrideOffset,
                          int meshPartIndex, bool useSourceIndexRanges) {
    auto drawRange = [&](const Texture* tex, const glm::vec4& color, const glm::vec2& tiling,
                         const glm::vec2& offset, uint32_t indexOffset, uint32_t indexCount) {
        renderer.DrawSkinnedMesh(mesh, entityWorld, partGeometryToWorld, model.displayCenter,
                                 model.displayScale, bones, boneCount, tex, color, tiling, offset,
                                 indexOffset, indexCount);
    };

    // PS1 rigid preview meshes are rebuilt into a new, compact index buffer. Source FBX
    // submesh offsets no longer address that buffer, so it must be drawn as one full range.
    // Pick the first applicable source material only as a visual approximation; the PS1
    // exporter likewise bakes the rebuilt geometry independently of the source draw ranges.
    if (!useSourceIndexRanges) {
        if (useComponentMaterial) {
            drawRange(overrideTexture, overrideColor, overrideTiling, overrideOffset, 0, 0);
            return;
        }

        AssetManager& assets = AssetManager::Get();
        for (const SkeletalModelSubmesh& sub : model.submeshes) {
            if (sub.indexCount == 0 || !SubmeshPassesPartFilter(model, sub, meshPartIndex))
                continue;

            glm::vec4 color = overrideColor;
            glm::vec2 tiling = overrideTiling;
            glm::vec2 offset = overrideOffset;
            const Texture* tex = nullptr;
            if (sub.materialIndex < model.materials.size()) {
                const SkeletalModelMaterial& mat = model.materials[sub.materialIndex];
                color *= mat.color;
                if (!mat.texturePath.empty())
                    tex = assets.GetTexture(mat.texturePath).get();
                tiling = mat.textureTiling;
                offset = mat.textureOffset;
            }
            drawRange(tex, color, tiling, offset, 0, 0);
            return;
        }

        drawRange(nullptr, overrideColor, overrideTiling, overrideOffset, 0, 0);
        return;
    }

    auto drawPartRanges = [&](const Texture* tex, const glm::vec4& color, const glm::vec2& tiling,
                            const glm::vec2& offset) -> bool {
        if (!model.submeshes.empty()) {
            bool drewAny = false;
            for (const SkeletalModelSubmesh& sub : model.submeshes) {
                if (sub.indexCount == 0 || !SubmeshPassesPartFilter(model, sub, meshPartIndex))
                    continue;
                drawRange(tex, color, tiling, offset, sub.indexOffset, sub.indexCount);
                drewAny = true;
            }
            if (drewAny)
                return true;
        }

        uint32_t indexOffset = 0;
        uint32_t indexCount = 0;
        if (meshPartIndex >= 0 && meshPartIndex < static_cast<int>(model.meshParts.size())) {
            const SkeletalModelMeshPart& part = model.meshParts[static_cast<size_t>(meshPartIndex)];
            indexOffset = part.indexOffset;
            indexCount = part.indexCount;
        }
        drawRange(tex, color, tiling, offset, indexOffset, indexCount);
        return indexCount > 0;
    };

    if (useComponentMaterial) {
        drawPartRanges(overrideTexture, overrideColor, overrideTiling, overrideOffset);
        return;
    }

    const bool hasFbxMaterials = !model.submeshes.empty() && !model.materials.empty();
    if (hasFbxMaterials) {
        AssetManager& assets = AssetManager::Get();
        bool drewAny = false;
        for (const SkeletalModelSubmesh& sub : model.submeshes) {
            if (sub.indexCount == 0 || !SubmeshPassesPartFilter(model, sub, meshPartIndex))
                continue;

            glm::vec4 color = overrideColor;
            glm::vec2 tiling = overrideTiling;
            glm::vec2 offset = overrideOffset;
            const Texture* tex = nullptr;

            if (sub.materialIndex < model.materials.size()) {
                const SkeletalModelMaterial& mat = model.materials[sub.materialIndex];
                color *= mat.color;
                if (!mat.texturePath.empty())
                    tex = assets.GetTexture(mat.texturePath).get();
                tiling = mat.textureTiling;
                offset = mat.textureOffset;
            }

            drawRange(tex, color, tiling, offset, sub.indexOffset, sub.indexCount);
            drewAny = true;
        }
        if (drewAny)
            return;
    }

    drawPartRanges(nullptr, overrideColor, overrideTiling, overrideOffset);
}

void PopulateSkeletalCharacterHierarchy(Scene& scene, Entity& root,
                                        const std::shared_ptr<SkeletalModelAsset>& skeletal,
                                        const std::string& projectRelPath) {
    if (!skeletal)
        return;

    auto& anim = root.AddComponent<AnimatorComponent>();
    anim.modelPath = projectRelPath;
    anim.ReloadAssets();

    Entity* armature = scene.CreateEntity("Armature");
    scene.SetParent(armature, &root);
    if (auto* armTag = armature->GetComponent<TagComponent>()) {
        armTag->tag = "Armature (" + std::to_string(skeletal->bones.size()) + " bones)";
    }

    Entity* meshesRoot = scene.CreateEntity("Meshes");
    scene.SetParent(meshesRoot, &root);

    AssetManager& assets = AssetManager::Get();
    for (size_t pi = 0; pi < skeletal->meshParts.size(); ++pi) {
        const SkeletalModelMeshPart& part = skeletal->meshParts[pi];
        Entity* partEnt = scene.CreateEntity(part.name.empty() ? "MeshPart" : part.name);
        scene.SetParent(partEnt, meshesRoot);

        auto& sk = partEnt->AddComponent<SkinnedMeshRendererComponent>();
        sk.meshPartIndex = static_cast<int>(pi);
        sk.modelPath = projectRelPath;
        sk.SetModelFile(projectRelPath);

        if (part.defaultMaterialIndex < skeletal->materials.size()) {
            const SkeletalModelMaterial& mat = skeletal->materials[part.defaultMaterialIndex];
            sk.color = mat.color;
        }
    }

    ApplySkeletalModelFitScale(root, projectRelPath);
}

float RecommendedUniformScaleForBounds(const glm::vec3& boundsMin, const glm::vec3& boundsMax,
                                       float targetSize) {
    const glm::vec3 extent = boundsMax - boundsMin;
    const float maxExtent = std::max({ extent.x, extent.y, extent.z, 1e-6f });
    return targetSize / maxExtent;
}

SkeletalModelAsset::~SkeletalModelAsset() {
    if (scene)
        ufbx_free_scene(scene);
    scene = nullptr;
}

int SkeletalModelAsset::FindClipIndexById(const std::string& clipId) const {
    if (clipId.empty())
        return -1;
    for (size_t i = 0; i < animationNames.size(); ++i) {
        if (animationNames[i] == clipId)
            return static_cast<int>(i);
    }
    return -1;
}

int SkeletalModelAsset::ClipStackIndex(const std::string& clipId) const {
    const int clipIdx = FindClipIndexById(clipId);
    if (clipIdx < 0)
        return -1;
    if (static_cast<size_t>(clipIdx) < animationStackIndices.size())
        return static_cast<int>(animationStackIndices[static_cast<size_t>(clipIdx)]);
    return clipIdx;
}

const ufbx_anim_stack* SkeletalModelAsset::FindAnimStackByIndex(int stackIndex) const {
    if (!scene || stackIndex < 0 ||
        static_cast<size_t>(stackIndex) >= scene->anim_stacks.count)
        return nullptr;
    return scene->anim_stacks.data[stackIndex];
}

int SkeletalModelAsset::ResolveClipStackIndex(const std::string& clipId,
                                              int clipStackIndexHint) const {
    if (clipStackIndexHint >= 0 && FindAnimStackByIndex(clipStackIndexHint))
        return clipStackIndexHint;
    return ClipStackIndex(clipId);
}

const ufbx_anim_stack* SkeletalModelAsset::FindAnimStack(const std::string& name) const {
    if (!scene || name.empty())
        return nullptr;

    for (size_t i = 0; i < animationNames.size(); ++i) {
        if (animationNames[i] != name)
            continue;
        const size_t stackIdx =
            i < animationStackIndices.size() ? animationStackIndices[i] : i;
        if (stackIdx < scene->anim_stacks.count)
            return scene->anim_stacks.data[stackIdx];
        return nullptr;
    }

    for (size_t i = 0; i < scene->anim_stacks.count; ++i) {
        const ufbx_anim_stack* stack = scene->anim_stacks.data[i];
        if (!stack || !stack->name.data)
            continue;
        if (name == stack->name.data)
            return stack;
    }
    return nullptr;
}

double SkeletalModelAsset::GetClipDuration(const std::string& animStackName) const {
    const ufbx_anim_stack* stack = FindAnimStack(animStackName);
    if (!stack)
        return 1.0;
    return std::max(0.001, stack->time_end - stack->time_begin);
}

double SkeletalModelAsset::GetClipDurationByStackIndex(int stackIndex) const {
    const ufbx_anim_stack* stack = FindAnimStackByIndex(stackIndex);
    if (!stack)
        return 1.0;
    return std::max(0.001, stack->time_end - stack->time_begin);
}

void SkeletalModelAsset::EvaluateBoneMatricesByStackIndex(int stackIndex, double timeSeconds,
                                                          glm::mat4 outMatrices[kMaxBones]) const {
    for (int i = 0; i < kMaxBones; ++i)
        outMatrices[i] = glm::mat4(1.0f);

    if (!scene || bones.empty())
        return;

    const ufbx_anim_stack* stack = FindAnimStackByIndex(stackIndex);
    if (!stack) {
        EvaluateBoneMatrices({}, 0.0, outMatrices);
        return;
    }

    const ufbx_anim* anim = ResolveAnimDescriptor(scene, stack);
    const double evalTime = stack->time_begin + timeSeconds;

    if (!anim) {
        static std::unordered_set<std::string> warnedPaths;
        if (warnedPaths.insert(sourcePath).second) {
            MIPSYNC_WARN(
                "FBX anim stack #{} has no animation data ({}); using bind pose. "
                "Re-export from Mixamo/Blender or check the file in Unity.",
                stackIndex, sourcePath);
        }
        const size_t count = std::min(bones.size(), static_cast<size_t>(kMaxBones));
        for (size_t i = 0; i < count; ++i)
            outMatrices[i] = bones[i].bindPoseMatrix;
        return;
    }

    const size_t count = std::min(bones.size(), static_cast<size_t>(kMaxBones));
    for (size_t i = 0; i < count; ++i) {
        const SkeletalBone& bone = bones[i];
        if (!bone.node)
            continue;
        outMatrices[i] = NodeToWorldAtAnimTime(anim, bone.node, evalTime) * bone.bindGeometryToBone;
    }
}

void SkeletalModelAsset::EvaluateBoneMatrices(const std::string& animStackName, double timeSeconds,
                                              glm::mat4 outMatrices[kMaxBones]) const {
    for (int i = 0; i < kMaxBones; ++i)
        outMatrices[i] = glm::mat4(1.0f);

    if (!scene || bones.empty())
        return;

    const bool useBindPose = animStackName.empty() && timeSeconds <= 0.0;
    if (useBindPose) {
        const size_t count = std::min(bones.size(), static_cast<size_t>(kMaxBones));
        for (size_t i = 0; i < count; ++i)
            outMatrices[i] = bones[i].bindPoseMatrix;
        return;
    }

    const int stackIdx = ClipStackIndex(animStackName);
    if (stackIdx >= 0) {
        EvaluateBoneMatricesByStackIndex(stackIdx, timeSeconds, outMatrices);
        return;
    }

    const ufbx_anim_stack* stack = FindAnimStack(animStackName);
    if (!stack && scene->anim_stacks.count > 0)
        stack = scene->anim_stacks.data[0];
    if (!stack) {
        const size_t count = std::min(bones.size(), static_cast<size_t>(kMaxBones));
        for (size_t i = 0; i < count; ++i)
            outMatrices[i] = bones[i].bindPoseMatrix;
        return;
    }

    int stackIndex = -1;
    for (size_t i = 0; i < scene->anim_stacks.count; ++i) {
        if (scene->anim_stacks.data[i] == stack) {
            stackIndex = static_cast<int>(i);
            break;
        }
    }
    if (stackIndex < 0) {
        const size_t count = std::min(bones.size(), static_cast<size_t>(kMaxBones));
        for (size_t i = 0; i < count; ++i)
            outMatrices[i] = bones[i].bindPoseMatrix;
        return;
    }
    EvaluateBoneMatricesByStackIndex(stackIndex, timeSeconds, outMatrices);
}

namespace {

std::string NormalizedRetargetBoneName(std::string name) {
    const size_t separator = name.find_last_of(":|/");
    if (separator != std::string::npos)
        name = name.substr(separator + 1);
    std::string normalized;
    normalized.reserve(name.size());
    for (unsigned char c : name) {
        if (std::isalnum(c))
            normalized.push_back(static_cast<char>(std::tolower(c)));
    }
    return normalized;
}

} // namespace

void RetargetBoneMatrices(const SkeletalModelAsset& targetModel,
                          const SkeletalModelAsset& clipModel,
                          const glm::mat4 clipMatrices[kMaxBones],
                          glm::mat4 outMatrices[kMaxBones]) {
    for (int i = 0; i < kMaxBones; ++i)
        outMatrices[i] = glm::mat4(1.0f);

    std::unordered_map<std::string, int> clipBoneByName;
    const size_t clipCount = std::min(clipModel.bones.size(), static_cast<size_t>(kMaxBones));
    clipBoneByName.reserve(clipCount);
    for (size_t i = 0; i < clipCount; ++i)
        clipBoneByName[NormalizedRetargetBoneName(clipModel.bones[i].name)] = static_cast<int>(i);

    const size_t targetCount = std::min(targetModel.bones.size(), static_cast<size_t>(kMaxBones));
    for (size_t i = 0; i < targetCount; ++i) {
        const SkeletalBone& targetBone = targetModel.bones[i];
        const auto found = clipBoneByName.find(NormalizedRetargetBoneName(targetBone.name));
        if (found == clipBoneByName.end()) {
            outMatrices[i] = targetBone.bindPoseMatrix;
            continue;
        }
        const int clipIndex = found->second;
        const glm::mat4& clipBind = clipModel.bones[static_cast<size_t>(clipIndex)].bindPoseMatrix;
        const float determinant = glm::determinant(clipBind);
        if (!std::isfinite(determinant) || std::abs(determinant) <= 1e-8f) {
            outMatrices[i] = clipMatrices[clipIndex];
            continue;
        }
        const glm::mat4 animationDelta = clipMatrices[clipIndex] * glm::inverse(clipBind);
        outMatrices[i] = animationDelta * targetBone.bindPoseMatrix;
    }
}

void EvaluateRetargetedBoneMatrices(const SkeletalModelAsset& targetModel,
                                    const SkeletalModelAsset& clipModel,
                                    int clipStackIndex, double timeSeconds,
                                    glm::mat4 outMatrices[kMaxBones]) {
    if (&targetModel == &clipModel) {
        clipModel.EvaluateBoneMatricesByStackIndex(clipStackIndex, timeSeconds, outMatrices);
        return;
    }
    glm::mat4 clipMatrices[kMaxBones];
    clipModel.EvaluateBoneMatricesByStackIndex(clipStackIndex, timeSeconds, clipMatrices);
    RetargetBoneMatrices(targetModel, clipModel, clipMatrices, outMatrices);
}

static bool IsLikelyRestPoseTakeName(const std::string& name) {
    std::string n = name;
    std::transform(n.begin(), n.end(), n.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (n == "take 001" || n == "take001" || n == "take 1" || n == "take 01")
        return true;
    if (n.find("rest pose") != std::string::npos || n.find("bind pose") != std::string::npos)
        return true;
    if (n.find("t-pose") != std::string::npos || n.find("tpose") != std::string::npos)
        return true;
    return false;
}

static bool BoneMatricesEqual(const glm::mat4 a[kMaxBones], const glm::mat4 b[kMaxBones],
                              size_t boneCount) {
    const size_t count = std::min(boneCount, static_cast<size_t>(kMaxBones));
    for (size_t bi = 0; bi < count; ++bi) {
        for (int c = 0; c < 4; ++c) {
            if (glm::length(glm::vec4(a[bi][c] - b[bi][c])) > 1e-4f)
                return false;
        }
    }
    return true;
}

static bool ClipChangesBonePoses(const SkeletalModelAsset& asset, int stackIndex) {
    if (asset.bones.empty() || stackIndex < 0)
        return true;

    const ufbx_anim_stack* stack = asset.FindAnimStackByIndex(stackIndex);
    if (!stack)
        return false;

    const double duration = std::max(0.0, stack->time_end - stack->time_begin);
    if (duration <= 1e-6)
        return false;

    glm::mat4 poseA[kMaxBones];
    glm::mat4 poseB[kMaxBones];
    asset.EvaluateBoneMatricesByStackIndex(stackIndex, 0.0, poseA);
    const double sampleTime = std::min(0.5, duration * 0.5);
    asset.EvaluateBoneMatricesByStackIndex(stackIndex, sampleTime, poseB);
    return !BoneMatricesEqual(poseA, poseB, asset.bones.size());
}

static void PopulateAnimationClips(SkeletalModelAsset& asset, ufbx_scene* scene,
                                   const std::string& path) {
    asset.animationNames.clear();
    asset.animationStackIndices.clear();

    struct StackEntry {
        size_t stackIndex = 0;
        std::string baseName;
    };
    std::vector<StackEntry> entries;
    entries.reserve(scene->anim_stacks.count);

    for (size_t i = 0; i < scene->anim_stacks.count; ++i) {
        const ufbx_anim_stack* stack = scene->anim_stacks.data[i];
        if (!stack)
            continue;

        StackEntry entry{};
        entry.stackIndex = i;
        if (stack->name.data && stack->name.length > 0)
            entry.baseName.assign(stack->name.data, stack->name.length);
        else
            entry.baseName = "Clip " + std::to_string(i);
        entries.push_back(std::move(entry));
    }

    bool hasNonRestTake = false;
    for (const StackEntry& entry : entries) {
        if (!IsLikelyRestPoseTakeName(entry.baseName))
            hasNonRestTake = true;
    }

    std::vector<size_t> keepStacks;
    keepStacks.reserve(entries.size());
    for (const StackEntry& entry : entries) {
        if (hasNonRestTake && IsLikelyRestPoseTakeName(entry.baseName)) {
            MIPSYNC_INFO("Skipping rest-pose take '{}' on {}", entry.baseName, path);
            continue;
        }
        keepStacks.push_back(entry.stackIndex);
    }

    if (!asset.bones.empty() && keepStacks.size() > 1) {
        std::vector<bool> animated;
        animated.reserve(keepStacks.size());
        for (size_t stackIndex : keepStacks)
            animated.push_back(ClipChangesBonePoses(asset, static_cast<int>(stackIndex)));

        const size_t animatedCount =
            static_cast<size_t>(std::count(animated.begin(), animated.end(), true));
        if (animatedCount >= 1 && animatedCount < keepStacks.size()) {
            std::vector<size_t> animatedStacks;
            animatedStacks.reserve(animatedCount);
            for (size_t i = 0; i < keepStacks.size(); ++i) {
                if (!animated[i]) {
                    const size_t stackIndex = keepStacks[i];
                    const ufbx_anim_stack* stack = scene->anim_stacks.data[stackIndex];
                    std::string name = stack && stack->name.data
                        ? std::string(stack->name.data, stack->name.length)
                        : "clip";
                    MIPSYNC_INFO("Skipping static take '{}' on {} (bind/rest pose)", name, path);
                } else {
                    animatedStacks.push_back(keepStacks[i]);
                }
            }
            keepStacks = std::move(animatedStacks);
        }
    }

    std::unordered_map<std::string, int> stackNameCount;
    for (size_t stackIndex : keepStacks) {
        const ufbx_anim_stack* stack = scene->anim_stacks.data[stackIndex];
        if (!stack)
            continue;

        std::string base;
        if (stack->name.data && stack->name.length > 0)
            base.assign(stack->name.data, stack->name.length);
        else
            base = "Clip " + std::to_string(stackIndex);

        const int duplicateIndex = stackNameCount[base]++;
        std::string uniqueName = base;
        if (duplicateIndex > 0)
            uniqueName = base + " (" + std::to_string(duplicateIndex + 1) + ")";

        asset.animationNames.push_back(uniqueName);
        asset.animationStackIndices.push_back(static_cast<uint32_t>(stackIndex));

        if (!stack->anim && !scene->anim) {
            MIPSYNC_WARN("FBX clip '{}' (stack #{}) has no anim curves in {}", uniqueName,
                         stackIndex, path);
        }
    }

    if (asset.animationNames.empty()) {
        MIPSYNC_WARN("Skeletal FBX has no animation stacks: {}", path);
    } else {
        MIPSYNC_INFO("Loaded skeletal animations from {} ({} clip(s))", path,
                     asset.animationNames.size());
    }
}

std::shared_ptr<SkeletalModelAsset> LoadSkeletalModelFromFile(const std::string& path) {
    auto asset = std::make_shared<SkeletalModelAsset>();
    asset->sourcePath = path;

    ufbx_load_opts opts{};
    opts.generate_missing_normals = true;
    opts.load_external_files = true;
    opts.ignore_missing_external_files = true;
    opts.target_axes.right = UFBX_COORDINATE_AXIS_POSITIVE_X;
    opts.target_axes.up = UFBX_COORDINATE_AXIS_POSITIVE_Y;
    opts.target_axes.front = UFBX_COORDINATE_AXIS_POSITIVE_Z;
    opts.target_unit_meters = 1.0f;

    ufbx_error error{};
    asset->scene = ufbx_load_file(path.c_str(), &opts, &error);
    if (!asset->scene) {
        MIPSYNC_ERROR("Skeletal FBX load failed {}: {}", path, error.description.data);
        return nullptr;
    }

    ufbx_scene* scene = asset->scene;

    std::vector<SkinnedVertex> vertices;
    std::vector<glm::mat4> vertexGeoToWorld;
    std::vector<uint32_t> indices;
    FbxMaterialTable materialTable;
    std::vector<SkeletalModelSubmesh> submeshes;
    int activeMaterialIndex = -1;
    uint32_t submeshStart = 0;
    size_t skinnedMeshCount = 0;
    size_t rigidMeshCount = 0;

    for (size_t mi = 0; mi < scene->meshes.count; ++mi) {
        const ufbx_mesh* mesh = scene->meshes.data[mi];
        if (!mesh || mesh->num_faces == 0)
            continue;

        const ufbx_skin_deformer* skin =
            mesh->skin_deformers.count > 0 ? mesh->skin_deformers.data[0] : nullptr;
        std::vector<int> clusterToBone;
        if (skin) {
            BuildBonesFromSkin(skin, path, asset->bones, clusterToBone);
            ++skinnedMeshCount;
        } else {
            ++rigidMeshCount;
        }

        auto appendMeshPart = [&](const ufbx_node* instanceNode, const std::string& partName) {
            const uint32_t indexBegin = static_cast<uint32_t>(indices.size());
            const size_t submeshBegin = submeshes.size();
            const size_t vertsBefore = vertices.size();

            const glm::mat4 geo =
                instanceNode ? UfbxMatrixToGlm(instanceNode->geometry_to_world) : glm::mat4(1.0f);
            AppendMeshTriangles(mesh, instanceNode, geo, skin, clusterToBone, materialTable, path,
                                submeshes, activeMaterialIndex, submeshStart, vertices,
                                vertexGeoToWorld, indices);

            const size_t added = vertices.size() - vertsBefore;
            const uint32_t indexEnd = static_cast<uint32_t>(indices.size());
            if (indexEnd <= indexBegin)
                return;

            SkeletalModelMeshPart part{};
            part.name = partName;
            part.indexOffset = indexBegin;
            part.indexCount = indexEnd - indexBegin;
            part.skinned = skin != nullptr;
            part.geometryToWorld = geo;
            for (size_t si = submeshBegin; si < submeshes.size(); ++si) {
                if (SubmeshBelongsToPart(submeshes[si], part)) {
                    part.defaultMaterialIndex = submeshes[si].materialIndex;
                    break;
                }
            }
            asset->meshParts.push_back(part);

            if (added > 0) {
                MIPSYNC_INFO("  mesh part '{}' ({} tris, {})", part.name, added / 3,
                             skin ? "skinned" : "rigid");
            }
        };

        const std::string baseName = mesh->name.data ? mesh->name.data : "Mesh";
        if (mesh->instances.count > 1) {
            for (size_t ii = 0; ii < mesh->instances.count; ++ii)
                appendMeshPart(mesh->instances.data[ii], baseName + "_inst" + std::to_string(ii));
        } else if (mesh->instances.count == 1) {
            appendMeshPart(mesh->instances.data[0], baseName);
        } else {
            appendMeshPart(nullptr, baseName);
        }
    }

    if (vertices.empty()) {
        MIPSYNC_WARN("FBX contained no renderable mesh geometry: {}", path);
        ufbx_free_scene(asset->scene);
        asset->scene = nullptr;
        return nullptr;
    }

    if (activeMaterialIndex >= 0)
        FinalizeSubmeshRange(submeshes, submeshStart, static_cast<uint32_t>(indices.size()),
                             activeMaterialIndex);

    if (asset->bones.empty()) {
        MIPSYNC_WARN("FBX has geometry but no skin bones (rigid-only): {}", path);
    }

    asset->hasSkin = !asset->bones.empty();
    materialTable.EnsureDefault();
    asset->materials = std::move(materialTable.materials);
    asset->submeshes = std::move(submeshes);

    PopulateAnimationClips(*asset, scene, path);

    if (asset->submeshes.empty() && !indices.empty()) {
        SkeletalModelSubmesh whole{};
        whole.indexCount = static_cast<uint32_t>(indices.size());
        asset->submeshes.push_back(whole);
    }

    asset->meshGeometryToWorld = glm::mat4(1.0f);
    if (!vertexGeoToWorld.empty())
        asset->meshGeometryToWorld = vertexGeoToWorld[0];

    asset->sourceVertices = std::move(vertices);
    asset->vertexGeometryToWorld = std::move(vertexGeoToWorld);
    BakeVertexNormalTransforms(*asset);
    asset->sourceIndices = std::move(indices);
    asset->cpuSkinBoneHash = 0;
    asset->mesh =
        std::make_shared<SkinnedMesh>(asset->sourceVertices, asset->sourceIndices);

    glm::mat4 bindBones[kMaxBones];
    asset->EvaluateBoneMatrices({}, 0.0, bindBones);
    ComputeDisplayFit(*asset, bindBones);

    MIPSYNC_INFO(
        "Loaded skeletal model: {} ({} skinned + {} rigid meshes, {} materials, {} bones, {} "
        "verts, {} clips, bake scale {:.4f})",
        path, skinnedMeshCount, rigidMeshCount, asset->materials.size(), asset->bones.size(),
        asset->sourceVertices.size(), asset->animationNames.size(), asset->vertexBakeScale);
    return asset;
}

void SkeletalModelAsset::UpdateCpuDisplayMesh(const glm::mat4 bones[kMaxBones]) const {
    if (sourceVertices.empty() || sourceIndices.empty())
        return;

    const uint64_t boneHash = HashBoneMatrices(bones, this->bones.size());
    if (cpuDisplayMesh && boneHash == cpuSkinBoneHash)
        return;

    const int maxBoneIndex = static_cast<int>(this->bones.size()) - 1;

    auto* self = const_cast<SkeletalModelAsset*>(this);
    std::vector<Vertex>& verts = self->cpuDisplayScratch;
    if (verts.size() != sourceVertices.size())
        verts.resize(sourceVertices.size());

    for (size_t vi = 0; vi < sourceVertices.size(); ++vi) {
        const SkinnedVertex& sv = sourceVertices[vi];
        const glm::mat4 geo =
            vi < vertexGeometryToWorld.size() ? vertexGeometryToWorld[vi] : meshGeometryToWorld;
        const glm::mat3 normalGeo =
            vi < vertexNormalGeo.size()
            ? vertexNormalGeo[vi]
            : glm::transpose(glm::inverse(glm::mat3(geo)));

        const float weightSum =
            sv.boneWeights.x + sv.boneWeights.y + sv.boneWeights.z + sv.boneWeights.w;
        const glm::vec3 geoPos = weightSum > 1e-6f && maxBoneIndex >= 0
            ? glm::vec3(SkinPositionInGeometry(sv, bones, maxBoneIndex))
            : sv.position;
        const glm::vec3 geoNrm = weightSum > 1e-6f && maxBoneIndex >= 0
            ? SkinNormalInGeometry(sv, bones, maxBoneIndex)
            : sv.normal;

        const glm::vec3 worldPos = glm::vec3(geo * glm::vec4(geoPos, 1.0f));
        const glm::vec3 worldNrm = glm::normalize(normalGeo * geoNrm);
        const glm::vec3 displayPos = (worldPos - displayCenter) * displayScale;
        verts[vi] = Vertex{ displayPos, worldNrm, sv.uv, sv.color };
    }

    if (!cpuDisplayMesh)
        self->cpuDisplayMesh = std::make_shared<Mesh>(verts, sourceIndices, true);
    else
        cpuDisplayMesh->UpdateVertexData(verts, false);
    self->cpuSkinBoneHash = boneHash;
}

void ApplySkeletalModelFitScale(Entity& entity, const std::string& modelProjectPath) {
    if (modelProjectPath.empty())
        return;
    const auto skel = AssetManager::Get().GetSkeletalModel(modelProjectPath);
    if (!skel)
        return;
    auto* tr = entity.GetComponent<TransformComponent>();
    if (!tr)
        return;
    // Fit scale is baked into skeletal vertices at load; keep transform scale at 1.
    tr->scale = glm::vec3(1.0f);
}

bool SkeletalModelFileHasSkin(const std::string& path) {
    const std::string ext = path.size() >= 4 ? path.substr(path.size() - 4) : std::string{};
    std::string lower;
    lower.resize(ext.size());
    std::transform(ext.begin(), ext.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower != ".fbx")
        return false;

    ufbx_load_opts opts{};
    opts.load_external_files = false;
    ufbx_error error{};
    ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &error);
    if (!scene)
        return false;

    bool hasSkin = false;
    for (size_t mi = 0; mi < scene->meshes.count; ++mi) {
        const ufbx_mesh* mesh = scene->meshes.data[mi];
        if (mesh && mesh->skin_deformers.count > 0) {
            hasSkin = true;
            break;
        }
    }
    ufbx_free_scene(scene);
    return hasSkin;
}

} // namespace MipsyncEngine
