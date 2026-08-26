#pragma once
// ─────────────────────────────────────────────────
// Mipsync Engine — Entity/Component System (Simplified ECS)
// ─────────────────────────────────────────────────

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <cstdint>

#include "../mips/PS1SceneExport.h"
#include <glm/glm.hpp>
#include "../renderer/Camera.h"

namespace MipsyncEngine {

class Entity;

// ─────────────────────────────────────────────────
// Components
// ─────────────────────────────────────────────────

struct Component {
    Entity* entity = nullptr;
    bool enabled = true;
    virtual ~Component() = default;
};

struct TagComponent : public Component {
    std::string tag = "Entity";
    TagComponent(const std::string& t) : tag(t) {}
};

struct TransformComponent : public Component {
    glm::vec3 position = { 0.0f, 0.0f, 0.0f };
    /// Euler degrees: X = pitch, Y = yaw, Z = roll. Composed as yaw → pitch → roll (Ry·Rx·Rz).
    glm::vec3 rotation = { 0.0f, 0.0f, 0.0f };
    glm::vec3 scale    = { 1.0f, 1.0f, 1.0f };

    static glm::mat4 RotationMatrixFromEuler(const glm::vec3& eulerDegrees);
    glm::mat4 GetTransform() const;
};

class Mesh;
class Texture;

struct SkeletalModelAsset;
struct AnimatorControllerAsset;
struct SkinnedMesh;

struct AnimatorRuntimeValues {
    std::unordered_map<std::string, float> floats;
    std::unordered_map<std::string, bool> bools;
    std::unordered_map<std::string, int> ints;
    std::unordered_map<std::string, bool> triggers;
};

struct SkinnedMeshRendererComponent : public Component {
    enum class Ps1ExportMode : uint8_t {
        Off = 0,
        StaticPose,
        RigidBones,
    };

    std::shared_ptr<SkinnedMesh> mesh;
    std::shared_ptr<Texture> texture;
    glm::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
    std::string modelPath;
    std::string texturePath;
    std::string materialPath;
    glm::vec2 textureTiling = { 1.0f, 1.0f };
    glm::vec2 textureOffset = { 0.0f, 0.0f };
    /// Index into SkeletalModelAsset::meshParts (-1 = draw all submeshes).
    int meshPartIndex = -1;
    Ps1ExportMode ps1ExportMode = Ps1ExportMode::RigidBones;
    int ps1VertexAnimFps = 30;
    int ps1VertexAnimMaxFrames = 96;
    int ps1VertexAnimTargetTris = 320;
    int ps1VertexAnimTargetVerts = 1000;
    float ps1VertexAnimMaxSourceMB = 4.0f;
    bool ps1SeamFill = false;

    std::shared_ptr<SkinnedMesh> ps1RigidPreviewMesh;
    uint32_t ps1RigidPreviewVersion = 0;

    void SetModelFile(const std::string& projectRelPath);
};

/// References a bone in a shared skeletal model (hierarchy display / future gizmos).
struct BoneComponent : public Component {
    std::string modelPath;
    int boneIndex = -1;
};

struct AnimatorComponent : public Component {
    std::string modelPath;
    std::string controllerPath;
    std::shared_ptr<SkeletalModelAsset> model;
    std::shared_ptr<AnimatorControllerAsset> controller;

    std::string currentState;
    std::string nextState;
    float stateTime = 0.0f;
    float transitionDuration = 0.0f;
    float transitionTime = 0.0f;
    bool inTransition = false;

    AnimatorRuntimeValues parameters;
    glm::mat4 boneMatrices[128]{};

    /// Global playback speed multiplier (default 1).
    float speed = 1.0f;
    /// Animation sampling rate used by baked/exported runtimes. It does not alter playback speed.
    float animationFps = 30.0f;

    /// Inspector debug: ignore controller and always skin with bind pose.
    bool debugBindPoseOnly = false;

    void ReloadAssets();
    void ResetToDefaultState();
};

struct MeshRendererComponent : public Component {
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Mesh> ps1PreviewMesh;
    uint32_t ps1PreviewMeshVersion = 0;
    std::shared_ptr<Texture> texture;
    std::shared_ptr<Texture> ps1PreviewTexture;
    std::string ps1PreviewTexturePath;
    /// Resolved material color used by the renderer. Not a per-object property.
    glm::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
    std::string meshPrimitive = "Cube";
    float meshSize = 1.0f;
    /// Project-relative path when meshPrimitive == "File" (e.g. assets/models/foo.fbx).
    std::string meshPath;

    /// Optional asset-bound texture / material (project-relative paths).
    std::string texturePath;
    std::string materialPath;
    /// Resolved UV scale/offset from the assigned material.
    glm::vec2 textureTiling = { 1.0f, 1.0f };
    glm::vec2 textureOffset = { 0.0f, 0.0f };
    /// Draw as first-person view model in PS1 export/runtime.
    bool viewModel = false;
    /// Visible only in editor Scene View; skipped by Game View, player, and PS1 export.
    bool editorOnly = false;
    /// Writes depth over a pre-rendered background without drawing color.
    bool prerenderOccluder = false;
    bool ps1SeamFill = false;

    void RebuildMesh();
    void SetPrimitive(const std::string& primitive, float size);
    void SetMeshFile(const std::string& projectRelPath);
};

struct TerrainComponent : public Component {
    enum class BrushMode : uint8_t {
        Raise = 0,
        Lower,
        Smooth,
        Paint,
    };

    float size = 32.0f;
    int subdivisions = 32;
    float heightScale = 2.0f;
    float noiseScale = 0.18f;
    int seed = 1337;
    bool flat = false;

    std::vector<float> heights;
    std::vector<glm::vec4> paintColors;

    bool brushEnabled = false;
    BrushMode brushMode = BrushMode::Raise;
    float brushRadius = 2.0f;
    float brushStrength = 0.35f;
    glm::vec4 brushColor = { 0.42f, 0.30f, 0.16f, 1.0f };

    void EnsureData();
    bool ApplyBrush(const glm::vec3& localPoint);
    void ResetProcedural();
    void ClearPaint();
    void RebuildMesh(MeshRendererComponent& renderer);
};

struct ProModelerVertex {
    glm::vec3 position{ 0.0f };
    glm::vec3 normal{ 0.0f, 1.0f, 0.0f };
    glm::vec2 uv{ 0.0f };
    glm::vec4 color{ 1.0f };
};

struct ProModelerComponent : public Component {
    enum class Shape : uint8_t {
        Box = 0,
        Plane,
        Ramp,
        Stairs,
        Custom,
    };

    Shape shape = Shape::Box;
    glm::vec3 size{ 1.0f, 1.0f, 1.0f };
    int steps = 4;
    float extrudeAmount = 0.25f;
    std::vector<ProModelerVertex> vertices;
    std::vector<uint32_t> indices;
    // Rendering still uses triangles, but editing uses stable polygon face IDs.
    // Two triangles with the same ID are one quad face; a loop cut assigns
    // different IDs so each side remains independently selectable.
    std::vector<uint32_t> triangleFaceIds;
    uint32_t nextFaceId = 1;
    // Per semantic face material overrides. Faces without an entry use the
    // MeshRenderer material.
    std::unordered_map<uint32_t, std::string> faceMaterialPaths;

    void ResetBox();
    void ResetPlane();
    void ResetRamp();
    void ResetStairs();
    void RecalculatePlanarUVs();
    void RecalculateNormals();
    void EnsureFaceTopology();
    void MoveFaceVertices(const glm::vec3& direction, float amount);
    void ExtrudeFaces(const std::vector<size_t>& faceTriangles, std::vector<uint32_t>& outSelectedVertices, std::vector<size_t>& outSelectedFaceTriangles);
    bool ConnectOppositeEdges(const std::pair<uint32_t, uint32_t>& first,
                              const std::pair<uint32_t, uint32_t>& second,
                              std::vector<uint32_t>& outSelectedVertices);
    void FlipFaces(const std::vector<size_t>& faceTriangles);
    void RebuildMesh(MeshRendererComponent& renderer);
};

struct CameraComponent : public Component {
    Camera camera;
    bool primary = true;
    std::string prerenderedBackgroundPath;
    /// Optional trigger entity that selects this pre-rendered shot.
    uint32_t shotTriggerEntityId = 0;
    int shotPriority = 0;
};

struct MipsScriptComponent : public Component {
    std::string scriptPath;
    std::shared_ptr<Mips::CompiledModule> module;
    /// Inspector values per compiled field index (float/int).
    std::vector<double> fieldValues;
    /// Project-relative asset paths for AudioClip fields, aligned to module fields.
    std::vector<std::string> fieldAssetPaths;
};

enum class ColliderShape : uint8_t {
    Box = 0,
    Sphere,
    Capsule,
    Mesh, ///< MeshRenderer geometry; convex hull or static triangle mesh.
};

struct ColliderComponent : public Component {
    ColliderShape shape = ColliderShape::Box;
    glm::vec3 center{ 0.0f, 0.0f, 0.0f };
    /// Box half-extents (local space, before transform scale).
    glm::vec3 halfExtents{ 0.5f, 0.5f, 0.5f };
    float radius = 0.5f;
    /// Capsule: height of cylindrical section (excluding hemisphere caps).
    float capsuleHeight = 1.0f;
    /// Mesh only. When false, preserve the source triangles as a concave static collider.
    bool convex = true;
    bool isTrigger = false;
    bool cameraShotTrigger = false;
    uint32_t cameraTargetEntityId = 0;
};

enum class RigidbodyType : uint8_t {
    Static = 0,
    Kinematic,
    Dynamic,
};

struct RigidbodyComponent : public Component {
    RigidbodyType bodyType = RigidbodyType::Dynamic;
    float mass = 1.0f;
    bool useGravity = true;
    float linearDrag = 0.05f;
    float bounciness = 0.2f;
    bool freezeRotation = false;
    /// When true, uses Jolt CharacterVirtual instead of a kinematic rigid body.
    bool characterController = false;
};

enum class LightType : uint8_t {
    Directional = 0,
    Point,
    Spot,
};

struct LightComponent : public Component {
    LightType type = LightType::Point;
    glm::vec3 color = { 1.0f, 1.0f, 1.0f };
    float intensity = 1.0f;
    /// Attenuation range for point/spot lights (world units).
    float range = 10.0f;
    /// Spot outer cone angle in degrees (half-angle from forward axis).
    float spotAngle = 45.0f;
    /// Spot inner cone angle in degrees (fully bright inside this cone).
    float spotInnerAngle = 30.0f;
};

/// Unity-style global post process volume. The highest-priority enabled volume
/// drives the renderer's fog / color grading / vignette settings for the scene.
struct PostProcessVolumeComponent : public Component {
    bool isGlobal = true;
    int priority = 0;

    bool fogEnabled = true;
    glm::vec3 fogColor{ 0.05f, 0.05f, 0.08f };
    float fogStart = 10.0f;
    float fogEnd = 40.0f;

    bool colorGradingEnabled = false;
    float exposure = 0.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    glm::vec3 colorFilter{ 1.0f, 1.0f, 1.0f };

    bool vignetteEnabled = false;
    glm::vec3 vignetteColor{ 0.0f, 0.0f, 0.0f };
    float vignetteIntensity = 0.35f;
    float vignetteSmoothness = 0.45f;

    bool skyboxEnabled = false;
    std::string skyboxTexturePath;
    std::shared_ptr<Texture> skyboxTexture;
    float skyboxRotationDegrees = 0.0f;
    float skyboxExposure = 0.0f;
    glm::vec3 skyboxTint{ 1.0f, 1.0f, 1.0f };
};

/// Unity-style audio emitter. Clip paths are project-relative and currently
/// support WAV/MP3 playback in the Windows editor/player.
struct AudioSourceComponent : public Component {
    std::string clipPath;
    bool playOnAwake = true;
    bool loop = false;
    bool mute = false;
    float volume = 1.0f;
};

// ─────────────────────────────────────────────────
// UI (Unity-style Canvas)
// ─────────────────────────────────────────────────

enum class UICanvasRenderMode : uint8_t {
    ScreenSpaceOverlay = 0,
    ScreenSpaceCamera,
    WorldSpace,
};

enum class UICanvasScaleMode : uint8_t {
    ConstantPixelSize = 0,
    ScaleWithScreenSize,
};

struct CanvasComponent : public Component {
    UICanvasRenderMode renderMode = UICanvasRenderMode::ScreenSpaceOverlay;
    UICanvasScaleMode scaleMode = UICanvasScaleMode::ConstantPixelSize;
    int sortOrder = 0;
    /// Entity ID of the camera used when renderMode == ScreenSpaceCamera (0 = primary).
    uint32_t eventCameraEntityId = 0;
    glm::vec2 referenceResolution{ 1920.0f, 1080.0f };
    float matchWidthOrHeight = 0.5f;
    float planeDistance = 1.0f;
};

struct RectTransformComponent : public Component {
    glm::vec2 anchorMin{ 0.5f, 0.5f };
    glm::vec2 anchorMax{ 0.5f, 0.5f };
    glm::vec2 pivot{ 0.5f, 0.5f };
    glm::vec2 anchoredPosition{ 0.0f, 0.0f };
    glm::vec2 sizeDelta{ 100.0f, 100.0f };
};

struct UIImageComponent : public Component {
    glm::vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
    std::string texturePath;
    std::shared_ptr<Texture> texture;
    bool preserveAspect = false;
};

enum class UITextAlignment : uint8_t {
    Left = 0,
    Center,
    Right,
};

struct UITextComponent : public Component {
    std::string text = "New Text";
    glm::vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
    float fontSize = 18.0f;
    UITextAlignment alignment = UITextAlignment::Center;
};

enum class UIConfirmKey : uint8_t {
    Enter = 0,
    Space,
    Z,
    X,
    E,
    F,
};

enum class UIConfirmGamepadButton : uint8_t {
    South = 0,  // Cross / A
    East,       // Circle / B
    West,       // Square / X
    North,      // Triangle / Y
    Start,
};

/// Navigation scope for child UIButtonComponents. It owns the selected index,
/// cursor sprite, and keyboard/gamepad confirm bindings.
struct UIButtonGroupComponent : public Component {
    int selectedIndex = 0;
    bool wrapNavigation = true;
    bool keyboardNavigation = true;
    bool gamepadNavigation = true;
    bool keyboardConfirm = true;
    bool gamepadConfirm = true;
    UIConfirmKey confirmKey = UIConfirmKey::Enter;
    UIConfirmGamepadButton confirmButton = UIConfirmGamepadButton::South;
    std::string cursorTexturePath;
    std::shared_ptr<Texture> cursorTexture;
    glm::vec2 cursorOffset{ -28.0f, 0.0f };
    glm::vec2 cursorSize{ 24.0f, 24.0f };
    bool pressedThisFrame = false;
};

/// Selectable UI button. Buttons are intended to live under a Button Group;
/// editor creation helpers automatically create / reparent into one.
struct UIButtonClickEvent {
    bool enabled = true;
    uint32_t targetEntityId = 0;
    std::string scriptPath;
    std::string methodName;
};

struct UIButtonComponent : public Component {
    bool interactable = true;
    std::string label = "Button";
    std::string backgroundTexturePath;
    std::shared_ptr<Texture> backgroundTexture;
    bool preserveAspect = false;
    glm::vec4 normalColor{ 0.18f, 0.20f, 0.23f, 0.94f };
    glm::vec4 selectedColor{ 0.18f, 0.45f, 0.90f, 0.96f };
    glm::vec4 pressedColor{ 0.10f, 0.70f, 0.45f, 1.0f };
    glm::vec4 textColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    float fontSize = 24.0f;
    std::vector<UIButtonClickEvent> onClick;
};

/// Runtime audio visualizer. A sourceEntityId of 0 follows the first playing
/// AudioSource, which is also the single streamed voice used by the PS1 build.
struct UIAudioSpectrumComponent : public Component {
    uint32_t sourceEntityId = 0;
    glm::vec4 color{ 0.1f, 0.9f, 0.65f, 1.0f };
    glm::vec4 backgroundColor{ 0.02f, 0.03f, 0.04f, 0.65f };
    int barCount = 16;
    float barGap = 4.0f;
    float sensitivity = 1.4f;
    float smoothing = 0.72f;
    std::vector<float> displayBands;
};

// ─────────────────────────────────────────────────
// Entity
// ─────────────────────────────────────────────────

class Entity {
public:
    Entity(uint32_t id, const std::string& name = "Entity") : m_ID(id) {
        AddComponent<TagComponent>(name);
        AddComponent<TransformComponent>();
    }

    uint32_t GetID() const { return m_ID; }
    bool IsActive() const { return m_Active; }
    void SetActive(bool active) { m_Active = active; }
    bool IsStatic() const { return m_Static; }
    void SetStatic(bool value) { m_Static = value; }
    const std::string& GetEditorTag() const { return m_EditorTag; }
    void SetEditorTag(std::string value) { m_EditorTag = std::move(value); }
    const std::string& GetEditorLayer() const { return m_EditorLayer; }
    void SetEditorLayer(std::string value) { m_EditorLayer = std::move(value); }

    template<typename T, typename... Args>
    T& AddComponent(Args&&... args) {
        auto comp = std::make_unique<T>(std::forward<Args>(args)...);
        comp->entity = this;
        T* rawPtr = comp.get();
        m_Components.push_back(std::move(comp));
        return *rawPtr;
    }

    template<typename T>
    T* GetComponent() {
        for (auto& comp : m_Components) {
            T* ptr = dynamic_cast<T*>(comp.get());
            if (ptr) return ptr;
        }
        return nullptr;
    }

    template<typename T>
    std::vector<T*> GetComponents() {
        std::vector<T*> result;
        for (auto& comp : m_Components) {
            if (T* ptr = dynamic_cast<T*>(comp.get()))
                result.push_back(ptr);
        }
        return result;
    }

    template<typename T>
    bool HasComponent() {
        return GetComponent<T>() != nullptr;
    }

    template<typename T>
    void RemoveComponent() {
        for (auto it = m_Components.begin(); it != m_Components.end(); ++it) {
            if (dynamic_cast<T*>(it->get())) {
                m_Components.erase(it);
                return;
            }
        }
    }

    template<typename T>
    void RemoveComponent(T* target) {
        if (!target) return;
        for (auto it = m_Components.begin(); it != m_Components.end(); ++it) {
            if (it->get() == target) {
                m_Components.erase(it);
                return;
            }
        }
    }

    /// 0 = no parent (root entity).
    uint32_t GetParentID() const { return m_ParentID; }
    void SetParentID(uint32_t id) { m_ParentID = id; }
    const std::vector<uint32_t>& GetChildIDs() const { return m_ChildIDs; }
    std::vector<uint32_t>& GetChildIDs() { return m_ChildIDs; }

    /// Non-empty when this entity was instantiated from a .nprefab file.
    const std::string& GetPrefabSourcePath() const { return m_PrefabSourcePath; }
    void SetPrefabSourcePath(const std::string& path) { m_PrefabSourcePath = path; }
    bool IsPrefabInstance() const { return !m_PrefabSourcePath.empty(); }

private:
    uint32_t m_ID;
    bool m_Active = true;
    bool m_Static = false;
    std::string m_EditorTag = "Untagged";
    std::string m_EditorLayer = "Default";
    uint32_t m_ParentID = 0;
    std::vector<uint32_t> m_ChildIDs;
    std::string m_PrefabSourcePath;
    std::vector<std::unique_ptr<Component>> m_Components;
};

// ─────────────────────────────────────────────────
// Scene
// ─────────────────────────────────────────────────

class Renderer;
class Framebuffer;
class Texture;

class Scene {
public:
    Scene() = default;
    ~Scene() = default;

    Entity* CreateEntity(const std::string& name = "Entity");
    Entity* CreateEntityWithId(uint32_t id, const std::string& name = "Entity");
    void DestroyEntity(Entity* entity);
    void Clear();
    
    void Update(float deltaTime);
    /// When false, animator clips are not advanced (edit mode); skinning uses bind pose.
    void SetAnimateCharacters(bool animate) { m_AnimateCharacters = animate; }
    bool GetAnimateCharacters() const { return m_AnimateCharacters; }
    void UploadLightsToRenderer(Renderer& renderer) const;

    void Render(Renderer& renderer, const Camera& camera, Framebuffer* targetFBO = nullptr,
                uint32_t highlightEntityId = 0, bool usePs1PreviewMeshes = false,
                bool includeEditorOnlyMeshes = false, const Texture* backgroundTexture = nullptr);
    /// Renders Unity-style UI canvases on top of the 3D pass (same FBO / viewport).
    void RenderUI(class UIRenderer& uiRenderer, const Camera& camera, int viewportWidth, int viewportHeight,
                  Framebuffer* targetFBO, uint32_t activeCameraEntityId = 0,
                  bool sceneView3D = false, int layoutWidth = 0, int layoutHeight = 0);
    Camera* GetPrimaryCamera();
    Entity* GetPrimaryCameraEntity();
    void SetActiveCameraEntityId(uint32_t id) { m_ActiveCameraEntityId = id; }
    uint32_t GetActiveCameraEntityId() const { return m_ActiveCameraEntityId; }

    /// Exactly one enabled camera is marked primary (for Game View).
    void SetPrimaryCamera(Entity& entity);
    void NormalizePrimaryCameras();

    std::vector<std::unique_ptr<Entity>>& GetEntities() { return m_Entities; }
    const std::vector<std::unique_ptr<Entity>>& GetEntities() const { return m_Entities; }

    Entity* FindEntity(uint32_t id);
    const Entity* FindEntity(uint32_t id) const;

    /// Reparents `child` under `parent` (or detaches when `parent == nullptr`).
    /// Refuses cycles. Returns true on success.
    bool SetParent(Entity* child, Entity* parent);
    /// Reparents and inserts before/after `sibling`. sibling == nullptr inserts at root/end.
    bool ReorderEntity(Entity* child, Entity* parent, Entity* sibling, bool insertAfter);

    /// Local→World matrix for `entity`, walking up the parent chain.
    glm::mat4 GetWorldMatrix(const Entity& entity) const;
    /// World matrix of the entity's parent (identity if none).
    glm::mat4 GetParentWorldMatrix(const Entity& entity) const;

    /// Pushes world translation/rotation back to each CameraComponent. Call
    /// once per frame in play mode so cameras parented to FPS rigs render correctly.
    void SyncCamerasToWorldTransforms();

    glm::vec3 GetWorldPosition(const Entity& entity) const;
    void SetWorldPosition(Entity& entity, const glm::vec3& worldPos);

    /// Deep-clones `source` and its child hierarchy (new entity IDs).
    Entity* DuplicateEntity(Entity& source);

private:
    void DetachFromParent(Entity* child);
    bool IsAncestor(uint32_t ancestorId, uint32_t descendantId) const;
    void DestroyEntityRecursive(uint32_t id);

    bool m_AnimateCharacters = false;

    std::vector<std::unique_ptr<Entity>> m_Entities;
    uint32_t m_NextEntityID = 1;
    uint32_t m_ActiveCameraEntityId = 0;
};

} // namespace MipsyncEngine
