#pragma once

#include "AnimationTypes.h"
#include "SkinnedMesh.h"
#include "../renderer/Mesh.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

struct ufbx_scene;
struct ufbx_node;
struct ufbx_anim_stack;

namespace MipsyncEngine {

class Entity;
class Renderer;
class Texture;

struct SkeletalBone {
    std::string name;
    ufbx_node* node = nullptr;
    uint32_t nodeTypedId = 0;
    glm::mat4 bindGeometryToBone{ 1.0f };
    /// Precomputed bind-pose skin matrix (node_to_world * geometry_to_bone).
    glm::mat4 bindPoseMatrix{ 1.0f };
};

/// Diffuse slot imported from FBX (one per ufbx material).
struct SkeletalModelMaterial {
    std::string name;
    glm::vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
    std::string texturePath;
    glm::vec2 textureTiling{ 1.0f, 1.0f };
    glm::vec2 textureOffset{ 0.0f, 0.0f };
};

/// Contiguous index range using a material from materials[].
struct SkeletalModelSubmesh {
    uint32_t indexOffset = 0;
    uint32_t indexCount = 0;
    uint32_t materialIndex = 0;
};

/// One renderable FBX mesh (e.g. body, hair, outfit).
struct SkeletalModelMeshPart {
    std::string name;
    uint32_t indexOffset = 0;
    uint32_t indexCount = 0;
    uint32_t defaultMaterialIndex = 0;
    glm::mat4 geometryToWorld{ 1.0f };
    bool skinned = true;
};

struct SkeletalModelAsset {
    std::string sourcePath;
    ufbx_scene* scene = nullptr;
    std::shared_ptr<SkinnedMesh> mesh;
    std::vector<SkinnedVertex> sourceVertices;
    /// Per-vertex instance transform (parallel to sourceVertices).
    std::vector<glm::mat4> vertexGeometryToWorld;
    /// Per-vertex normal transform (transpose(inverse(geo)), baked at load).
    std::vector<glm::mat3> vertexNormalGeo;
    std::vector<uint32_t> sourceIndices;
    std::shared_ptr<Mesh> cpuDisplayMesh;
    std::vector<SkeletalBone> bones;
    std::vector<SkeletalModelMaterial> materials;
    std::vector<SkeletalModelSubmesh> submeshes;
    std::vector<SkeletalModelMeshPart> meshParts;
    /// Unique clip ids for UI / Animator (disambiguated when FBX stacks share a name).
    std::vector<std::string> animationNames;
    /// Parallel ufbx anim_stacks indices for animationNames.
    std::vector<uint32_t> animationStackIndices;
    glm::vec3 boundsMin{ 0.0f };
    glm::vec3 boundsMax{ 0.0f };
    glm::vec3 meshCenter{ 0.0f };
    glm::mat4 meshGeometryToWorld{ 1.0f };
    /// CenterAndFit applied after skinning + geometry_to_world (matches static FBX preview).
    glm::vec3 displayCenter{ 0.0f };
    float displayScale = 1.0f;
    /// Recommended uniform Transform scale (Mixamo FBX is often ~100–400 units tall).
    float vertexBakeScale = 1.0f;
    float recommendedUniformScale = 1.0f;
    bool hasSkin = false;

    /// Reused each frame by UpdateCpuDisplayMesh to avoid allocations.
    mutable std::vector<Vertex> cpuDisplayScratch;
    /// Skip reskin when bone matrices are unchanged since the last upload.
    mutable uint64_t cpuSkinBoneHash = 0;

    ~SkeletalModelAsset();
    SkeletalModelAsset() = default;
    SkeletalModelAsset(const SkeletalModelAsset&) = delete;
    SkeletalModelAsset& operator=(const SkeletalModelAsset&) = delete;

    const ufbx_anim_stack* FindAnimStack(const std::string& name) const;
    const ufbx_anim_stack* FindAnimStackByIndex(int stackIndex) const;
    /// Index in animationNames, or -1.
    int FindClipIndexById(const std::string& clipId) const;
    /// ufbx anim_stacks index for a clip id, or -1.
    int ClipStackIndex(const std::string& clipId) const;
    void EvaluateBoneMatrices(const std::string& animStackName, double timeSeconds,
                              glm::mat4 outMatrices[kMaxBones]) const;
    void EvaluateBoneMatricesByStackIndex(int stackIndex, double timeSeconds,
                                            glm::mat4 outMatrices[kMaxBones]) const;
    double GetClipDuration(const std::string& animStackName) const;
    double GetClipDurationByStackIndex(int stackIndex) const;
    /// Preferred playback stack for a controller state.
    int ResolveClipStackIndex(const std::string& clipId, int clipStackIndexHint) const;
    void UpdateCpuDisplayMesh(const glm::mat4 bones[kMaxBones]) const;
};

/// GPU-skinned draw (SkinnedMesh VBO + bone UBO). Transform order matches CPU skinning:
/// bone * pos -> part geometry -> display fit -> entity world.
void DrawSkeletalModelGpu(Renderer& renderer, const SkeletalModelAsset& model, const SkinnedMesh& mesh,
                          const glm::mat4& entityWorld, const glm::mat4& partGeometryToWorld,
                          const glm::mat4 bones[kMaxBones], int boneCount, bool useComponentMaterial,
                          const Texture* overrideTexture, const glm::vec4& overrideColor,
                          const glm::vec2& overrideTiling, const glm::vec2& overrideOffset,
                          int meshPartIndex = -1, bool useSourceIndexRanges = true);

/// Reorder an evaluated pose from a clip FBX onto the mesh FBX by normalized bone name.
/// Also transfers the animation delta between differing bind poses.
void RetargetBoneMatrices(const SkeletalModelAsset& targetModel,
                          const SkeletalModelAsset& clipModel,
                          const glm::mat4 clipMatrices[kMaxBones],
                          glm::mat4 outMatrices[kMaxBones]);
void EvaluateRetargetedBoneMatrices(const SkeletalModelAsset& targetModel,
                                    const SkeletalModelAsset& clipModel,
                                    int clipStackIndex, double timeSeconds,
                                    glm::mat4 outMatrices[kMaxBones]);

class Scene;

/// Unity-style hierarchy: root keeps Animator; children are Armature/bones + per-mesh parts.
void PopulateSkeletalCharacterHierarchy(Scene& scene, Entity& root,
                                        const std::shared_ptr<SkeletalModelAsset>& skeletal,
                                        const std::string& projectRelPath);

std::shared_ptr<SkeletalModelAsset> LoadSkeletalModelFromFile(const std::string& path);
bool SkeletalModelFileHasSkin(const std::string& path);
float RecommendedUniformScaleForBounds(const glm::vec3& boundsMin, const glm::vec3& boundsMax,
                                       float targetSize = 2.0f);
void ApplySkeletalModelFitScale(Entity& entity, const std::string& modelProjectPath);

} // namespace MipsyncEngine
