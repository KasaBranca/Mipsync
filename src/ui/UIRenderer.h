#pragma once

#include "../renderer/Shader.h"
#include "../renderer/Mesh.h"
#include "../renderer/Texture.h"
#include "../renderer/Framebuffer.h"
#include "../renderer/Camera.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <memory>

namespace MipsyncEngine {

class Scene;

class UIRenderer {
public:
    UIRenderer();
    ~UIRenderer();

    void Init();
    void Shutdown();

    void Render(Scene& scene, const Camera& camera, int viewportWidth, int viewportHeight,
                  Framebuffer* targetFBO, uint32_t activeCameraEntityId, bool sceneView3D = false,
                  int layoutWidth = 0, int layoutHeight = 0);

    /// Supplies pointer coordinates in canvas layout space (origin at bottom-left).
    void SetPointerState(bool active, float x, float y, bool held, bool pressed, bool released);

private:
    void CreateShaders();
    void BeginPass(int viewportWidth, int viewportHeight, Framebuffer* targetFBO);
    void EndPass();

    void DrawSolidQuad(const glm::mat4& mvp, const glm::vec4& color, const Texture* texture);
    void DrawGlyphQuad(const glm::mat4& mvp, const glm::vec4& color, GLuint textureId, const glm::vec3 corners[4],
                       const glm::vec2 uvs[4]);
    /// Unity-style: TextGenerator mesh on the canvas plane (same transform as UIImage / wireframe).
    void DrawCanvasPlaneText(const char* text, float fontSize, const glm::vec4& color, const glm::mat4& canvasWorld,
                             const glm::mat4& view, const glm::mat4& proj, float drawX, float layoutYTop);

    Shader m_UIShader;
    Mesh m_Quad;
    std::unique_ptr<Texture> m_WhiteTexture;
    GLuint m_TextVAO = 0;
    GLuint m_TextVBO = 0;
    int m_ViewportWidth = 1;
    int m_ViewportHeight = 1;
    bool m_InPass = false;
    Framebuffer* m_ActiveFBO = nullptr;
    bool m_PointerActive = false;
    bool m_PointerHeld = false;
    bool m_PointerPressed = false;
    bool m_PointerReleased = false;
    glm::vec2 m_PointerPosition{};
    uint32_t m_PointerPressedButtonEntityId = 0;
};

} // namespace MipsyncEngine
