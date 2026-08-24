#include "EditorSceneCamera.h"
#include <algorithm>
#include <cmath>

namespace MipsyncEngine {

namespace {
// Camera defaults are PS1 in-game (far 50). Scene View needs Unity-style draw distance for editing.
constexpr float kSceneViewNearClip = 0.001f;
constexpr float kSceneViewFarClip = 2000.0f;
} // namespace

EditorSceneCamera::EditorSceneCamera() {
    m_Camera.nearClip = kSceneViewNearClip;
    m_Camera.farClip = kSceneViewFarClip;
    m_Camera.SetPerspective(m_Camera.fov, 16.0f / 9.0f, kSceneViewNearClip, kSceneViewFarClip);
    UpdateFromOrbit();
}

void EditorSceneCamera::SetViewportAspect(float aspect) {
    m_Camera.SetPerspective(m_Camera.fov, aspect, m_Camera.nearClip, m_Camera.farClip);
}

void EditorSceneCamera::FocusOn(const glm::vec3& point, float distance) {
    m_Pivot = point;
    if (distance > 0.0f) {
        m_Distance = distance;
    }
    UpdateFromOrbit();
}

void EditorSceneCamera::ProcessOrbit(float deltaX, float deltaY, float sensitivity) {
    m_OrbitYaw += deltaX * sensitivity;
    m_OrbitPitch += deltaY * sensitivity;
    m_OrbitPitch = std::clamp(m_OrbitPitch, -89.0f, 89.0f);
    UpdateFromOrbit();
}

void EditorSceneCamera::ProcessPan(float deltaX, float deltaY, float sensitivity) {
    const glm::vec3& right = m_Camera.GetRight();
    const glm::vec3& up = m_Camera.GetUp();
    glm::vec3 offset = (-right * deltaX + up * deltaY) * m_Distance * sensitivity;
    m_Pivot += offset;
    UpdateFromOrbit();
}

void EditorSceneCamera::ProcessZoom(float scrollDelta) {
    m_Distance -= scrollDelta * m_Distance * 0.15f;
    m_Distance = std::clamp(m_Distance, 0.005f, 500.0f);
    UpdateFromOrbit();
}

void EditorSceneCamera::ProcessFly(const glm::vec3& direction, float speed, float deltaTime) {
    glm::vec3 move = direction * speed * deltaTime;
    m_Pivot += move;
    m_Camera.SetPosition(m_Camera.GetPosition() + move);
    m_Camera.LookAt(m_Pivot);
}

void EditorSceneCamera::SetOrbitState(const glm::vec3& pivot, float distance, float yawDegrees,
                                      float pitchDegrees) {
    m_Pivot = pivot;
    m_Distance = std::clamp(distance, 0.005f, 500.0f);
    m_OrbitYaw = yawDegrees;
    m_OrbitPitch = std::clamp(pitchDegrees, -89.0f, 89.0f);
    UpdateFromOrbit();
}

void EditorSceneCamera::UpdateFromOrbit() {
    float yawRad = glm::radians(m_OrbitYaw);
    float pitchRad = glm::radians(m_OrbitPitch);

    glm::vec3 offset;
    offset.x = m_Distance * cosf(pitchRad) * cosf(yawRad);
    offset.y = m_Distance * sinf(pitchRad);
    offset.z = m_Distance * cosf(pitchRad) * sinf(yawRad);

    m_Camera.SetPosition(m_Pivot + offset);
    m_Camera.LookAt(m_Pivot);
}

} // namespace MipsyncEngine
