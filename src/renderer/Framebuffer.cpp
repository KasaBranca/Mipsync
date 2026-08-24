#include "Framebuffer.h"
#include "../core/Log.h"

namespace MipsyncEngine {

Framebuffer::Framebuffer(int width, int height) : m_Width(width), m_Height(height) {
    Create();
}

Framebuffer::~Framebuffer() { Cleanup(); }

Framebuffer::Framebuffer(Framebuffer&& o) noexcept 
    : m_FBO(o.m_FBO), m_ColorTexture(o.m_ColorTexture), m_DepthTexture(o.m_DepthTexture),
      m_Width(o.m_Width), m_Height(o.m_Height) {
    o.m_FBO = o.m_ColorTexture = o.m_DepthTexture = 0;
}

Framebuffer& Framebuffer::operator=(Framebuffer&& o) noexcept {
    if (this != &o) {
        Cleanup();
        m_FBO = o.m_FBO; m_ColorTexture = o.m_ColorTexture; m_DepthTexture = o.m_DepthTexture;
        m_Width = o.m_Width; m_Height = o.m_Height;
        o.m_FBO = o.m_ColorTexture = o.m_DepthTexture = 0;
    }
    return *this;
}

void Framebuffer::Create() {
    if (m_FBO) Cleanup();

    glGenFramebuffers(1, &m_FBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);

    // Color attachment (texture for ImGui::Image)
    glGenTextures(1, &m_ColorTexture);
    glBindTexture(GL_TEXTURE_2D, m_ColorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_Width, m_Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    // PS1: nearest neighbor (no filtering on upscale)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ColorTexture, 0);

    // Depth attachment (texture so shadow pass can depth-test against the scene)
    glGenTextures(1, &m_DepthTexture);
    glBindTexture(GL_TEXTURE_2D, m_DepthTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, m_Width, m_Height, 0, GL_DEPTH_COMPONENT,
                 GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_DepthTexture, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        MIPSYNC_ERROR("Framebuffer is not complete!");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::Bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);
    glViewport(0, 0, m_Width, m_Height);
}

void Framebuffer::Unbind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::Resize(int width, int height) {
    if (width <= 0 || height <= 0 || (width == m_Width && height == m_Height)) return;
    m_Width = width;
    m_Height = height;
    Create();
}

void Framebuffer::Cleanup() {
    if (m_FBO) glDeleteFramebuffers(1, &m_FBO);
    if (m_ColorTexture) glDeleteTextures(1, &m_ColorTexture);
    if (m_DepthTexture) glDeleteTextures(1, &m_DepthTexture);
    m_FBO = m_ColorTexture = m_DepthTexture = 0;
}

} // namespace MipsyncEngine
