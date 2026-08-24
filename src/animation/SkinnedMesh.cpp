#include "SkinnedMesh.h"
#include <algorithm>

namespace MipsyncEngine {

SkinnedMesh::SkinnedMesh(const std::vector<SkinnedVertex>& vertices, const std::vector<uint32_t>& indices) {
    Setup(vertices, indices);
}

SkinnedMesh::~SkinnedMesh() { Cleanup(); }

SkinnedMesh::SkinnedMesh(SkinnedMesh&& o) noexcept
    : m_VAO(o.m_VAO), m_VBO(o.m_VBO), m_EBO(o.m_EBO), m_IndexCount(o.m_IndexCount),
      m_BoundsMin(o.m_BoundsMin), m_BoundsMax(o.m_BoundsMax) {
    o.m_VAO = o.m_VBO = o.m_EBO = 0;
    o.m_IndexCount = 0;
}

SkinnedMesh& SkinnedMesh::operator=(SkinnedMesh&& o) noexcept {
    if (this != &o) {
        Cleanup();
        m_VAO = o.m_VAO;
        m_VBO = o.m_VBO;
        m_EBO = o.m_EBO;
        m_IndexCount = o.m_IndexCount;
        m_BoundsMin = o.m_BoundsMin;
        m_BoundsMax = o.m_BoundsMax;
        o.m_VAO = o.m_VBO = o.m_EBO = 0;
        o.m_IndexCount = 0;
    }
    return *this;
}

void SkinnedMesh::Setup(const std::vector<SkinnedVertex>& vertices, const std::vector<uint32_t>& indices) {
    m_IndexCount = static_cast<uint32_t>(indices.size());
    if (vertices.empty()) {
        m_BoundsMin = m_BoundsMax = glm::vec3(0.0f);
        return;
    }

    m_BoundsMin = vertices[0].position;
    m_BoundsMax = vertices[0].position;
    for (size_t i = 1; i < vertices.size(); ++i) {
        m_BoundsMin = glm::min(m_BoundsMin, vertices[i].position);
        m_BoundsMax = glm::max(m_BoundsMax, vertices[i].position);
    }

    if (!glGenVertexArrays || !glGenBuffers || !glBindVertexArray || !glBufferData)
        return;

    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
    glGenBuffers(1, &m_EBO);

    glBindVertexArray(m_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(SkinnedVertex), vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex),
                          reinterpret_cast<void*>(offsetof(SkinnedVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex),
                          reinterpret_cast<void*>(offsetof(SkinnedVertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex),
                          reinterpret_cast<void*>(offsetof(SkinnedVertex, uv)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex),
                          reinterpret_cast<void*>(offsetof(SkinnedVertex, color)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex),
                          reinterpret_cast<void*>(offsetof(SkinnedVertex, boneIndices)));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex),
                          reinterpret_cast<void*>(offsetof(SkinnedVertex, boneWeights)));

    glBindVertexArray(0);
}

void SkinnedMesh::Cleanup() {
    if (m_VAO) glDeleteVertexArrays(1, &m_VAO);
    if (m_VBO) glDeleteBuffers(1, &m_VBO);
    if (m_EBO) glDeleteBuffers(1, &m_EBO);
    m_VAO = m_VBO = m_EBO = 0;
}

void SkinnedMesh::Bind() const { glBindVertexArray(m_VAO); }
void SkinnedMesh::Unbind() const { glBindVertexArray(0); }

} // namespace MipsyncEngine
