#pragma once
// ─────────────────────────────────────────────────
// Mipsync Engine — PS1 Renderer
// ─────────────────────────────────────────────────

#include "Shader.h"
#include "Mesh.h"
#include "Texture.h"
#include "Camera.h"
#include "Framebuffer.h"
#include <glm/glm.hpp>
#include <memory>

namespace MipsyncEngine {

// PS1 rendering parameters (adjustable from editor)
struct PS1Settings {
    float vertexJitter      = 160.0f;  // Snapping resolution (lower = more jitter)
    bool  affineMapping     = true;    // Affine texture mapping
    bool  colorDepthLimit   = true;    // 15-bit color (5-5-5)
    bool  ditheringEnabled  = true;    // Ordered dithering
    bool  fogEnabled        = true;
    float fogStart          = 10.0f;
    float fogEnd            = 40.0f;
    glm::vec3 fogColor      = { 0.05f, 0.05f, 0.08f };
    bool  colorGradingEnabled = false;
    float exposure          = 0.0f;    // EV-style bias: +1 = twice as bright
    float contrast          = 1.0f;
    float saturation        = 1.0f;
    glm::vec3 colorFilter   = { 1.0f, 1.0f, 1.0f };
    bool  vignetteEnabled   = false;
    float vignetteIntensity = 0.35f;
    float vignetteSmoothness = 0.45f;
    glm::vec3 vignetteColor = { 0.0f, 0.0f, 0.0f };
    int   internalWidth     = 320;
    int   internalHeight    = 240;
    bool  wireframeMode     = false;
};

struct LightData {
    glm::vec3 direction = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f));
    glm::vec3 color     = { 1.0f, 0.95f, 0.85f };
    float     ambient   = 0.15f;
};

/// Packed scene light for vertex Gouraud shading (max count in shader).
struct SceneLightGpu {
    glm::vec4 posType{};    // xyz = world position, w = type (0 dir, 1 point, 2 spot)
    glm::vec4 dirRange{};   // xyz = world forward (spot/directional), w = range
    glm::vec4 colorIntensity{}; // rgb = color, a = intensity
    glm::vec4 spotParams{}; // x = cos(outer), y = cos(inner)
};

constexpr int kMaxSceneLights = 8;

enum class RenderMode : int {
    Shaded = 0,
    Wireframe,
    ShadedWireframe,
    Unlit,
    Normals,
};

class SkinnedMesh;

class Renderer {
public:
    Renderer();
    ~Renderer();

    void Init();
    void Shutdown();

    // Begin/End frame
    void BeginScene(const Camera& camera, Framebuffer* targetFBO = nullptr,
                    const Texture* backgroundTexture = nullptr);
    void EndScene(Framebuffer* targetFBO = nullptr);

    // Draw commands
    void DrawMesh(const Mesh& mesh, const glm::mat4& transform, const Texture* texture = nullptr,
                  const glm::vec4& materialColor = glm::vec4(1.0f),
                  const glm::vec2& textureTiling = glm::vec2(1.0f),
                  const glm::vec2& textureOffset = glm::vec2(0.0f),
                  uint32_t indexOffset = 0, uint32_t indexCount = 0,
                  float projectionDepthClamp = 0.0f, bool doubleSided = false);
    void DrawMeshDepthOnly(const Mesh& mesh, const glm::mat4& transform);

    void DrawSkinnedMesh(const SkinnedMesh& mesh, const glm::mat4& entityWorld,
                         const glm::mat4& partGeometryToWorld, const glm::vec3& displayCenter,
                         float displayScale, const glm::mat4* boneMatrices, int boneCount,
                         const Texture* texture = nullptr,
                         const glm::vec4& materialColor = glm::vec4(1.0f),
                         const glm::vec2& textureTiling = glm::vec2(1.0f),
                         const glm::vec2& textureOffset = glm::vec2(0.0f),
                         uint32_t indexOffset = 0, uint32_t indexCount = 0);

    // Render mode (resets to Shaded after EndScene)
    void SetRenderMode(RenderMode mode) { m_RenderMode = mode; }
    RenderMode GetRenderMode() const { return m_RenderMode; }

    // Post-process selection outline: draws a clean N-pixel outline around the
    // silhouette of `mesh` directly on top of `targetFBO` using a mask + edge pass.
    void RenderSelectionOutline(Framebuffer& targetFBO, const Mesh& mesh,
                                const glm::mat4& transform,
                                const glm::vec3& color, float pixelWidth = 3.0f);

    // Access
    GLuint GetOutputTexture() const;
    PS1Settings& GetPS1Settings() { return m_PS1Settings; }
    LightData& GetLightData() { return m_Light; }

    void SetSkyboxTexture(const Texture* texture, const glm::vec3& tint,
                          float exposure, float rotationDegrees);
    void ClearSkyboxTexture();

    /// Scene lights for vertex shading; pass count 0 to use legacy single directional uLightDir.
    void SetSceneLights(const SceneLightGpu* lights, int count);
    const Framebuffer& GetFramebuffer() const { return m_PS1Framebuffer; }
    Framebuffer& GetRenderTarget(Framebuffer* targetFBO) {
        return targetFBO ? *targetFBO : m_PS1Framebuffer;
    }

    void ResizeOutput(int width, int height);

    /// Reset GL state after off-screen passes (FBO render, thumbnails, outline).
    static void RestoreDefaultOpenGLState();

private:
    void CreateShaders();
    void CreateDefaultResources();

    Shader m_PS1Shader;
    Shader m_PS1SkinnedShader;
    Shader m_ScreenShader;
    Shader m_SkyboxShader;
    Shader m_OutlineMaskShader;     // Renders silhouette to mask FBO
    Shader m_OutlineComposeShader;  // Edge-detects mask and writes outline pixels
    /// Grow-only: Scene/Game views use different sizes; avoid reallocating every frame.
    void EnsureOutlineMaskFBO(int width, int height);
    Framebuffer m_PS1Framebuffer;
    Framebuffer m_OutlineMaskFBO;
    Mesh m_ScreenQuad;
    Texture m_DefaultTexture;

    PS1Settings m_PS1Settings;
    LightData m_Light;
    int m_SceneLightCount = 0;
    SceneLightGpu m_SceneLights[kMaxSceneLights]{};
    RenderMode m_RenderMode = RenderMode::Shaded;

    const Texture* m_SkyboxTexture = nullptr;
    glm::vec3 m_SkyboxTint{1.0f, 1.0f, 1.0f};
    float m_SkyboxExposure = 0.0f;
    float m_SkyboxRotationDegrees = 0.0f;

    // Current frame state
    glm::mat4 m_ViewMatrix;
    glm::mat4 m_ProjectionMatrix;
    int m_ViewportWidth = 1;
    int m_ViewportHeight = 1;
};

} // namespace MipsyncEngine
