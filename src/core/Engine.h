#pragma once
// ─────────────────────────────────────────────────
// Mipsync Engine — Core Engine Lifecycle
// ─────────────────────────────────────────────────

#include "Window.h"
#include "Time.h"
#include "Input.h"
#include "../renderer/Renderer.h"
#include "../scene/Scene.h"
#include "../ui/UIRenderer.h"
#include "../editor/EditorApp.h"
#include <memory>
#include <vector>

namespace MipsyncEngine {

namespace Mips {
class MipsRuntime;
}

class PhysicsWorld;
class AudioSystem;

enum class EngineLaunchMode {
    Editor,
    /// Standalone player: fullscreen game view, auto-play, no editor chrome.
    Player,
};

class Engine {
public:
    explicit Engine(const std::string& projectPath = "",
                    EngineLaunchMode launchMode = EngineLaunchMode::Editor,
                    const std::string& playerDataDirectory = "");
    ~Engine();

    void Run();
    void Quit();

    static Engine& Get() { return *s_Instance; }

    bool IsPlayerMode() const { return m_LaunchMode == EngineLaunchMode::Player; }
    EngineLaunchMode GetLaunchMode() const { return m_LaunchMode; }

    /// Project-relative path, e.g. "scenes/level2.nscene". Applied at end of frame.
    void RequestSceneLoad(const std::string& projectRelativePath);
    /// Index into build scene list (player build or project player settings).
    void RequestSceneLoadBuildIndex(int buildIndex);
    void RequestQuit();

    const std::vector<std::string>& GetBuildScenes() const { return m_BuildScenes; }

    EditorApp& GetEditor() { return *m_Editor; }
    Window& GetWindow() { return *m_Window; }
    Renderer& GetRenderer() { return *m_Renderer; }
    UIRenderer& GetUIRenderer() { return *m_UIRenderer; }
    Scene& GetScene() { return *m_Scene; }
    Mips::MipsRuntime& GetMipsRuntime() { return *m_MipsRuntime; }
    PhysicsWorld& GetPhysicsWorld() { return *m_PhysicsWorld; }
    AudioSystem& GetAudioSystem() { return *m_AudioSystem; }

    const std::string& GetProjectPath() const { return m_ProjectPath; }
    const std::string& GetProjectName() const { return m_ProjectName; }

private:
    void Init();
    void Update();
    void ProcessDeferredActions();
    void SetupDefaultScene();
    bool EnsureBuiltinScripts();
    bool EnsureDemoContent();

    static Engine* s_Instance;

    std::unique_ptr<Window> m_Window;
    std::unique_ptr<Renderer> m_Renderer;
    std::unique_ptr<UIRenderer> m_UIRenderer;
    std::unique_ptr<Scene> m_Scene;
    std::unique_ptr<EditorApp> m_Editor;
    std::unique_ptr<Mips::MipsRuntime> m_MipsRuntime;
    std::unique_ptr<PhysicsWorld> m_PhysicsWorld;
    std::unique_ptr<AudioSystem> m_AudioSystem;

    std::string m_ProjectPath;
    std::string m_ProjectName;
    std::string m_PlayerDataDirectory;
    std::vector<std::string> m_BuildScenes;
    EngineLaunchMode m_LaunchMode = EngineLaunchMode::Editor;
    std::string m_PendingSceneLoad;
    bool m_RequestQuit = false;
    bool m_Running = true;
};

} // namespace MipsyncEngine
