#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <functional>
#include <memory>

namespace MipsyncEngine {

/// Physics contact callback: selfEntity, otherEntity, isTrigger, isEnter (false = exit).
using PhysicsContactCallback = std::function<void(uint32_t selfEntityId, uint32_t otherEntityId,
                                                  bool isTrigger, bool isEnter)>;

class Scene;
class Entity;

/// Jolt Physics integration: rigid bodies + CharacterVirtual controllers.
class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    void BeginPlay(Scene& scene);
    void EndPlay();
    bool IsActive() const;

    /// Rebuild Jolt bodies/characters from the scene (play mode only).
    void RefreshBodies(Scene& scene);

    /// Step physics: characters move + slide, dynamic bodies simulate, transforms write back.
    void Simulate(Scene& scene, float deltaTime);

    /// Push one entity's transform into its Jolt body / character (e.g. after a gizmo edit).
    void SyncEntityFromScene(Scene& scene, Entity& entity);

    /// Set the desired velocity (m/s) for a CharacterVirtual; consumed by the next Simulate.
    bool SetCharacterVelocity(Entity& entity, const glm::vec3& velocity);

    /// True if the CharacterVirtual is supported by ground (slope <= max).
    bool IsCharacterGrounded(Entity& entity) const;

    /// Called when rigid bodies / characters begin or end contact (play mode only).
    void SetContactCallback(PhysicsContactCallback callback);

    struct Impl;

private:
    std::unique_ptr<Impl> m_Impl;

    void EnsureInitialized();
    void DestroyAllBodies();
    void RebuildBodies(Scene& scene);
    void SyncKinematicAndStaticFromScene(Scene& scene, float deltaTime);
    void SyncDynamicToScene(Scene& scene);
    void UpdateCharacters(Scene& scene, float deltaTime);
    void SyncCharactersToScene(Scene& scene);
};

} // namespace MipsyncEngine
