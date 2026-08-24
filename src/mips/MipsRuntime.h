#pragma once

#include "Bytecode.h"
#include "MipsPhysicsEvents.h"
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

private:
    struct TransformSnapshot {
        uint32_t entityId = 0;
        glm::vec3 position{};
        glm::vec3 rotation{};
        glm::vec3 scale{1.0f};
    };

    void CollectInstances(MipsyncEngine::Scene& scene);
    void CaptureSceneSnapshot(MipsyncEngine::Scene& scene);
    void RestoreSceneSnapshot(MipsyncEngine::Scene& scene);
    void InvokeLifecycleAll(const char* method, MipsyncEngine::Scene& scene, float deltaTime);

    VM m_VM;
    MipsPhysicsEventQueue m_PhysicsEvents;
    std::unordered_map<std::string, std::filesystem::file_time_type> m_ScriptFileTimes;
    std::vector<ScriptInstance> m_Instances;
    std::vector<TransformSnapshot> m_EditSnapshot;
    MipsyncEngine::PhysicsWorld* m_PhysicsWorld = nullptr;
    bool m_Playing = false;
    bool m_Paused = false;
};

} // namespace Mips
} // namespace MipsyncEngine
