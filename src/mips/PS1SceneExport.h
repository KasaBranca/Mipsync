#pragma once

#include "../build/BuildManifest.h"
#include "Bytecode.h"
#include "PS1Export.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

namespace MipsyncEngine {
class Mesh;
struct SkeletalModelAsset;
class SkinnedMesh;
}

namespace MipsyncEngine::Mips {

struct Ps1ExportedScript {
    std::string path;
    std::string className;
    std::unordered_map<std::string, double> fieldOverrides;
    std::unordered_map<std::string, std::string> assetFieldOverrides;
    std::vector<int32_t> fieldValuesQ16;
    std::vector<std::string> assetFieldPaths;
};

struct Ps1TransformAnimKey {
    uint16_t frame = 0;
    float position[3] = { 0, 0, 0 };
    float rotation[3] = { 0, 0, 0 };
    float scale[3] = { 1, 1, 1 };
};

/// Flattened entity record extracted from a `.nscene` file for PS1 runtime.
struct Ps1ExportedEntity {
    uint32_t id = 0;
    uint32_t sourceEntityId = 0;
    std::string name;
    // Source-scene hierarchy. Transforms below remain flattened world-space
    // values for the renderer; the PS1 runtime uses this relationship to
    // propagate runtime parent transform changes to flattened descendants.
    uint32_t parentEntityId = 0;
    int parentEntityIndex = -1;
    float position[3] = { 0, 0, 0 };
    float rotation[3] = { 0, 0, 0 };
    float scale[3]    = { 1, 1, 1 };

    /// 0=none, 1=Cube, 2=Plane, 3=Sphere
    int meshKind = 0;
    float meshSize = 1.0f;
    std::string meshPath; // project-relative (for custom meshes)
    uint16_t meshIndex = 0;
    uint8_t color[3] = { 180, 180, 180 };
    std::string materialPath;
    std::string texturePath; // project-relative, from meshRenderer or .nmat
    float textureTiling[2] = { 1.0f, 1.0f };
    float textureOffset[2] = { 0.0f, 0.0f };
    uint8_t textureIndex = 0; // 0 = none; 1+ into texturePaths
    bool meshEnabled = false;
    bool viewModel = false;
    bool prerenderOccluder = false;
    bool seamFill = false;
    uint16_t vertexAnimFirstMeshIndex = 0;
    uint16_t vertexAnimFrameCount = 0;
    uint8_t vertexAnimFps = 0;
    uint16_t vertexAnimTargetTris = 320;
    uint16_t vertexAnimTargetVerts = 1000;
    uint16_t rigidAnimFirstFrame = 0;
    uint16_t rigidAnimFrameCount = 0;
    uint8_t rigidAnimFps = 0;
    uint16_t rigidIdleFirstFrame = 0;
    uint16_t rigidIdleFrameCount = 0;
    uint8_t rigidIdleFps = 0;
    uint16_t rigidWalkFirstFrame = 0;
    uint16_t rigidWalkFrameCount = 0;
    uint8_t rigidWalkFps = 0;
    uint16_t rigidAimFirstFrame = 0;
    uint16_t rigidAimFrameCount = 0;
    uint8_t rigidAimFps = 0;
    uint32_t rigidRootEntityId = 0;
    int rigidRootEntityIndex = -1;

    // Default Animator state when it references an editable .nanim clip.
    uint16_t transformAnimFirstKey = 0;
    uint16_t transformAnimKeyCount = 0;
    uint16_t transformAnimLengthFrames = 0;
    uint8_t transformAnimFps = 0;
    bool transformAnimLoop = false;

    bool hasCamera = false;
    bool cameraPrimary = false;
    float cameraFov = 60.0f;
    float cameraNear = 0.1f;
    float cameraFar = 100.0f;
    std::string prerenderedBackgroundPath; // project-relative PNG for pre-rendered BG
    uint8_t cameraBackgroundTextureIndex = 0; // 0 = none; 1+ into texturePaths
    uint32_t cameraShotTriggerId = 0;
    int cameraShotTriggerEntityIndex = -1;
    int cameraShotPriority = 0;

    /// Collider data parsed from .nscene
    int colliderShape = -1; // -1=none, 0=box, 1=sphere, 2=capsule, 3=mesh-plane
    float colliderCenter[3] = { 0, 0, 0 };
    float colliderHalfExtents[3] = { 0, 0, 0 };
    float colliderRadius = 0.0f;
    float colliderCapsuleHeight = 0.0f;
    bool colliderIsTrigger = false;
    bool colliderCameraShotTrigger = false;
    uint32_t colliderCameraTargetId = 0;
    int colliderCameraTargetEntityIndex = -1;

    std::string audioClipPath;
    uint8_t audioClipIndex = 0; // 0=none, 1+ into generated audio table
    bool audioEnabled = false;
    bool audioPlayOnAwake = true;
    bool audioLoop = false;
    bool audioMute = false;
    float audioVolume = 1.0f;

    std::vector<Ps1ExportedScript> scripts;
};

struct Ps1ExportedUiElement {
    uint8_t kind = 0; // 1=image, 2=text, 3=audio spectrum
    int16_t x = 0;
    int16_t y = 0;
    int16_t w = 0;
    int16_t h = 0;
    uint8_t color[4] = { 255, 255, 255, 255 };
    std::string texturePath;
    uint8_t textureIndex = 0; // 0 = none; 1+ into texturePaths
    uint8_t preserveAspect = 0;
    uint8_t alignment = 1; // 0=left, 1=center, 2=right
    uint8_t fontSize = 8;
    uint16_t textOffset = 0;
    uint16_t textLength = 0;
    uint8_t spectrumBackground[4] = { 0, 0, 0, 0 };
    uint8_t spectrumBars = 16;
    uint8_t spectrumGap = 1;
    uint16_t spectrumSensitivityQ8 = 256;
    int16_t rotationDegrees = 0;
};

struct Ps1ExportedUiButtonRect {
    int16_t x = 0;
    int16_t y = 0;
    int16_t w = 0;
    int16_t h = 0;
    uint16_t actionOffset = 0;
    uint8_t actionCount = 0;
    uint8_t interactable = 1;
};

struct Ps1ExportedUiButtonAction {
    uint32_t targetSourceEntityId = 0;
    std::string scriptPath;
    std::string methodName;
    uint16_t targetEntityIndex = 0;
    uint8_t moduleIndex = 0;
};

struct Ps1ExportedUiButtonGroup {
    uint8_t selectedIndex = 0;
    uint8_t wrapNavigation = 1;
    uint8_t gamepadNavigation = 1;
    uint8_t gamepadConfirm = 1;
    uint8_t confirmButton = 0;
    uint16_t buttonRectOffset = 0;
    uint8_t buttonCount = 0;
    int16_t cursorOffsetX = -28;
    int16_t cursorOffsetY = 0;
    int16_t cursorW = 24;
    int16_t cursorH = 24;
    std::string cursorTexturePath;
    uint8_t cursorTextureIndex = 0;
};

struct Ps1ExportedUiGlyph {
    uint32_t codepoint = 0;
    uint8_t width = 0;
    uint8_t height = 0;
    uint8_t advance = 0;
    uint16_t rowOffset = 0;
};

struct Ps1ExportedAudioClip {
    std::string path;
    bool loop = false;
    uint32_t sampleRate = 0;
    std::vector<uint8_t> data;
};

struct Ps1TerrainMeshData {
    std::string key;
    float size = 32.0f;
    int subdivisions = 32;
    float heightScale = 2.0f;
    float noiseScale = 0.18f;
    int seed = 1337;
    bool flat = false;
    std::vector<float> heights;
    std::vector<uint32_t> colors;
};

struct Ps1ProModelerMeshData {
    std::string key;
    std::vector<float> positions; // xyz triplets
    std::vector<float> normals;   // xyz triplets
    std::vector<float> uvs;       // uv pairs
    std::vector<uint32_t> colors;  // RGBA packed
    std::vector<uint32_t> indices;
};

struct Ps1SkinnedAnimMeshData {
    std::string key;
    std::vector<float> positions; // xyz triplets
    std::vector<float> normals;   // xyz triplets
    std::vector<float> uvs;       // uv pairs
    std::vector<uint32_t> colors;  // RGBA packed
    std::vector<uint32_t> indices;
};

struct Ps1RigidBoneMeshData {
    std::string key;
    std::vector<float> positions; // xyz triplets, bone-local bind pose
    std::vector<float> normals;   // xyz triplets
    std::vector<float> uvs;       // uv pairs
    std::vector<uint32_t> colors;  // RGBA packed
    std::vector<uint32_t> indices;
};

struct Ps1RigidAnimFrame {
    float position[3] = { 0.0f, 0.0f, 0.0f };
    int16_t matrix[3][3] = {
        { 4096, 0, 0 },
        { 0, 4096, 0 },
        { 0, 0, 4096 },
    };
};

struct Ps1SceneExportResult {
    std::vector<Ps1ExportedEntity> entities;
    int cameraEntityIndex = -1;
    std::vector<CompiledModule> modules; // unique, scene-referenced scripts only
    std::vector<std::string> moduleClassNames;
    uint32_t bindingCount = 0;

    // Single directional light export (Milestone: minimal lighting).
    bool     hasLight = false;
    int16_t  lightDirXYZ12_4[3] = { 0, -2048, -2048 }; // default-ish
    uint8_t  lightColor[3] = { 255, 255, 255 };
    uint8_t  ambientColor[3] = { 32, 32, 32 };

    // Highest-priority enabled Post Process Volume. Distances stay in editor
    // world units here and are emitted as 16.16 fixed for the PS1 runtime.
    bool     fogEnabled = false;
    uint8_t  fogColor[3] = { 13, 13, 20 };
    float    fogStart = 10.0f;
    float    fogEnd = 40.0f;

    // Custom mesh export.
    std::vector<std::string> meshPaths; // unique project-relative
    std::vector<bool> meshViewModelVariants; // parallel to meshPaths; true = first-person variant
    std::vector<bool> meshOccluderVariants; // parallel to meshPaths; true = pre-render mask variant
    std::vector<Ps1TerrainMeshData> terrainMeshes;
    std::vector<Ps1ProModelerMeshData> proModelerMeshes;
    std::vector<Ps1SkinnedAnimMeshData> skinnedAnimMeshes;
    std::vector<Ps1RigidBoneMeshData> rigidBoneMeshes;
    std::vector<Ps1RigidAnimFrame> rigidAnimFrames;
    std::vector<Ps1TransformAnimKey> transformAnimKeys;

    // Textures referenced by entities (project-relative paths).
    std::vector<std::string> texturePaths;

    // Pre-rendered background image (project-relative path, empty = none).
    std::string backgroundImagePath;
    std::vector<std::string> backgroundImagePaths;
    uint8_t backgroundTextureIndex = 0; // 0 = none; 1+ into texturePaths

    // Screen-space Canvas UI rendered by the PS1 runtime.
    std::vector<Ps1ExportedUiElement> uiElements;
    std::vector<Ps1ExportedUiButtonGroup> uiButtonGroups;
    std::vector<Ps1ExportedUiButtonRect> uiButtonRects;
    std::vector<Ps1ExportedUiButtonAction> uiButtonActions;
    std::string uiTextBlob;
    std::vector<Ps1ExportedUiGlyph> uiGlyphs;
    std::vector<uint16_t> uiGlyphRows;

    std::vector<Ps1ExportedAudioClip> audioClips;
};

/// Parse the startup scene from `bootManifest`, compile referenced `.mips`
/// scripts, and write `generated/scene_data.c` + refresh `scripts_data.c`.
bool ExportPs1SceneAndScripts(const std::string& projectRoot,
                              const BuildManifest& bootManifest,
                              const std::string& generatedDir,
                              Ps1SceneExportResult& outResult,
                              std::string& outError);

/// Builds the same reduced static mesh used by PS1 export, but uploads it for editor preview.
std::shared_ptr<Mesh> BuildPs1PreviewMesh(const Mesh& sourceMesh, bool preserveUvSeams = true);



/// Build a simplified SkinnedMesh for editor preview mirroring the PS1
/// rigid bone pipeline.
std::shared_ptr<MipsyncEngine::SkinnedMesh> BuildPs1RigidBonePreview(
    const MipsyncEngine::SkeletalModelAsset& model, int meshPartIndex, bool seamFill,
    int targetTris, int targetVerts);

} // namespace MipsyncEngine::Mips
