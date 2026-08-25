#pragma once

#include "Bytecode.h"
#include "MipsPhysicsEvents.h"
#include "MipsSceneSnapshot.h"
#include "VM.h"
#include <cstdint>
#include <filesystem>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace MipsyncEngine {

struct MipsScriptComponent;
class PhysicsWorld;

namespace Mips {

class MipsRuntime {
public:
    void SetPhysicsWorld(MipsyncEngine::PhysicsWorld* world) { m_PhysicsWorld = world; }

    void OnPlayStarted(MipsyncEngine::Scene& scene);
    void OnPlayStopped(MipsyncEngine::Scene& scene);
    void Update(MipsyncEngine::Scene& scene, float deltaTime);
    void LateUpdate(MipsyncEngine::Scene& scene, float deltaTime);

    bool IsPlaying() const { return m_Playing; }
    bool IsPaused() const { return m_Paused; }
    void SetPaused(bool paused) { m_Paused = paused; }

    /// Captures current scene transforms as the edit-mode baseline (call after scene load or inspector edits).
    void SyncEditSnapshot(MipsyncEngine::Scene& scene);

    /// Recompiles .mips assets when their file timestamp changes.
    void ReloadChangedScripts(MipsyncEngine::Scene& scene);

    static std::shared_ptr<CompiledModule> CompileScriptFile(const std::string& path,
                                                             std::vector<std::string>& errors);

    /// Compile if needed and resize fieldValues to match module defaults.
    static bool EnsureScriptReady(MipsScriptComponent& script, std::vector<std::string>& errors);

    /// Recompile a script asset and reset inspector field values from module defaults.
    void ResetScriptsByFileName(MipsyncEngine::Scene& scene, const std::string& fileName);

    void ApplyFieldOverrides(Entity* entity, const MipsScriptComponent& script);

    MipsPhysicsEventQueue& PhysicsEvents() { return m_PhysicsEvents; }
    void DispatchPhysicsEvents(MipsyncEngine::Scene& scene);

    /// Invokes a persistent UI listener on a script attached to targetEntityId.
    /// Returns false when the target/script/method is unavailable or play mode is inactive.
    bool InvokeButtonEvent(MipsyncEngine::Scene& scene, uint32_t targetEntityId,
                           const std::string& scriptPath, const std::string& methodName,
                           std::vector<std::string>& errors);

private:
    void CollectInstances(MipsyncEngine::Scene& scene);
    void InvokeLifecycleAll(const char* method, MipsyncEngine::Scene& scene, float deltaTime);

    VM m_VM;
    MipsPhysicsEventQueue m_PhysicsEvents;
    std::unordered_map<std::string, std::filesystem::file_time_type> m_ScriptFileTimes;
    std::vector<ScriptInstance> m_Instances;
    MipsSceneSnapshot m_EditSnapshot;
    MipsyncEngine::PhysicsWorld* m_PhysicsWorld = nullptr;
    bool m_Playing = false;
    bool m_Paused = false;
};

} // namespace Mips
} // namespace MipsyncEngine
