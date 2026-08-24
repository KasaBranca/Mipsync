#pragma once
// ─────────────────────────────────────────────────
// Mipsync Engine — Unity-style Scene View Camera
// ─────────────────────────────────────────────────

#include "../renderer/Camera.h"
#include <glm/glm.hpp>

namespace MipsyncEngine {

class EditorSceneCamera {
public:
    EditorSceneCamera();

    Camera& GetCamera() { return m_Camera; }
    const Camera& GetCamera() const { return m_Camera; }

    void SetViewportAspect(float aspect);
    void FocusOn(const glm::vec3& point, float distance = -1.0f);

    // Unity Scene View controls
    void ProcessOrbit(float deltaX, float deltaY, float sensitivity = 0.3f);
    void ProcessPan(float deltaX, float deltaY, float sensitivity = 0.01f);
    void ProcessZoom(float scrollDelta);
    void ProcessFly(const glm::vec3& direction, float speed, float deltaTime);

    const glm::vec3& GetPivot() const { return m_Pivot; }
    float GetDistance() const { return m_Distance; }
    float GetOrbitYaw() const { return m_OrbitYaw; }
    float GetOrbitPitch() const { return m_OrbitPitch; }

    void SetOrbitState(const glm::vec3& pivot, float distance, float yawDegrees, float pitchDegrees);

private:
    void UpdateFromOrbit();

    Camera m_Camera;
    glm::vec3 m_Pivot = { 0.0f, 0.0f, 0.0f };
    float m_Distance = 8.0f;
    float m_OrbitYaw = -135.0f;
    float m_OrbitPitch = 30.0f;
};

} // namespace MipsyncEngine
