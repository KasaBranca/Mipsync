#include "AssetBrowserPanel.h"
#include "MipsEditorIntegration.h"
#include "EditorApp.h"
#include "EditorTheme.h"
#include "EditorAssetIcons.h"
#include "../assets/AssetThumbnail.h"
#include "../core/Engine.h"
#include "../core/Log.h"
#include "../scene/Scene.h"
#include "../scene/SceneIO.h"
#include "../assets/AssetManager.h"
#include "../assets/Material.h"
#include "../animation/AnimatorControllerIO.h"
#include "../animation/SkeletalModel.h"
#include "../renderer/Texture.h"
#include "../core/OsFileDrop.h"
#include <imgui.h>
#include <memory>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <filesystem>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shellapi.h>
#  include <commdlg.h>
#endif

namespace MipsyncEngine {

namespace fs = std::filesystem;

namespace {

bool StrEndsWithCI(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    for (size_t i = 0; i < suffix.size(); ++i) {
        char a = static_cast<char>(std::tolower(static_cast<unsigned char>(s[s.size() - suffix.size() + i])));
        char b = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
        if (a != b) return false;
    }
    return true;
}

#ifdef _WIN32
/// Returns the picked file as a UTF-8 std::string, or empty on cancel.
/// Uses the wide API to avoid system-codepage round-trips that break MinGW filesystem.
std::string OpenFileDialogW(const wchar_t* filter, HWND owner = nullptr) {
    wchar_t buf[MAX_PATH * 2] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.lpstrFilter = filter;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn))
        return PathUtf8::FromWide(std::wstring(buf));
    return {};
}
#endif

void RevealFolderInOS(const std::string& absPath) {
#ifdef _WIN32
    std::wstring wpath = std::filesystem::path(PathUtf8::FromString(absPath)).wstring();
    ShellExecuteW(nullptr, L"open", wpath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    (void)absPath;
#endif
}

const char* AssetKindLabel(AssetKind kind) {
    switch (kind) {
    case AssetKind::Folder:   return "[DIR]";
    case AssetKind::Scene:    return "SCN";
    case AssetKind::Script:   return "MIPS";
    case AssetKind::Texture:  return "TEX";
    case AssetKind::Material: return "MAT";
    case AssetKind::Prefab:   return "PFB";
    case AssetKind::Model:    return "MDL";
    case AssetKind::AnimatorController: return "AC";
    case AssetKind::AnimationClip: return "ANIM";
    case AssetKind::Audio: return "SND";
    case AssetKind::Other:
    default:                  return "···";
    }
}

ImVec4 AssetKindColor(AssetKind kind) {
    switch (kind) {
    case AssetKind::Folder:   return EditorTheme::TextSecondary;
    case AssetKind::Scene:    return EditorTheme::LinkCyan;
    case AssetKind::Script:   return EditorTheme::SuccessLabel;
    case AssetKind::Texture:  return EditorTheme::PsAccent;
    case AssetKind::Material: return UiTokens::Component;
    case AssetKind::Prefab:   return UiTokens::WarningText;
    case AssetKind::Model:    return UiTokens::TextBrand;
    case AssetKind::AnimatorController: return UiTokens::Component;
    case AssetKind::AnimationClip: return UiTokens::Warning;
    case AssetKind::Audio: return UiTokens::BrandSecondary;
    default:                  return EditorTheme::TextMuted;
    }
}

constexpr const char* kClipVirtualSep = "::clip::";

std::string JoinProjectRel(const std::string& a, const std::string& b) {
    if (a.empty() || a == ".") return b;
    if (b.empty()) return a;
    return a + "/" + b;
}

/// Unity-style project panel roots (project filesystem root is hidden).
constexpr const char* kAssetsRoot = "assets";
constexpr const char* kScenesRoot = "scenes";

bool IsContentRoot(const std::string& folder) {
    return folder == kAssetsRoot || folder == kScenesRoot;
}

std::string ContentRootFor(const std::string& folder) {
    if (folder.rfind(kScenesRoot, 0) == 0)
        return kScenesRoot;
    return kAssetsRoot;
}

std::string NormalizeContentFolder(std::string folder) {
    if (folder.empty() || folder == ".")
        return kAssetsRoot;
    return folder;
}

std::string DisplayFolderPath(const std::string& projectRel) {
    const std::string root = ContentRootFor(projectRel);
    if (projectRel == root)
        return "";
    const std::string prefix = root + "/";
    if (projectRel.rfind(prefix, 0) == 0)
        return projectRel.substr(prefix.size());
    return projectRel;
}

bool IsPathUnderFolder(const std::string& folderRel, const std::string& itemRel) {
    if (folderRel == itemRel)
        return true;
    const std::string prefix = folderRel + "/";
    return itemRel.size() > prefix.size() && itemRel.rfind(prefix, 0) == 0;
}

/// True when `descendantRel` is a strict child of `ancestorRel` (not equal).
bool IsStrictSubfolderOf(const std::string& ancestorRel, const std::string& descendantRel) {
    if (ancestorRel == descendantRel)
        return false;
    const std::string prefix = ancestorRel + "/";
    return descendantRel.size() > prefix.size() && descendantRel.rfind(prefix, 0) == 0;
}

std::string NormalizeProjectRel(std::string path) {
    for (char& c : path) {
        if (c == '\\')
            c = '/';
    }
    return NormalizeContentFolder(std::move(path));
}

std::string ParentProjectRel(const std::string& projectRelPath) {
    const fs::path parent = PathUtf8::FromString(projectRelPath).parent_path();
    if (parent.empty() || parent == ".")
        return kAssetsRoot;
    std::string rel = PathUtf8::ToString(parent);
    for (char& c : rel) {
        if (c == '\\')
            c = '/';
    }
    return rel;
}

} // namespace

namespace DragDrop {

std::string BuildAssetMovePayload(const std::vector<std::string>& projectRelPaths) {
    std::string payload;
    for (const std::string& path : projectRelPaths) {
        if (path.empty())
            continue;
        payload.append(path);
        payload.push_back('\0');
    }
    return payload;
}

std::vector<std::string> ParseAssetMovePayload(const void* data, size_t dataSize) {
    std::vector<std::string> paths;
    if (!data || dataSize == 0)
        return paths;

    const char* bytes = static_cast<const char*>(data);
    size_t offset = 0;
    while (offset < dataSize) {
        const std::string path(bytes + offset);
        if (path.empty())
            break;
        paths.push_back(path);
        offset += path.size() + 1;
    }
    return paths;
}

std::string BuildAnimClipPayload(const std::string& modelProjectPath, const std::string& clipName,
                                 int clipStackIndex) {
    std::string payload = modelProjectPath;
    payload.push_back('\0');
    payload.append(clipName);
    payload.push_back('\0');
    if (clipStackIndex >= 0)
        payload.append(std::to_string(clipStackIndex));
    payload.push_back('\0');
    return payload;
}

bool ParseAnimClipPayload(const void* data, size_t dataSize, std::string& outModelPath,
                          std::string& outClipName, int* outClipStackIndex) {
    outModelPath.clear();
    outClipName.clear();
    if (outClipStackIndex)
        *outClipStackIndex = -1;
    if (!data || dataSize == 0)
        return false;

    const char* bytes = static_cast<const char*>(data);
    const char* end = bytes + dataSize;
    const char* sep = static_cast<const char*>(std::memchr(bytes, '\0', dataSize));
    if (!sep || sep == bytes)
        return false;

    outModelPath.assign(bytes, sep);
    const char* clipStart = sep + 1;
    if (clipStart >= end)
        return false;

    const size_t clipSpan = static_cast<size_t>(end - clipStart);
    const char* clipEnd =
        static_cast<const char*>(std::memchr(clipStart, '\0', clipSpan));
    if (clipEnd)
        outClipName.assign(clipStart, clipEnd);
    else
        outClipName.assign(clipStart, clipSpan);

    if (!outModelPath.empty() && !outClipName.empty() && clipEnd && clipEnd + 1 < end) {
        const char* stackStart = clipEnd + 1;
        const size_t stackSpan = static_cast<size_t>(end - stackStart);
        const char* stackEnd =
            static_cast<const char*>(std::memchr(stackStart, '\0', stackSpan));
        std::string stackText;
        if (stackEnd)
            stackText.assign(stackStart, stackEnd);
        else
            stackText.assign(stackStart, stackSpan);
        if (!stackText.empty() && outClipStackIndex) {
            try {
                *outClipStackIndex = std::stoi(stackText);
            } catch (...) {
                *outClipStackIndex = -1;
            }
        }
    }

    return !outModelPath.empty() && !outClipName.empty();
}

} // namespace DragDrop

const std::string AssetBrowserPanel::kEmptyPath;

AssetBrowserPanel::AssetBrowserPanel(EditorApp* editor)
    : m_Editor(editor), m_CurrentFolder(kAssetsRoot) {
    m_ExpandedTreeFolders.insert(kAssetsRoot);
    m_ExpandedTreeFolders.insert(kScenesRoot);
}

AssetKind AssetBrowserPanel::ClassifyByExtension(const std::string& filename) {
    if (StrEndsWithCI(filename, ".nscene"))   return AssetKind::Scene;
    if (StrEndsWithCI(filename, ".mips"))     return AssetKind::Script;
    if (StrEndsWithCI(filename, ".nmat"))     return AssetKind::Material;
    if (StrEndsWithCI(filename, ".ncontroller")) return AssetKind::AnimatorController;
    if (StrEndsWithCI(filename, ".nanim"))    return AssetKind::AnimationClip;
    if (StrEndsWithCI(filename, ".nprefab"))  return AssetKind::Prefab;
    if (StrEndsWithCI(filename, ".fbx") || StrEndsWithCI(filename, ".obj") ||
        StrEndsWithCI(filename, ".glb"))
        return AssetKind::Model;
    if (StrEndsWithCI(filename, ".png") || StrEndsWithCI(filename, ".jpg") ||
        StrEndsWithCI(filename, ".jpeg") || StrEndsWithCI(filename, ".tga") ||
        StrEndsWithCI(filename, ".bmp") || StrEndsWithCI(filename, ".hdr"))
        return AssetKind::Texture;
    if (StrEndsWithCI(filename, ".wav") || StrEndsWithCI(filename, ".mp3") ||
        StrEndsWithCI(filename, ".vag"))      return AssetKind::Audio;
    return AssetKind::Other;
}

AssetKind AssetBrowserPanel::ClassifyAssetByPath(const std::string& projectRelPath) {
    std::string modelPath;
    std::string clipName;
    if (TryParseAnimationClipVirtualPath(projectRelPath, modelPath, clipName))
        return AssetKind::AnimationClip;

    const fs::path p = PathUtf8::FromString(projectRelPath);
    std::error_code ec;
    const std::string abs = AssetManager::Get().ToAbsolute(projectRelPath);
    if (fs::is_directory(PathUtf8::FromString(abs), ec))
        return AssetKind::Folder;
    return ClassifyByExtension(PathUtf8::ToString(p.filename()));
}

std::string AssetBrowserPanel::MakeAnimationClipVirtualPath(const std::string& modelProjectPath,
                                                            const std::string& clipName,
                                                            int clipStackIndex) {
    if (clipStackIndex >= 0)
        return modelProjectPath + kClipVirtualSep + std::to_string(clipStackIndex) + "::" + clipName;
    return modelProjectPath + kClipVirtualSep + clipName;
}

bool AssetBrowserPanel::TryParseAnimationClipVirtualPath(const std::string& virtualPath,
                                                         std::string& outModelPath,
                                                         std::string& outClipName,
                                                         int* outClipStackIndex) {
    if (outClipStackIndex)
        *outClipStackIndex = -1;
    const size_t sep = virtualPath.find(kClipVirtualSep);
    if (sep == std::string::npos)
        return false;
    outModelPath = virtualPath.substr(0, sep);
    std::string rest = virtualPath.substr(sep + std::strlen(kClipVirtualSep));
    const size_t stackSep = rest.find("::");
    if (stackSep != std::string::npos) {
        const std::string maybeStack = rest.substr(0, stackSep);
        bool allDigits = !maybeStack.empty();
        for (char c : maybeStack) {
            if (c < '0' || c > '9') {
                allDigits = false;
                break;
            }
        }
        if (allDigits) {
            try {
                if (outClipStackIndex)
                    *outClipStackIndex = std::stoi(maybeStack);
                outClipName = rest.substr(stackSep + 2);
                return !outModelPath.empty() && !outClipName.empty();
            } catch (...) {
            }
        }
    }
    outClipName = std::move(rest);
    return !outModelPath.empty() && !outClipName.empty();
}

const char* AssetBrowserPanel::DragDropTypeFor(AssetKind kind) {
    switch (kind) {
    case AssetKind::Texture:  return DragDrop::kAssetTexture;
    case AssetKind::Material: return DragDrop::kAssetMaterial;
    case AssetKind::AnimatorController: return DragDrop::kAssetAnimatorController;
    case AssetKind::Prefab:   return DragDrop::kAssetPrefab;
    case AssetKind::Model:    return DragDrop::kAssetModel;
    case AssetKind::AnimationClip: return DragDrop::kAssetAnimClip;
    case AssetKind::Script:   return DragDrop::kAssetScript;
    case AssetKind::Audio:    return DragDrop::kAssetAudio;
    default:                  return nullptr;
    }
}

void AssetBrowserPanel::Refresh() {
    m_ScanRequested = true;
}

void AssetBrowserPanel::SetCurrentFolder(const std::string& projectRelPath) {
    m_CurrentFolder = NormalizeContentFolder(projectRelPath);
    ExpandTreeToFolder(m_CurrentFolder);
    ClearAssetSelection();
    Refresh();
}

bool AssetBrowserPanel::RevealAndSelectAsset(const std::string& projectOrAbsolutePath) {
    const std::string rel = NormalizeProjectRel(
        AssetManager::Get().ToProjectRelative(projectOrAbsolutePath));
    if (rel.empty())
        return false;
    const std::string abs = AssetManager::Get().ToAbsolute(rel);
    std::error_code ec;
    if (!fs::is_regular_file(PathUtf8::FromString(abs), ec))
        return false;

    m_CurrentFolder = NormalizeContentFolder(ParentProjectRel(rel));
    ExpandTreeToFolder(m_CurrentFolder);
    m_SelectedAssets.clear();
    SelectedAsset selected{};
    selected.projectRelPath = rel;
    selected.kind = ClassifyAssetByPath(rel);
    m_SelectedAssets.push_back(std::move(selected));
    m_SelectionAnchorIndex = SIZE_MAX;
    Refresh();
    return true;
}

void AssetBrowserPanel::ExpandTreeToFolder(const std::string& projectRelPath) {
    std::string path = NormalizeProjectRel(projectRelPath);
    while (!IsContentRoot(path)) {
        m_ExpandedTreeFolders.insert(path);
        path = ParentProjectRel(path);
    }
    m_ExpandedTreeFolders.insert(path);
}

void AssetBrowserPanel::SetExpandedTreeFolders(const std::vector<std::string>& projectRelPaths) {
    m_ExpandedTreeFolders.clear();
    m_ExpandedTreeFolders.insert(kAssetsRoot);
    m_ExpandedTreeFolders.insert(kScenesRoot);
    for (const std::string& path : projectRelPaths) {
        if (!path.empty())
            m_ExpandedTreeFolders.insert(NormalizeProjectRel(path));
    }
}

std::vector<std::string> AssetBrowserPanel::GetExpandedTreeFolderPaths() const {
    std::vector<std::string> paths(m_ExpandedTreeFolders.begin(), m_ExpandedTreeFolders.end());
    std::sort(paths.begin(), paths.end());
    return paths;
}

bool AssetBrowserPanel::TryRestoreFolder(const std::string& projectRelPath) {
    if (projectRelPath.empty())
        return false;

    const std::string folder = NormalizeContentFolder(projectRelPath);
    const std::string& root = AssetManager::Get().GetProjectRoot();
    if (root.empty())
        return false;

    const fs::path absFolder = PathUtf8::FromString(root) / PathUtf8::FromString(folder);
    std::error_code ec;
    if (!fs::exists(absFolder, ec) || !fs::is_directory(absFolder, ec))
        return false;

    SetCurrentFolder(folder);
    return true;
}

const std::string& AssetBrowserPanel::GetSelectedAssetPath() const {
    return m_SelectedAssets.empty() ? kEmptyPath : m_SelectedAssets.back().projectRelPath;
}

AssetKind AssetBrowserPanel::GetSelectedAssetKind() const {
    return m_SelectedAssets.empty() ? AssetKind::Other : m_SelectedAssets.back().kind;
}

bool AssetBrowserPanel::GetSelectedAnimationClip(std::string& outModelPath,
                                                 std::string& outClipName) const {
    if (m_SelectedAssets.size() != 1 ||
        m_SelectedAssets.back().kind != AssetKind::AnimationClip)
        return false;
    const SelectedAsset& sel = m_SelectedAssets.back();
    if (!sel.parentModelPath.empty() && !sel.clipName.empty()) {
        outModelPath = sel.parentModelPath;
        outClipName = sel.clipName;
        return true;
    }
    return TryParseAnimationClipVirtualPath(sel.projectRelPath, outModelPath, outClipName);
}

void AssetBrowserPanel::ClearAssetSelection() {
    m_SelectedAssets.clear();
    m_SelectionAnchorIndex = SIZE_MAX;
}

bool AssetBrowserPanel::IsAssetSelected(const std::string& projectRelPath) const {
    for (const auto& sel : m_SelectedAssets) {
        if (sel.projectRelPath == projectRelPath)
            return true;
    }
    return false;
}

static SelectedAsset MakeSelectedAsset(const AssetEntry& entry) {
    SelectedAsset sel{};
    sel.projectRelPath = entry.projectRelPath;
    sel.kind = entry.kind;
    sel.parentModelPath = entry.parentModelPath;
    sel.clipName = entry.clipName;
    return sel;
}

void AssetBrowserPanel::SelectAssetOnly(const AssetEntry& entry, size_t displayIndex) {
    m_SelectedAssets.clear();
    m_SelectedAssets.push_back(MakeSelectedAsset(entry));
    m_SelectionAnchorIndex = displayIndex;
    if (entry.kind == AssetKind::AnimatorController && m_Editor)
        m_Editor->OpenAnimatorControllerWindow(entry.projectRelPath);
}

void AssetBrowserPanel::ToggleAssetSelection(const AssetEntry& entry, size_t displayIndex) {
    for (size_t i = 0; i < m_SelectedAssets.size(); ++i) {
        if (m_SelectedAssets[i].projectRelPath == entry.projectRelPath) {
            m_SelectedAssets.erase(m_SelectedAssets.begin() + static_cast<ptrdiff_t>(i));
            if (m_SelectedAssets.empty())
                m_SelectionAnchorIndex = SIZE_MAX;
            return;
        }
    }
    m_SelectedAssets.push_back(MakeSelectedAsset(entry));
    m_SelectionAnchorIndex = displayIndex;
}

void AssetBrowserPanel::SelectAssetRange(const std::vector<AssetEntry>& display, size_t fromIndex,
                                         size_t toIndex) {
    if (fromIndex >= display.size() || toIndex >= display.size())
        return;
    if (fromIndex > toIndex)
        std::swap(fromIndex, toIndex);

    m_SelectedAssets.clear();
    for (size_t i = fromIndex; i <= toIndex; ++i)
        m_SelectedAssets.push_back(MakeSelectedAsset(display[i]));
}

void AssetBrowserPanel::HandleAssetClickSelection(const AssetEntry& entry, size_t entryIndex) {
    if (m_Editor)
        m_Editor->ClearEntitySelection();

    ImGuiIO& io = ImGui::GetIO();
    const std::vector<AssetEntry> display = BuildDisplayEntries();
    if (io.KeyShift && m_SelectionAnchorIndex != SIZE_MAX)
        SelectAssetRange(display, m_SelectionAnchorIndex, entryIndex);
    else if (io.KeyCtrl)
        ToggleAssetSelection(entry, entryIndex);
    else
        SelectAssetOnly(entry, entryIndex);
}

static void InvalidateAssetCaches(const std::string& projectRelPath, AssetKind kind) {
    AssetManager& assets = AssetManager::Get();
    if (kind == AssetKind::Texture)
        assets.DropTexture(projectRelPath);
    else if (kind == AssetKind::Model)
        assets.DropMesh(projectRelPath);
    else if (kind == AssetKind::Material)
        AssetThumbnail::Get().DropMaterialThumbnail(projectRelPath);
    else if (kind == AssetKind::AnimatorController)
        assets.DropAnimatorController(projectRelPath);
}

bool AssetBrowserPanel::DeleteAssetAt(const std::string& projectRelPath, AssetKind kind,
                                      std::string& outError) {
    if (kind == AssetKind::AnimationClip) {
        outError = "animation clips are embedded in the model file";
        return false;
    }

    if (projectRelPath == kAssetsRoot || projectRelPath == kScenesRoot) {
        outError = "cannot delete a project root folder";
        return false;
    }

    const std::string abs = AssetManager::Get().ToAbsolute(projectRelPath);
    std::error_code ec;
    const fs::path path = PathUtf8::FromString(abs);
    if (!fs::exists(path, ec)) {
        outError = "not found";
        return false;
    }

    if (fs::is_directory(path, ec)) {
        const auto removed = fs::remove_all(path, ec);
        if (ec || removed == 0) {
            outError = ec ? ec.message() : "delete failed";
            return false;
        }
    } else {
        if (!fs::remove(path, ec) || ec) {
            outError = ec ? ec.message() : "delete failed";
            return false;
        }
        InvalidateAssetCaches(projectRelPath, kind);
    }
    return true;
}

void AssetBrowserPanel::DeleteSelectedAssets() {
    if (m_SelectedAssets.empty())
        return;

    std::vector<SelectedAsset> toDelete = m_SelectedAssets;
    int deleted = 0;
    std::string lastErr;

    for (const auto& sel : toDelete) {
        std::string err;
        if (DeleteAssetAt(sel.projectRelPath, sel.kind, err))
            ++deleted;
        else if (!err.empty())
            lastErr = err;
    }

    ClearAssetSelection();
    if (deleted > 0) {
        m_LastInfo = "Deleted " + std::to_string(deleted) + " item(s)";
        m_LastError.clear();
        RescanFolderTree();
        Refresh();
    } else {
        m_LastError = lastErr.empty() ? "delete failed" : lastErr;
    }
}

bool AssetBrowserPanel::RenameAssetAt(const std::string& projectRelPath, const std::string& newName,
                                      std::string& outError) {
    if (newName.empty() || newName.find_first_of("\\/:*?\"<>|") != std::string::npos) {
        outError = "invalid name";
        return false;
    }
    if (projectRelPath == kAssetsRoot || projectRelPath == kScenesRoot) {
        outError = "cannot rename a project root folder";
        return false;
    }

    const fs::path oldAbs = PathUtf8::FromString(AssetManager::Get().ToAbsolute(projectRelPath));
    const fs::path newAbs = oldAbs.parent_path() / PathUtf8::FromString(newName);

    std::error_code ec;
    if (fs::exists(newAbs, ec)) {
        outError = "name already exists";
        return false;
    }
    if (!fs::exists(oldAbs, ec)) {
        outError = "not found";
        return false;
    }

    fs::rename(oldAbs, newAbs, ec);
    if (ec) {
        outError = ec.message();
        return false;
    }

    return true;
}

void AssetBrowserPanel::OpenRenameDialogForSelection() {
    if (m_SelectedAssets.size() != 1)
        return;
    if (m_SelectedAssets.front().kind == AssetKind::AnimationClip)
        return;
    m_RenameTargetPath = m_SelectedAssets.front().projectRelPath;
    const fs::path p = PathUtf8::FromString(m_RenameTargetPath);
    const std::string current = PathUtf8::ToString(p.filename());
    std::memset(m_RenameBuffer, 0, sizeof(m_RenameBuffer));
    std::strncpy(m_RenameBuffer, current.c_str(), sizeof(m_RenameBuffer) - 1);
    m_OpenRenameDialog = true;
}

void AssetBrowserPanel::DrawAssetContextMenu(const AssetEntry& entry, size_t entryIndex) {
    if (!ImGui::BeginPopupContextItem("##ctx"))
        return;

    if (!IsAssetSelected(entry.projectRelPath))
        SelectAssetOnly(entry, entryIndex);

    const size_t selCount = m_SelectedAssets.size();

    if (selCount == 1 && !entry.isSubAsset && ImGui::MenuItem("Rename..."))
        OpenRenameDialogForSelection();

    if (selCount == 1 && entry.kind == AssetKind::Scene && ImGui::MenuItem("Open Scene")) {
        if (m_Editor)
            m_Editor->LoadSceneFromPath(entry.absolutePath);
    }

    if (selCount == 1 && entry.kind == AssetKind::Script) {
        if (ImGui::MenuItem("Open in IDE")) {
            if (!MipsEditorIntegration::OpenScriptInIde(entry.projectRelPath))
                m_LastError = "Failed to open script in IDE";
        }
        if (ImGui::MenuItem("Validate Mips#")) {
            const auto result = MipsEditorIntegration::ValidateScript(entry.projectRelPath);
            MipsEditorIntegration::LogValidationResult(entry.projectRelPath, result);
            if (result.success) {
                m_LastError.clear();
                m_LastInfo = "Mips# OK: " + entry.projectRelPath;
            } else {
                m_LastInfo.clear();
                m_LastError = "Mips# compile errors; see Console";
                if (!result.diagnostics.empty() && result.diagnostics.front().hasLocation) {
                    const auto& first = result.diagnostics.front();
                    MipsEditorIntegration::OpenScriptInIde(entry.projectRelPath, first.line, first.column);
                }
            }
        }
    }

    if (!entry.isSubAsset &&
        ImGui::MenuItem(selCount > 1 ? "Delete Selected" : "Delete"))
        DeleteSelectedAssets();

    if (ImGui::MenuItem("Reveal in Explorer")) {
        if (entry.isSubAsset && !entry.parentModelPath.empty()) {
            const std::string abs = AssetManager::Get().ToAbsolute(entry.parentModelPath);
            fs::path target = PathUtf8::FromString(abs);
            target = target.parent_path();
            RevealFolderInOS(PathUtf8::ToString(target));
        } else {
            fs::path target = PathUtf8::FromString(entry.absolutePath);
            if (!entry.isDirectory)
                target = target.parent_path();
            RevealFolderInOS(PathUtf8::ToString(target));
        }
    }

    ImGui::EndPopup();
}

void AssetBrowserPanel::HandleGridKeyboardShortcuts() {
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows))
        return;

    if (ImGui::IsKeyPressed(ImGuiKey_Delete))
        DeleteSelectedAssets();
    if (m_SelectedAssets.size() == 1 && ImGui::IsKeyPressed(ImGuiKey_F2))
        OpenRenameDialogForSelection();
}

void AssetBrowserPanel::DrawRenameDialog() {
    if (m_OpenRenameDialog) {
        ImGui::OpenPopup("Rename Asset");
        m_OpenRenameDialog = false;
    }

    ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Rename Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextColored(EditorTheme::TextSecondary, "%s", m_RenameTargetPath.c_str());
    ImGui::Spacing();
    ImGui::PushItemWidth(-1.0f);
    ImGui::InputText("##rename", m_RenameBuffer, sizeof(m_RenameBuffer));
    ImGui::PopItemWidth();
    ImGui::Spacing();

    const float bw = 100.0f;
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - bw * 2.0f -
                          ImGui::GetStyle().ItemSpacing.x);
    if (EditorTheme::AeroButton("Cancel", ImVec2(bw, EditorTheme::ButtonHeight), AeroButtonKind::Secondary))
        ImGui::CloseCurrentPopup();
    ImGui::SameLine();
    if (EditorTheme::AeroButton("Rename", ImVec2(bw, EditorTheme::ButtonHeight), AeroButtonKind::Primary)) {
        std::string err;
        AssetKind kind = AssetKind::Other;
        for (const auto& e : m_Entries) {
            if (e.projectRelPath == m_RenameTargetPath) {
                kind = e.kind;
                break;
            }
        }
        if (RenameAssetAt(m_RenameTargetPath, m_RenameBuffer, err)) {
            InvalidateAssetCaches(m_RenameTargetPath, kind);
            m_LastInfo = "Renamed to: " + std::string(m_RenameBuffer);
            m_LastError.clear();
            ClearAssetSelection();
            RescanFolderTree();
            Refresh();
            ImGui::CloseCurrentPopup();
        } else {
            m_LastError = err;
        }
    }
    ImGui::EndPopup();
}

void AssetBrowserPanel::RescanFolderTree() {
    m_FolderTreeRoots.clear();
    const std::string& root = AssetManager::Get().GetProjectRoot();
    if (root.empty()) return;

    std::error_code ec;
    fs::create_directories(PathUtf8::FromString(root) / kAssetsRoot, ec);
    fs::create_directories(PathUtf8::FromString(root) / kScenesRoot, ec);
    m_FolderTreeRoots.push_back(kAssetsRoot);
    m_FolderTreeRoots.push_back(kScenesRoot);
}

void AssetBrowserPanel::RescanFolder() {
    m_Entries.clear();
    AssetManager& assets = AssetManager::Get();
    const std::string& root = assets.GetProjectRoot();
    if (root.empty()) return;

    fs::path rootPath = PathUtf8::FromString(root);
    const std::string folder = NormalizeContentFolder(m_CurrentFolder);
    fs::path absFolder = rootPath / PathUtf8::FromString(folder);

    std::error_code ec;
    if (!fs::exists(absFolder, ec) || !fs::is_directory(absFolder, ec))
        return;

    for (const auto& entry : fs::directory_iterator(absFolder, ec)) {
        if (ec) break;
        AssetEntry asset;
        try {
            asset.name = PathUtf8::ToString(entry.path().filename());
            asset.absolutePath = PathUtf8::ToString(entry.path());
        } catch (const std::exception&) {
            continue;
        }
        if (asset.name.empty() || asset.name[0] == '.') continue;
        asset.projectRelPath = JoinProjectRel(folder, asset.name);
        asset.isDirectory = entry.is_directory();
        asset.kind = asset.isDirectory ? AssetKind::Folder : ClassifyByExtension(asset.name);
        m_Entries.push_back(std::move(asset));
    }

    std::sort(m_Entries.begin(), m_Entries.end(), [](const AssetEntry& a, const AssetEntry& b) {
        if (a.isDirectory != b.isDirectory) return a.isDirectory;
        return a.name < b.name;
    });
}

std::vector<AssetEntry> AssetBrowserPanel::BuildDisplayEntries() const {
    std::vector<AssetEntry> display;
    display.reserve(m_Entries.size() + 8);

    for (const AssetEntry& entry : m_Entries) {
        display.push_back(entry);
        if (entry.kind != AssetKind::Model || entry.isDirectory)
            continue;
        const std::string modelKey = NormalizeProjectRel(entry.projectRelPath);
        if (m_ExpandedModels.find(modelKey) == m_ExpandedModels.end())
            continue;

        const auto model = AssetManager::Get().GetSkeletalModel(entry.projectRelPath);
        if (!model || model->animationNames.empty())
            continue;

        for (size_t ci = 0; ci < model->animationNames.size(); ++ci) {
            const std::string& clip = model->animationNames[ci];
            const int stackIdx = static_cast<int>(model->animationStackIndices[ci]);
            AssetEntry clipEntry{};
            clipEntry.name = clip;
            clipEntry.projectRelPath =
                MakeAnimationClipVirtualPath(entry.projectRelPath, clip, stackIdx);
            clipEntry.kind = AssetKind::AnimationClip;
            clipEntry.isSubAsset = true;
            clipEntry.parentModelPath = entry.projectRelPath;
            clipEntry.clipName = clip;
            clipEntry.clipStackIndex = stackIdx;
            display.push_back(std::move(clipEntry));
        }
    }
    return display;
}

void AssetBrowserPanel::HandleEntityDropOnFolder(const std::string& projectRelFolder) {
    const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DragDrop::kEntityId);
    if (!payload || !payload->Data ||
        payload->DataSize < static_cast<int>(sizeof(uint32_t)) ||
        payload->DataSize % static_cast<int>(sizeof(uint32_t)) != 0)
        return;

    if (!m_Editor || !m_Editor->GetEngine())
        return;
    Scene& scene = m_Editor->GetEngine()->GetScene();
    const auto* ids = static_cast<const uint32_t*>(payload->Data);
    const size_t idCount = static_cast<size_t>(payload->DataSize) / sizeof(uint32_t);
    size_t savedCount = 0;
    std::string lastSaved;

    for (size_t idIndex = 0; idIndex < idCount; ++idIndex) {
        Entity* target = scene.FindEntity(ids[idIndex]);
        if (!target)
            continue;

        std::string baseName = "Prefab";
        if (auto* tag = target->GetComponent<TagComponent>())
            baseName = tag->tag;

        const std::string destination =
            projectRelFolder.empty() ? "assets/prefabs" : projectRelFolder;
        std::string relTarget = JoinProjectRel(destination, baseName + ".nprefab");
        std::string absTarget = AssetManager::Get().ToAbsolute(relTarget);
        int counter = 1;
        while (fs::exists(PathUtf8::FromString(absTarget))) {
            relTarget = JoinProjectRel(
                destination, baseName + "_" + std::to_string(counter++) + ".nprefab");
            absTarget = AssetManager::Get().ToAbsolute(relTarget);
        }

        std::string err;
        if (!SceneIO::SaveEntityToFile(*target, scene, absTarget, err)) {
            m_LastError = "prefab save failed: " + err;
            continue;
        }
        target->SetPrefabSourcePath(absTarget);
        AssetThumbnail::Get().DropPrefabThumbnail(relTarget);
        MIPSYNC_INFO("Prefab saved: {}", absTarget);
        lastSaved = relTarget;
        ++savedCount;
    }

    if (savedCount > 0) {
        m_LastError.clear();
        m_LastInfo = savedCount == 1
            ? "Saved prefab: " + lastSaved
            : "Saved " + std::to_string(savedCount) + " prefabs";
        RevealAndSelectAsset(lastSaved);
    } else if (m_LastError.empty()) {
        m_LastError = "dropped entity no longer exists";
    }
}

std::vector<std::string> AssetBrowserPanel::FilterTopLevelPaths(
    const std::vector<std::string>& paths) const {
    std::vector<std::string> result;
    for (const std::string& path : paths) {
        bool nested = false;
        for (const std::string& other : paths) {
            if (path != other && IsPathUnderFolder(other, path)) {
                nested = true;
                break;
            }
        }
        if (!nested)
            result.push_back(path);
    }
    return result;
}

std::vector<std::string> AssetBrowserPanel::GetDragMovePaths(const AssetEntry& entry) const {
    std::vector<std::string> paths;
    if (IsAssetSelected(entry.projectRelPath)) {
        for (const auto& sel : m_SelectedAssets) {
            if (sel.kind == AssetKind::AnimationClip)
                continue;
            paths.push_back(sel.projectRelPath);
        }
    } else if (!entry.isSubAsset) {
        paths.push_back(entry.projectRelPath);
    }
    return FilterTopLevelPaths(paths);
}

bool AssetBrowserPanel::MoveAssetToFolder(const std::string& srcProjectRel,
                                          const std::string& destFolderRel,
                                          std::string& outError) {
    if (srcProjectRel.empty() || srcProjectRel == kAssetsRoot || srcProjectRel == kScenesRoot) {
        outError = "cannot move a project root folder";
        return false;
    }

    const std::string src = NormalizeProjectRel(srcProjectRel);
    const std::string dest = NormalizeProjectRel(destFolderRel);

    // Dropped onto the same folder (e.g. folder tile or tree node) — ignore quietly.
    if (src == dest) {
        outError.clear();
        return true;
    }

    if (IsStrictSubfolderOf(src, dest)) {
        outError = "cannot move a folder into its subfolder";
        return false;
    }

    const std::string srcParent = ParentProjectRel(src);
    if (dest == NormalizeProjectRel(srcParent)) {
        outError.clear();
        return true;
    }

    const fs::path srcAbs = PathUtf8::FromString(AssetManager::Get().ToAbsolute(src));
    const fs::path destDir = PathUtf8::FromString(AssetManager::Get().ToAbsolute(dest));
    const fs::path destAbs = destDir / srcAbs.filename();

    std::error_code ec;
    if (!fs::exists(srcAbs, ec)) {
        outError = "not found";
        return false;
    }
    if (fs::exists(destAbs, ec)) {
        outError = "destination already exists";
        return false;
    }

    fs::create_directories(destDir, ec);
    fs::rename(srcAbs, destAbs, ec);
    if (ec) {
        outError = ec.message();
        return false;
    }

  // Invalidate caches for file assets (old path).
    AssetKind kind = ClassifyAssetByPath(src);
    if (kind == AssetKind::Texture)
        AssetManager::Get().DropTexture(src);
    else if (kind == AssetKind::Model) {
        AssetManager::Get().DropMesh(src);
        AssetThumbnail::Get().DropMeshThumbnail(src);
    }

    return true;
}

void AssetBrowserPanel::MoveAssetsToFolder(const std::vector<std::string>& srcPaths,
                                           const std::string& destFolderRel) {
    const std::vector<std::string> filtered = FilterTopLevelPaths(srcPaths);
    if (filtered.empty())
        return;

    int moved = 0;
    int skipped = 0;
    std::string lastErr;
    for (const std::string& src : filtered) {
        const std::string srcNorm = NormalizeProjectRel(src);
        const std::string destNorm = NormalizeProjectRel(destFolderRel);
        const std::string srcParent = ParentProjectRel(srcNorm);
        if (srcNorm == destNorm || destNorm == NormalizeProjectRel(srcParent)) {
            ++skipped;
            continue;
        }

        std::string err;
        if (MoveAssetToFolder(src, destFolderRel, err))
            ++moved;
        else if (!err.empty())
            lastErr = err;
    }

    if (moved > 0) {
        m_LastInfo = "Moved " + std::to_string(moved) + " item(s)";
        m_LastError.clear();
        ClearAssetSelection();
        RescanFolderTree();
        Refresh();
    } else if (!lastErr.empty() && skipped == 0) {
        m_LastError = lastErr;
    } else {
        m_LastError.clear();
    }
}

void AssetBrowserPanel::HandleAssetMoveDropOnFolder(const std::string& projectRelFolder) {
    const ImGuiPayload* payload =
        ImGui::AcceptDragDropPayload(DragDrop::kAssetMove);
    if (!payload)
        return;

    const std::vector<std::string> paths =
        DragDrop::ParseAssetMovePayload(payload->Data, payload->DataSize);
    if (paths.empty())
        return;

    MoveAssetsToFolder(paths, projectRelFolder);
}

void AssetBrowserPanel::HandleFolderDropTargets(const std::string& projectRelFolder) {
    HandleEntityDropOnFolder(projectRelFolder);
    HandleAssetMoveDropOnFolder(projectRelFolder);
}

void AssetBrowserPanel::HandleCurrentFolderDropTarget() {
    if (!ImGui::BeginDragDropTarget())
        return;
    HandleFolderDropTargets(m_CurrentFolder);
    ImGui::EndDragDropTarget();
}

void AssetBrowserPanel::BeginAssetMoveDragSource(const AssetEntry& entry) {
    const std::vector<std::string> paths = GetDragMovePaths(entry);
    if (paths.empty())
        return;

    const std::string payload = DragDrop::BuildAssetMovePayload(paths);
    if (payload.empty())
        return;

    if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        return;

    ImGui::SetDragDropPayload(DragDrop::kAssetMove, payload.data(),
                              static_cast<int>(payload.size()));

    if (paths.size() == 1)
        ImGui::TextUnformatted(entry.name.c_str());
    else
        ImGui::Text("Move %zu items", paths.size());

    ImGui::EndDragDropSource();
}

void AssetBrowserPanel::BeginAssetDragSource(const AssetEntry& entry) {
    if (entry.kind == AssetKind::AnimationClip && !entry.parentModelPath.empty() &&
        !entry.clipName.empty()) {
        if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            return;
        const std::string payload = DragDrop::BuildAnimClipPayload(
            NormalizeProjectRel(entry.parentModelPath), entry.clipName, entry.clipStackIndex);
        ImGui::SetDragDropPayload(DragDrop::kAssetAnimClip, payload.data(),
                                  static_cast<int>(payload.size()), ImGuiCond_Once);
        ImGui::TextUnformatted(entry.clipName.c_str());
        ImGui::EndDragDropSource();
        return;
    }

    if (!entry.isSubAsset)
        BeginAssetMoveDragSource(entry);
}

void AssetBrowserPanel::DrawPanelContextMenu() {
    if (!ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_MouseButtonRight))
        return;

    if (ImGui::MenuItem("Refresh"))
        Refresh();
    if (ImGui::MenuItem("New Folder..."))
        m_OpenNewFolderDialog = true;
    if (ImGui::MenuItem("New Scene..."))
        m_OpenNewSceneDialog = true;
    if (ImGui::MenuItem("New Material..."))
        m_OpenNewMaterialDialog = true;
    if (ImGui::MenuItem("New Animator Controller..."))
        m_OpenNewAnimatorControllerDialog = true;
    if (ImGui::MenuItem("New Script..."))
        m_OpenNewScriptDialog = true;
#ifdef _WIN32
    if (ImGui::MenuItem("Import..."))
        m_OpenImportDialog = true;
#endif
    ImGui::Separator();
    if (ImGui::MenuItem("Reveal in Explorer")) {
        const std::string abs =
            AssetManager::Get().ToAbsolute(NormalizeContentFolder(m_CurrentFolder));
        RevealFolderInOS(abs);
    }

    if (!m_SelectedAssets.empty()) {
        ImGui::Separator();
        if (m_SelectedAssets.size() == 1 && ImGui::MenuItem("Rename Selected..."))
            OpenRenameDialogForSelection();
        if (ImGui::MenuItem("Delete Selected"))
            DeleteSelectedAssets();
    }

    ImGui::EndPopup();
}

void AssetBrowserPanel::DrawFolderTreeNode(const std::string& projectRelPath, const std::string& displayName) {
    const std::string& root = AssetManager::Get().GetProjectRoot();
    fs::path rootPath = PathUtf8::FromString(root);
    fs::path abs = projectRelPath.empty() ? rootPath : rootPath / PathUtf8::FromString(projectRelPath);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (m_CurrentFolder == projectRelPath)
        flags |= ImGuiTreeNodeFlags_Selected;

    if (m_ExpandedTreeFolders.count(projectRelPath) > 0)
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);

    bool hasChildren = false;
    std::error_code ec;
    if (fs::exists(abs, ec) && fs::is_directory(abs, ec)) {
        for (const auto& sub : fs::directory_iterator(abs, ec)) {
            if (sub.is_directory()) { hasChildren = true; break; }
        }
    }
    if (!hasChildren)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    bool open = ImGui::TreeNodeEx(projectRelPath.empty() ? "(root)" : projectRelPath.c_str(),
                                   flags, "%s", displayName.c_str());

    if (open)
        m_ExpandedTreeFolders.insert(projectRelPath);
    else if (ImGui::IsItemToggledOpen())
        m_ExpandedTreeFolders.erase(projectRelPath);

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        SetCurrentFolder(projectRelPath);

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        std::vector<std::string> paths;
        if (IsAssetSelected(projectRelPath)) {
            for (const auto& sel : m_SelectedAssets)
                paths.push_back(sel.projectRelPath);
        } else {
            paths.push_back(projectRelPath);
        }
        paths = FilterTopLevelPaths(paths);
        const std::string payload = DragDrop::BuildAssetMovePayload(paths);
        if (!payload.empty()) {
            ImGui::SetDragDropPayload(DragDrop::kAssetMove, payload.data(),
                                      static_cast<int>(payload.size()));
            ImGui::TextUnformatted(displayName.c_str());
        }
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginDragDropTarget()) {
        HandleFolderDropTargets(projectRelPath);
        ImGui::EndDragDropTarget();
    }

    if (open && hasChildren) {
        std::vector<std::string> subdirs;
        for (const auto& sub : fs::directory_iterator(abs, ec)) {
            if (!sub.is_directory()) continue;
            std::string name;
            try { name = PathUtf8::ToString(sub.path().filename()); }
            catch (const std::exception&) { continue; }
            if (name.empty() || name[0] == '.') continue;
            subdirs.push_back(name);
        }
        std::sort(subdirs.begin(), subdirs.end());
        for (const auto& sub : subdirs) {
            const std::string childRel = JoinProjectRel(projectRelPath, sub);
            DrawFolderTreeNode(childRel, sub);
        }
        ImGui::TreePop();
    }
}

void AssetBrowserPanel::DrawFolderTree() {
    for (const auto& topLevel : m_FolderTreeRoots) {
        const char* label = topLevel.c_str();
        if (topLevel == kAssetsRoot)
            label = "Assets";
        else if (topLevel == kScenesRoot)
            label = "Scenes";
        DrawFolderTreeNode(topLevel, label);
    }
    DrawPanelContextMenu();
}

void AssetBrowserPanel::DrawAssetThumbnail(ImDrawList* dl, const AssetEntry& entry,
                                            ImVec2 cellPos, float thumb) {
    const ImVec2 cellMax(cellPos.x + thumb, cellPos.y + thumb);

    auto drawTex = [&](const std::shared_ptr<Texture>& tex) {
        if (!tex || tex->GetID() == 0) return false;
        DrawTexturedImageAspectFit(dl, *tex, cellPos, cellMax);
        return true;
    };

    auto drawKindIconOrLabel = [&](AssetKind kind) {
        ImVec2 iconMin = cellPos;
        ImVec2 iconMax = cellMax;
        const bool useCompactIcon =
            kind == AssetKind::AnimationClip ||
            kind == AssetKind::AnimatorController ||
            kind == AssetKind::Scene ||
            kind == AssetKind::Script;
        if (useCompactIcon) {
            constexpr float kIconScale = 0.65f;
            const float inset = thumb * (1.0f - kIconScale) * 0.5f;
            iconMin = ImVec2(cellPos.x + inset, cellPos.y + inset);
            iconMax = ImVec2(cellMax.x - inset, cellMax.y - inset);
        }

        if (kind == AssetKind::Folder) {
            // Keep the Project browser aligned with the bundled art direction.
            // The vector icon is only a fallback for an incomplete install.
            if (!TryDrawProjectAssetIcon(dl, AssetKind::Folder, cellPos, cellMax))
                DrawFolderIcon(dl, cellPos, cellMax);
            return;
        }
        if (TryDrawProjectAssetIcon(dl, kind, iconMin, iconMax, entry.projectRelPath))
            return;
        if (kind == AssetKind::Script) {
            DrawScriptIcon(dl, iconMin, iconMax);
            return;
        }
        const char* kindStr = AssetKindLabel(kind);
        const ImVec2 ts = ImGui::CalcTextSize(kindStr);
        dl->AddText(ImVec2(cellPos.x + (thumb - ts.x) * 0.5f, cellPos.y + (thumb - ts.y) * 0.5f),
                    ImGui::ColorConvertFloat4ToU32(AssetKindColor(kind)), kindStr);
    };

    if (entry.isDirectory || entry.kind == AssetKind::Folder) {
        drawKindIconOrLabel(AssetKind::Folder);
        return;
    }

    if (entry.kind == AssetKind::Script) {
        drawKindIconOrLabel(AssetKind::Script);
        return;
    }

    if (entry.kind == AssetKind::AnimationClip) {
        drawKindIconOrLabel(AssetKind::AnimationClip);
        return;
    }

    if (entry.kind == AssetKind::Model && m_Editor && m_Editor->GetEngine()) {
        if (drawTex(AssetThumbnail::Get().GetMeshThumbnail(entry.projectRelPath,
                                                           m_Editor->GetEngine()->GetRenderer())))
            return;
        drawKindIconOrLabel(AssetKind::Model);
        return;
    }

    if (entry.kind == AssetKind::Texture) {
        if (drawTex(AssetManager::Get().GetTexture(entry.projectRelPath)))
            return;
        drawKindIconOrLabel(AssetKind::Texture);
        return;
    }

    if (entry.kind == AssetKind::Material && m_Editor && m_Editor->GetEngine()) {
        if (drawTex(AssetThumbnail::Get().GetMaterialThumbnail(entry.projectRelPath,
                                                               m_Editor->GetEngine()->GetRenderer())))
            return;
        drawKindIconOrLabel(AssetKind::Material);
        return;
    }

    if (entry.kind == AssetKind::Audio) {
        if (drawTex(AssetThumbnail::Get().GetAudioThumbnail(entry.projectRelPath)))
            return;
        drawKindIconOrLabel(AssetKind::Audio);
        return;
    }

    if (entry.kind == AssetKind::Prefab) {
        if (m_Editor && m_Editor->GetEngine())
            drawTex(AssetThumbnail::Get().GetPrefabThumbnail(
                entry.projectRelPath, m_Editor->GetEngine()->GetRenderer()));
        // A prefab with no referenced 3D model intentionally shows only the tile background.
        return;
    }

    drawKindIconOrLabel(entry.kind);
}

namespace {

bool PointInScreenRect(const ImVec2& p, const ImVec2& min, const ImVec2& max) {
    return p.x >= min.x && p.y >= min.y && p.x < max.x && p.y < max.y;
}

} // namespace

void AssetBrowserPanel::DrawFileGridCell(const AssetEntry& entry, size_t displayIndex,
                                         float cellThumb, int& col, int columns) {
    ImGui::PushID(entry.projectRelPath.c_str());

    if (entry.isSubAsset && col > 0)
        col = 0;
    if (entry.isSubAsset) {
        ImGui::Dummy(ImVec2(20.0f, 0.0f));
        ImGui::SameLine(0.0f, 4.0f);
    }

    ImGui::BeginGroup();

    const ImVec2 cellPos = ImGui::GetCursorScreenPos();
    const bool isSelected = IsAssetSelected(entry.projectRelPath);
    const ImVec2 cellMax(cellPos.x + cellThumb, cellPos.y + cellThumb);

    const bool isModel = entry.kind == AssetKind::Model && !entry.isDirectory;
    const std::string modelKey = NormalizeProjectRel(entry.projectRelPath);

    std::shared_ptr<SkeletalModelAsset> skelModel;
    const bool showExpand = isModel;
    const bool modelExpanded = showExpand && m_ExpandedModels.count(modelKey) > 0;
    // Loading FBX just to draw a grid cell is extremely expensive.
    // Only load skeletal model data when the user expands (requests clip list).
    if (isModel && modelExpanded)
        skelModel = AssetManager::Get().GetSkeletalModel(entry.projectRelPath);

    const bool hasAnimClips = skelModel && !skelModel->animationNames.empty();

    constexpr float kExpandHit = 20.0f;
    const ImVec2 expandMax(cellPos.x + kExpandHit, cellPos.y + kExpandHit);

    ImGuiIO& io = ImGui::GetIO();
    bool expandActivated = false;
    if (showExpand && io.MouseClicked[ImGuiMouseButton_Left] &&
        PointInScreenRect(io.MouseClickedPos[ImGuiMouseButton_Left], cellPos, expandMax)) {
        expandActivated = true;
        if (modelExpanded)
            m_ExpandedModels.erase(modelKey);
        else
            m_ExpandedModels.insert(modelKey);

        if (!skelModel && !modelExpanded) {
            // We are about to expand; load now so the info text is meaningful.
            skelModel = AssetManager::Get().GetSkeletalModel(entry.projectRelPath);
        }

        if (!hasAnimClips) {
            m_LastInfo = skelModel ? "No animation clips in this model"
                                   : "Could not load model for clip list";
        } else {
            m_LastInfo = "Animation clips: " + std::to_string(skelModel->animationNames.size());
        }
        m_LastError.clear();
    }

    const bool clickStartedInExpand =
        showExpand &&
        PointInScreenRect(io.MouseClickedPos[ImGuiMouseButton_Left], cellPos, expandMax);

    ImGui::SetCursorScreenPos(cellPos);
    ImGui::Selectable("##cell", isSelected, ImGuiSelectableFlags_AllowDoubleClick,
                      ImVec2(cellThumb, cellThumb));
    const bool cellHovered = ImGui::IsItemHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    DrawAssetThumbnail(dl, entry, cellPos, cellThumb);

    if (showExpand) {
        const bool expandHot =
            PointInScreenRect(io.MousePos, cellPos, expandMax) && ImGui::IsWindowHovered();
        const ImVec2 triCenter(cellPos.x + kExpandHit * 0.5f, cellPos.y + kExpandHit * 0.5f);
        const float tri = 4.0f;
        const ImU32 triCol = ImGui::ColorConvertFloat4ToU32(
            expandHot || expandActivated ? EditorTheme::TextPrimary : EditorTheme::TextSecondary);
        if (modelExpanded) {
            dl->AddTriangleFilled(ImVec2(triCenter.x - tri, triCenter.y - tri * 0.5f),
                                  ImVec2(triCenter.x + tri, triCenter.y - tri * 0.5f),
                                  ImVec2(triCenter.x, triCenter.y + tri * 0.8f), triCol);
        } else {
            dl->AddTriangleFilled(ImVec2(triCenter.x - tri * 0.6f, triCenter.y - tri),
                                  ImVec2(triCenter.x - tri * 0.6f, triCenter.y + tri),
                                  ImVec2(triCenter.x + tri * 0.9f, triCenter.y), triCol);
        }
    }

    if (isSelected) {
        const ImU32 sel = ImGui::ColorConvertFloat4ToU32(EditorTheme::Selection);
        dl->AddRect(cellPos, cellMax, sel, 0.0f, 0, 2.5f);
    }

    if (!expandActivated && !clickStartedInExpand && entry.isDirectory &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && cellHovered) {
        SetCurrentFolder(entry.projectRelPath);
    } else if (!expandActivated && !clickStartedInExpand &&
               entry.kind == AssetKind::AnimatorController &&
               ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && cellHovered) {
        HandleAssetClickSelection(entry, displayIndex);
        if (m_Editor)
            m_Editor->OpenAnimatorControllerWindow(entry.projectRelPath);
    } else if (!expandActivated && !clickStartedInExpand &&
               entry.kind == AssetKind::AnimationClip && !entry.isSubAsset &&
               ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && cellHovered) {
        HandleAssetClickSelection(entry, displayIndex);
        if (m_Editor)
            m_Editor->OpenAnimationClipWindow(entry.projectRelPath);
    } else if (!expandActivated && !clickStartedInExpand &&
               entry.kind == AssetKind::Scene &&
               ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && cellHovered) {
        HandleAssetClickSelection(entry, displayIndex);
        if (m_Editor)
            m_Editor->LoadSceneFromPath(entry.absolutePath);
    } else if (!expandActivated && !clickStartedInExpand && cellHovered &&
               io.MouseReleased[ImGuiMouseButton_Left] &&
               !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        const ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
        constexpr float kSelectDragThreshold = 6.0f;
        if (drag.x * drag.x + drag.y * drag.y < kSelectDragThreshold * kSelectDragThreshold)
            HandleAssetClickSelection(entry, displayIndex);
    }

    BeginAssetDragSource(entry);

    if (entry.isDirectory && ImGui::BeginDragDropTarget()) {
        HandleFolderDropTargets(entry.projectRelPath);
        ImGui::EndDragDropTarget();
    }

    DrawAssetContextMenu(entry, displayIndex);

    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + cellThumb);
    if (entry.isSubAsset)
        ImGui::TextColored(EditorTheme::TextMuted, "%s", entry.name.c_str());
    else
        ImGui::TextWrapped("%s", entry.name.c_str());
    ImGui::PopTextWrapPos();

    ImGui::EndGroup();
    ImGui::PopID();

    if (++col < columns)
        ImGui::SameLine(0.0f, 8.0f);
    else
        col = 0;
}

void AssetBrowserPanel::DrawFileGrid() {
    const float thumb = 96.0f;
    const float cellW = thumb + 8.0f;
    const float available = ImGui::GetContentRegionAvail().x;
    const int columns = std::max(1, static_cast<int>(available / cellW));

    const std::string folder = NormalizeContentFolder(m_CurrentFolder);
    const std::string display = DisplayFolderPath(folder);
    const std::string root = ContentRootFor(folder);
    const char* rootLabel = (root == kScenesRoot) ? "Scenes" : "Assets";
    ImGui::TextColored(EditorTheme::TextSecondary, "%s/%s", rootLabel, display.c_str());
    if (!m_LastError.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(EditorTheme::Error, "— %s", m_LastError.c_str());
    } else if (!m_LastInfo.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(EditorTheme::SuccessLabel, "— %s", m_LastInfo.c_str());
    }

    if (!IsContentRoot(folder)) {
        ImGui::SameLine();
        if (ImGui::SmallButton("..")) {
            SetCurrentFolder(ParentProjectRel(folder));
        }
    }

    ImGui::Separator();

    if (m_Entries.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(EditorTheme::TextMuted, "Empty folder.");
        HandleGridKeyboardShortcuts();
        DrawPanelContextMenu();
        return;
    }

    const float clipThumb = thumb * 0.82f;
    int col = 0;
    size_t displayIndex = 0;

    for (const AssetEntry& entry : m_Entries) {
        DrawFileGridCell(entry, displayIndex++, thumb, col, columns);

        if (entry.kind != AssetKind::Model || entry.isDirectory)
            continue;

        const std::string modelKey = NormalizeProjectRel(entry.projectRelPath);
        if (m_ExpandedModels.find(modelKey) == m_ExpandedModels.end())
            continue;

        const auto model = AssetManager::Get().GetSkeletalModel(entry.projectRelPath);
        if (!model || model->animationNames.empty())
            continue;

        if (col != 0)
            col = 0;

        for (size_t ci = 0; ci < model->animationNames.size(); ++ci) {
            const std::string& clip = model->animationNames[ci];
            const int stackIdx = static_cast<int>(model->animationStackIndices[ci]);
            AssetEntry clipEntry{};
            clipEntry.name = clip;
            clipEntry.projectRelPath =
                MakeAnimationClipVirtualPath(entry.projectRelPath, clip, stackIdx);
            clipEntry.kind = AssetKind::AnimationClip;
            clipEntry.isSubAsset = true;
            clipEntry.parentModelPath = entry.projectRelPath;
            clipEntry.clipName = clip;
            clipEntry.clipStackIndex = stackIdx;
            DrawFileGridCell(clipEntry, displayIndex++, clipThumb, col, columns);
        }
    }

    {
        const ImVec2 remaining = ImGui::GetContentRegionAvail();
        if (remaining.x > 2.0f && remaining.y > 2.0f) {
            ImGui::InvisibleButton("##gridmovedrop", remaining);
            if (ImGui::BeginDragDropTarget()) {
                HandleAssetMoveDropOnFolder(folder);
                ImGui::EndDragDropTarget();
            }
        }
    }

    if (ImGui::BeginDragDropTarget()) {
        HandleAssetMoveDropOnFolder(folder);
        ImGui::EndDragDropTarget();
    }

    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsAnyItemHovered())
        ClearAssetSelection();

    HandleGridKeyboardShortcuts();
    DrawPanelContextMenu();
}

void AssetBrowserPanel::DrawNewScriptDialog() {
    if (m_OpenNewScriptDialog) {
        ImGui::OpenPopup("New Script");
        m_OpenNewScriptDialog = false;
    }
    ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("New Script", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(EditorTheme::TextPrimary, "Create a new .mips script");
        ImGui::Spacing();
        ImGui::PushItemWidth(-1.0f);
        ImGui::InputText("##scriptname", m_NewScriptName, sizeof(m_NewScriptName));
        ImGui::PopItemWidth();
        ImGui::Spacing();

        const float bw = 100.0f;
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - bw * 2.0f -
                              ImGui::GetStyle().ItemSpacing.x);
        if (EditorTheme::AeroButton("Cancel", ImVec2(bw, EditorTheme::ButtonHeight), AeroButtonKind::Secondary))
            ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (EditorTheme::AeroButton("Create", ImVec2(bw, EditorTheme::ButtonHeight), AeroButtonKind::Primary)) {
            std::string name = m_NewScriptName;
            if (name.find(".mips") == std::string::npos)
                name += ".mips";

            std::string className = name;
            const size_t dot = className.rfind('.');
            if (dot != std::string::npos)
                className = className.substr(0, dot);

            std::string targetSub = NormalizeContentFolder(m_CurrentFolder);
            if (targetSub == kAssetsRoot)
                targetSub = "assets/scripts";
            const std::string rel = JoinProjectRel(targetSub, name);
            const std::string abs = AssetManager::Get().ToAbsolute(rel);

            const std::string body =
                "class " + className + " : MipsBehaviour\n"
                "{\n"
                "    void Start()\n"
                "    {\n"
                "    }\n"
                "\n"
                "    void Update()\n"
                "    {\n"
                "    }\n"
                "}\n";

            std::ofstream file(abs);
            if (file.is_open()) {
                file << body;
                m_LastInfo = "Created: " + rel;
                Refresh();
                ImGui::CloseCurrentPopup();
            } else {
                m_LastError = "Failed to write: " + abs;
            }
        }
        ImGui::EndPopup();
    }
}

void AssetBrowserPanel::DrawNewAnimatorControllerDialog() {
    if (m_OpenNewAnimatorControllerDialog) {
        ImGui::OpenPopup("New Animator Controller");
        m_OpenNewAnimatorControllerDialog = false;
    }
    ImGui::SetNextWindowSize(ImVec2(400.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("New Animator Controller", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(EditorTheme::TextPrimary, "Create a new .ncontroller asset");
        ImGui::Spacing();
        ImGui::PushItemWidth(-1.0f);
        ImGui::InputText("Name", m_NewControllerName, sizeof(m_NewControllerName));
        EditorTheme::Checkbox("Generate states from FBX clips", &m_NewControllerFromModel);
        if (m_NewControllerFromModel) {
            ImGui::InputText("Source model (project path)", m_NewControllerModelPath,
                             sizeof(m_NewControllerModelPath));
            ImGui::TextColored(EditorTheme::TextMuted,
                                "e.g. assets/models/Capoeira.fbx — clip names must match the FBX.");
        }
        ImGui::PopItemWidth();
        ImGui::Spacing();

        const float bw = 100.0f;
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - bw * 2.0f -
                              ImGui::GetStyle().ItemSpacing.x);
        if (EditorTheme::AeroButton("Cancel", ImVec2(bw, EditorTheme::ButtonHeight), AeroButtonKind::Secondary))
            ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (EditorTheme::AeroButton("Create", ImVec2(bw, EditorTheme::ButtonHeight), AeroButtonKind::Primary)) {
            std::string name = m_NewControllerName;
            if (name.find(".ncontroller") == std::string::npos)
                name += ".ncontroller";
            std::string targetSub = NormalizeContentFolder(m_CurrentFolder);
            if (targetSub == kAssetsRoot)
                targetSub = "assets/animations";
            const std::string rel = JoinProjectRel(targetSub, name);
            const std::string abs = AssetManager::Get().ToAbsolute(rel);

            std::shared_ptr<AnimatorControllerAsset> asset;
            if (m_NewControllerFromModel && m_NewControllerModelPath[0] != '\0') {
                auto model = AssetManager::Get().GetSkeletalModel(m_NewControllerModelPath);
                if (model && !model->animationNames.empty())
                    asset = CreateDefaultControllerForModel(*model);
                else
                    m_LastError = "Could not read clips from model (check path / reload FBX).";
            }
            if (!asset)
                asset = CreateEmptyController();

            std::string err;
            if (SaveAnimatorController(abs, *asset, err)) {
                m_LastInfo = "Created: " + rel;
                Refresh();
                AssetEntry created{};
                created.projectRelPath = rel;
                created.kind = AssetKind::AnimatorController;
                SelectAssetOnly(created, SIZE_MAX);
                if (m_Editor)
                    m_Editor->OpenAnimatorControllerWindow(rel);
                ImGui::CloseCurrentPopup();
            } else if (m_LastError.empty()) {
                m_LastError = err;
            }
        }
        ImGui::EndPopup();
    }
}

void AssetBrowserPanel::DrawNewMaterialDialog() {
    if (m_OpenNewMaterialDialog) {
        ImGui::OpenPopup("New Material");
        m_OpenNewMaterialDialog = false;
    }
    ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("New Material", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(EditorTheme::TextPrimary, "Create a new .nmat asset");
        ImGui::Spacing();
        ImGui::PushItemWidth(-1.0f);
        ImGui::InputText("##matname", m_NewMaterialName, sizeof(m_NewMaterialName));
        ImGui::PopItemWidth();
        ImGui::Spacing();

        const float bw = 100.0f;
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - bw * 2.0f -
                              ImGui::GetStyle().ItemSpacing.x);
        if (EditorTheme::AeroButton("Cancel", ImVec2(bw, EditorTheme::ButtonHeight), AeroButtonKind::Secondary))
            ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (EditorTheme::AeroButton("Create", ImVec2(bw, EditorTheme::ButtonHeight), AeroButtonKind::Primary)) {
            std::string name = m_NewMaterialName;
            if (name.find(".nmat") == std::string::npos) name += ".nmat";
            std::string targetSub = NormalizeContentFolder(m_CurrentFolder);
            if (targetSub == kAssetsRoot)
                targetSub = "assets/materials";
            std::string rel = JoinProjectRel(targetSub, name);
            std::string abs = AssetManager::Get().ToAbsolute(rel);
            Material mat;
            std::string err;
            if (Material::Save(abs, mat, err)) {
                m_LastInfo = "Created: " + rel;
                Refresh();
                ImGui::CloseCurrentPopup();
            } else {
                m_LastError = err;
            }
        }
        ImGui::EndPopup();
    }
}

void AssetBrowserPanel::DrawNewFolderDialog() {
    if (m_OpenNewFolderDialog) {
        ImGui::OpenPopup("New Folder");
        m_OpenNewFolderDialog = false;
    }
    ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("New Folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(EditorTheme::TextPrimary, "Folder name");
        ImGui::Spacing();
        ImGui::PushItemWidth(-1.0f);
        ImGui::InputText("##fname", m_NewFolderName, sizeof(m_NewFolderName));
        ImGui::PopItemWidth();
        ImGui::Spacing();

        const float bw = 100.0f;
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - bw * 2.0f -
                              ImGui::GetStyle().ItemSpacing.x);
        if (EditorTheme::AeroButton("Cancel", ImVec2(bw, EditorTheme::ButtonHeight), AeroButtonKind::Secondary))
            ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (EditorTheme::AeroButton("Create", ImVec2(bw, EditorTheme::ButtonHeight), AeroButtonKind::Primary)) {
            std::string parentRel = NormalizeContentFolder(m_CurrentFolder);
            std::string rel = JoinProjectRel(parentRel, m_NewFolderName);
            std::string abs = AssetManager::Get().ToAbsolute(rel);
            std::error_code ec;
            fs::create_directories(PathUtf8::FromString(abs), ec);
            if (ec)
                m_LastError = "mkdir failed: " + ec.message();
            else {
                m_LastInfo = "Created: " + rel;
                RescanFolderTree();
                Refresh();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}

void AssetBrowserPanel::DrawNewSceneDialog() {
    if (m_OpenNewSceneDialog) {
        ImGui::OpenPopup("New Scene");
        m_OpenNewSceneDialog = false;
    }
    ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("New Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(EditorTheme::TextPrimary, "Create a new .nscene asset");
        ImGui::Spacing();
        ImGui::PushItemWidth(-1.0f);
        ImGui::InputText("##scenename", m_NewSceneName, sizeof(m_NewSceneName));
        ImGui::PopItemWidth();
        ImGui::Spacing();

        const float bw = 100.0f;
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - bw * 2.0f -
                              ImGui::GetStyle().ItemSpacing.x);
        if (EditorTheme::AeroButton("Cancel", ImVec2(bw, EditorTheme::ButtonHeight), AeroButtonKind::Secondary))
            ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (EditorTheme::AeroButton("Create", ImVec2(bw, EditorTheme::ButtonHeight), AeroButtonKind::Primary)) {
            std::string name = m_NewSceneName;
            if (name.find(".nscene") == std::string::npos) name += ".nscene";
            std::string targetSub = NormalizeContentFolder(m_CurrentFolder);
            if (targetSub == kAssetsRoot || targetSub == kScenesRoot)
                targetSub = "scenes";
            std::string rel = JoinProjectRel(targetSub, name);
            std::string abs = AssetManager::Get().ToAbsolute(rel);

            Scene sc;
            Entity* cam = sc.CreateEntity("Main Camera");
            auto& cc = cam->AddComponent<CameraComponent>();
            cc.camera.fov = 60.0f;
            cc.camera.nearClip = 0.1f;
            cc.camera.farClip = 100.0f;
            cc.primary = true;
            cam->GetComponent<TransformComponent>()->position = { 0.0f, 2.0f, 6.0f };

            std::string err;
            if (SceneIO::SaveToFile(sc, abs, err)) {
                m_LastInfo = "Created: " + rel;
                Refresh();
                ImGui::CloseCurrentPopup();
            } else {
                m_LastError = err;
            }
        }
        ImGui::EndPopup();
    }
}

void AssetBrowserPanel::OnImGuiRender() {
    if (m_FolderTreeRoots.empty())
        RescanFolderTree();

    if (m_ScanRequested) {
        RescanFolder();
        m_ScanRequested = false;
    }

    ImGui::Begin("Project");

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    constexpr float kMinGridW = 96.0f;
    constexpr float kMinTreeW = 72.0f;
    constexpr float kPreferTreeW = 200.0f;
    constexpr float kStackMinH = 200.0f;

    const bool stackVertically =
        avail.x < kMinTreeW + kMinGridW + ImGui::GetStyle().ItemSpacing.x &&
        avail.y >= kStackMinH;

    if (stackVertically) {
        const float treeH = std::clamp(avail.y * 0.36f, 100.0f, 200.0f);
        ImGui::BeginChild("##tree", ImVec2(-1.0f, treeH), true);
        DrawFolderTree();
        ImGui::EndChild();

        ImGui::BeginChild("##grid", ImVec2(-1.0f, 0.0f), true);
        DrawFileGrid();
        ImGui::EndChild();
        HandleCurrentFolderDropTarget();
    } else {
        float treeW = std::min(kPreferTreeW, avail.x * 0.38f);
        treeW = std::clamp(treeW, kMinTreeW, std::max(kMinTreeW, avail.x - kMinGridW));

        ImGui::BeginChild("##tree", ImVec2(treeW, 0.0f), true);
        DrawFolderTree();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##grid", ImVec2(0.0f, 0.0f), true);
        DrawFileGrid();
        ImGui::EndChild();
        HandleCurrentFolderDropTarget();
    }

    DrawNewMaterialDialog();
    DrawNewAnimatorControllerDialog();
    DrawNewScriptDialog();
    DrawNewFolderDialog();
    DrawNewSceneDialog();
    DrawRenameDialog();

    if (!OsFileDrop::Peek().empty() &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) {
        const ImVec2 pos = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        ImGui::GetWindowDrawList()->AddRect(
            pos, ImVec2(pos.x + size.x, pos.y + size.y),
            ImGui::ColorConvertFloat4ToU32(EditorTheme::Selection), 0.0f, 0, 2.0f);
        ImGui::SetCursorPos(ImVec2(8.0f, size.y - 24.0f));
        ImGui::TextColored(EditorTheme::Selection, "Drop files or folders to import");
    }

    ProcessOsFileDrop();

    ImGui::End();

    // Run OS file dialog AFTER the panel's End() so we are not nested in any
    // BeginChild / drag-drop scope when the modal Win32 message pump runs.
    RunPendingImportDialog();
}

namespace {

bool ShouldSkipImportPath(const fs::path& path) {
    for (const auto& part : path) {
        const std::string name = PathUtf8::ToString(part);
        if (!name.empty() && name[0] == '.')
            return true;
    }
    return false;
}

bool ImportExternalFileImpl(const fs::path& srcFile, const fs::path& destDir,
                            std::string& outError, fs::path* outImportedPath = nullptr) {
    std::error_code ec;
    fs::create_directories(destDir, ec);

    fs::path dest = destDir / srcFile.filename();
    int counter = 1;
    const std::string stem = PathUtf8::ToString(srcFile.stem());
    const std::string ext  = PathUtf8::ToString(srcFile.extension());
    while (fs::exists(dest, ec) && !ec) {
        dest = destDir / PathUtf8::FromString(stem + "_" + std::to_string(counter) + ext);
        ++counter;
    }

    fs::copy_file(srcFile, dest, fs::copy_options::none, ec);
    if (ec) {
        outError = ec.message();
        return false;
    }
    if (outImportedPath)
        *outImportedPath = dest;
    return true;
}

bool IsFbxPath(const fs::path& path) {
    std::string ext = PathUtf8::ToString(path.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".fbx";
}

void ImportModelSidecarAssets(const std::vector<fs::path>& importedPaths) {
    AssetManager& assets = AssetManager::Get();
    for (const fs::path& importedPath : importedPaths) {
        if (!IsFbxPath(importedPath))
            continue;

        const std::string rel = assets.ToProjectRelative(PathUtf8::ToString(importedPath));
        if (rel.empty())
            continue;

        // Loading the FBX is also the import step for embedded textures: ufbx
        // writes them beside the model as <model>_textures/*.  Do this before
        // the Project panel rescans so those generated assets are visible in
        // the same frame instead of only after the next editor launch.
        assets.DropSkeletalModel(rel);
        assets.DropMesh(rel);
        if (!assets.GetSkeletalModel(rel))
            MIPSYNC_WARN("FBX post-import processing failed: {}", rel);
    }
}

fs::path UniqueDestDirectory(const fs::path& parentDir, const fs::path& folderName) {
    std::error_code ec;
    fs::path dest = parentDir / folderName;
    if (!fs::exists(dest, ec))
        return dest;

    const std::string base = PathUtf8::ToString(folderName);
    for (int counter = 1; counter < 10000; ++counter) {
        dest = parentDir / PathUtf8::FromString(base + "_" + std::to_string(counter));
        if (!fs::exists(dest, ec))
            return dest;
    }
    return parentDir / folderName;
}

int ImportExternalDirectory(const fs::path& srcDir, const fs::path& destParentDir,
                            fs::path& outDestRoot, std::string& outError,
                            std::vector<fs::path>* outImportedPaths = nullptr) {
    std::error_code ec;
    if (!fs::is_directory(srcDir, ec)) {
        outError = "not a directory";
        return 0;
    }

    outDestRoot = UniqueDestDirectory(destParentDir, srcDir.filename());
    int imported = 0;

    for (const auto& entry : fs::recursive_directory_iterator(srcDir, ec)) {
        if (ec)
            break;
        if (!entry.is_regular_file())
            continue;
        if (ShouldSkipImportPath(fs::relative(entry.path(), srcDir, ec)))
            continue;

        fs::path rel = fs::relative(entry.path(), srcDir, ec);
        if (ec || rel.empty())
            continue;

        const fs::path targetDir = outDestRoot / rel.parent_path();
        std::string fileErr;
        fs::path importedPath;
        if (ImportExternalFileImpl(entry.path(), targetDir, fileErr, &importedPath)) {
            ++imported;
            if (outImportedPaths)
                outImportedPaths->push_back(std::move(importedPath));
        } else if (outError.empty()) {
            outError = fileErr;
        }
    }

    if (imported == 0 && outError.empty())
        outError = "folder contained no files";

    return imported;
}

} // namespace

int AssetBrowserPanel::ImportExternalPath(const std::string& absSourcePath) {
    m_LastError.clear();
    std::error_code ec;
    fs::path src = PathUtf8::FromString(absSourcePath);
    if (!fs::exists(src, ec)) {
        m_LastError = "path not found";
        return 0;
    }

    std::string targetSubdir = NormalizeContentFolder(m_CurrentFolder);
    fs::path destDir = PathUtf8::FromString(AssetManager::Get().GetProjectRoot()) /
                       PathUtf8::FromString(targetSubdir);

    int imported = 0;
    std::vector<fs::path> importedPaths;
    try {
        if (fs::is_directory(src, ec)) {
            std::string err;
            fs::path destRoot;
            imported = ImportExternalDirectory(src, destDir, destRoot, err, &importedPaths);
            if (imported > 0) {
                m_LastInfo = "Imported folder: " + PathUtf8::ToString(src.filename()) +
                             " (" + std::to_string(imported) + " files)";
                MIPSYNC_INFO("Imported folder {} → {}", PathUtf8::ToString(src),
                              PathUtf8::ToString(destRoot));
            } else if (!err.empty()) {
                m_LastError = err;
            }
        } else if (fs::is_regular_file(src, ec)) {
            std::string err;
            fs::path importedPath;
            if (ImportExternalFileImpl(src, destDir, err, &importedPath)) {
                imported = 1;
                importedPaths.push_back(std::move(importedPath));
                m_LastInfo = "Imported: " + PathUtf8::ToString(src.filename());
                MIPSYNC_INFO("Imported asset: {}", PathUtf8::ToString(destDir / src.filename()));
            } else {
                m_LastError = "import failed: " + err;
            }
        }
    } catch (const std::exception& ex) {
        m_LastError = std::string("import exception: ") + ex.what();
        return imported;
    }

    if (imported > 0) {
        ImportModelSidecarAssets(importedPaths);
        if (imported > 1 && m_LastInfo.empty())
            m_LastInfo = "Imported " + std::to_string(imported) + " file(s)";
        RescanFolderTree();
        Refresh();
    }
    return imported;
}

void AssetBrowserPanel::ProcessOsFileDrop() {
    if (OsFileDrop::Peek().empty())
        return;

    if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) {
        OsFileDrop::Clear();
        return;
    }

    const std::vector<std::string> paths = OsFileDrop::Consume();
    int total = 0;
    for (const std::string& path : paths)
        total += ImportExternalPath(path);

    if (total > 0 && m_LastInfo.empty())
        m_LastInfo = "Imported " + std::to_string(total) + " file(s)";
}

void AssetBrowserPanel::RunPendingImportDialog() {
#ifdef _WIN32
    if (!m_OpenImportDialog) return;
    m_OpenImportDialog = false;

    static const wchar_t kFilter[] =
        L"Textures (*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.hdr)\0*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.hdr\0"
        L"Models (*.fbx;*.obj;*.glb)\0*.fbx;*.obj;*.glb\0"
        L"Mips# Scripts (*.mips)\0*.mips\0"
        L"All Files (*.*)\0*.*\0";

    std::string picked;
    try {
        picked = OpenFileDialogW(kFilter);
    } catch (...) {
        m_LastError = "file dialog raised exception";
        return;
    }

    if (!picked.empty())
        ImportExternalPath(picked);
#endif
}

} // namespace MipsyncEngine
