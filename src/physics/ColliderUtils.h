#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

namespace MipsyncEngine {

class Mesh;
class ColliderComponent;
class RigidbodyComponent;
class Entity;
class Scene;

namespace ColliderUtils {

struct ColliderWorldPose {
    glm::vec3 center{ 0.0f };
    glm::quat rotation{ 1.0f, 0.0f, 0.0f, 1.0f };
    glm::vec3 lossyScale{ 1.0f };
    glm::vec3 worldEulerDegrees{ 0.0f };
};

glm::vec3 GetLossyScale(const glm::mat4& worldMatrix);

ColliderWorldPose ComputeWorldPose(const Scene& scene, const Entity& entity,
                                   const ColliderComponent& col);

/// Sets box/sphere/capsule fields from mesh local AABB (mesh space).
void FitColliderToMesh(ColliderComponent& col, const Mesh& mesh);

/// Builds a Jolt shape in the entity's local axes (scale baked in via lossyScale).
JPH::RefConst<JPH::Shape> CreateShape(const ColliderComponent& col, const Mesh* mesh,
                                      const glm::vec3& lossyScale);

/// Capsule + kinematic rigidbody marker for FirstPersonController.
/// Marks the entity as a CharacterVirtual-driven controller.
void EnsureFirstPersonPhysics(Entity& entity);

/// True if the rigidbody marks this entity as a character controller (CharacterVirtual).
bool IsCharacterController(const RigidbodyComponent* rb);

/// World rotation used for physics bodies / casts (yaw-only when rb->freezeRotation).
glm::quat PhysicsWorldRotation(const RigidbodyComponent* rb, const glm::quat& visualWorldRotation);

ColliderWorldPose ComputePhysicsWorldPose(const Scene& scene, const Entity& entity,
                                          const ColliderComponent& col,
                                          const RigidbodyComponent* rb);

} // namespace ColliderUtils
} // namespace MipsyncEngine
