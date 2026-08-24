#pragma once
// ─────────────────────────────────────────────────
// Mipsync Engine — Mesh (VAO/VBO/EBO)
// ─────────────────────────────────────────────────

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

namespace MipsyncEngine {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 color;
};

class Mesh {
public:
    Mesh() = default;
    Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices,
         bool dynamicVbo = false, bool skipGpuUpload = false);
    ~Mesh();

    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    void Bind() const;
    void Unbind() const;
    /// Updates vertex positions/normals in place (VBO must be GL_DYNAMIC_DRAW).
    /// When keepCpuCopy is false, skips mirroring into m_Vertices (faster skinned meshes).
    void UpdateVertexData(const std::vector<Vertex>& vertices, bool keepCpuCopy = true);
    uint32_t GetIndexCount() const { return m_IndexCount; }
    uint32_t GetVertexCount() const { return m_VertexCount; }
    const glm::vec3& GetBoundsMin() const { return m_BoundsMin; }
    const glm::vec3& GetBoundsMax() const { return m_BoundsMax; }
    const std::vector<Vertex>& GetVertices() const { return m_Vertices; }
    const std::vector<uint32_t>& GetIndices() const { return m_Indices; }

    // Primitive generators
    static Mesh CreateCube(float size = 1.0f);
    static Mesh CreatePlane(float size = 10.0f, int subdivisions = 1);
    static Mesh CreateTerrain(float size = 32.0f, int subdivisions = 32,
                              float heightScale = 2.0f, float noiseScale = 0.18f,
                              int seed = 1337, bool flat = false,
                              bool cpuOnly = false);
    static Mesh CreateTerrainFromData(float size, int subdivisions,
                                      const std::vector<float>& heights,
                                      const std::vector<glm::vec4>& colors,
                                      bool cpuOnly = false);
    static Mesh CreateSphere(float radius = 0.5f, int sectors = 16, int stacks = 12);
    static Mesh CreateScreenQuad();

    /// Load mesh from file (.obj, .fbx, .glb). Centers and scales to unit extent (matches CreateCube(1)).
    static Mesh LoadFromFile(const std::string& path);
    /// Same geometry as LoadFromFile but does not touch OpenGL (for PS1 export / headless).
    static Mesh LoadFromFileCpu(const std::string& path);
    static Mesh LoadOBJ(const std::string& path, bool cpuOnly = false);
    static Mesh LoadFBX(const std::string& path, bool cpuOnly = false);
    static Mesh LoadGLB(const std::string& path, bool cpuOnly = false);

private:
    void Setup(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices,
               bool dynamicVbo = false);
    void Cleanup();

    void ComputeBounds(const std::vector<Vertex>& vertices);

    GLuint m_VAO = 0, m_VBO = 0, m_EBO = 0;
    uint32_t m_IndexCount = 0;
    uint32_t m_VertexCount = 0;
    glm::vec3 m_BoundsMin = { 0.0f, 0.0f, 0.0f };
    glm::vec3 m_BoundsMax = { 0.0f, 0.0f, 0.0f };
    std::vector<Vertex> m_Vertices;
    std::vector<uint32_t> m_Indices;
};

} // namespace MipsyncEngine
