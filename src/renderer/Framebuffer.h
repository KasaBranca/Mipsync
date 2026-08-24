#pragma once
// ─────────────────────────────────────────────────
// Mipsync Engine — Framebuffer (low-res PS1 rendering)
// ─────────────────────────────────────────────────

#include <glad/glad.h>

namespace MipsyncEngine {

class Framebuffer {
public:
    Framebuffer() = default;
    Framebuffer(int width, int height);
    ~Framebuffer();

    Framebuffer(Framebuffer&& o) noexcept;
    Framebuffer& operator=(Framebuffer&& o) noexcept;
    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    void Bind() const;
    void Unbind() const;
    void Resize(int width, int height);

    GLuint GetColorAttachment() const { return m_ColorTexture; }
    GLuint GetDepthAttachment() const { return m_DepthTexture; }
    int GetWidth() const { return m_Width; }
    int GetHeight() const { return m_Height; }

private:
    void Create();
    void Cleanup();

    GLuint m_FBO = 0;
    GLuint m_ColorTexture = 0;
    GLuint m_DepthTexture = 0;
    int m_Width = 320, m_Height = 240;  // PS1 default resolution
};

} // namespace MipsyncEngine
