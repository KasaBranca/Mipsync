#include "EditorUndoStack.h"
#include "../scene/Scene.h"
#include "../scene/SceneIO.h"
#include "../core/AssetFiles.h"
#include "../assets/AssetManager.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace MipsyncEngine {
namespace fs = std::filesystem;
void EditorUndoStack::Clear() { m_Undo.clear(); m_Redo.clear(); CancelFrame(); m_PreviousGesture = 0; }
void EditorUndoStack::TrimFront() { while (m_Undo.size() > m_MaxSteps) m_Undo.erase(m_Undo.begin()); }
void EditorUndoStack::SetMaxSteps(size_t steps) {
    m_MaxSteps = std::max(size_t(1), steps); TrimFront();
    while (m_Redo.size() > m_MaxSteps) m_Redo.erase(m_Redo.begin());
}
bool EditorUndoStack::RestoreSnapshot(Scene& scene, const std::string& snapshot, std::string& error) {
    return SceneIO::LoadFromJsonString(scene, snapshot, error);
}
void EditorUndoStack::PushState(Scene& scene) {
    if (m_FrameActive) return;
    Entry entry; std::string error;
    if (!SceneIO::SerializeSceneFingerprint(scene, entry.scene, error)) return;
    if (!m_Undo.empty() && m_Undo.back().files.empty() && m_Undo.back().scene == entry.scene) return;
    m_Undo.push_back(std::move(entry)); TrimFront(); m_Redo.clear(); m_PreviousGesture = 0;
}
void EditorUndoStack::CancelFrame() { m_FrameActive = false; m_FrameBefore = {}; m_FrameRoots.clear(); m_PreviewTransforms.clear(); }
bool EditorUndoStack::BeginFrame(Scene& scene, std::string& error) {
    CancelFrame();
    m_FrameActive = SceneIO::SerializeSceneFingerprint(scene, m_FrameBefore.scene, error);
    return m_FrameActive;
}
bool EditorUndoStack::SameFile(const PathState& a, const PathState& b) {
    return a.exists == b.exists && a.directory == b.directory &&
        (a.bytes == b.bytes || (a.bytes && b.bytes && *a.bytes == *b.bytes));
}
void EditorUndoStack::CapturePath(const std::string& path, bool recursive, Files& files) {
    const auto native = PathUtf8::FromString(path);
    std::error_code ec; const auto status = fs::symlink_status(native, ec);
    if (ec && ec != std::errc::no_such_file_or_directory && ec != std::errc::not_a_directory)
        throw fs::filesystem_error("Cannot snapshot asset", native, ec);
    if (fs::is_symlink(status)) throw std::runtime_error("Cannot snapshot a symbolic link: " + path);
    PathState state; state.exists = fs::exists(status); state.directory = fs::is_directory(status);
    if (state.exists && !state.directory) {
        if (!fs::is_regular_file(status)) throw std::runtime_error("Unsupported asset file: " + path);
        std::ifstream stream(native, std::ios::binary);
        if (!stream) throw std::runtime_error("Cannot read asset for Undo: " + path);
        auto bytes = std::make_shared<std::string>((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        if (stream.bad()) throw std::runtime_error("Cannot read complete asset for Undo: " + path);
        state.bytes = std::move(bytes);
    }
    files.emplace(path, std::move(state));
    if (recursive && fs::is_directory(status))
        for (const auto& entry : fs::recursive_directory_iterator(native))
            CapturePath(PathUtf8::ToString(entry.path()), false, files);
}
bool EditorUndoStack::TrackPath(const std::string& path, bool recursive, std::string& error) {
    if (!m_FrameActive) { error = "No active editor transaction"; return false; }
    try {
        const auto native = fs::absolute(PathUtf8::FromString(path)).lexically_normal();
        const auto normalized = PathUtf8::ToString(native);
        if (m_FrameRoots.contains(normalized) && (!recursive || m_FrameRoots[normalized])) return true;
        Files captured; CapturePath(normalized, recursive, captured);
        for (auto& [key, state] : captured) m_FrameBefore.files.try_emplace(key, std::move(state));
        m_FrameRoots[normalized] = recursive;
        for (auto parent = native.parent_path(); !parent.empty() && !fs::exists(parent); parent = parent.parent_path()) {
            const auto key = PathUtf8::ToString(parent);
            m_FrameBefore.files.try_emplace(key, PathState{}); m_FrameRoots.try_emplace(key, false);
            if (parent == parent.parent_path()) break;
        }
        return true;
    } catch (const std::exception& ex) { error = ex.what(); return false; }
}
bool EditorUndoStack::EndFrame(Scene& scene, uint64_t gesture, std::string& error, bool* sceneChanged) {
    if (sceneChanged) *sceneChanged = false;
    if (!m_FrameActive) { m_PreviousGesture = gesture; return true; }
    try {
        std::string current;
        if (!SceneIO::SerializeSceneFingerprint(scene, current, error)) { CancelFrame(); return false; }
        if (!m_PreviewTransforms.empty()) {
            const auto before = nlohmann::json::parse(m_FrameBefore.scene);
            auto afterScene = nlohmann::json::parse(current);
            for (auto& entity : afterScene["entities"]) {
                const auto id = entity.value("id", 0u);
                if (!m_PreviewTransforms.contains(id)) continue;
                for (const auto& original : before["entities"])
                    if (original.value("id", 0u) == id && original.contains("transform")) {
                        entity["transform"] = original["transform"]; break;
                    }
            }
            current = afterScene.dump();
            // Fingerprints may use a different indentation. Compare like for like.
            m_FrameBefore.scene = before.dump();
        }
        Files after;
        for (const auto& [root, recursive] : m_FrameRoots) CapturePath(root, recursive, after);
        Files changed;
        for (const auto& [path, before] : m_FrameBefore.files) {
            const auto found = after.find(path);
            if (!SameFile(before, found == after.end() ? PathState{} : found->second)) changed.emplace(path, before);
        }
        for (const auto& [path, now] : after)
            if (!m_FrameBefore.files.contains(path) && now.exists) changed.emplace(path, PathState{});
        const bool changedScene = current != m_FrameBefore.scene;
        if (sceneChanged) *sceneChanged = changedScene;
        if (changedScene || !changed.empty()) {
            const uint64_t group = gesture ? gesture : m_PreviousGesture;
            if (group && m_PreviousGesture == group && !m_Undo.empty() && m_Undo.back().gesture == group) {
                for (auto& [path, before] : changed) m_Undo.back().files.try_emplace(path, std::move(before));
            } else {
                m_FrameBefore.files = std::move(changed); m_FrameBefore.gesture = group;
                if (!m_Undo.empty() && m_Undo.back().files.empty() && m_Undo.back().gesture == 0 &&
                    m_Undo.back().scene == m_FrameBefore.scene) m_Undo.pop_back();
                m_Undo.push_back(std::move(m_FrameBefore)); TrimFront();
            }
            m_Redo.clear();
        }
        CancelFrame(); m_PreviousGesture = gesture; return true;
    } catch (const std::exception& ex) { error = ex.what(); CancelFrame(); return false; }
}
bool EditorUndoStack::AppendAssetHistory(const EditorUndoStack& playHistory, Scene& editScene,
                                        std::string& error) {
    std::string snapshot;
    if (!SceneIO::SerializeSceneFingerprint(editScene, snapshot, error)) return false;
    const auto hasAssets = [](const Entry& entry) { return !entry.files.empty(); };
    if (!std::any_of(playHistory.m_Undo.begin(), playHistory.m_Undo.end(), hasAssets) &&
        !std::any_of(playHistory.m_Redo.begin(), playHistory.m_Redo.end(), hasAssets)) return true;
    CancelFrame(); m_PreviousGesture = 0; m_Redo.clear();
    for (const auto& entry : playHistory.m_Undo)
        if (hasAssets(entry)) m_Undo.push_back(Entry{snapshot, entry.files, 0});
    for (const auto& entry : playHistory.m_Redo)
        if (hasAssets(entry)) m_Redo.push_back(Entry{snapshot, entry.files, 0});
    TrimFront();
    while (m_Redo.size() > m_MaxSteps) m_Redo.erase(m_Redo.begin());
    return true;
}
void EditorUndoStack::RestoreFiles(const Files& files) {
    std::vector<std::string> paths;
    for (const auto& [path, state] : files) paths.push_back(path);
    std::sort(paths.begin(), paths.end(), [](const auto& a, const auto& b) { return a.size() > b.size(); });
    // Remove children before parents. Nonempty directories fail rather than
    // deleting unrelated files created outside this transaction.
    for (const auto& path : paths) {
        const auto native = PathUtf8::FromString(path); const auto& state = files.at(path);
        if (fs::exists(native) && (!state.exists || fs::is_directory(native) != state.directory)) fs::remove(native);
    }
    std::reverse(paths.begin(), paths.end());
    for (const auto& path : paths) {
        const auto& state = files.at(path); if (!state.exists) continue;
        const auto native = PathUtf8::FromString(path);
        if (state.directory) fs::create_directories(native);
        else {
            fs::create_directories(native.parent_path());
            std::ofstream stream(native, std::ios::binary | std::ios::trunc);
            if (!stream) throw std::runtime_error("Cannot restore asset: " + path);
            stream.write(state.bytes->data(), static_cast<std::streamsize>(state.bytes->size())); stream.close();
            if (!stream) throw std::runtime_error("Cannot restore complete asset: " + path);
        }
    }
}
bool EditorUndoStack::RestoreEntry(Scene& scene, const Entry& target, Entry& inverse, std::string& error) {
    if (!SceneIO::SerializeSceneFingerprint(scene, inverse.scene, error)) return false;
    bool attempted = false;
    try {
        for (const auto& [path, state] : target.files) CapturePath(path, false, inverse.files);
        attempted = true;
        RestoreFiles(target.files);
        if (!target.files.empty()) AssetManager::Get().Clear();
        if (!RestoreSnapshot(scene, target.scene, error)) throw std::runtime_error(error);
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        try { if (attempted) {
            RestoreFiles(inverse.files);
            if (!inverse.files.empty()) AssetManager::Get().Clear();
            std::string rollback;
            if (!RestoreSnapshot(scene, inverse.scene, rollback)) error += "; rollback: " + rollback;
        } }
        catch (const std::exception& rollback) { error += "; rollback: " + std::string(rollback.what()); }
        return false;
    }
}
bool EditorUndoStack::Undo(Scene& scene, std::string& error) {
    CancelFrame(); m_PreviousGesture = 0;
    if (!CanUndo()) return false;
    Entry inverse;
    if (!RestoreEntry(scene, m_Undo.back(), inverse, error)) return false;
    m_Undo.pop_back(); m_Redo.push_back(std::move(inverse)); return true;
}
bool EditorUndoStack::Redo(Scene& scene, std::string& error) {
    CancelFrame(); m_PreviousGesture = 0;
    if (!CanRedo()) return false;
    Entry inverse;
    if (!RestoreEntry(scene, m_Redo.back(), inverse, error)) return false;
    m_Redo.pop_back(); m_Undo.push_back(std::move(inverse)); TrimFront(); return true;
}
} // namespace MipsyncEngine
