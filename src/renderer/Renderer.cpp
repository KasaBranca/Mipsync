#include "Renderer.h"
#include "ProjectRenderer.h"
#include "../core/AssetFiles.h"
#include <chrono>
#include <stdexcept>
#include "../ps1/PSyncGeometry.h"
#include "../ps1/Ps1ColorGrading.h"
#include "../animation/AnimationTypes.h"
#include "../animation/SkinnedMesh.h"
#include "../core/Log.h"
#include <glad/glad.h>
#include <algorithm>
#include <cstring>
#include <bit>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace MipsyncEngine {

namespace {
void SetColorGradingUniforms(Shader& shader, const PS1Settings& settings) {
    shader.SetInt("uColorGradingEnabled",settings.colorGradingEnabled ? 1 : 0);
    if (!settings.colorGradingEnabled) return;
    const auto transform=Ps1::BuildColorGradingTransform({true,settings.exposure,settings.contrast,
        settings.saturation,{settings.colorFilter.r,settings.colorFilter.g,settings.colorFilter.b}});
    glm::mat3 matrix;
    for (int row=0; row<3; ++row) for (int col=0; col<3; ++col)
        matrix[col][row]=static_cast<float>(transform.matrix[row*3+col]);
    shader.SetMat3("uColorGradingMatrix",matrix);
    shader.SetVec3("uColorGradingOffset",glm::vec3(transform.offset[0],transform.offset[1],transform.offset[2]));
}
uint64_t PSyncMatrixSignature(const glm::mat4& matrix) {
    uint64_t hash = 1469598103934665603ULL;
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
        hash = (hash ^ std::bit_cast<uint32_t>(matrix[c][r])) * 1099511628211ULL;
    return hash;
}
uint64_t PSyncLightSignature(const SceneLightGpu* lights, int count, const glm::vec4& color) {
    uint64_t hash = 1469598103934665603ULL;
    const auto* bytes = reinterpret_cast<const unsigned char*>(lights);
    for (size_t i = 0; i < sizeof(SceneLightGpu) * count; ++i) hash = (hash ^ bytes[i]) * 1099511628211ULL;
    for (int i = 0; i < 4; ++i) hash = (hash ^ std::bit_cast<uint32_t>(color[i])) * 1099511628211ULL;
    return hash;
}
}

// ─────────────────────────────────────────────
// Implementation
// ─────────────────────────────────────────────

Renderer::Renderer() {}
Renderer::~Renderer() { Shutdown(); }

void Renderer::Init() {
    MIPSYNC_INFO("Initializing PS1-style renderer...");

    // Global OpenGL state
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    CreateShaders();
    CreateDefaultResources();

    // PS1 resolution framebuffer
    m_PS1Framebuffer = Framebuffer(m_PS1Settings.internalWidth, m_PS1Settings.internalHeight);

    MIPSYNC_INFO("PS1 Renderer initialized ({}x{} internal resolution)", m_PS1Settings.internalWidth, m_PS1Settings.internalHeight);
}

void Renderer::Shutdown() {
    MIPSYNC_INFO("Shutting down renderer");
}

void Renderer::CreateShaders() {
    const auto& defaults = ProjectRenderer::DefaultShaders();
    m_BackgroundShader = Shader(defaults.at("screen.vert"), defaults.at("screen.frag"));
    if (!ApplyShaderSources(ProjectRenderer::DefaultShaders()))
        throw std::runtime_error("Failed to create built-in renderer shaders");
    ReloadProjectShaders();
}

void Renderer::SetProjectRoot(const std::filesystem::path& root, bool autoReload) {
    m_ProjectRoot = root;
    m_AutoReloadShaders = autoReload;
    m_LastShaderSources.clear();
    m_NextShaderPoll = {};
}

bool Renderer::ApplyShaderSources(const std::map<std::string, std::string>& sources) {
    auto make = [&](const char* vertex, const char* fragment) {
        return Shader(sources.at(vertex), sources.at(fragment));
    };
    Shader mesh = make("mesh.vert", "mesh.frag");
    Shader skinned = make("skinned.vert", "mesh.frag");
    Shader screen = make("screen.vert", "screen.frag");
    Shader skybox = make("screen.vert", "skybox.frag");
    Shader mask = make("outline-mask.vert", "outline-mask.frag");
    Shader outline = make("outline-compose.vert", "outline-compose.frag");
    if (!mesh.IsValid() || !skinned.IsValid() || !screen.IsValid() ||
        !skybox.IsValid() || !mask.IsValid() || !outline.IsValid()) return false;
    m_PS1Shader = std::move(mesh);
    m_PS1SkinnedShader = std::move(skinned);
    m_ScreenShader = std::move(screen);
    m_SkyboxShader = std::move(skybox);
    m_OutlineMaskShader = std::move(mask);
    m_OutlineComposeShader = std::move(outline);
    const auto& defaults = ProjectRenderer::DefaultShaders();
    m_CustomScreenPass = sources.at("screen.vert") != defaults.at("screen.vert") ||
                         sources.at("screen.frag") != defaults.at("screen.frag");
    m_MeshLightingUploaded = m_SkinnedLightingUploaded = false;
    return true;
}

bool Renderer::ReloadProjectShaders() {
    if (m_ProjectRoot.empty()) return true;
    try {
        auto sources = ProjectRenderer::DefaultShaders();
        for (auto& [name, source] : sources) {
            const auto path = m_ProjectRoot / "renderer/pc" / name;
            if (AssetFiles::Exists(path)) source = AssetFiles::ReadText(path);
        }
        if (sources == m_LastShaderSources) return m_LastShaderReloadSucceeded;
        // Remember failed attempts too, so an unchanged error is not reported
        // every frame. Saving a correction automatically retries the full set.
        m_LastShaderSources = sources;
        m_LastShaderReloadSucceeded = ApplyShaderSources(sources);
        if (!m_LastShaderReloadSucceeded)
            MIPSYNC_ERROR("Project renderer compilation failed; keeping the last valid shaders (renderer/pc).");
        else MIPSYNC_INFO("Loaded project renderer shaders: {}", m_ProjectRoot.string());
        return m_LastShaderReloadSucceeded;
    } catch (const std::exception& ex) {
        MIPSYNC_ERROR("Cannot load project renderer: {}", ex.what());
        return false;
    }
}

void Renderer::CreateDefaultResources() {
    m_ScreenQuad = Mesh::CreateScreenQuad();
    // A material with no image is a color-only material. The white texel is
    // neutral, so uMaterialColor becomes the visible surface color instead of
    // silently introducing a checkerboard texture.
    m_DefaultTexture = Texture::CreateSolid(255, 255, 255, 255);
    m_OutlineMaskFBO = Framebuffer(2, 2);

}

void Renderer::EnsureOutlineMaskFBO(int width, int height) {
    if (width <= 0 || height <= 0)
        return;
    if (width <= m_OutlineMaskFBO.GetWidth() && height <= m_OutlineMaskFBO.GetHeight())
        return;
    m_OutlineMaskFBO.Resize(std::max(width, m_OutlineMaskFBO.GetWidth()),
                            std::max(height, m_OutlineMaskFBO.GetHeight()));
}

void Renderer::SetSceneLights(const SceneLightGpu* lights, int count) {
    m_MeshLightingUploaded = m_SkinnedLightingUploaded = false;
    m_SceneLightCount = std::clamp(count, 0, kMaxSceneLights);
    for (int i = 0; i < m_SceneLightCount; ++i)
        m_SceneLights[i] = lights[i];
}

void Renderer::SetSkyboxTexture(const Texture* texture, const glm::vec3& tint,
                                float exposure, float rotationDegrees) {
    m_SkyboxTexture = texture;
    m_SkyboxTint = tint;
    m_SkyboxExposure = exposure;
    m_SkyboxRotationDegrees = rotationDegrees;
}

void Renderer::ClearSkyboxTexture() {
    m_SkyboxTexture = nullptr;
    m_SkyboxTint = glm::vec3(1.0f);
    m_SkyboxExposure = 0.0f;
    m_SkyboxRotationDegrees = 0.0f;
}

void Renderer::BeginScene(const Camera& camera, Framebuffer* targetFBO, const Texture* backgroundTexture) {
    if (m_AutoReloadShaders && std::chrono::steady_clock::now() >= m_NextShaderPoll) {
        m_NextShaderPoll = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        ReloadProjectShaders();
    }
    m_Statistics = {};
    m_MeshLightingUploaded = m_SkinnedLightingUploaded = false;
    if (m_PSyncWorkload) {
        m_PSyncPrevious.swap(m_PSyncCurrent);
        m_PSyncCurrent.clear();
        m_PSyncCachedVertices = 0;
        m_PSyncWorkload->lights = m_SceneLightCount;
        for (int i = 0; i < m_SceneLightCount; ++i) {
            if (m_SceneLights[i].posType.w == 1.0f) ++m_PSyncWorkload->pointLights;
            if (m_SceneLights[i].posType.w == 2.0f) ++m_PSyncWorkload->spotLights;
        }
        if (backgroundTexture || m_SkyboxTexture) {
            // Exported prerender/sky background is one textured full-screen FT4.
            m_PSyncWorkload->gpuCycles += 320.0 * 240.0 * 2.0;
            m_PSyncWorkload->dmaWords += 10;
        }
    }
    if (targetFBO) {
        targetFBO->Bind();
    } else {
        // Resize internal FBO if settings changed
        if (m_PS1Framebuffer.GetWidth() != m_PS1Settings.internalWidth ||
            m_PS1Framebuffer.GetHeight() != m_PS1Settings.internalHeight) {
            m_PS1Framebuffer.Resize(m_PS1Settings.internalWidth, m_PS1Settings.internalHeight);
        }
        m_PS1Framebuffer.Bind();
    }

    m_ViewMatrix = camera.GetViewMatrix();
    m_ProjectionMatrix = camera.GetProjectionMatrix();

    if (targetFBO) {
        m_ViewportWidth = targetFBO->GetWidth();
        m_ViewportHeight = targetFBO->GetHeight();
    } else {
        m_ViewportWidth = m_PS1Settings.internalWidth;
        m_ViewportHeight = m_PS1Settings.internalHeight;
    }

    glClearColor(m_PS1Settings.fogColor.r, m_PS1Settings.fogColor.g, m_PS1Settings.fogColor.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (backgroundTexture && backgroundTexture->GetID() != 0) {
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        m_BackgroundShader.Bind();
        m_BackgroundShader.SetInt("uScreen", 0);
        backgroundTexture->Bind(0);
        m_ScreenQuad.Bind();
        glDrawElements(GL_TRIANGLES, m_ScreenQuad.GetIndexCount(), GL_UNSIGNED_INT, nullptr);
        m_ScreenQuad.Unbind();
        backgroundTexture->Unbind();
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    } else if (m_SkyboxTexture && m_SkyboxTexture->GetID() != 0) {
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        m_SkyboxShader.Bind();
        m_SkyboxShader.SetInt("uSkybox", 0);
        m_SkyboxShader.SetMat4("uInvProjection", glm::inverse(m_ProjectionMatrix));
        m_SkyboxShader.SetMat4("uInvView", glm::inverse(glm::mat4(glm::mat3(m_ViewMatrix))));
        m_SkyboxShader.SetVec3("uTint", m_SkyboxTint);
        m_SkyboxShader.SetFloat("uExposure", m_SkyboxExposure);
        m_SkyboxShader.SetFloat("uRotationRadians", glm::radians(m_SkyboxRotationDegrees));
        m_SkyboxTexture->Bind(0);
        m_ScreenQuad.Bind();
        glDrawElements(GL_TRIANGLES, m_ScreenQuad.GetIndexCount(), GL_UNSIGNED_INT, nullptr);
        m_ScreenQuad.Unbind();
        m_SkyboxTexture->Unbind();
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    }

    const bool wantWireframeOnly = (m_RenderMode == RenderMode::Wireframe) || m_PS1Settings.wireframeMode;
    if (wantWireframeOnly)
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    else
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    int shaderRenderMode = 0; // Shaded
    switch (m_RenderMode) {
        case RenderMode::Unlit:           shaderRenderMode = 1; break;
        case RenderMode::Normals:         shaderRenderMode = 2; break;
        case RenderMode::Wireframe:       shaderRenderMode = 1; break; // unlit lines
        case RenderMode::ShadedWireframe: shaderRenderMode = 0; break;
        case RenderMode::Shaded:
        default:                          shaderRenderMode = 0; break;
    }

    // Set PS1 shader uniforms
    m_PS1Shader.Bind();
    m_PS1Shader.SetMat4("uView", m_ViewMatrix);
    m_PS1Shader.SetMat4("uProjection", m_ProjectionMatrix);
    m_PS1Shader.SetFloat("uVertexJitter", m_PS1Settings.vertexJitter);
    m_PS1Shader.SetInt("uAffineMapping", m_PS1Settings.affineMapping ? 1 : 0);
    m_PS1Shader.SetInt("uColorDepthLimit", m_PS1Settings.colorDepthLimit ? 1 : 0);
    m_PS1Shader.SetInt("uDitheringEnabled", m_PS1Settings.ditheringEnabled ? 1 : 0);
    // Lighting is uploaded lazily by the first Lit draw of each shader.
    const float fogStart = std::max(0.0f, m_PS1Settings.fogStart);
    const float fogEnd = std::max(fogStart + 0.1f, m_PS1Settings.fogEnd);
    m_PS1Shader.SetFloat("uFogStart", fogStart);
    m_PS1Shader.SetFloat("uFogEnd", fogEnd);
    m_PS1Shader.SetVec3("uFogColor", m_PS1Settings.fogColor);
    m_PS1Shader.SetInt("uFogEnabled", m_PS1Settings.fogEnabled ? 1 : 0);
    SetColorGradingUniforms(m_PS1Shader,m_PS1Settings);
    m_PS1Shader.SetInt("uVignetteEnabled", m_PS1Settings.vignetteEnabled ? 1 : 0);
    m_PS1Shader.SetVec3("uVignetteColor", m_PS1Settings.vignetteColor);
    m_PS1Shader.SetFloat("uVignetteIntensity", std::clamp(m_PS1Settings.vignetteIntensity, 0.0f, 1.0f));
    m_PS1Shader.SetFloat("uVignetteSmoothness", std::clamp(m_PS1Settings.vignetteSmoothness, 0.001f, 2.0f));
    m_PS1Shader.SetVec2("uViewportSize", glm::vec2(
        static_cast<float>(std::max(m_ViewportWidth, 1)),
        static_cast<float>(std::max(m_ViewportHeight, 1))));
    m_PS1Shader.SetInt("uRenderMode", shaderRenderMode);
    m_PS1Shader.SetVec3("uOverrideColor", glm::vec3(0.0f));
    m_PS1Shader.SetVec4("uMaterialColor", glm::vec4(1.0f));
    m_PS1Shader.SetVec4("uMainTexST", glm::vec4(1.0f, 1.0f, 0.0f, 0.0f));
}

void Renderer::RestoreDefaultOpenGLState() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glDisable(GL_BLEND);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void Renderer::EndScene(Framebuffer* targetFBO) {
    if (m_CustomScreenPass) {
        auto& target = GetRenderTarget(targetFBO);
        if (!m_PostProcessFramebuffer.GetColorAttachment())
            m_PostProcessFramebuffer = Framebuffer(target.GetWidth(), target.GetHeight());
        else if (m_PostProcessFramebuffer.GetWidth() != target.GetWidth() ||
                 m_PostProcessFramebuffer.GetHeight() != target.GetHeight())
            m_PostProcessFramebuffer.Resize(target.GetWidth(), target.GetHeight());
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        // Sample through a separate FBO; reading and writing the target's
        // texture in one draw would be undefined. Preserve its depth buffer.
        m_PostProcessFramebuffer.Bind();
        m_ScreenShader.SetInt("uScreen", 0);
        m_ScreenShader.SetVec2("uViewportSize", {target.GetWidth(), target.GetHeight()});
        m_ScreenShader.SetFloat("uTime", std::chrono::duration<float>(
            std::chrono::steady_clock::now() - m_ShaderClockStart).count());
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, target.GetColorAttachment());
        m_ScreenQuad.Bind();
        glDrawElements(GL_TRIANGLES, m_ScreenQuad.GetIndexCount(), GL_UNSIGNED_INT, nullptr);
        target.Bind();
        m_BackgroundShader.SetInt("uScreen", 0);
        glBindTexture(GL_TEXTURE_2D, m_PostProcessFramebuffer.GetColorAttachment());
        glDrawElements(GL_TRIANGLES, m_ScreenQuad.GetIndexCount(), GL_UNSIGNED_INT, nullptr);
        m_ScreenQuad.Unbind();
        glBindTexture(GL_TEXTURE_2D, 0);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    }
    if (targetFBO)
        targetFBO->Unbind();
    else
        m_PS1Framebuffer.Unbind();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    m_RenderMode = RenderMode::Shaded;
    m_ObjectUnlit = false;
}

void Renderer::UploadLightingUniforms(Shader& shader, bool& uploaded) {
    if (uploaded) return;
    shader.SetInt("uLightCount", m_SceneLightCount);
    if (m_SceneLightCount > 0) {
        glm::vec4 posType[kMaxSceneLights], dirRange[kMaxSceneLights];
        glm::vec4 colorIntensity[kMaxSceneLights], spot[kMaxSceneLights];
        for (int i = 0; i < m_SceneLightCount; ++i) {
            posType[i] = m_SceneLights[i].posType;
            dirRange[i] = m_SceneLights[i].dirRange;
            colorIntensity[i] = m_SceneLights[i].colorIntensity;
            spot[i] = m_SceneLights[i].spotParams;
        }
        shader.SetVec4Array("uLightPosType", posType, m_SceneLightCount);
        shader.SetVec4Array("uLightDirRange", dirRange, m_SceneLightCount);
        shader.SetVec4Array("uLightColorIntensity", colorIntensity, m_SceneLightCount);
        shader.SetVec4Array("uLightSpot", spot, m_SceneLightCount);
    }
    shader.SetVec3("uLightDir", m_Light.direction);
    shader.SetVec3("uLightColor", m_Light.color);
    shader.SetFloat("uAmbient", m_Light.ambient);
    uploaded = true;
    ++m_Statistics.lightingUploads;
}

void Renderer::DrawSkinnedMesh(const SkinnedMesh& mesh, const glm::mat4& entityWorld,
                              const glm::mat4& partGeometryToWorld, const glm::vec3& displayCenter,
                              float displayScale, const glm::mat4* boneMatrices, int boneCount,
                              const Texture* texture, const glm::vec4& materialColor,
                              const glm::vec2& textureTiling, const glm::vec2& textureOffset,
                              uint32_t indexOffset, uint32_t indexCount) {
    if (mesh.GetIndexCount() == 0)
        return;

    glm::mat4 palette[kMaxBones];
    for (int i = 0; i < kMaxBones; ++i)
        palette[i] = glm::mat4(1.0f);

    if (boneMatrices && boneCount > 0) {
        const int count = std::min(boneCount, kMaxBones);
        for (int i = 0; i < count; ++i)
            palette[i] = boneMatrices[i];
    }

    m_PS1SkinnedShader.Bind();
    m_PS1SkinnedShader.SetMat4Array("uBones", palette, kMaxBones);
    m_PS1SkinnedShader.SetMat4("uEntityWorld", entityWorld);
    m_PS1SkinnedShader.SetMat4("uPartGeometryToWorld", partGeometryToWorld);
    m_PS1SkinnedShader.SetVec3("uDisplayCenter", displayCenter);
    m_PS1SkinnedShader.SetFloat("uDisplayScale", displayScale);

    m_PS1SkinnedShader.SetMat4("uView", m_ViewMatrix);
    m_PS1SkinnedShader.SetMat4("uProjection", m_ProjectionMatrix);
    m_PS1SkinnedShader.SetFloat("uVertexJitter", m_PS1Settings.vertexJitter);
    m_PS1SkinnedShader.SetInt("uAffineMapping", m_PS1Settings.affineMapping ? 1 : 0);
    m_PS1SkinnedShader.SetInt("uColorDepthLimit", m_PS1Settings.colorDepthLimit ? 1 : 0);
    m_PS1SkinnedShader.SetInt("uDitheringEnabled", m_PS1Settings.ditheringEnabled ? 1 : 0);
    const float fogStart = std::max(0.0f, m_PS1Settings.fogStart);
    const float fogEnd = std::max(fogStart + 0.1f, m_PS1Settings.fogEnd);
    m_PS1SkinnedShader.SetFloat("uFogStart", fogStart);
    m_PS1SkinnedShader.SetFloat("uFogEnd", fogEnd);
    m_PS1SkinnedShader.SetVec3("uFogColor", m_PS1Settings.fogColor);
    m_PS1SkinnedShader.SetInt("uFogEnabled", m_PS1Settings.fogEnabled ? 1 : 0);
    SetColorGradingUniforms(m_PS1SkinnedShader,m_PS1Settings);
    m_PS1SkinnedShader.SetInt("uVignetteEnabled", m_PS1Settings.vignetteEnabled ? 1 : 0);
    m_PS1SkinnedShader.SetVec3("uVignetteColor", m_PS1Settings.vignetteColor);
    m_PS1SkinnedShader.SetFloat("uVignetteIntensity", std::clamp(m_PS1Settings.vignetteIntensity, 0.0f, 1.0f));
    m_PS1SkinnedShader.SetFloat("uVignetteSmoothness", std::clamp(m_PS1Settings.vignetteSmoothness, 0.001f, 2.0f));
    m_PS1SkinnedShader.SetVec2("uViewportSize", glm::vec2(
        static_cast<float>(std::max(m_ViewportWidth, 1)),
        static_cast<float>(std::max(m_ViewportHeight, 1))));

    int shaderRenderMode = 0;
    switch (m_RenderMode) {
        case RenderMode::Unlit: shaderRenderMode = 1; break;
        case RenderMode::Normals: shaderRenderMode = 2; break;
        case RenderMode::Wireframe: shaderRenderMode = 1; break;
        default: shaderRenderMode = m_ObjectUnlit ? 1 : 0; break;
    }
    m_PS1SkinnedShader.SetInt("uRenderMode", shaderRenderMode);
    if (shaderRenderMode == 0)
        UploadLightingUniforms(m_PS1SkinnedShader, m_SkinnedLightingUploaded);
    m_PS1SkinnedShader.SetVec3("uOverrideColor", glm::vec3(0.0f));
    m_PS1SkinnedShader.SetVec4("uMaterialColor", materialColor);
    m_PS1SkinnedShader.SetVec4("uMainTexST",
                               glm::vec4(textureTiling.x, textureTiling.y, textureOffset.x, textureOffset.y));

    if (texture)
        texture->Bind(0);
    else
        m_DefaultTexture.Bind(0);
    m_PS1SkinnedShader.SetInt("uAlbedo", 0);

    // Mixamo / FBX skinned meshes often have inconsistent winding; draw both sides.
    const GLboolean cullWasEnabled = glIsEnabled(GL_CULL_FACE);
    if (cullWasEnabled)
        glDisable(GL_CULL_FACE);

    if (indexCount == 0)
        indexCount = mesh.GetIndexCount();

    if (m_PSyncWorkload) {
        const glm::mat4 vp = m_ProjectionMatrix * m_ViewMatrix * entityWorld;
        const auto project = [&](const SkinnedVertex& v) {
            const glm::vec4 position(v.position, 1.0f);
            glm::vec4 skinned(0.0f);
            const float weight = v.boneWeights.x + v.boneWeights.y + v.boneWeights.z + v.boneWeights.w;
            if (weight > 1e-6f) {
                for (int i = 0; i < 4; ++i) {
                    const int bone = std::clamp(static_cast<int>(v.boneIndices[i] + 0.5f), 0, kMaxBones - 1);
                    skinned += palette[bone] * position * v.boneWeights[i];
                }
                skinned /= weight;
            } else skinned = position;
            const glm::vec3 fitted = (glm::vec3(partGeometryToWorld * skinned) - displayCenter) * displayScale;
            return vp * glm::vec4(fitted, 1.0f);
        };
        ProfilePSyncGeometry(*m_PSyncWorkload, mesh.GetVertices(), mesh.GetIndices(), indexOffset,
            indexCount, project, texture != nullptr, true,
            PSyncLightSignature(m_SceneLights, shaderRenderMode == 0 ? m_SceneLightCount : 0, materialColor),
            m_PSyncPrevious, m_PSyncCurrent, m_PSyncCachedVertices,
            PSyncMatrixSignature(entityWorld), true, false, shaderRenderMode == 0);
    }

    mesh.Bind();
    const void* indexPtr =
        reinterpret_cast<const void*>(static_cast<uintptr_t>(indexOffset) * sizeof(uint32_t));
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT, indexPtr);
    m_Statistics.RecordGeometry(indexCount);
    mesh.Unbind();

    if (cullWasEnabled)
        glEnable(GL_CULL_FACE);
}

void Renderer::DrawMesh(const Mesh& mesh, const glm::mat4& transform, const Texture* texture,
                        const glm::vec4& materialColor, const glm::vec2& textureTiling,
                        const glm::vec2& textureOffset, uint32_t indexOffset, uint32_t indexCount,
                        float projectionDepthClamp, bool doubleSided, PSyncDrawOptions syncOptions) {
    if (indexCount == 0)
        indexCount = mesh.GetIndexCount();
    if (indexCount == 0)
        return;

    const int objectRenderMode = m_RenderMode == RenderMode::Normals ? 2 :
        (m_RenderMode == RenderMode::Unlit || m_RenderMode == RenderMode::Wireframe || m_ObjectUnlit ? 1 : 0);

    if (m_PSyncWorkload) {
        const glm::mat4 mvp = m_ProjectionMatrix * m_ViewMatrix * transform;
        const uint64_t world = PSyncMatrixSignature(transform);
        const uint64_t lighting = PSyncLightSignature(m_SceneLights, objectRenderMode == 0 ? m_SceneLightCount : 0, materialColor) ^ world;
        if (syncOptions.plane) {
            // Use the same material-boundary cells as the PS1 runtime, including
            // offsets and the compact atlas used only for very dense repeats.
            const float atlas=std::abs(textureTiling.x)>8 || std::abs(textureTiling.y)>8 ? 8.f : 1.f;
            ps1_plane_span spansX[PS1_PLANE_AXIS_CAP],spansZ[PS1_PLANE_AXIS_CAP];
            const int nx=PSyncPlaneAxis(texture ? textureTiling.x/atlas : 0,textureOffset.x/atlas,
                syncOptions.subdivisionLevels,spansX);
            const int nz=PSyncPlaneAxis(texture ? textureTiling.y/atlas : 0,textureOffset.y/atlas,
                syncOptions.subdivisionLevels,spansZ);
            const auto lo = mesh.GetBoundsMin(), hi = mesh.GetBoundsMax();
            std::vector<glm::vec4> positions;
            std::vector<uint32_t> triangles;
            for (int z=0; z<nz; ++z) for (int x=0; x<nx; ++x) {
                const float x0=glm::mix(lo.x,hi.x,spansX[x].start_q16/65536.f), x1=glm::mix(lo.x,hi.x,spansX[x].end_q16/65536.f);
                const float z0=glm::mix(lo.z,hi.z,spansZ[z].start_q16/65536.f), z1=glm::mix(lo.z,hi.z,spansZ[z].end_q16/65536.f);
                const uint32_t first=static_cast<uint32_t>(positions.size());
                positions.insert(positions.end(), {{x0,lo.y,z0,1}, {x1,lo.y,z0,1}, {x1,lo.y,z1,1}, {x0,lo.y,z1,1}});
                triangles.insert(triangles.end(), {first,first+1,first+2,first,first+2,first+3});
            }
            ProfilePSyncGeometry(*m_PSyncWorkload, positions, triangles, 0, static_cast<uint32_t>(triangles.size()),
                [&](const glm::vec4& p) { return mvp*p; }, texture != nullptr, true, lighting,
                m_PSyncPrevious,m_PSyncCurrent,m_PSyncCachedVertices,world,false,false,objectRenderMode == 0);
        } else ProfilePSyncGeometry(*m_PSyncWorkload, mesh.GetVertices(), mesh.GetIndices(), indexOffset,
            indexCount, [&](const Vertex& v) { return mvp * glm::vec4(v.position, 1.0f); },
            texture != nullptr, doubleSided, lighting,
            m_PSyncPrevious, m_PSyncCurrent, m_PSyncCachedVertices,world,
            syncOptions.flatShading,syncOptions.cacheable,objectRenderMode == 0);
    }

    m_PS1Shader.Bind();
    m_PS1Shader.SetMat4("uModel", transform);
    m_PS1Shader.SetMat4("uView", m_ViewMatrix);
    m_PS1Shader.SetMat4("uProjection", m_ProjectionMatrix);
    m_PS1Shader.SetInt("uRenderMode", objectRenderMode);
    if (objectRenderMode == 0)
        UploadLightingUniforms(m_PS1Shader, m_MeshLightingUploaded);
    m_PS1Shader.SetFloat("uProjectionDepthClamp", projectionDepthClamp);
    m_PS1Shader.SetVec4("uMaterialColor", materialColor);
    m_PS1Shader.SetVec4("uMainTexST",
                        glm::vec4(textureTiling.x, textureTiling.y, textureOffset.x, textureOffset.y));

    if (texture) {
        texture->Bind(0);
    } else {
        m_DefaultTexture.Bind(0);
    }
    m_PS1Shader.SetInt("uAlbedo", 0);

    const GLboolean cullWasEnabled = glIsEnabled(GL_CULL_FACE);
    if (doubleSided && cullWasEnabled)
        glDisable(GL_CULL_FACE);

    mesh.Bind();
    const void* indexPtr =
        reinterpret_cast<const void*>(static_cast<uintptr_t>(indexOffset) * sizeof(uint32_t));
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT, indexPtr);
    m_Statistics.RecordGeometry(indexCount);

    if (m_RenderMode == RenderMode::ShadedWireframe) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glDepthFunc(GL_LEQUAL);

        m_PS1Shader.SetInt("uRenderMode", 3);
        m_PS1Shader.SetVec3("uOverrideColor", glm::vec3(0.05f, 0.05f, 0.05f));
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT, indexPtr);
        m_Statistics.RecordGeometry(indexCount);

        m_PS1Shader.SetInt("uRenderMode", objectRenderMode);
        m_PS1Shader.SetVec3("uOverrideColor", glm::vec3(0.0f));

        glDepthFunc(GL_LESS);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    mesh.Unbind();
    if (doubleSided && cullWasEnabled)
        glEnable(GL_CULL_FACE);
}

void Renderer::DrawMeshDepthOnly(const Mesh& mesh, const glm::mat4& transform) {
    if (mesh.GetIndexCount() == 0)
        return;

    const GLboolean cullWasEnabled = glIsEnabled(GL_CULL_FACE);
    const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    // Preserve the framebuffer color while still running the depth test/write.
    glEnable(GL_BLEND);
    glBlendFunc(GL_ZERO, GL_ONE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    m_PS1Shader.Bind();
    m_PS1Shader.SetMat4("uModel", transform);
    m_PS1Shader.SetMat4("uView", m_ViewMatrix);
    m_PS1Shader.SetMat4("uProjection", m_ProjectionMatrix);
    m_PS1Shader.SetFloat("uProjectionDepthClamp", 0.0f);
    m_PS1Shader.SetVec4("uMaterialColor", glm::vec4(1.0f));
    m_PS1Shader.SetVec4("uMainTexST", glm::vec4(1.0f, 1.0f, 0.0f, 0.0f));
    m_DefaultTexture.Bind(0);
    m_PS1Shader.SetInt("uAlbedo", 0);

    mesh.Bind();
    glDrawElements(GL_TRIANGLES, mesh.GetIndexCount(), GL_UNSIGNED_INT, nullptr);
    mesh.Unbind();

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (!blendWasEnabled)
        glDisable(GL_BLEND);
    if (cullWasEnabled)
        glEnable(GL_CULL_FACE);
    else
        glDisable(GL_CULL_FACE);

    const bool wireframe =
        m_RenderMode == RenderMode::Wireframe || m_PS1Settings.wireframeMode;
    glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
}

void Renderer::RenderSelectionOutline(Framebuffer& targetFBO, const Mesh& mesh,
                                      const glm::mat4& transform,
                                      const glm::vec3& color, float pixelWidth) {
    const int w = targetFBO.GetWidth();
    const int h = targetFBO.GetHeight();
    if (w <= 0 || h <= 0) return;

    EnsureOutlineMaskFBO(w, h);

    // ── Pass 1: render mesh silhouette as white onto mask FBO ──
    m_OutlineMaskFBO.Bind();
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glDisable(GL_CULL_FACE);  // both sides count for thin meshes (planes)
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    m_OutlineMaskShader.Bind();
    m_OutlineMaskShader.SetMat4("uModel", transform);
    m_OutlineMaskShader.SetMat4("uView", m_ViewMatrix);
    m_OutlineMaskShader.SetMat4("uProjection", m_ProjectionMatrix);

    mesh.Bind();
    glDrawElements(GL_TRIANGLES, mesh.GetIndexCount(), GL_UNSIGNED_INT, nullptr);
    mesh.Unbind();

    // ── Pass 2: edge-detect mask and blend outline onto scene FBO ──
    targetFBO.Bind();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_OutlineComposeShader.Bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_OutlineMaskFBO.GetColorAttachment());
    m_OutlineComposeShader.SetInt("uMask", 0);
    m_OutlineComposeShader.SetVec2("uTexelSize", glm::vec2(1.0f / (float)w, 1.0f / (float)h));
    m_OutlineComposeShader.SetFloat("uOutlinePixels", pixelWidth);
    m_OutlineComposeShader.SetVec3("uOutlineColor", color);

    m_ScreenQuad.Bind();
    glDrawElements(GL_TRIANGLES, m_ScreenQuad.GetIndexCount(), GL_UNSIGNED_INT, nullptr);
    m_ScreenQuad.Unbind();

    targetFBO.Unbind();
    RestoreDefaultOpenGLState();
}

GLuint Renderer::GetOutputTexture() const {
    return m_PS1Framebuffer.GetColorAttachment();
}

void Renderer::ResizeOutput(int width, int height) {
    m_PS1Settings.internalWidth = width;
    m_PS1Settings.internalHeight = height;
    m_PS1Framebuffer.Resize(width, height);
}

} // namespace MipsyncEngine
