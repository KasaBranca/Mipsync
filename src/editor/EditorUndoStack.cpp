#include "EditorUndoStack.h"
#include "../scene/Scene.h"
#include "../scene/SceneIO.h"

namespace MipsyncEngine {

void EditorUndoStack::Clear() {
    m_Undo.clear();
    m_Redo.clear();
}

void EditorUndoStack::TrimFront() {
    while (m_Undo.size() > kMaxSteps)
        m_Undo.erase(m_Undo.begin());
}

bool EditorUndoStack::RestoreSnapshot(Scene& scene, const std::string& snapshot, std::string& outError) {
    return SceneIO::LoadFromJsonString(scene, snapshot, outError);
}

void EditorUndoStack::PushState(Scene& scene) {
    std::string snapshot;
    std::string err;
    if (!SceneIO::SerializeSceneFingerprint(scene, snapshot, err))
        return;

    if (!m_Undo.empty() && m_Undo.back() == snapshot)
        return;

    m_Undo.push_back(std::move(snapshot));
    TrimFront();
    m_Redo.clear();
}

bool EditorUndoStack::Undo(Scene& scene, std::string& outError) {
    if (!CanUndo())
        return false;

    std::string current;
    if (!SceneIO::SerializeSceneFingerprint(scene, current, outError))
        return false;

    const std::string target = std::move(m_Undo.back());
    m_Undo.pop_back();
    m_Redo.push_back(std::move(current));

    if (!RestoreSnapshot(scene, target, outError)) {
        m_Undo.push_back(std::move(target));
        m_Redo.pop_back();
        return false;
    }
    return true;
}

bool EditorUndoStack::Redo(Scene& scene, std::string& outError) {
    if (!CanRedo())
        return false;

    std::string current;
    if (!SceneIO::SerializeSceneFingerprint(scene, current, outError))
        return false;

    m_Undo.push_back(std::move(current));
    TrimFront();

    const std::string target = std::move(m_Redo.back());
    m_Redo.pop_back();

    if (!RestoreSnapshot(scene, target, outError)) {
        m_Undo.pop_back();
        m_Redo.push_back(std::move(target));
        return false;
    }
    return true;
}

} // namespace MipsyncEngine
