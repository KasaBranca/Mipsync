#pragma once

#include <glm/glm.hpp>

namespace MipsyncEngine {

class Scene;
class Entity;
class Camera;
class EditorSceneCamera;

/// Computes world-space AABB for an entity and its descendants (meshes, colliders, UI rects).
bool ComputeEntityWorldBounds(Scene& scene, Entity& entity, glm::vec3& outMin, glm::vec3& outMax,
                              const Camera* editorSceneCamera = nullptr, float editorViewportAspect = 16.0f / 9.0f,
                              int layoutWidth = 1920, int layoutHeight = 1080);

/// Orbit distance so the bounds fit in the scene view camera FOV.
float FocusDistanceForBounds(const glm::vec3& boundsMin, const glm::vec3& boundsMax, float fovDegrees,
                             float aspect);

void FrameEntityInSceneView(Scene& scene, Entity& entity, EditorSceneCamera& sceneCamera, float viewportAspect,
                            int layoutWidth = 1920, int layoutHeight = 1080);

} // namespace MipsyncEngine
