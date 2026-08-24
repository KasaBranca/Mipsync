#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace MipsyncEngine {

struct SkinnedVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 color{ 1.0f };
    glm::vec4 boneIndices{ 0.0f };
    glm::vec4 boneWeights{ 0.0f };
};

class SkinnedMesh {
public:
    SkinnedMesh() = default;
    SkinnedMesh(const std::vector<SkinnedVertex>& vertices, const std::vector<uint32_t>& indices);
    ~SkinnedMesh();

    SkinnedMesh(SkinnedMesh&& other) noexcept;
    SkinnedMesh& operator=(SkinnedMesh&& other) noexcept;
    SkinnedMesh(const SkinnedMesh&) = delete;
    SkinnedMesh& operator=(const SkinnedMesh&) = delete;

    void Bind() const;
    void Unbind() const;
    uint32_t GetIndexCount() const { return m_IndexCount; }
    const glm::vec3& GetBoundsMin() const { return m_BoundsMin; }
    const glm::vec3& GetBoundsMax() const { return m_BoundsMax; }

private:
    void Setup(const std::vector<SkinnedVertex>& vertices, const std::vector<uint32_t>& indices);
    void Cleanup();

    GLuint m_VAO = 0, m_VBO = 0, m_EBO = 0;
    uint32_t m_IndexCount = 0;
    glm::vec3 m_BoundsMin{ 0.0f };
    glm::vec3 m_BoundsMax{ 0.0f };
};

} // namespace MipsyncEngine
