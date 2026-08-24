#define GLM_ENABLE_EXPERIMENTAL
#include "Camera.h"
#include "../scene/Scene.h"
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <algorithm>
#include <cmath>

namespace MipsyncEngine {

Camera::Camera() {
    RecalculateView();
    SetPerspective(fov, m_Aspect, nearClip, farClip);
}

void Camera::SetPerspective(float fovDeg, float aspect, float nearPlane, float farPlane) {
    m_IsPerspective = true;
    fov = fovDeg;
    m_Aspect = aspect;
    nearClip = nearPlane;
    farClip = farPlane;
    RecalculateProjection();
}

void Camera::SetOrthographic(float size, float aspect, float nearPlane, float farPlane) {
    m_IsPerspective = false;
    m_Aspect = aspect;
    nearClip = nearPlane;
    farClip = farPlane;
    float hw = size * aspect * 0.5f;
    float hh = size * 0.5f;
    m_ProjectionMatrix = glm::ortho(-hw, hw, -hh, hh, nearPlane, farPlane);
}

void Camera::RecalculateProjection() {
    if (m_IsPerspective) {
        m_ProjectionMatrix = glm::perspective(glm::radians(fov), m_Aspect, nearClip, farClip);
    }
}

void Camera::RecalculateView() {
    glm::vec3 front;
    front.x = cos(glm::radians(m_Yaw)) * cos(glm::radians(m_Pitch));
    front.y = sin(glm::radians(m_Pitch));
    front.z = sin(glm::radians(m_Yaw)) * cos(glm::radians(m_Pitch));
    m_Forward = glm::normalize(front);
    m_Right   = glm::normalize(glm::cross(m_Forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    m_Up      = glm::normalize(glm::cross(m_Right, m_Forward));
    m_ViewMatrix = glm::lookAt(m_Position, m_Position + m_Forward, m_Up);
}

void Camera::ProcessMouseMovement(float deltaX, float deltaY, float sensitivity) {
    m_Yaw   += deltaX * sensitivity;
    m_Pitch += deltaY * sensitivity;
    m_Pitch = std::clamp(m_Pitch, -89.0f, 89.0f);
    RecalculateView();
}

void Camera::ProcessMovement(const glm::vec3& direction, float speed, float deltaTime) {
    m_Position += direction * speed * deltaTime;
    RecalculateView();
}

void Camera::ProcessZoom(float delta) {
    fov -= delta * 2.0f;
    fov = std::clamp(fov, 15.0f, 120.0f);
    RecalculateProjection();
}

void Camera::LookAt(const glm::vec3& target) {
    m_Forward = glm::normalize(target - m_Position);
    m_Pitch = glm::degrees(asinf(m_Forward.y));
    m_Yaw = glm::degrees(atan2f(m_Forward.z, m_Forward.x));
    RecalculateView();
}

void Camera::SyncFromWorldMatrix(const glm::vec3& position, const glm::mat4& worldMatrix) {
    m_Position = position;

    glm::vec3 scale, skew, trans;
    glm::quat rot;
    glm::vec4 perspective;
    glm::decompose(worldMatrix, scale, rot, trans, skew, perspective);

    const glm::mat3 rotMat = glm::mat3_cast(rot);
    m_Forward = glm::vec3(rotMat * glm::vec3(0.0f, 0.0f, -1.0f));
    if (glm::dot(m_Forward, m_Forward) < 1e-8f)
        m_Forward = glm::vec3(0.0f, 0.0f, -1.0f);
    else
        m_Forward = glm::normalize(m_Forward);

    m_Right = glm::cross(m_Forward, glm::vec3(0.0f, 1.0f, 0.0f));
    if (glm::dot(m_Right, m_Right) < 1e-8f)
        m_Right = glm::normalize(glm::cross(m_Forward, glm::vec3(0.0f, 0.0f, 1.0f)));
    else
        m_Right = glm::normalize(m_Right);
    m_Up = glm::normalize(glm::cross(m_Right, m_Forward));
    m_ViewMatrix = glm::lookAt(m_Position, m_Position + m_Forward, m_Up);

    m_Pitch = glm::degrees(asinf(glm::clamp(m_Forward.y, -1.0f, 1.0f)));
    m_Yaw = glm::degrees(atan2f(m_Forward.z, m_Forward.x));
}

void Camera::SyncFromTransform(const glm::vec3& position, const glm::vec3& eulerDegrees) {
    const glm::mat4 rot = TransformComponent::RotationMatrixFromEuler(eulerDegrees);
    SyncFromWorldMatrix(position, rot);
}

void Camera::SyncTransformFromCamera(TransformComponent& transform) const {
    transform.position = m_Position;

    glm::mat4 world = glm::inverse(m_ViewMatrix);
    glm::mat3 rotMat = glm::mat3(world);
    float x = 0.0f, y = 0.0f, z = 0.0f;
    glm::extractEulerAngleXYZ(glm::mat4(rotMat), x, y, z);
    transform.rotation = glm::degrees(glm::vec3(x, y, z));
}

} // namespace MipsyncEngine
