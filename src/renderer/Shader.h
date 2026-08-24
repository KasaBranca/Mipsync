#pragma once
// ─────────────────────────────────────────────────
// Mipsync Engine — Shader Management
// ─────────────────────────────────────────────────

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

namespace MipsyncEngine {

class Shader {
public:
    Shader() = default;
    Shader(const std::string& vertexSrc, const std::string& fragmentSrc, bool fromFile = false);
    ~Shader();

    // No copy, allow move
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    void Bind() const;
    void Unbind() const;

    // Uniform setters
    void SetInt(const std::string& name, int value);
    void SetFloat(const std::string& name, float value);
    void SetVec2(const std::string& name, const glm::vec2& value);
    void SetVec3(const std::string& name, const glm::vec3& value);
    void SetVec4(const std::string& name, const glm::vec4& value);
    void SetVec4Array(const std::string& name, const glm::vec4* values, int count);
    void SetMat3(const std::string& name, const glm::mat3& value);
    void SetMat4(const std::string& name, const glm::mat4& value);
    void SetMat4Array(const std::string& name, const glm::mat4* values, int count);

    GLuint GetID() const { return m_Program; }
    bool IsValid() const { return m_Program != 0; }

private:
    GLuint CompileShader(GLenum type, const std::string& source);
    GLint GetUniformLocation(const std::string& name);
    std::string LoadFile(const std::string& path);

    GLuint m_Program = 0;
    std::unordered_map<std::string, GLint> m_UniformCache;
};

} // namespace MipsyncEngine
