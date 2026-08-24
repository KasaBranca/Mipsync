#pragma once
// ─────────────────────────────────────────────────
// Nostalty — Project / Asset Browser Panel
// ─────────────────────────────────────────────────

#include <imgui.h>
#include <string>
#include <unordered_set>
#include <vector>

namespace MipsyncEngine {

class Engine;
class Scene;
class EditorApp;

enum class AssetKind {
    Folder,
    Scene,
    Script,
    Texture,
    Material,
    AnimatorController,
    Prefab,
    Model,
    AnimationClip,
    Audio,
    Other,
};

struct AssetEntry {
    std::string name;            // file name without path
    std::string projectRelPath;  // forward-slash, project-relative (virtual for sub-assets)
    std::string absolutePath;
    AssetKind kind = AssetKind::Other;
    bool isDirectory = false;
    /// Embedded clip under an FBX/OBJ (not a file on disk).
    bool isSubAsset = false;
    std::string parentModelPath;
    std::string clipName;
    int clipStackIndex = -1;
};

struct SelectedAsset {
    std::string projectRelPath;
    AssetKind kind = AssetKind::Other;
    std::string parentModelPath;
    std::string clipName;
};

/// ImGui drag-drop payload type strings.
namespace DragDrop {
constexpr const char* kEntityId      = "NSTL_ENTITY";
constexpr const char* kAssetTexture  = "NSTL_TEX";
constexpr const char* kAssetMaterial = "NSTL_MAT";
constexpr const char* kAssetAnimatorController = "NSTL_AC";
constexpr const char* kAssetPrefab   = "NSTL_PREFAB";
constexpr const char* kAssetModel    = "NSTL_MODEL";
constexpr const char* kAssetAnimClip = "NSTL_ANIM_CLIP";
constexpr const char* kAssetScript   = "NSTL_SCRIPT";
constexpr const char* kAssetAudio    = "NSTL_AUDIO";
/// In-project move (null-separated project-relative paths).
constexpr const char* kAssetMove     = "NSTL_ASSET_MOVE";

std::string BuildAssetMovePayload(const std::vector<std::string>& projectRelPaths);
std::vector<std::string> ParseAssetMovePayload(const void* data, size_t dataSize);
/// Null-separated: model path, clip name, optional stack index (each null-terminated).
std::string BuildAnimClipPayload(const std::string& modelProjectPath, const std::string& clipName,
                                 int clipStackIndex = -1);
bool ParseAnimClipPayload(const void* data, size_t dataSize, std::string& outModelPath,
                          std::string& outClipName, int* outClipStackIndex = nullptr);
} // namespace DragDrop

class AssetBrowserPanel {
public:
    explicit AssetBrowserPanel(EditorApp* editor);

    void OnImGuiRender();
    void Refresh();

    /// Currently displayed folder, project-relative.
    const std::string& GetCurrentFolder() const { return m_CurrentFolder; }
    void SetCurrentFolder(const std::string& projectRelPath);
    /// Restores folder from saved settings if it still exists under the project.
    bool TryRestoreFolder(const std::string& projectRelPath);

    void SetExpandedTreeFolders(const std::vector<std::string>& projectRelPaths);
    std::vector<std::string> GetExpandedTreeFolderPaths() const;

    /// Last-selected asset (for inspector / drag-drop). Empty if none selected.
    const std::string& GetSelectedAssetPath() const;
    AssetKind GetSelectedAssetKind() const;
    const std::vector<SelectedAsset>& GetSelectedAssets() const { return m_SelectedAssets; }
    size_t GetSelectedAssetCount() const { return m_SelectedAssets.size(); }
    void ClearAssetSelection();
    /// Opens the containing folder and selects an asset by relative or absolute path.
    bool RevealAndSelectAsset(const std::string& projectOrAbsolutePath);
    /// When a virtual animation clip sub-asset is selected.
    bool GetSelectedAnimationClip(std::string& outModelPath, std::string& outClipName) const;

    static AssetKind ClassifyByExtension(const std::string& filename);
    static AssetKind ClassifyAssetByPath(const std::string& projectRelPath);
    static std::string MakeAnimationClipVirtualPath(const std::string& modelProjectPath,
                                                    const std::string& clipName,
                                                    int clipStackIndex = -1);
    static bool TryParseAnimationClipVirtualPath(const std::string& virtualPath,
                                                 std::string& outModelPath,
                                                 std::string& outClipName,
                                                 int* outClipStackIndex = nullptr);

private:
    void DrawPanelContextMenu();
    void DrawFolderTree();
    void DrawFolderTreeNode(const std::string& projectRelPath, const std::string& displayName);
    void DrawFileGrid();
    void DrawFileGridCell(const AssetEntry& entry, size_t displayIndex, float cellThumb,
                          int& col, int columns);
    void DrawNewMaterialDialog();
    void DrawNewAnimatorControllerDialog();
    void DrawNewScriptDialog();
    void DrawNewFolderDialog();
    void DrawNewSceneDialog();
    void DrawRenameDialog();

    bool IsAssetSelected(const std::string& projectRelPath) const;
    void SelectAssetOnly(const AssetEntry& entry, size_t displayIndex);
    void ToggleAssetSelection(const AssetEntry& entry, size_t displayIndex);
    void SelectAssetRange(const std::vector<AssetEntry>& display, size_t fromIndex,
                          size_t toIndex);
    std::vector<AssetEntry> BuildDisplayEntries() const;
    void HandleAssetClickSelection(const AssetEntry& entry, size_t entryIndex);
    void DrawAssetContextMenu(const AssetEntry& entry, size_t entryIndex);
    void OpenRenameDialogForSelection();
    void DeleteSelectedAssets();
    bool DeleteAssetAt(const std::string& projectRelPath, AssetKind kind, std::string& outError);
    bool RenameAssetAt(const std::string& projectRelPath, const std::string& newName, std::string& outError);
    void HandleGridKeyboardShortcuts();

    void RescanFolder();
    void RescanFolderTree();
    void HandleEntityDropOnFolder(const std::string& projectRelFolder);
    void HandleAssetMoveDropOnFolder(const std::string& projectRelFolder);
    void HandleFolderDropTargets(const std::string& projectRelFolder);
    void HandleCurrentFolderDropTarget();
    std::vector<std::string> GetDragMovePaths(const AssetEntry& entry) const;
    std::vector<std::string> FilterTopLevelPaths(const std::vector<std::string>& paths) const;
    bool MoveAssetToFolder(const std::string& srcProjectRel, const std::string& destFolderRel,
                           std::string& outError);
    void MoveAssetsToFolder(const std::vector<std::string>& srcPaths,
                            const std::string& destFolderRel);
    void BeginAssetDragSource(const AssetEntry& entry);
    void BeginAssetMoveDragSource(const AssetEntry& entry);
    void ExpandTreeToFolder(const std::string& projectRelPath);
    void DrawAssetThumbnail(ImDrawList* dl, const AssetEntry& entry, ImVec2 cellPos, float thumb);

    static const char* DragDropTypeFor(AssetKind kind);

    EditorApp* m_Editor;
    std::string m_CurrentFolder;     // project-relative; "" or "." for root
    std::vector<AssetEntry> m_Entries;
    std::vector<std::string> m_FolderTreeRoots; // top-level folders inside the project
    std::unordered_set<std::string> m_ExpandedTreeFolders;
    std::unordered_set<std::string> m_ExpandedModels;
    bool m_ScanRequested = true;

    // Modals
    bool m_OpenNewMaterialDialog = false;
    bool m_OpenNewAnimatorControllerDialog = false;
    bool m_OpenNewScriptDialog   = false;
    bool m_OpenNewFolderDialog   = false;
    bool m_OpenNewSceneDialog    = false;
    bool m_OpenImportDialog      = false;
    bool m_OpenRenameDialog      = false;
    char m_NewMaterialName[128] = "NewMaterial.nmat";
    char m_NewControllerName[128] = "NewController.ncontroller";
    char m_NewControllerModelPath[512] = {};
    bool m_NewControllerFromModel = false;
    char m_NewScriptName[128]   = "NewScript.mips";
    char m_NewFolderName[128]   = "NewFolder";
    char m_NewSceneName[128]    = "NewScene.nscene";
    char m_RenameBuffer[256]    = {};
    std::string m_RenameTargetPath;

    std::string m_LastError;
    std::string m_LastInfo;

    std::vector<SelectedAsset> m_SelectedAssets;
    size_t m_SelectionAnchorIndex = SIZE_MAX;
    static const std::string kEmptyPath;

    void RunPendingImportDialog();
    void ProcessOsFileDrop();
    int ImportExternalPath(const std::string& absSourcePath);
};

} // namespace MipsyncEngine
