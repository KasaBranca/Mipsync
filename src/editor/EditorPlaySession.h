#pragma once
#include "../scene/Scene.h"
#include "../scene/SceneIO.h"
#include <string>

namespace MipsyncEngine {
// A play session spans every runtime scene transition. Its original edit scene
// stays in memory, including unsaved edits, rather than being reloaded from disk.
class EditorPlaySession {
public:
    bool Begin(Scene& scene, const std::string& path, std::string& error) {
        if (Active()) return true;
        if (!SceneIO::SerializeSceneFingerprint(scene, m_Scene, error)) return false;
        m_Path = path;
        m_Transitioned = false;
        return true;
    }
    bool Active() const { return !m_Scene.empty(); }
    bool Transitioned() const { return m_Transitioned; }
    void MarkTransition() { if (Active()) m_Transitioned = true; }
    const std::string& OriginalPath() const { return m_Path; }
    const std::string& OriginalSnapshot() const { return m_Scene; }
    bool Restore(Scene& scene, std::string& error) const {
        if (!Active()) return true;
        Scene original;
        if (!SceneIO::LoadFromJsonString(original, m_Scene, error)) return false;
        scene = std::move(original);
        return true;
    }
    void Reset() { m_Scene.clear(); m_Path.clear(); m_Transitioned = false; }
private:
    std::string m_Scene, m_Path;
    bool m_Transitioned = false;
};
} // namespace MipsyncEngine
