#pragma once
#include "mips/MipsRuntime.h"
#include "scene/Scene.h"
#include "assets/AssetManager.h"
#include "core/NativePaths.h"
#include <chrono>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace MipsyncEngine {
inline bool RunMipsPrefabSpawnTests(std::string& error) {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / ("mipsync-prefab-spawn-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        fs::path path;
        std::string original;
        ~Cleanup() {
            AssetManager::Get().SetProjectRoot(original);
            try { NativePaths::RemoveTree(path); } catch (...) {}
        }
    } cleanup{root, AssetManager::Get().GetProjectRoot()};
    try {
        fs::create_directories(root / "assets/scripts");
        fs::create_directories(root / "assets/prefabs");
        AssetManager::Get().SetProjectRoot(PathUtf8::ToString(root));

        std::ofstream(root / "assets/scripts/Enemy.mips") << R"(
            class Enemy : MipsBehaviour {
                public float spawned = 0;
                void OnSpawn() { spawned = 1; transform.position.y = 5; }
            })";
        std::ofstream(root / "assets/scripts/Spawner.mips") << R"(
            class Spawner : MipsBehaviour {
                public Prefab enemyPrefab;
                public Prefab unassignedPrefab;
                public Entity[] active;
                public float result = 0;
                public float assignedNotNull = 0;
                public float unassignedIsNull = 0;
                private Entity enemy = 0;
                void Start() {
                    // GitHub #4: an assigned Prefab must not compare equal to null.
                    if (enemyPrefab != null) assignedNotNull = 1;
                    if (unassignedPrefab == null) unassignedIsNull = 1;
                    if (enemyPrefab == null) return;
                    Scene.Prewarm(enemyPrefab, 2);
                    enemy = Scene.Spawn(enemyPrefab, 1, 2, 3);
                    active = new Entity[1];
                    active[0] = enemy;
                    if (enemy != 0) result = 1;
                }
                void Update() {
                    if (enemy != 0) { result = result + 1; Scene.Despawn(enemy); }
                }
            })";
        std::ofstream(root / "assets/prefabs/Enemy.nprefab") << R"({
            "version": 2,
            "kind": "prefab",
            "entity": {
                "id": 900,
                "name": "Enemy",
                "transform": { "position": [0,0,0], "rotation": [0,0,0], "scale": [1,1,1] },
                "mipsScripts": [ { "path": "assets/scripts/Enemy.mips", "fields": {} } ],
                "componentOrder": [ "Transform", "MipsScript" ]
            }
        })";

        Scene scene;
        Entity* spawnerEntity = scene.CreateEntity("Spawner");
        auto& spawner = spawnerEntity->AddComponent<MipsScriptComponent>();
        spawner.scriptPath = "assets/scripts/Spawner.mips";
        std::vector<std::string> compileErrors;
        if (!Mips::MipsRuntime::EnsureScriptReady(spawner, compileErrors))
            throw std::runtime_error("prefab spawn fixture did not compile");
        const int prefabField = spawner.module->FindFieldIndex("enemyPrefab");
        if (prefabField < 0)
            throw std::runtime_error("Prefab field was not compiled as an asset field");
        if (static_cast<size_t>(prefabField) >= spawner.fieldAssetPaths.size())
            spawner.fieldAssetPaths.resize(spawner.module->fields.size());
        spawner.fieldAssetPaths[static_cast<size_t>(prefabField)] =
            "assets/prefabs/Enemy.nprefab";

        Mips::MipsRuntime runtime;
        runtime.OnPlayStarted(scene);
        Entity* enemy = nullptr;
        for (const auto& entity : scene.GetEntities())
            if (entity->GetComponent<TagComponent>() &&
                entity->GetComponent<TagComponent>()->tag == "Enemy")
                enemy = entity.get();
        if (!enemy)
            throw std::runtime_error("Scene.Spawn did not create the prefab entity "
                                     "(an assigned Prefab compared equal to null?)");
        const auto spawnerFields = runtime.InspectFields(spawner);
        if (spawnerFields["fields"].value("assignedNotNull", 0.0) != 1.0)
            throw std::runtime_error("assigned Prefab field compared equal to null");
        if (spawnerFields["fields"].value("unassignedIsNull", 0.0) != 1.0)
            throw std::runtime_error("unassigned Prefab field did not compare equal to null");
        if (!spawnerFields.contains("fields") ||
            !spawnerFields["fields"].contains("active") ||
            !spawnerFields["fields"]["active"].is_array() ||
            spawnerFields["fields"]["active"].empty() ||
            !spawnerFields["fields"]["active"][0].contains("id"))
            throw std::runtime_error("runtime.fields did not expose Entity[] as id/name objects");
        const auto* enemyTransform = enemy->GetComponent<TransformComponent>();
        if (!enemyTransform || std::abs(enemyTransform->position.y - 5.0f) > 1e-4f)
            throw std::runtime_error("prefab OnSpawn lifecycle did not run");
        auto* enemyScript = enemy->GetComponent<MipsScriptComponent>();
        if (!enemyScript)
            throw std::runtime_error("prefab script was not attached");
        const auto pools = runtime.InspectPools();
        if (!pools.contains("prefabs") || pools["prefabs"].empty() ||
            pools["prefabs"][0]["name"] != "assets/prefabs/Enemy.nprefab" ||
            pools["prefabs"][0]["capacity"] != 2)
            throw std::runtime_error("runtime.pool did not report the prefab pool");

        runtime.Update(scene, 0.016f);
        if (enemy->IsActive())
            throw std::runtime_error("Scene.Despawn did not deactivate the prefab entity");
        runtime.OnPlayStopped(scene);
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}
}
