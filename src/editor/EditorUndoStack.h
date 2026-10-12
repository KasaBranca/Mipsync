#pragma once

#include <cstddef>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>
#include <set>

namespace MipsyncEngine {

class Scene;

/// One chronological history for scene edits and project asset changes.
class EditorUndoStack {
public:
    void Clear();
    void SetMaxSteps(std::size_t steps);

    /// Push current scene onto undo (clears redo). Skips if identical to the latest undo entry.
    void PushState(Scene& scene);
    bool BeginFrame(Scene& scene, std::string& outError);
    bool EndFrame(Scene& scene, uint64_t activeGesture, std::string& outError,
                  bool* sceneChanged = nullptr);
    /// Retain Play asset transactions against the restored edit scene.
    bool AppendAssetHistory(const EditorUndoStack& playHistory, Scene& editScene,
                            std::string& outError);
    void CancelFrame();
    bool IsFrameActive() const { return m_FrameActive; }
    void IgnorePreviewTransform(uint32_t entityID) { m_PreviewTransforms.insert(entityID); }
    /// Capture before writing, creating, deleting or moving an asset.
    bool TrackPath(const std::string& absolutePath, bool recursive, std::string& outError);

    bool CanUndo() const { return !m_Undo.empty(); }
    bool CanRedo() const { return !m_Redo.empty(); }

    bool Undo(Scene& scene, std::string& outError);
    bool Redo(Scene& scene, std::string& outError);

private:
    std::size_t m_MaxSteps = 64;

    bool RestoreSnapshot(Scene& scene, const std::string& snapshot, std::string& outError);
    void TrimFront();

    struct PathState {
        bool exists = false, directory = false;
        std::shared_ptr<const std::string> bytes;
    };
    using Files = std::map<std::string, PathState>;
    struct Entry { std::string scene; Files files; uint64_t gesture = 0; };
    std::vector<Entry> m_Undo, m_Redo;
    Entry m_FrameBefore;
    std::map<std::string, bool> m_FrameRoots;
    bool m_FrameActive = false;
    uint64_t m_PreviousGesture = 0;
    std::set<uint32_t> m_PreviewTransforms;
    static void CapturePath(const std::string& path, bool recursive, Files& files);
    static bool SameFile(const PathState& a, const PathState& b);
    static void RestoreFiles(const Files& files);
    bool RestoreEntry(Scene& scene, const Entry& target, Entry& inverse, std::string& outError);
};

} // namespace MipsyncEngine
