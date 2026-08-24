#include "Shader.h"
#include "../core/Log.h"
#include <glm/gtc/type_ptr.hpp>
#include <fstream>
#include <sstream>

namespace MipsyncEngine {

Shader::Shader(const std::string& vertexSrc, const std::string& fragmentSrc, bool fromFile) {
    std::string vSrc = fromFile ? LoadFile(vertexSrc) : vertexSrc;
    std::string fSrc = fromFile ? LoadFile(fragmentSrc) : fragmentSrc;

    GLuint vs = CompileShader(GL_VERTEX_SHADER, vSrc);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fSrc);

    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return;
    }

    m_Program = glCreateProgram();
    glAttachShader(m_Program, vs);
    glAttachShader(m_Program, fs);
    glLinkProgram(m_Program);

    GLint success;
    glGetProgramiv(m_Program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetProgramInfoLog(m_Program, sizeof(log), nullptr, log);
        MIPSYNC_ERROR("Shader link error: {0}", log);
        glDeleteProgram(m_Program);
        m_Program = 0;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
}

Shader::~Shader() {
    if (m_Program) glDeleteProgram(m_Program);
}

Shader::Shader(Shader&& other) noexcept : m_Program(other.m_Program), m_UniformCache(std::move(other.m_UniformCache)) {
    other.m_Program = 0;
}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        if (m_Program) glDeleteProgram(m_Program);
        m_Program = other.m_Program;
        m_UniformCache = std::move(other.m_UniformCache);
        other.m_Program = 0;
    }
    return *this;
}

void Shader::Bind() const { glUseProgram(m_Program); }
void Shader::Unbind() const { glUseProgram(0); }

GLuint Shader::CompileShader(GLenum type, const std::string& source) {
    GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        const char* typeName = (type == GL_VERTEX_SHADER) ? "VERTEX" : "FRAGMENT";
        MIPSYNC_ERROR("{0} shader compile error: {1}", typeName, log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLint Shader::GetUniformLocation(const std::string& name) {
    auto it = m_UniformCache.find(name);
    if (it != m_UniformCache.end()) return it->second;
    GLint loc = glGetUniformLocation(m_Program, name.c_str());
    m_UniformCache[name] = loc;
    return loc;
}

std::string Shader::LoadFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        MIPSYNC_ERROR("Failed to open shader file: {0}", path);
        return "";
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

void Shader::SetInt(const std::string& name, int value) { Bind(); glUniform1i(GetUniformLocation(name), value); }
void Shader::SetFloat(const std::string& name, float value) { Bind(); glUniform1f(GetUniformLocation(name), value); }
void Shader::SetVec2(const std::string& name, const glm::vec2& v) { Bind(); glUniform2f(GetUniformLocation(name), v.x, v.y); }
void Shader::SetVec3(const std::string& name, const glm::vec3& v) { Bind(); glUniform3f(GetUniformLocation(name), v.x, v.y, v.z); }
void Shader::SetVec4(const std::string& name, const glm::vec4& v) { Bind(); glUniform4f(GetUniformLocation(name), v.x, v.y, v.z, v.w); }
void Shader::SetVec4Array(const std::string& name, const glm::vec4* values, int count) {
    if (!values || count <= 0) return;
    Bind();
    glUniform4fv(GetUniformLocation(name), count, reinterpret_cast<const GLfloat*>(values));
}
void Shader::SetMat3(const std::string& name, const glm::mat3& m) { Bind(); glUniformMatrix3fv(GetUniformLocation(name), 1, GL_FALSE, glm::value_ptr(m)); }
void Shader::SetMat4(const std::string& name, const glm::mat4& m) {
    Bind();
    glUniformMatrix4fv(GetUniformLocation(name), 1, GL_FALSE, glm::value_ptr(m));
}

void Shader::SetMat4Array(const std::string& name, const glm::mat4* values, int count) {
    if (!values || count <= 0)
        return;
    Bind();
    GLint loc = GetUniformLocation(name);
    if (loc < 0 && name.find('[') == std::string::npos)
        loc = GetUniformLocation(name + "[0]");
    if (loc < 0)
        return;
    glUniformMatrix4fv(loc, count, GL_FALSE, reinterpret_cast<const GLfloat*>(values));
}

} // namespace MipsyncEngine
