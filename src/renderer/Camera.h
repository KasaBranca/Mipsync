#pragma once
// ─────────────────────────────────────────────────
// Mipsync Engine — Camera
// ─────────────────────────────────────────────────

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace MipsyncEngine {

class Camera {
public:
    Camera();

    // View/Projection
    const glm::mat4& GetViewMatrix() const { return m_ViewMatrix; }
    const glm::mat4& GetProjectionMatrix() const { return m_ProjectionMatrix; }
    glm::mat4 GetViewProjection() const { return m_ProjectionMatrix * m_ViewMatrix; }

    // Transform
    void SetPosition(const glm::vec3& pos) { m_Position = pos; RecalculateView(); }
    void SetRotation(float yaw, float pitch) { m_Yaw = yaw; m_Pitch = pitch; RecalculateView(); }

    const glm::vec3& GetPosition() const { return m_Position; }
    const glm::vec3& GetForward() const { return m_Forward; }
    const glm::vec3& GetRight() const { return m_Right; }
    const glm::vec3& GetUp() const { return m_Up; }
    float GetYaw() const { return m_Yaw; }
    float GetPitch() const { return m_Pitch; }

    // Projection
    void SetPerspective(float fovDeg, float aspect, float nearPlane, float farPlane);
    void SetOrthographic(float size, float aspect, float nearPlane, float farPlane);

    // Editor camera controls
    void ProcessMouseMovement(float deltaX, float deltaY, float sensitivity = 0.1f);
    void ProcessMovement(const glm::vec3& direction, float speed, float deltaTime);
    void ProcessZoom(float delta);
    void LookAt(const glm::vec3& target);

    // Sync view from entity transform (euler: X=pitch, Y=yaw, Z=roll; Ry·Rx·Rz).
    void SyncFromTransform(const glm::vec3& position, const glm::vec3& eulerDegrees);
    /// Sync view from a world matrix (handles parent hierarchy; ignores non-uniform scale safely).
    void SyncFromWorldMatrix(const glm::vec3& position, const glm::mat4& worldMatrix);
    void SyncTransformFromCamera(class TransformComponent& transform) const;

    // PS1-style settings
    float fov = 60.0f;
    float nearClip = 0.1f;
    float farClip = 50.0f;  // PS1: very short draw distance

private:
    void RecalculateView();
    void RecalculateProjection();

    glm::vec3 m_Position = { 0.0f, 2.0f, 5.0f };
    glm::vec3 m_Forward  = { 0.0f, 0.0f, -1.0f };
    glm::vec3 m_Right    = { 1.0f, 0.0f, 0.0f };
    glm::vec3 m_Up       = { 0.0f, 1.0f, 0.0f };

    float m_Yaw   = -90.0f;
    float m_Pitch = -15.0f;
    float m_Aspect = 4.0f / 3.0f;  // PS1 aspect ratio

    glm::mat4 m_ViewMatrix       = glm::mat4(1.0f);
    glm::mat4 m_ProjectionMatrix = glm::mat4(1.0f);

    bool m_IsPerspective = true;
};

} // namespace MipsyncEngine
