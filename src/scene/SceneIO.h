#pragma once

#include <string>

namespace MipsyncEngine {

class Scene;
class Entity;

namespace SceneIO {

bool SaveToFile(Scene& scene, const std::string& path, std::string& outError);
bool LoadFromFile(Scene& scene, const std::string& path, std::string& outError);

/// Canonical JSON fingerprint of the scene (same content as SaveToFile, without writing disk).
bool SerializeSceneFingerprint(Scene& scene, std::string& outFingerprint, std::string& outError);

/// Restore scene from a JSON document produced by SerializeSceneFingerprint / SaveToFile.
bool LoadFromJsonString(Scene& scene, const std::string& jsonText, std::string& outError);

/// Saves a single entity and its full child hierarchy as a .nprefab.
bool SaveEntityToFile(const Entity& entity, const Scene& scene,
                      const std::string& path, std::string& outError);
/// Creates a new entity (and its children) in `scene` from a .nprefab and returns the root.
Entity* InstantiateFromFile(Scene& scene, const std::string& path, std::string& outError);

/// Re-serializes `entity` (and children) back to the .nprefab at `path`,
/// and marks every node in the tree as an instance of that file.
bool ApplyPrefabToFile(Entity& entity, const Scene& scene,
                       const std::string& path, std::string& outError);

/// Destroys `instance` and re-instantiates from its stored prefabSourcePath.
/// Returns the new root entity, or nullptr on failure.
Entity* RevertEntityFromPrefab(Scene& scene, Entity& instance, std::string& outError);

/// Spawns an entity with a mesh loaded from a project-relative model path (.fbx, .obj).
Entity* SpawnModelFromAsset(Scene& scene, const std::string& projectRelPath, std::string& outError);

} // namespace SceneIO
} // namespace MipsyncEngine
