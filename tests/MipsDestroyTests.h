#pragma once
#include "mips/MipsRuntime.h"
#include "scene/Scene.h"
#include "physics/PhysicsWorld.h"
#include "editor/EditorPlaySession.h"
#include "game/GameSave.h"
#include "assets/AssetManager.h"
#include "core/NativePaths.h"
#include <chrono>
#include <fstream>
#include <stdexcept>

namespace MipsyncEngine {
inline bool RunMipsDestroyTests(std::string& error) {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / ("mipsync-destroy-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        fs::path path; std::string original;
        ~Cleanup() { AssetManager::Get().SetProjectRoot(original); try { NativePaths::RemoveTree(path); } catch (...) {} }
    } cleanup{root, AssetManager::Get().GetProjectRoot()};
    try {
        fs::create_directories(root);
        AssetManager::Get().SetProjectRoot(PathUtf8::ToString(root));
        auto require = [](bool condition, const char* message) { if (!condition) throw std::runtime_error(message); };
        auto script = [&](Entity* entity, const char* name, const std::string& body) {
            std::ofstream(root / (std::string(name) + ".mips")) << "class " << name << " : MipsBehaviour { " << body << " }";
            entity->AddComponent<MipsScriptComponent>().scriptPath = std::string(name) + ".mips";
        };
        auto& save = GameSave::Get();
        save.SetInt("destroy_test_calls", 0);
        save.SetInt("destroy_test_after", 0);
        Scene scene;
        auto* sensor = scene.CreateEntity("Trigger pickup");
        const auto sensorId = sensor->GetID();
        sensor->AddComponent<ColliderComponent>().isTrigger = true;
        auto* child = scene.CreateEntity("Pickup child");
        const auto childId = child->GetID();
        scene.SetParent(child, sensor);
        auto* chain = scene.CreateEntity("OnDestroy chained target");
        const auto chainId = chain->GetID();
        chain->GetComponent<TransformComponent>()->position.x = 10;
        chain->AddComponent<ColliderComponent>();
        auto* player = scene.CreateEntity("Player");
        const auto playerId = player->GetID();
        player->AddComponent<ColliderComponent>();
        auto& body = player->AddComponent<RigidbodyComponent>();
        body.bodyType = RigidbodyType::Dynamic; body.useGravity = false;
        script(sensor, "Pickup", "void OnTriggerEnter() { Destroy(gameObject); Destroy(gameObject); Save.SetInt(\"destroy_test_after\", 1); } "
            "void OnDestroy() { Save.SetInt(\"destroy_test_calls\", Save.GetInt(\"destroy_test_calls\", 0) + 1); Destroy(" + std::to_string(chainId) + "); }");
        script(child, "PickupChild", "void OnDestroy() { Save.SetInt(\"destroy_test_calls\", Save.GetInt(\"destroy_test_calls\", 0) + 1); }");
        script(chain, "Chain", "void OnDestroy() { Save.SetInt(\"destroy_test_calls\", Save.GetInt(\"destroy_test_calls\", 0) + 1); }");
        EditorPlaySession session;
        require(session.Begin(scene, "scenes/unsaved.nscene", error), "Destroy: failed to capture edit scene");
        Mips::MipsRuntime runtime;
        PhysicsWorld physics;
        runtime.SetPhysicsWorld(&physics);
        physics.SetContactCallback([&](uint32_t self, uint32_t other, bool trigger, bool enter) {
            if (trigger) runtime.PhysicsEvents().Push({enter ? MipsPhysicsEvent::Kind::TriggerEnter : MipsPhysicsEvent::Kind::TriggerExit, self, other});
        });
        runtime.OnPlayStarted(scene);
        physics.BeginPlay(scene);
        physics.Simulate(scene, 1.f / 60);
        require(!runtime.PhysicsEvents().Events().empty(), "Destroy: actual physics trigger did not fire");
        runtime.DispatchPhysicsEvents(scene);
        require(!scene.FindEntity(sensorId) && !scene.FindEntity(childId) && !scene.FindEntity(chainId),
            "Destroy: self, descendants or OnDestroy chained target survived");
        require(scene.FindEntity(playerId) && save.GetInt("destroy_test_calls", 0) == 3 && save.GetInt("destroy_test_after", 0) == 1,
            "Destroy: duplicate callback, wrong target or callback did not complete safely");
        runtime.Update(scene, 1.f / 60); physics.Simulate(scene, 1.f / 60);
        runtime.DispatchPhysicsEvents(scene); runtime.LateUpdate(scene, 1.f / 60);
        runtime.OnPlayStopped(scene); physics.EndPlay();
        require(save.GetInt("destroy_test_calls", 0) == 3, "Destroy: OnDestroy repeated at Stop");
        require(session.Restore(scene, error) && scene.FindEntity(sensorId) && scene.FindEntity(childId) &&
            scene.FindEntity(chainId) && scene.FindEntity(childId)->GetParentID() == sensorId,
            "Destroy: stopping Play lost original scene objects or hierarchy");

        Scene second;
        auto* killer = second.CreateEntity("Kill zone");
        auto* victim = second.CreateEntity("Other object");
        const auto killerId = killer->GetID(), victimId = victim->GetID();
        script(killer, "KillZone", "void OnTriggerEnter() { Destroy(Physics.otherEntityId); Destroy(Physics.otherEntityId); Destroy(0); Destroy(null); Destroy(-1); Destroy(0.5); Destroy(4294967296); } "
            "public void Click() { Destroy(gameObject); }");
        script(victim, "Victim", "void OnDestroy() { Save.SetInt(\"destroy_test_calls\", Save.GetInt(\"destroy_test_calls\", 0) + 1); }");
        Mips::MipsRuntime otherRuntime;
        otherRuntime.OnPlayStarted(second);
        otherRuntime.PhysicsEvents().Push({MipsPhysicsEvent::Kind::TriggerEnter, killerId, victimId});
        otherRuntime.PhysicsEvents().Push({MipsPhysicsEvent::Kind::TriggerEnter, killerId, victimId});
        otherRuntime.DispatchPhysicsEvents(second);
        require(second.FindEntity(killerId) && !second.FindEntity(victimId) && save.GetInt("destroy_test_calls", 0) == 4,
            "Destroy: otherEntityId/no-op arguments or duplicate events failed");
        std::vector<std::string> errors;
        require(otherRuntime.InvokeButtonEvent(second, killerId, "KillZone.mips", "Click", errors) && errors.empty() && !second.FindEntity(killerId),
            "Destroy: UI callback invalidated live instance iteration");
        otherRuntime.OnPlayStopped(second);

        Scene phases;
        save.SetInt("destroy_test_coroutine", 0);
        auto* startObject = phases.CreateEntity("Destroy in Start");
        const auto startId = startObject->GetID();
        script(startObject, "StartDestroy", "void Start() { Destroy(gameObject); }");
        auto* updateObject = phases.CreateEntity("Destroy before coroutine resumes");
        const auto updateId = updateObject->GetID();
        script(updateObject, "UpdateDestroy", "void Start() { StartCoroutine(Routine()); } void Update() { Destroy(gameObject); } "
            "public IEnumerator Routine() { Save.SetInt(\"destroy_test_coroutine\", 1); yield return null; }");
        Mips::MipsRuntime phasesRuntime;
        phasesRuntime.OnPlayStarted(phases);
        require(!phases.FindEntity(startId) && phases.FindEntity(updateId), "Destroy: Start invalidated lifecycle iteration");
        phasesRuntime.Update(phases, 1.f / 60);
        require(!phases.FindEntity(updateId) && save.GetInt("destroy_test_coroutine", 0) == 0,
            "Destroy: pending object resumed a coroutine or survived Update");
        phasesRuntime.OnPlayStopped(phases);
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}
}
