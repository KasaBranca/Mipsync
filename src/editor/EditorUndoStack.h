#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace MipsyncEngine {

class Scene;

/// Full-scene undo/redo via SceneIO JSON snapshots.
class EditorUndoStack {
public:
    void Clear();

    /// Push current scene onto undo (clears redo). Skips if identical to the latest undo entry.
    void PushState(Scene& scene);

    bool CanUndo() const { return !m_Undo.empty(); }
    bool CanRedo() const { return !m_Redo.empty(); }

    bool Undo(Scene& scene, std::string& outError);
    bool Redo(Scene& scene, std::string& outError);

private:
    static constexpr std::size_t kMaxSteps = 64;

    bool RestoreSnapshot(Scene& scene, const std::string& snapshot, std::string& outError);
    void TrimFront();

    std::vector<std::string> m_Undo;
    std::vector<std::string> m_Redo;
};

} // namespace MipsyncEngine
