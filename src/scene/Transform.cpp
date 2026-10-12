#include "Scene.h"
#include <glm/gtc/matrix_transform.hpp>

namespace MipsyncEngine {

glm::mat4 TransformComponent::RotationMatrixFromEuler(const glm::vec3& eulerDegrees) {
    // FPS / camera: yaw around world Y, then pitch around local X, then roll.
    return glm::rotate(glm::mat4(1.0f), glm::radians(eulerDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f))
         * glm::rotate(glm::mat4(1.0f), glm::radians(eulerDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f))
         * glm::rotate(glm::mat4(1.0f), glm::radians(eulerDegrees.z), glm::vec3(0.0f, 0.0f, 1.0f));
}

glm::mat4 TransformComponent::GetTransform() const {
    const glm::mat4 rot = RotationMatrixFromEuler(rotation);
    return glm::translate(glm::mat4(1.0f), position) * rot * glm::scale(glm::mat4(1.0f), scale);
}

} // namespace MipsyncEngine
