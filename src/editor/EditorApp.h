#pragma once
// ─────────────────────────────────────────────────
// Mipsync Engine — Editor Application
// ─────────────────────────────────────────────────

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "../renderer/Camera.h"
#include "../renderer/Framebuffer.h"
#include "../renderer/Renderer.h"
#include "EditorSceneCamera.h"
#include "EditorUndoStack.h"
#include "EditorCameraGizmo.h"
#include "GameViewSettings.h"
#include "EditorBuildSettings.h"
#include <imgui.h>
#include <ImGuizmo.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace MipsyncEngine {

class Engine;
class Entity;
class AssetBrowserPanel;

enum class DockLayoutPreset {
    ClassicEditor,
    SceneTopGameBottom,
};

/// Scene View only — toggles PS1-style post-vertex effects (Game View uses global PS1Settings).
struct SceneViewPsxToggles {
    bool vertexJitter    = true;
    bool affineMapping   = true;
    bool colorDepthLimit = true;
    bool dithering       = true;
    bool meshPreview     = true;
};

class EditorApp {
public:
    EditorApp(Engine* engine);
    ~EditorApp();

    void Init();
    void Shutdown();

    void BeginFrame();
    void EndFrame();
    void OnImGuiRender();

    bool IsPlaying() const { return m_IsPlaying; }
    bool IsPaused() const { return m_IsPaused; }
    bool ConsumeSingleStepRequest() {
        const bool requested = m_StepOneFrame;
        m_StepOneFrame = false;
        return requested;
    }

    void SetAutoPlayOnStart(bool enabled) { m_AutoPlayOnStart = enabled; }
    void TickAutoPlayOnStart();

    /// Runs deferred PhysicsWorld::BeginPlay (called from Engine::Update, not inside ImGui).
    void TickPendingPlaySetup();

    void HandlePlayModeShortcuts();
    void HandleEditorShortcuts();
    void UpdateUndoRecording();
    void RecordUndoSnapshot();
    void AfterSceneRestoredFromHistory();
    void UpdatePlayInputState();

    bool IsSceneDirty() const { return m_SceneDirty; }
    /// GLFW close callback: return true to cancel window close (show save prompt).
    bool OnWindowCloseRequested();

    Engine* GetEngine() const { return m_Engine; }

    void ClearEntitySelection() {
        m_SelectedEntityID = 0;
        m_SelectedEntityIDs.clear();
        m_HierarchySelectionAnchor = 0;
    }

    /// Opens/focuses the docked Animator Controller window (Unity-style, not Inspector).
    void OpenAnimatorControllerWindow(const std::string& projectRelPath);
    /// Load a scene file (absolute path). Stops play mode unless restartPlayIfWasActive.
    void LoadSceneFromPath(const std::string& absolutePath, bool restartPlayIfWasActive = false);
    const std::string& GetSceneFilePath() const { return m_SceneFilePath; }

    void OpenBuildSettings();
    EditorBuildSettings& GetBuildSettings() { return *m_BuildSettings; }

    const std::string& GetAnimatorControllerWindowPath() const { return m_AnimatorControllerWindowPath; }
    /// FBX model path from a scene Animator using this controller (for clip picker).
    std::string FindModelPathForController(const std::string& controllerRelPath) const;

    AssetBrowserPanel* GetAssetBrowser() { return m_AssetBrowser.get(); }
    void FocusDockedWindow(const char* windowName);
    void OpenAnimationWindow();
    void OpenAnimationClipWindow(const std::string& projectRelPath);

    struct AnimationKeyframe {
        int frame = 0;
        glm::vec3 position{ 0.0f };
        glm::vec3 rotation{ 0.0f };
        glm::vec3 scale{ 1.0f };
    };
    struct AnimationWindowState {
        std::string clipPath; // project-relative .nanim
        std::string clipName = "New Animation";
        std::string clipSourcePath;
        std::string controllerStateName;
        uint32_t targetEntityId = 0;
        uint32_t followedSelectionEntityId = 0xFFFFFFFFu;
        std::string followedControllerPath;
        int frame = 0;
        int fps = 30;
        int lengthFrames = 60;
        int selectedKeyFrame = -1;
        bool clipReadOnly = false;
        bool keyClipboardValid = false;
        AnimationKeyframe keyClipboard{};
        bool record = false;
        bool preview = false;
        bool playing = false;
        double lastTick = 0.0;
        bool recordBaselineValid = false;
        uint32_t recordBaselineEntityId = 0;
        glm::vec3 recordBaselinePosition{ 0.0f };
        glm::vec3 recordBaselineRotation{ 0.0f };
        glm::vec3 recordBaselineScale{ 1.0f };
        std::vector<AnimationKeyframe> keys;
    };

private:
    void SetupDockspace();
    void ApplyDockLayout(DockLayoutPreset preset, ImGuiID dockspaceId);
    void RegisterSettingsHandler();
    bool HasSavedLayout() const;
    void RequestDockLayoutApply();
    void ResetDockLayout();
    void CollectDockTabSelections();
    void ApplySavedTabSelections();
    void ApplyLoadedTabIdsToNodes();
    void EnsureSceneViewTabSelected();
    void EnsureProjectTabSelected();
    void EnsureAnimatorControllerDocked();
    void DrawSceneViewConsolePanels();
    static ImGuiID CalcWindowTabId(const char* windowName);
    std::string GetSceneConsolePreferredTab() const;
    
    // Panels
    void DrawSceneViewPanel();
    void DrawGameViewPanel();
    void DrawPlayerModeUI();
    void StartPlayMode();
    void StopPlayMode();
    Entity* ResolveActiveShotCameraEntity(bool allowShotSelection);
    void ResetShotCameraState();
    void DrawGameViewToolbar();
    void DrawHierarchyPanel();
    void DrawHierarchyEntityNode(class Entity& entity);
    void DrawHierarchyContextMenu(class Entity* parentForNew);
    class Entity* CreatePointLight(class Entity* parentForNew, const glm::vec3& worldPosition);
    class Entity* CreateSpotLight(class Entity* parentForNew, const glm::vec3& worldPosition);
    class Entity* CreatePostProcessVolume(class Entity* parentForNew);
    class Entity* CreateCamera(class Entity* parentForNew);
    class Entity* CreateFirstPersonController(class Entity* parentForNew);
    class Entity* CreateUICanvas(class Entity* parentForNew);
    class Entity* CreateUIImage(class Entity* canvasParent);
    class Entity* CreateUIText(class Entity* canvasParent);
    class Entity* CreateUIButtonGroup(class Entity* canvasParent);
    class Entity* CreateUIButton(class Entity* parentOrCanvas);
    class Entity* CreateUIAudioSpectrum(class Entity* canvasParent);
    void DrawUITextureSlot(class UIImageComponent& image);
    void DrawUIButtonBackgroundSlot(class UIButtonComponent& button);
    void DrawUIButtonGroupCursorSlot(class UIButtonGroupComponent& group);
    void DrawAddComponentScriptMenu(class Entity& entity);
    std::vector<std::string> CollectProjectScripts() const;
    void DrawInspectorPanel();
    void DrawMipsScriptAssetInspector(const std::string& projectRelPath);
    void DrawMipsScriptIdeActions(const std::string& projectRelPath);
    void DrawAnimatorControllerPanel();
    void DrawAnimationPanel();
    void DrawConsolePanel();
    void DrawProjectPanel();
    void DrawProModelerWindow();
    void DrawMainToolbar();
    void DrawMenuBarPlayButton();
    void DrawLayoutMenu();

    /// Drag-drop helpers
    void AcceptSceneViewDrops(const ImVec2& imageMin, const ImVec2& imageSize);
    bool HandleTerrainBrush(const ImVec2& imageMin, const ImVec2& imageSize, bool viewportHovered);
    bool HandleProModelerSubobjectPick(const ImVec2& imageMin, const ImVec2& imageSize, bool viewportHovered);
    bool DrawProModelerSubobjectGizmo(const ImVec2& imageMin, const ImVec2& imageSize, const Camera& camera);
    void DrawProModelerOverlay(const ImVec2& imageMin, const ImVec2& imageSize, const Camera& camera);
    void ClearProModelerSelection();
    void AcceptPrefabDrop();
    void SpawnModelFromProjectAsset(const std::string& projectRelPath,
                                    const ImVec2* viewMin = nullptr,
                                    const ImVec2* viewSize = nullptr);
    void DispatchProjectAssetDrop(const std::string& projectRelPath,
                                  const ImVec2* viewMin = nullptr,
                                  const ImVec2* viewSize = nullptr);
    void DrawMaterialSlot(struct MeshRendererComponent& mr);
    void DrawMaterialSlot(struct SkinnedMeshRendererComponent& mr);
    void DrawMaterialAssetInspector(const std::string& projectRelPath);

    void RenderSceneToFramebuffer(Framebuffer& fbo, const Camera& camera, ImVec2 size,
                                  uint32_t highlightEntityId = 0, uint32_t activeCameraEntityId = 0,
                                  bool sceneView3D = false, int layoutWidth = 0, int layoutHeight = 0);
    void ApplyPostProcessVolumeSettings(struct PS1Settings& settings);
    const Texture* ResolvePrerenderedBackgroundTexture(uint32_t cameraEntityId);
    void HandleSceneViewControls(bool viewportHovered);
    void DrawGizmoToolbar();
    void DrawRenderModeSelector();
    void DrawSceneViewPsxToolbar();
    void ApplySceneViewPsxOverrides(PS1Settings& settings) const;
    void DrawGizmoControls(const ImVec2& imageMin, const ImVec2& imageSize, const Camera& camera);
    void DrawCameraFrustumGizmo(const ImVec2& imageMin, const ImVec2& imageSize, const Camera& camera);
    void DrawCanvasGizmo(const ImVec2& imageMin, const ImVec2& imageSize, const Camera& camera);
    void DrawColliderGizmos(const ImVec2& imageMin, const ImVec2& imageSize, const Camera& camera);
    void DrawLightGizmos(const ImVec2& imageMin, const ImVec2& imageSize, const Camera& camera);
    void FrameEntityInSceneView(class Entity& entity);
    void SyncSelectedCameraFromTransform(Entity* entity);
    void SyncPhysicsAfterTransformEdit(Entity* entity);

    Entity* GetSelectedEntity();
    bool IsEntitySelected(uint32_t entityId) const;
    void SelectSingleEntity(uint32_t entityId);
    void SyncAnimatorWindowToSelectedEntity();
    void HandleHierarchySelection(uint32_t entityId);
    void CollectHierarchyOrder(class Entity& entity, std::vector<uint32_t>& out) const;
    std::vector<uint32_t> GetSelectedHierarchyRootIDs(uint32_t fallbackId) const;
    void QueueHierarchyReparent(const std::vector<uint32_t>& childIds, uint32_t parentId);
    void CreateEmptyParentForSelection();

    Engine* m_Engine;
    Entity* m_PendingHierarchyDelete = nullptr;
    std::vector<uint32_t> m_PendingHierarchyDeleteIDs;
    std::vector<uint32_t> m_PendingReparentChildren;
    uint32_t m_PendingReparentParent = 0; // 0 = unparent
    uint32_t m_PendingReparentSibling = 0; // 0 = append
    bool m_PendingReparentAfterSibling = false;
    EditorSceneCamera m_SceneCamera;
    Framebuffer m_SceneViewFBO;
    Framebuffer m_GameViewFBO;
    std::unordered_map<std::string, std::shared_ptr<Texture>> m_PrerenderedBackgroundTextures;
    Camera m_FallbackGameCamera;
    GameViewSettings m_GameViewSettings;
    
    // State
    bool m_SceneViewHovered = false;
    bool m_SceneViewFocused = false;
    float m_LastSceneViewAspect = 16.0f / 9.0f;
    uint32_t m_SelectedEntityID = 0;
    uint32_t m_PendingSelectedEntityID = 0;
    std::unordered_set<uint32_t> m_PendingSelectedEntityIDs;
    std::unordered_set<uint32_t> m_SelectedEntityIDs;
    uint32_t m_HierarchySelectionAnchor = 0;
    std::vector<uint32_t> m_HierarchyOrder;
    std::vector<uint32_t> m_HierarchyClipboardEntityIDs;
    ImGuizmo::OPERATION m_GizmoOperation = ImGuizmo::TRANSLATE;
    int m_ProModelerEditMode = 0; // 0=object, 1=vertex, 2=edge, 3=face
    uint32_t m_ProModelerSelectionEntityID = 0;
    std::vector<uint32_t> m_ProModelerSelectedVertices;
    std::vector<std::pair<uint32_t, uint32_t>> m_ProModelerSelectedEdges;
    std::vector<size_t> m_ProModelerSelectedFaceTriangles;
    bool m_ProModelerSelectionIsExtrudedCap = false;

    DockLayoutPreset m_DockLayoutPreset = DockLayoutPreset::SceneTopGameBottom;
    ImGuiID m_SceneConsoleDockNodeId = 0;
    ImGuiID m_ProjectDockNodeId = 0;
    bool m_ForceSceneViewTab = false;
    bool m_ForceProjectTab = false;
    bool m_PendingDockLayoutApply = false;
    bool m_PendingDockLayoutReset = false;
    bool m_DockLayoutInitialized = false;
    std::string m_LayoutIniPath;
    std::unordered_map<ImGuiID, std::string> m_LoadedTabSelections;
    std::unordered_map<ImGuiID, std::string> m_SavedTabSelections;
    bool m_InitialTabSelectionRestored = false;
    bool m_SceneCameraRestoredFromSettings = false;
    bool m_PendingSceneCamPivot = false;
    glm::vec3 m_PendingSceneCamPivotPos{ 0.0f, 0.0f, 0.0f };
    float m_PendingSceneCamDistance = 8.0f;
    float m_PendingSceneCamYaw = -135.0f;
    float m_PendingSceneCamPitch = 30.0f;
    std::string m_PendingAssetBrowserFolder;
    std::vector<std::string> m_PendingAssetBrowserTreeExpanded;

    RenderMode m_SceneRenderMode = RenderMode::Shaded;

    SceneViewPsxToggles m_SceneViewPsx;
    PS1Settings m_SceneViewPsxBaseline;

    bool m_IsPlaying = false;
    bool m_AutoPlayOnStart = false;
    bool m_AutoPlayTriggered = false;
    bool m_IsPaused = false;
    bool m_StepOneFrame = false;
    bool m_PendingPhysicsBeginPlay = false;
    bool m_GameViewHovered = false;
    bool m_PlayCursorLocked = false;
    bool m_PlayShortcutSuppressUntilRelease = false;
    uint32_t m_ActiveShotCameraEntityID = 0;
    bool m_ShotCameraStatePrimed = false;
    std::unordered_map<uint32_t, bool> m_ShotTriggerOverlap;
    std::unordered_map<uint32_t, glm::vec3> m_ShotTargetPreviousCenters;

    std::string m_SceneFilePath; // absolute path under project root
    void SaveScene();
    void LoadScene(bool restartPlayIfWasActive = false);
    void PersistEditorLastScene();

    void RefreshSavedSceneState();
    void UpdateSceneDirtyState();
    void DrawSaveOnExitModal();
    void DrawFontRestartModal();

    std::string m_SceneSavedFingerprint;
    EditorUndoStack m_UndoStack;
    bool m_HadActiveEdit = false;
    bool m_SceneDirtyCheckPending = false;
    bool m_SceneDirty = false;
    bool m_OpenSaveOnExitModal = false;
    bool m_OpenFontRestartModal = false;
    bool m_RequestQuitAfterSaveDialog = false;
    bool m_RequestRestartAfterSaveDialog = false;
    void TriggerRestart();

    std::unique_ptr<AssetBrowserPanel> m_AssetBrowser;
    std::unique_ptr<EditorBuildSettings> m_BuildSettings;

    std::string m_AnimatorControllerWindowPath;
    bool m_AnimatorControllerDockEnsured = false;

    AnimationWindowState m_AnimationWindow;
    bool m_ShowAnimationWindow = false;
    bool LoadAnimationClipForWindow(const std::string& projectRelPath);
    bool SaveAnimationClipFromWindow(std::string* outError = nullptr);
    void EnsureAnimatorForAnimationWindowClip();
    void SampleAnimationWindowToEntity();
    void AddAnimationWindowKeyAtCurrentFrame();
    void DeleteAnimationWindowKeyAtCurrentFrame();

    // Toast / popup for "Build PS1" and "Build && Run" results invoked from the
    // main menu (otherwise the outcome never surfaces unless Build Settings is open).
    std::string m_BuildToastMessage;
    bool m_BuildToastSuccess = true;
    double m_BuildToastExpires = 0.0;
    void TriggerBuildToast(const std::string& message, bool success);
    void DrawBuildToast();

    // Font size / user preferences
    float m_FontSize = 0.0f; // 0 = not yet loaded
    void LoadEditorSettings();
    void SaveEditorSettings();
    void DrawPreferencesMenu();
};

} // namespace MipsyncEngine
