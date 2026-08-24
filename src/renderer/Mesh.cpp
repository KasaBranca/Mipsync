#include "Mesh.h"
#include "../core/Log.h"
#include "../assets/AssetManager.h"
#include <ufbx.h>
#include <nlohmann/json.hpp>
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace MipsyncEngine {

Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, bool dynamicVbo,
           bool skipGpuUpload) {
    m_Vertices = vertices;
    m_Indices = indices;
    m_VertexCount = static_cast<uint32_t>(vertices.size());
    m_IndexCount = static_cast<uint32_t>(indices.size());
    ComputeBounds(vertices);
    if (!skipGpuUpload)
        Setup(vertices, indices, dynamicVbo);
}

Mesh::~Mesh() { Cleanup(); }

Mesh::Mesh(Mesh&& o) noexcept
    : m_VAO(o.m_VAO), m_VBO(o.m_VBO), m_EBO(o.m_EBO),
      m_IndexCount(o.m_IndexCount), m_VertexCount(o.m_VertexCount),
      m_BoundsMin(o.m_BoundsMin), m_BoundsMax(o.m_BoundsMax),
      m_Vertices(std::move(o.m_Vertices)),
      m_Indices(std::move(o.m_Indices)) {
    o.m_VAO = o.m_VBO = o.m_EBO = 0;
    o.m_IndexCount = o.m_VertexCount = 0;
    o.m_BoundsMin = o.m_BoundsMax = glm::vec3(0.0f);
}

Mesh& Mesh::operator=(Mesh&& o) noexcept {
    if (this != &o) {
        Cleanup();
        m_VAO = o.m_VAO; m_VBO = o.m_VBO; m_EBO = o.m_EBO;
        m_IndexCount = o.m_IndexCount; m_VertexCount = o.m_VertexCount;
        m_BoundsMin = o.m_BoundsMin; m_BoundsMax = o.m_BoundsMax;
        m_Vertices = std::move(o.m_Vertices);
        m_Indices = std::move(o.m_Indices);
        o.m_VAO = o.m_VBO = o.m_EBO = 0;
        o.m_IndexCount = o.m_VertexCount = 0;
        o.m_BoundsMin = o.m_BoundsMax = glm::vec3(0.0f);
    }
    return *this;
}

void Mesh::ComputeBounds(const std::vector<Vertex>& vertices) {
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
}

void Mesh::UpdateVertexData(const std::vector<Vertex>& vertices, bool keepCpuCopy) {
    if (vertices.size() != m_VertexCount || m_VBO == 0)
        return;
    if (keepCpuCopy)
        m_Vertices = vertices;
    ComputeBounds(vertices);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(m_VertexCount * sizeof(Vertex)), vertices.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Mesh::Setup(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices,
                 bool dynamicVbo) {
    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
    glGenBuffers(1, &m_EBO);

    glBindVertexArray(m_VAO);

    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    const GLenum vboUsage = dynamicVbo ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), vboUsage);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

    // Position (location 0)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
    // Normal (location 1)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    // UV (location 2)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));
    // Color (location 3)
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));

    glBindVertexArray(0);
}

void Mesh::Cleanup() {
    if (m_VAO) glDeleteVertexArrays(1, &m_VAO);
    if (m_VBO) glDeleteBuffers(1, &m_VBO);
    if (m_EBO) glDeleteBuffers(1, &m_EBO);
    m_VAO = m_VBO = m_EBO = 0;
}

void Mesh::Bind() const { glBindVertexArray(m_VAO); }
void Mesh::Unbind() const { glBindVertexArray(0); }

Mesh Mesh::CreateCube(float size) {
    float h = size * 0.5f;
    glm::vec4 white(1.0f);

    std::vector<Vertex> vertices = {
        // Front face
        {{-h, -h,  h}, { 0,  0,  1}, {0, 0}, white},
        {{ h, -h,  h}, { 0,  0,  1}, {1, 0}, white},
        {{ h,  h,  h}, { 0,  0,  1}, {1, 1}, white},
        {{-h,  h,  h}, { 0,  0,  1}, {0, 1}, white},
        // Back face
        {{ h, -h, -h}, { 0,  0, -1}, {0, 0}, white},
        {{-h, -h, -h}, { 0,  0, -1}, {1, 0}, white},
        {{-h,  h, -h}, { 0,  0, -1}, {1, 1}, white},
        {{ h,  h, -h}, { 0,  0, -1}, {0, 1}, white},
        // Top face
        {{-h,  h,  h}, { 0,  1,  0}, {0, 0}, white},
        {{ h,  h,  h}, { 0,  1,  0}, {1, 0}, white},
        {{ h,  h, -h}, { 0,  1,  0}, {1, 1}, white},
        {{-h,  h, -h}, { 0,  1,  0}, {0, 1}, white},
        // Bottom face
        {{-h, -h, -h}, { 0, -1,  0}, {0, 0}, white},
        {{ h, -h, -h}, { 0, -1,  0}, {1, 0}, white},
        {{ h, -h,  h}, { 0, -1,  0}, {1, 1}, white},
        {{-h, -h,  h}, { 0, -1,  0}, {0, 1}, white},
        // Right face
        {{ h, -h,  h}, { 1,  0,  0}, {0, 0}, white},
        {{ h, -h, -h}, { 1,  0,  0}, {1, 0}, white},
        {{ h,  h, -h}, { 1,  0,  0}, {1, 1}, white},
        {{ h,  h,  h}, { 1,  0,  0}, {0, 1}, white},
        // Left face
        {{-h, -h, -h}, {-1,  0,  0}, {0, 0}, white},
        {{-h, -h,  h}, {-1,  0,  0}, {1, 0}, white},
        {{-h,  h,  h}, {-1,  0,  0}, {1, 1}, white},
        {{-h,  h, -h}, {-1,  0,  0}, {0, 1}, white},
    };

    std::vector<uint32_t> indices;
    for (uint32_t i = 0; i < 6; i++) {
        uint32_t base = i * 4;
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }

    return Mesh(vertices, indices);
}

Mesh Mesh::CreatePlane(float size, int subdivisions) {
    float h = size * 0.5f;
    float step = size / static_cast<float>(subdivisions);
    glm::vec4 white(1.0f);

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    for (int z = 0; z <= subdivisions; z++) {
        for (int x = 0; x <= subdivisions; x++) {
            float px = -h + x * step;
            float pz = -h + z * step;
            float u = static_cast<float>(x) / subdivisions;
            float v = static_cast<float>(z) / subdivisions;
            vertices.push_back({{px, 0, pz}, {0, 1, 0}, {u, v}, white});
        }
    }

    for (int z = 0; z < subdivisions; z++) {
        for (int x = 0; x < subdivisions; x++) {
            uint32_t tl = z * (subdivisions + 1) + x;
            uint32_t tr = tl + 1;
            uint32_t bl = (z + 1) * (subdivisions + 1) + x;
            uint32_t br = bl + 1;
            indices.insert(indices.end(), {tl, bl, tr, tr, bl, br});
        }
    }

    return Mesh(vertices, indices);
}

Mesh Mesh::CreateTerrain(float size, int subdivisions, float heightScale, float noiseScale,
                         int seed, bool flat, bool cpuOnly) {
    size = std::max(size, 0.01f);
    subdivisions = std::clamp(subdivisions, 1, 128);
    heightScale = std::max(heightScale, 0.0f);
    noiseScale = std::max(noiseScale, 0.0001f);

    const float half = size * 0.5f;
    const float step = size / static_cast<float>(subdivisions);
    const int row = subdivisions + 1;
    const glm::vec4 white(1.0f);

    auto sampleHeight = [&](float x, float z) {
        if (flat || heightScale <= 0.0f)
            return 0.0f;
        const float sx = x * noiseScale;
        const float sz = z * noiseScale;
        const float s = static_cast<float>(seed);
        const float a = std::sin(sx * 1.37f + s * 0.013f) * std::cos(sz * 1.91f - s * 0.017f);
        const float b = std::sin((sx + sz) * 0.73f + s * 0.071f);
        const float c = std::sin(sx * 3.10f - s * 0.110f) * std::sin(sz * 2.70f + s * 0.050f);
        return (a * 0.55f + b * 0.30f + c * 0.15f) * heightScale;
    };

    std::vector<float> heights(static_cast<size_t>(row * row), 0.0f);
    for (int z = 0; z <= subdivisions; ++z) {
        for (int x = 0; x <= subdivisions; ++x) {
            const float px = -half + static_cast<float>(x) * step;
            const float pz = -half + static_cast<float>(z) * step;
            heights[static_cast<size_t>(z * row + x)] = sampleHeight(px, pz);
        }
    }

    return CreateTerrainFromData(size, subdivisions, heights, {}, cpuOnly);
}

Mesh Mesh::CreateTerrainFromData(float size, int subdivisions,
                                 const std::vector<float>& heights,
                                 const std::vector<glm::vec4>& colors,
                                 bool cpuOnly) {
    size = std::max(size, 0.01f);
    subdivisions = std::clamp(subdivisions, 1, 128);
    const float half = size * 0.5f;
    const float step = size / static_cast<float>(subdivisions);
    const int row = subdivisions + 1;
    const size_t expected = static_cast<size_t>(row * row);
    const glm::vec4 white(1.0f);

    auto hAt = [&](int x, int z) {
        x = std::clamp(x, 0, subdivisions);
        z = std::clamp(z, 0, subdivisions);
        const size_t idx = static_cast<size_t>(z * row + x);
        return idx < heights.size() ? heights[idx] : 0.0f;
    };

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(expected);
    indices.reserve(static_cast<size_t>(subdivisions * subdivisions * 6));

    for (int z = 0; z <= subdivisions; ++z) {
        for (int x = 0; x <= subdivisions; ++x) {
            const float px = -half + static_cast<float>(x) * step;
            const float pz = -half + static_cast<float>(z) * step;
            const float y = hAt(x, z);
            glm::vec3 normal = glm::normalize(glm::vec3(hAt(x - 1, z) - hAt(x + 1, z),
                                                        step * 2.0f,
                                                        hAt(x, z - 1) - hAt(x, z + 1)));
            if (!std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z))
                normal = { 0.0f, 1.0f, 0.0f };
            const float u = static_cast<float>(x) / static_cast<float>(subdivisions);
            const float v = static_cast<float>(z) / static_cast<float>(subdivisions);
            const size_t idx = static_cast<size_t>(z * row + x);
            vertices.push_back({ { px, y, pz }, normal, { u, v }, idx < colors.size() ? colors[idx] : white });
        }
    }

    for (int z = 0; z < subdivisions; ++z) {
        for (int x = 0; x < subdivisions; ++x) {
            const uint32_t tl = static_cast<uint32_t>(z * row + x);
            const uint32_t tr = tl + 1;
            const uint32_t bl = static_cast<uint32_t>((z + 1) * row + x);
            const uint32_t br = bl + 1;
            indices.insert(indices.end(), { tl, bl, tr, tr, bl, br });
        }
    }

    return Mesh(vertices, indices, false, cpuOnly);
}

Mesh Mesh::CreateSphere(float radius, int sectors, int stacks) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    glm::vec4 white(1.0f);

    for (int i = 0; i <= stacks; i++) {
        float stackAngle = static_cast<float>(M_PI) / 2.0f - static_cast<float>(i) * static_cast<float>(M_PI) / stacks;
        float xy = radius * cosf(stackAngle);
        float z  = radius * sinf(stackAngle);

        for (int j = 0; j <= sectors; j++) {
            float sectorAngle = static_cast<float>(j) * 2.0f * static_cast<float>(M_PI) / sectors;
            float x = xy * cosf(sectorAngle);
            float y = xy * sinf(sectorAngle);
            glm::vec3 pos(x, z, y);
            glm::vec3 norm = glm::normalize(pos);
            glm::vec2 uv(static_cast<float>(j) / sectors, static_cast<float>(i) / stacks);
            vertices.push_back({pos, norm, uv, white});
        }
    }

    for (int i = 0; i < stacks; i++) {
        for (int j = 0; j < sectors; j++) {
            uint32_t cur = i * (sectors + 1) + j;
            uint32_t next = cur + sectors + 1;
            // CCW outward-facing winding (was inverted → looked inside-out with back-face culling).
            if (i != 0)          indices.insert(indices.end(), {cur, cur + 1, next});
            if (i != stacks - 1) indices.insert(indices.end(), {cur + 1, next + 1, next});
        }
    }

    return Mesh(vertices, indices);
}

Mesh Mesh::CreateScreenQuad() {
    std::vector<Vertex> vertices = {
        {{-1, -1, 0}, {0,0,1}, {0, 0}, {1,1,1,1}},
        {{ 1, -1, 0}, {0,0,1}, {1, 0}, {1,1,1,1}},
        {{ 1,  1, 0}, {0,0,1}, {1, 1}, {1,1,1,1}},
        {{-1,  1, 0}, {0,0,1}, {0, 1}, {1,1,1,1}},
    };
    std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};
    return Mesh(vertices, indices);
}

namespace {

using json = nlohmann::json;

glm::vec3 ToGlm(const ufbx_vec3& v) { return { v.x, v.y, v.z }; }

std::string ExtensionLower(const std::string& path) {
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos)
        return {};
    std::string ext = path.substr(dot);
    for (char& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

// Default import extent matches CreateCube(1.0f) edge length (PS1 cube contract).
void CenterAndFit(std::vector<Vertex>& vertices, float targetSize = 1.0f) {
    if (vertices.empty())
        return;

    glm::vec3 min = vertices[0].position;
    glm::vec3 max = vertices[0].position;
    for (size_t i = 1; i < vertices.size(); ++i) {
        min = glm::min(min, vertices[i].position);
        max = glm::max(max, vertices[i].position);
    }

    /* Pivot at bottom-center so props sit on y=0 (matches floor placement in scenes). */
    const glm::vec3 pivot((min.x + max.x) * 0.5f, min.y, (min.z + max.z) * 0.5f);
    const glm::vec3 extent = max - min;
    const float maxExtent = std::max({ extent.x, extent.y, extent.z });
    const float scale = (maxExtent > 1e-6f) ? (targetSize / maxExtent) : 1.0f;

    for (auto& v : vertices)
        v.position = (v.position - pivot) * scale;
}

Vertex MakeVertex(const glm::vec3& pos, const glm::vec3& normal, const glm::vec2& uv) {
    Vertex v{};
    v.position = pos;
    v.normal = glm::length(normal) > 1e-6f ? glm::normalize(normal) : glm::vec3(0.0f, 1.0f, 0.0f);
    v.uv = uv;
    v.color = { 1.0f, 1.0f, 1.0f, 1.0f };
    return v;
}

std::vector<uint8_t> ReadBinaryFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size <= 0)
        return {};
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), size);
    return bytes;
}

uint32_t ReadLe32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

int GltfComponentSize(int componentType) {
    switch (componentType) {
    case 5120: case 5121: return 1;
    case 5122: case 5123: return 2;
    case 5125: case 5126: return 4;
    default: return 0;
    }
}

int GltfComponentCount(const std::string& type) {
    if (type == "SCALAR") return 1;
    if (type == "VEC2") return 2;
    if (type == "VEC3") return 3;
    if (type == "VEC4") return 4;
    if (type == "MAT4") return 16;
    return 0;
}

const uint8_t* GltfAccessorPtr(const json& root, const std::vector<uint8_t>& bin,
                               int accessorIndex, size_t elementIndex, size_t* outStride = nullptr) {
    if (accessorIndex < 0 || !root.contains("accessors") ||
        accessorIndex >= static_cast<int>(root["accessors"].size()))
        return nullptr;

    const json& accessor = root["accessors"][accessorIndex];
    const int viewIndex = accessor.value("bufferView", -1);
    if (viewIndex < 0 || !root.contains("bufferViews") ||
        viewIndex >= static_cast<int>(root["bufferViews"].size()))
        return nullptr;

    const json& view = root["bufferViews"][viewIndex];
    const int componentType = accessor.value("componentType", 0);
    const std::string type = accessor.value("type", std::string{});
    const int compSize = GltfComponentSize(componentType);
    const int compCount = GltfComponentCount(type);
    if (compSize <= 0 || compCount <= 0)
        return nullptr;

    const size_t accessorOffset = accessor.value("byteOffset", 0u);
    const size_t viewOffset = view.value("byteOffset", 0u);
    const size_t viewLength = view.value("byteLength", 0u);
    const size_t stride = view.value("byteStride", static_cast<unsigned int>(compSize * compCount));
    const size_t offset = viewOffset + accessorOffset + elementIndex * stride;
    const size_t elementSize = static_cast<size_t>(compSize * compCount);
    if (offset + elementSize > bin.size() || accessorOffset + elementIndex * stride + elementSize >
                                             accessorOffset + viewLength)
        return nullptr;
    if (outStride)
        *outStride = stride;
    return bin.data() + offset;
}

float GltfReadComponent(const uint8_t* p, int componentType, bool normalized) {
    switch (componentType) {
    case 5120: {
        int8_t v;
        std::memcpy(&v, p, sizeof(v));
        return normalized ? std::max(-1.0f, static_cast<float>(v) / 127.0f) : static_cast<float>(v);
    }
    case 5121: {
        uint8_t v = *p;
        return normalized ? static_cast<float>(v) / 255.0f : static_cast<float>(v);
    }
    case 5122: {
        int16_t v;
        std::memcpy(&v, p, sizeof(v));
        return normalized ? std::max(-1.0f, static_cast<float>(v) / 32767.0f) : static_cast<float>(v);
    }
    case 5123: {
        uint16_t v;
        std::memcpy(&v, p, sizeof(v));
        return normalized ? static_cast<float>(v) / 65535.0f : static_cast<float>(v);
    }
    case 5125: {
        uint32_t v;
        std::memcpy(&v, p, sizeof(v));
        return static_cast<float>(v);
    }
    case 5126: {
        float v;
        std::memcpy(&v, p, sizeof(v));
        return v;
    }
    default:
        return 0.0f;
    }
}

glm::vec3 GltfReadVec3(const json& root, const std::vector<uint8_t>& bin, int accessorIndex,
                       size_t elementIndex, const glm::vec3& fallback) {
    if (accessorIndex < 0)
        return fallback;
    const json& accessor = root["accessors"][accessorIndex];
    const int componentType = accessor.value("componentType", 0);
    const bool normalized = accessor.value("normalized", false);
    const uint8_t* p = GltfAccessorPtr(root, bin, accessorIndex, elementIndex);
    const int compSize = GltfComponentSize(componentType);
    if (!p || compSize <= 0)
        return fallback;
    return {
        GltfReadComponent(p + compSize * 0, componentType, normalized),
        GltfReadComponent(p + compSize * 1, componentType, normalized),
        GltfReadComponent(p + compSize * 2, componentType, normalized),
    };
}

glm::vec2 GltfReadVec2(const json& root, const std::vector<uint8_t>& bin, int accessorIndex,
                       size_t elementIndex) {
    if (accessorIndex < 0)
        return glm::vec2(0.0f);
    const json& accessor = root["accessors"][accessorIndex];
    const int componentType = accessor.value("componentType", 0);
    const bool normalized = accessor.value("normalized", false);
    const uint8_t* p = GltfAccessorPtr(root, bin, accessorIndex, elementIndex);
    const int compSize = GltfComponentSize(componentType);
    if (!p || compSize <= 0)
        return glm::vec2(0.0f);
    return {
        GltfReadComponent(p + compSize * 0, componentType, normalized),
        GltfReadComponent(p + compSize * 1, componentType, normalized),
    };
}

uint32_t GltfReadIndex(const json& root, const std::vector<uint8_t>& bin, int accessorIndex,
                       size_t elementIndex) {
    if (accessorIndex < 0)
        return static_cast<uint32_t>(elementIndex);
    const json& accessor = root["accessors"][accessorIndex];
    const int componentType = accessor.value("componentType", 0);
    const uint8_t* p = GltfAccessorPtr(root, bin, accessorIndex, elementIndex);
    if (!p)
        return 0;
    switch (componentType) {
    case 5121: return *p;
    case 5123: {
        uint16_t v;
        std::memcpy(&v, p, sizeof(v));
        return v;
    }
    case 5125: {
        uint32_t v;
        std::memcpy(&v, p, sizeof(v));
        return v;
    }
    default:
        return 0;
    }
}

int GltfAttributeAccessor(const json& primitive, const char* name) {
    if (!primitive.contains("attributes") || !primitive["attributes"].contains(name))
        return -1;
    return primitive["attributes"][name].get<int>();
}

glm::mat4 GltfNodeLocalMatrix(const json& node) {
    if (node.contains("matrix") && node["matrix"].is_array() && node["matrix"].size() >= 16) {
        glm::mat4 m(1.0f);
        for (int i = 0; i < 16; ++i)
            m[i % 4][i / 4] = node["matrix"][i].get<float>();
        return m;
    }

    glm::vec3 t(0.0f);
    glm::vec3 s(1.0f);
    glm::quat r(1.0f, 0.0f, 0.0f, 0.0f);
    if (node.contains("translation") && node["translation"].is_array() &&
        node["translation"].size() >= 3) {
        t = { node["translation"][0].get<float>(), node["translation"][1].get<float>(),
              node["translation"][2].get<float>() };
    }
    if (node.contains("scale") && node["scale"].is_array() && node["scale"].size() >= 3) {
        s = { node["scale"][0].get<float>(), node["scale"][1].get<float>(),
              node["scale"][2].get<float>() };
    }
    if (node.contains("rotation") && node["rotation"].is_array() && node["rotation"].size() >= 4) {
        r = glm::quat(node["rotation"][3].get<float>(), node["rotation"][0].get<float>(),
                      node["rotation"][1].get<float>(), node["rotation"][2].get<float>());
    }
    return glm::translate(glm::mat4(1.0f), t) * glm::mat4_cast(r) *
           glm::scale(glm::mat4(1.0f), s);
}

} // namespace

Mesh Mesh::LoadOBJ(const std::string& path, bool cpuOnly) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    const std::string mtlDir = PathUtf8::ToString(
        PathUtf8::FromString(path).parent_path());

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str(), mtlDir.c_str())) {
        MIPSYNC_ERROR("OBJ load failed {}: {}", path, err);
        return CreateCube(1.0f);
    }
    if (!warn.empty())
        MIPSYNC_WARN("OBJ warning {}: {}", path, warn);

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    for (const auto& shape : shapes) {
        size_t indexOffset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f) {
            const int fv = shape.mesh.num_face_vertices[f];
            if (fv < 3) {
                indexOffset += static_cast<size_t>(fv);
                continue;
            }

            auto emitCorner = [&](int corner) {
                const tinyobj::index_t idx = shape.mesh.indices[indexOffset + static_cast<size_t>(corner)];
                glm::vec3 pos(0.0f);
                if (idx.vertex_index >= 0) {
                    const float* vp = &attrib.vertices[3 * idx.vertex_index];
                    pos = { vp[0], vp[1], vp[2] };
                }
                glm::vec3 normal(0.0f, 1.0f, 0.0f);
                if (idx.normal_index >= 0) {
                    const float* np = &attrib.normals[3 * idx.normal_index];
                    normal = { np[0], np[1], np[2] };
                }
                glm::vec2 uv(0.0f);
                if (idx.texcoord_index >= 0) {
                    const float* tp = &attrib.texcoords[2 * idx.texcoord_index];
                    uv = { tp[0], tp[1] };
                }
                vertices.push_back(MakeVertex(pos, normal, uv));
                indices.push_back(static_cast<uint32_t>(vertices.size() - 1));
            };

            emitCorner(0);
            for (int tri = 1; tri + 1 < fv; ++tri) {
                emitCorner(tri);
                emitCorner(tri + 1);
            }
            indexOffset += static_cast<size_t>(fv);
        }
    }

    if (vertices.empty()) {
        MIPSYNC_WARN("OBJ contained no geometry: {}", path);
        return CreateCube(1.0f);
    }

    CenterAndFit(vertices);
    MIPSYNC_INFO("Loaded OBJ: {} ({} verts, {} tris)", path, vertices.size(), indices.size() / 3);
    return Mesh(vertices, indices, false, cpuOnly);
}

Mesh Mesh::LoadFBX(const std::string& path, bool cpuOnly) {
    ufbx_load_opts opts{};
    opts.generate_missing_normals = true;
    ufbx_error error{};
    ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &error);
    if (!scene) {
        MIPSYNC_ERROR("FBX load failed {}: {}", path, error.description.data);
        return CreateCube(1.0f);
    }

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    auto appendMesh = [&](const ufbx_mesh* mesh, const ufbx_matrix& geometryToWorld) {
        const ufbx_matrix normalMatrix = ufbx_matrix_for_normals(&geometryToWorld);
        const ufbx_vec2 defaultUv{ 0.0f, 0.0f };

        std::vector<uint32_t> triIndices(mesh->max_face_triangles * 3);
        if (triIndices.empty())
            triIndices.resize(3);

        for (size_t fi = 0; fi < mesh->num_faces; ++fi) {
            const ufbx_face face = mesh->faces.data[fi];
            if (face.num_indices < 3)
                continue;

            const size_t numTris =
                ufbx_triangulate_face(triIndices.data(), triIndices.size(), mesh, face);
            if (numTris == 0)
                continue;

            for (size_t ti = 0; ti < numTris * 3; ++ti) {
                const uint32_t ix = triIndices[ti];
                const ufbx_vec3 pos = ufbx_get_vertex_vec3(&mesh->vertex_position, ix);
                const ufbx_vec3 nrm = ufbx_get_vertex_vec3(&mesh->vertex_normal, ix);
                const ufbx_vec2 uv =
                    mesh->vertex_uv.exists ? ufbx_get_vertex_vec2(&mesh->vertex_uv, ix) : defaultUv;

                const ufbx_vec3 wpos = ufbx_transform_position(&geometryToWorld, pos);
                const ufbx_vec3 wnrm = ufbx_transform_direction(&normalMatrix, nrm);

                vertices.push_back(MakeVertex(ToGlm(wpos), ToGlm(wnrm), { uv.x, uv.y }));
                indices.push_back(static_cast<uint32_t>(vertices.size() - 1));
            }
        }
    };

    for (size_t mi = 0; mi < scene->meshes.count; ++mi) {
        const ufbx_mesh* mesh = scene->meshes.data[mi];
        if (!mesh)
            continue;

        if (mesh->instances.count > 0) {
            for (size_t ni = 0; ni < mesh->instances.count; ++ni) {
                const ufbx_node* node = mesh->instances.data[ni];
                const ufbx_matrix world =
                    node ? node->geometry_to_world : ufbx_identity_matrix;
                appendMesh(mesh, world);
            }
        } else {
            appendMesh(mesh, ufbx_identity_matrix);
        }
    }

    ufbx_free_scene(scene);

    if (vertices.empty()) {
        MIPSYNC_WARN("FBX contained no geometry: {}", path);
        return CreateCube(1.0f);
    }

    CenterAndFit(vertices);
    MIPSYNC_INFO("Loaded FBX: {} ({} verts, {} tris)", path, vertices.size(), indices.size() / 3);
    return Mesh(vertices, indices, false, cpuOnly);
}

Mesh Mesh::LoadGLB(const std::string& path, bool cpuOnly) {
    const std::vector<uint8_t> bytes = ReadBinaryFile(path);
    if (bytes.size() < 20) {
        MIPSYNC_ERROR("GLB load failed {}: file too small", path);
        return CreateCube(1.0f);
    }

    if (ReadLe32(bytes.data()) != 0x46546C67u || ReadLe32(bytes.data() + 4) != 2u) {
        MIPSYNC_ERROR("GLB load failed {}: unsupported header", path);
        return CreateCube(1.0f);
    }

    const uint32_t fileLength = ReadLe32(bytes.data() + 8);
    if (fileLength > bytes.size()) {
        MIPSYNC_ERROR("GLB load failed {}: truncated file", path);
        return CreateCube(1.0f);
    }

    std::string jsonText;
    std::vector<uint8_t> bin;
    size_t offset = 12;
    while (offset + 8 <= bytes.size()) {
        const uint32_t chunkLength = ReadLe32(bytes.data() + offset);
        const uint32_t chunkType = ReadLe32(bytes.data() + offset + 4);
        offset += 8;
        if (offset + chunkLength > bytes.size())
            break;
        if (chunkType == 0x4E4F534Au) {
            jsonText.assign(reinterpret_cast<const char*>(bytes.data() + offset), chunkLength);
        } else if (chunkType == 0x004E4942u) {
            bin.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                       bytes.begin() + static_cast<std::ptrdiff_t>(offset + chunkLength));
        }
        offset += (chunkLength + 3u) & ~3u;
    }

    if (jsonText.empty() || bin.empty()) {
        MIPSYNC_ERROR("GLB load failed {}: missing JSON or BIN chunk", path);
        return CreateCube(1.0f);
    }

    json root;
    try {
        root = json::parse(jsonText);
    } catch (const std::exception& ex) {
        MIPSYNC_ERROR("GLB JSON parse failed {}: {}", path, ex.what());
        return CreateCube(1.0f);
    }

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    auto appendMesh = [&](int meshIndex, const glm::mat4& world) {
        if (!root.contains("meshes") || meshIndex < 0 ||
            meshIndex >= static_cast<int>(root["meshes"].size()))
            return;

        const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(world)));
        const json& mesh = root["meshes"][meshIndex];
        if (!mesh.contains("primitives") || !mesh["primitives"].is_array())
            return;

        for (const json& prim : mesh["primitives"]) {
            const int mode = prim.value("mode", 4);
            if (mode != 4)
                continue;

            const int posAccessor = GltfAttributeAccessor(prim, "POSITION");
            if (posAccessor < 0)
                continue;
            const int nrmAccessor = GltfAttributeAccessor(prim, "NORMAL");
            const int uvAccessor = GltfAttributeAccessor(prim, "TEXCOORD_0");
            const int idxAccessor = prim.value("indices", -1);

            const size_t posCount = root["accessors"][posAccessor].value("count", 0u);
            const size_t indexCount =
                idxAccessor >= 0 ? root["accessors"][idxAccessor].value("count", 0u) : posCount;
            if (indexCount < 3)
                continue;

            for (size_t ii = 0; ii + 2 < indexCount; ii += 3) {
                const uint32_t cornerIndex[3] = {
                    GltfReadIndex(root, bin, idxAccessor, ii + 0),
                    GltfReadIndex(root, bin, idxAccessor, ii + 1),
                    GltfReadIndex(root, bin, idxAccessor, ii + 2),
                };

                const uint32_t base = static_cast<uint32_t>(vertices.size());
                glm::vec3 facePos[3];
                glm::vec3 faceNrm[3];
                glm::vec2 faceUv[3];

                for (int ci = 0; ci < 3; ++ci) {
                    const size_t src = cornerIndex[ci];
                    facePos[ci] = GltfReadVec3(root, bin, posAccessor, src, glm::vec3(0.0f));
                    faceNrm[ci] = GltfReadVec3(root, bin, nrmAccessor, src, glm::vec3(0.0f, 1.0f, 0.0f));
                    faceUv[ci] = GltfReadVec2(root, bin, uvAccessor, src);
                    facePos[ci] = glm::vec3(world * glm::vec4(facePos[ci], 1.0f));
                    faceNrm[ci] = glm::normalize(normalMatrix * faceNrm[ci]);
                }

                if (nrmAccessor < 0) {
                    const glm::vec3 computed = glm::normalize(glm::cross(facePos[1] - facePos[0],
                                                                         facePos[2] - facePos[0]));
                    faceNrm[0] = faceNrm[1] = faceNrm[2] =
                        glm::length(computed) > 1e-6f ? computed : glm::vec3(0.0f, 1.0f, 0.0f);
                }

                for (int ci = 0; ci < 3; ++ci) {
                    vertices.push_back(MakeVertex(facePos[ci], faceNrm[ci], faceUv[ci]));
                    indices.push_back(base + static_cast<uint32_t>(ci));
                }
            }
        }
    };

    std::function<void(int, const glm::mat4&)> visitNode = [&](int nodeIndex, const glm::mat4& parent) {
        if (!root.contains("nodes") || nodeIndex < 0 ||
            nodeIndex >= static_cast<int>(root["nodes"].size()))
            return;
        const json& node = root["nodes"][nodeIndex];
        const glm::mat4 world = parent * GltfNodeLocalMatrix(node);
        if (node.contains("mesh"))
            appendMesh(node["mesh"].get<int>(), world);
        if (node.contains("children") && node["children"].is_array()) {
            for (const json& child : node["children"])
                visitNode(child.get<int>(), world);
        }
    };

    bool visitedScene = false;
    const int sceneIndex = root.value("scene", 0);
    if (root.contains("scenes") && sceneIndex >= 0 &&
        sceneIndex < static_cast<int>(root["scenes"].size())) {
        const json& scene = root["scenes"][sceneIndex];
        if (scene.contains("nodes") && scene["nodes"].is_array()) {
            for (const json& node : scene["nodes"])
                visitNode(node.get<int>(), glm::mat4(1.0f));
            visitedScene = true;
        }
    }

    if (!visitedScene && root.contains("nodes")) {
        for (int ni = 0; ni < static_cast<int>(root["nodes"].size()); ++ni)
            visitNode(ni, glm::mat4(1.0f));
    } else if (!visitedScene && root.contains("meshes")) {
        for (int mi = 0; mi < static_cast<int>(root["meshes"].size()); ++mi)
            appendMesh(mi, glm::mat4(1.0f));
    }

    if (vertices.empty()) {
        MIPSYNC_WARN("GLB contained no triangle mesh geometry: {}", path);
        return CreateCube(1.0f);
    }

    CenterAndFit(vertices);
    MIPSYNC_INFO("Loaded GLB: {} ({} verts, {} tris)", path, vertices.size(), indices.size() / 3);
    return Mesh(vertices, indices, false, cpuOnly);
}

Mesh Mesh::LoadFromFileCpu(const std::string& path) {
    const std::string ext = ExtensionLower(path);
    if (ext == ".obj")
        return LoadOBJ(path, true);
    if (ext == ".fbx")
        return LoadFBX(path, true);
    if (ext == ".glb")
        return LoadGLB(path, true);
    MIPSYNC_WARN("Unsupported mesh format '{}': {}", ext, path);
    return CreateCube(1.0f);
}

Mesh Mesh::LoadFromFile(const std::string& path) {
    const std::string ext = ExtensionLower(path);
    if (ext == ".obj")
        return LoadOBJ(path, false);
    if (ext == ".fbx")
        return LoadFBX(path, false);
    if (ext == ".glb")
        return LoadGLB(path, false);
    MIPSYNC_WARN("Unsupported mesh format '{}': {}", ext, path);
    return CreateCube(1.0f);
}

} // namespace MipsyncEngine
