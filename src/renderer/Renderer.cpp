#include "Renderer.h"
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
// PS1-style vertex shader (embedded GLSL)
// ─────────────────────────────────────────────
static const char* PS1_VERTEX_SHADER = R"(
#version 330 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aColor;

out vec2 vUV;                    // perspective-correct UV
noperspective out vec2 vUVAffine; // PS1-style affine UV (× clip w)
noperspective out float vAffineW;
out vec3 vNormal;
out vec4 vColor;
out float vFogFactor;
out float vDepth;
uniform int uRenderMode;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform float uVertexJitter;    // Snapping resolution
uniform float uProjectionDepthClamp;

// Lighting (legacy single directional when uLightCount == 0)
uniform int uLightCount;
#define MAX_LIGHTS 8
uniform vec4 uLightPosType[MAX_LIGHTS];
uniform vec4 uLightDirRange[MAX_LIGHTS];
uniform vec4 uLightColorIntensity[MAX_LIGHTS];
uniform vec4 uLightSpot[MAX_LIGHTS];
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform float uAmbient;

vec3 EvaluateVertexLighting(vec3 worldPos, vec3 worldNormal) {
    if (uLightCount <= 0) {
        float diff = max(dot(worldNormal, -normalize(uLightDir)), 0.0);
        return uAmbient * uLightColor + diff * uLightColor;
    }

    vec3 accum = vec3(0.0);
    for (int i = 0; i < uLightCount; ++i) {
        int typ = int(uLightPosType[i].w + 0.5);
        vec3 L;
        float atten = 1.0;
        if (typ == 0) {
            L = normalize(-uLightDirRange[i].xyz);
        } else {
            vec3 toLight = uLightPosType[i].xyz - worldPos;
            float dist = length(toLight);
            L = dist > 0.0001 ? normalize(toLight) : vec3(0.0, 1.0, 0.0);
            float range = uLightDirRange[i].w;
            if (range > 0.0) {
                float t = clamp(1.0 - dist / range, 0.0, 1.0);
                atten = t * t;
            }
            if (typ == 2) {
                vec3 spotDir = normalize(uLightDirRange[i].xyz);
                float cosTheta = dot(spotDir, -L);
                atten *= smoothstep(uLightSpot[i].x, uLightSpot[i].y, cosTheta);
            }
        }
        float diff = max(dot(worldNormal, L), 0.0);
        accum += uLightColorIntensity[i].rgb * uLightColorIntensity[i].a * diff * atten;
    }
    return accum + uAmbient * vec3(0.12);
}

// Fog
uniform float uFogStart;
uniform float uFogEnd;
uniform int uFogEnabled;

// Main texture ST (Unity _MainTex_ST): xy = tiling, zw = offset
uniform vec4 uMainTexST;

vec4 ClampProjectionDepthPreserveRay(vec4 viewPos) {
    if (uProjectionDepthClamp <= 0.0) {
        return viewPos;
    }

    float depth = -viewPos.z;
    if (depth <= 0.0 || depth >= uProjectionDepthClamp) {
        return viewPos;
    }

    float scale = uProjectionDepthClamp / max(depth, 0.0001);
    return vec4(viewPos.xyz * scale, viewPos.w);
}

void main() {
    vec4 worldPos = uModel * vec4(aPosition, 1.0);
    vec4 viewPos  = uView * worldPos;
    vec4 projectedViewPos = ClampProjectionDepthPreserveRay(viewPos);
    vec4 clipPos  = uProjection * projectedViewPos;

    // ★ PS1 Vertex Snapping (Jitter)
    if (uVertexJitter > 0.0) {
        vec2 snapped = clipPos.xy / clipPos.w;
        snapped = floor(snapped * uVertexJitter) / uVertexJitter;
        clipPos.xy = snapped * clipPos.w;
    }

    gl_Position = clipPos;

    vec2 tiledUV = aUV * uMainTexST.xy + uMainTexST.zw;
    vUV = tiledUV;
    vAffineW = clipPos.w;
    vUVAffine = tiledUV * clipPos.w;

    // Gouraud lighting (per-vertex)
    vNormal = vec3(0.0, 1.0, 0.0);
    vColor = aColor;
    if (uRenderMode != 1) {
        vNormal = normalize(mat3(transpose(inverse(uModel))) * aNormal);
        if (uRenderMode == 0) vColor *= vec4(EvaluateVertexLighting(worldPos.xyz, vNormal), 1.0);
    }

    // Fog (distance-based in view space)
    float dist = length(projectedViewPos.xyz);
    vFogFactor = clamp((uFogEnd - dist) / (uFogEnd - uFogStart), 0.0, 1.0);
    vDepth = dist;
}
)";

static const char* PS1_SKINNED_VERTEX_SHADER = R"(
#version 330 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aColor;
layout(location = 4) in vec4 aBoneIndices;
layout(location = 5) in vec4 aBoneWeights;

out vec2 vUV;
noperspective out vec2 vUVAffine;
noperspective out float vAffineW;
out vec3 vNormal;
out vec4 vColor;
out float vFogFactor;
out float vDepth;
uniform int uRenderMode;

uniform mat4 uEntityWorld;
uniform mat4 uPartGeometryToWorld;
uniform vec3 uDisplayCenter;
uniform float uDisplayScale;
uniform mat4 uView;
uniform mat4 uProjection;
uniform float uVertexJitter;

uniform mat4 uBones[128];

uniform int uLightCount;
#define MAX_LIGHTS 8
uniform vec4 uLightPosType[MAX_LIGHTS];
uniform vec4 uLightDirRange[MAX_LIGHTS];
uniform vec4 uLightColorIntensity[MAX_LIGHTS];
uniform vec4 uLightSpot[MAX_LIGHTS];
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform float uAmbient;

vec3 EvaluateVertexLighting(vec3 worldPos, vec3 worldNormal) {
    if (uLightCount <= 0) {
        float diff = max(dot(worldNormal, -normalize(uLightDir)), 0.0);
        return uAmbient * uLightColor + diff * uLightColor;
    }
    vec3 accum = vec3(0.0);
    for (int i = 0; i < uLightCount; ++i) {
        int typ = int(uLightPosType[i].w + 0.5);
        vec3 L;
        float atten = 1.0;
        if (typ == 0) {
            L = normalize(-uLightDirRange[i].xyz);
        } else {
            vec3 toLight = uLightPosType[i].xyz - worldPos;
            float dist = length(toLight);
            L = dist > 0.0001 ? normalize(toLight) : vec3(0.0, 1.0, 0.0);
            float range = uLightDirRange[i].w;
            if (range > 0.0) {
                float t = clamp(1.0 - dist / range, 0.0, 1.0);
                atten = t * t;
            }
            if (typ == 2) {
                vec3 spotDir = normalize(uLightDirRange[i].xyz);
                float cosTheta = dot(spotDir, -L);
                atten *= smoothstep(uLightSpot[i].x, uLightSpot[i].y, cosTheta);
            }
        }
        float diff = max(dot(worldNormal, L), 0.0);
        accum += uLightColorIntensity[i].rgb * uLightColorIntensity[i].a * diff * atten;
    }
    return accum + uAmbient * vec3(0.12);
}

uniform float uFogStart;
uniform float uFogEnd;
uniform int uFogEnabled;
uniform vec4 uMainTexST;

void main() {
    float weightSum = aBoneWeights.x + aBoneWeights.y + aBoneWeights.z + aBoneWeights.w;

    vec4 skinnedPos = vec4(aPosition, 1.0);
    vec3 skinnedNormal = aNormal;

    if (weightSum > 1e-6) {
        int bi0 = clamp(int(aBoneIndices.x + 0.5), 0, 127);
        int bi1 = clamp(int(aBoneIndices.y + 0.5), 0, 127);
        int bi2 = clamp(int(aBoneIndices.z + 0.5), 0, 127);
        int bi3 = clamp(int(aBoneIndices.w + 0.5), 0, 127);

        skinnedPos =
            (uBones[bi0] * vec4(aPosition, 1.0)) * aBoneWeights.x +
            (uBones[bi1] * vec4(aPosition, 1.0)) * aBoneWeights.y +
            (uBones[bi2] * vec4(aPosition, 1.0)) * aBoneWeights.z +
            (uBones[bi3] * vec4(aPosition, 1.0)) * aBoneWeights.w;
        skinnedPos /= weightSum;

        if (uRenderMode != 1) {
        vec3 n0 = mat3(transpose(inverse(uBones[bi0]))) * aNormal;
        vec3 n1 = mat3(transpose(inverse(uBones[bi1]))) * aNormal;
        vec3 n2 = mat3(transpose(inverse(uBones[bi2]))) * aNormal;
        vec3 n3 = mat3(transpose(inverse(uBones[bi3]))) * aNormal;
        skinnedNormal =
            n0 * aBoneWeights.x + n1 * aBoneWeights.y + n2 * aBoneWeights.z + n3 * aBoneWeights.w;
        skinnedNormal = normalize(skinnedNormal);
        }
    }

    vec4 geoPos = uPartGeometryToWorld * skinnedPos;
    vec3 fittedPos = (geoPos.xyz - uDisplayCenter) * uDisplayScale;
    vec4 worldPos = uEntityWorld * vec4(fittedPos, 1.0);
    vec4 viewPos  = uView * worldPos;
    vec4 clipPos  = uProjection * viewPos;

    if (uVertexJitter > 0.0) {
        vec2 snapped = clipPos.xy / clipPos.w;
        snapped = floor(snapped * uVertexJitter) / uVertexJitter;
        clipPos.xy = snapped * clipPos.w;
    }

    gl_Position = clipPos;

    vec2 tiledUV = aUV * uMainTexST.xy + uMainTexST.zw;
    vUV = tiledUV;
    vAffineW = clipPos.w;
    vUVAffine = tiledUV * clipPos.w;

    vNormal = vec3(0.0, 1.0, 0.0);
    vColor = aColor;
    if (uRenderMode != 1) {
        vec3 geoNormal = mat3(transpose(inverse(uPartGeometryToWorld))) * skinnedNormal;
        vNormal = normalize(mat3(transpose(inverse(uEntityWorld))) * geoNormal);
        if (uRenderMode == 0) vColor *= vec4(EvaluateVertexLighting(worldPos.xyz, vNormal), 1.0);
    }

    float dist = length(viewPos.xyz);
    vFogFactor = clamp((uFogEnd - dist) / (uFogEnd - uFogStart), 0.0, 1.0);
    vDepth = dist;
}
)";

// ─────────────────────────────────────────────
// PS1-style fragment shader (embedded GLSL)
// ─────────────────────────────────────────────
static const char* PS1_FRAGMENT_SHADER = R"(
#version 330 core

in vec2 vUV;
noperspective in vec2 vUVAffine;
noperspective in float vAffineW;
in vec3 vNormal;
in vec4 vColor;
in float vFogFactor;
in float vDepth;

out vec4 fragColor;

uniform sampler2D uAlbedo;
uniform vec4 uMaterialColor;
uniform vec3 uFogColor;
uniform bool uAffineMapping;
uniform bool uColorDepthLimit;
uniform bool uDitheringEnabled;
uniform int uFogEnabled;
uniform int uColorGradingEnabled;
uniform mat3 uColorGradingMatrix;
uniform vec3 uColorGradingOffset;
uniform int uVignetteEnabled;
uniform vec3 uVignetteColor;
uniform float uVignetteIntensity;
uniform float uVignetteSmoothness;
uniform vec2 uViewportSize;

// Render mode: 0=Shaded, 1=Unlit, 2=Normals, 3=WireOverlay (flat color)
uniform int uRenderMode;
uniform vec3 uOverrideColor;

// 4x4 Bayer dithering matrix
const float bayerMatrix[16] = float[16](
     0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
    12.0/16.0,  4.0/16.0, 14.0/16.0,  6.0/16.0,
     3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
    15.0/16.0,  7.0/16.0, 13.0/16.0,  5.0/16.0
);

vec3 ApplyColorGrading(vec3 color) {
    return clamp(uColorGradingMatrix * color + uColorGradingOffset, 0.0, 1.0);
}

vec3 ApplyVignette(vec3 color) {
    vec2 uv = gl_FragCoord.xy / max(uViewportSize, vec2(1.0));
    vec2 centered = uv * 2.0 - 1.0;
    centered.x *= uViewportSize.x / max(uViewportSize.y, 1.0);
    float dist = length(centered);
    float inner = max(0.0, 1.0 - uVignetteIntensity);
    float outer = max(inner + 0.001, inner + max(uVignetteSmoothness, 0.001));
    float mask = smoothstep(inner, outer, dist);
    return mix(color, uVignetteColor, clamp(mask * uVignetteIntensity, 0.0, 1.0));
}

void main() {
    if (uRenderMode == 3) {
        fragColor = vec4(uOverrideColor, 1.0);
        return;
    }

    if (uRenderMode == 2) {
        vec3 n = normalize(vNormal) * 0.5 + 0.5;
        fragColor = vec4(n, 1.0);
        return;
    }

    vec2 sampleUV = vUV;
    if (uAffineMapping) {
        float w = max(abs(vAffineW), 1e-5);
        sampleUV = vUVAffine / w;
    }
    vec4 texColor = texture(uAlbedo, sampleUV);
    if (texColor.a < 0.5) {
        discard;
    }
    vec3 color = texColor.rgb * vColor.rgb * uMaterialColor.rgb;

    // Color depth limit + dither only apply to lit/unlit textured modes
    if (uColorDepthLimit) {
        float levels = 31.0;

        if (uDitheringEnabled) {
            int x = int(gl_FragCoord.x) % 4;
            int y = int(gl_FragCoord.y) % 4;
            float dither = bayerMatrix[y * 4 + x] - 0.5;
            color += dither / levels;
        }

        color = floor(color * levels + 0.5) / levels;
    }

    if (uRenderMode == 0) {
        color = mix(uFogColor, color, vFogFactor);
    }

    if (uColorGradingEnabled != 0) {
        color = ApplyColorGrading(color);
    }

    if (uVignetteEnabled != 0) {
        color = ApplyVignette(color);
    }

    float alpha = texColor.a * uMaterialColor.a * vColor.a;
    fragColor = vec4(color, alpha);
}
)";

// ─────────────────────────────────────────────
// Selection outline (post-process, Unity-style)
//   1) Render mesh silhouette to a mask FBO (white)
//   2) Edge-detect: any pixel outside the mesh whose neighbour is inside
//      becomes outline-colored, blended on top of the scene.
// ─────────────────────────────────────────────
static const char* OUTLINE_MASK_VERTEX_SHADER = R"(
#version 330 core
layout(location = 0) in vec3 aPosition;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
void main() {
    gl_Position = uProjection * uView * uModel * vec4(aPosition, 1.0);
}
)";

static const char* OUTLINE_MASK_FRAGMENT_SHADER = R"(
#version 330 core
out vec4 fragColor;
void main() {
    fragColor = vec4(1.0);
}
)";

static const char* OUTLINE_COMPOSE_VERTEX_SHADER = R"(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec2 aUV;
out vec2 vUV;
void main() {
    gl_Position = vec4(aPosition, 1.0);
    vUV = aUV;
}
)";

static const char* OUTLINE_COMPOSE_FRAGMENT_SHADER = R"(
#version 330 core
in vec2 vUV;
out vec4 fragColor;

uniform sampler2D uMask;
uniform vec2  uTexelSize;
uniform float uOutlinePixels;
uniform vec3  uOutlineColor;

void main() {
    float center = texture(uMask, vUV).r;
    if (center > 0.5) discard; // inside the silhouette → no outline drawn

    int radius = int(ceil(uOutlinePixels));
    float maxNeighbour = 0.0;
    float r2 = uOutlinePixels * uOutlinePixels;

    for (int x = -radius; x <= radius; ++x) {
        for (int y = -radius; y <= radius; ++y) {
            if (float(x*x + y*y) > r2) continue;            // circular kernel
            vec2 offset = vec2(float(x), float(y)) * uTexelSize;
            maxNeighbour = max(maxNeighbour, texture(uMask, vUV + offset).r);
        }
    }

    if (maxNeighbour < 0.5) discard; // not on silhouette edge

    fragColor = vec4(uOutlineColor, 1.0);
}
)";

// ─────────────────────────────────────────────
// Screen quad shaders (for upscaling the PS1 FBO)
// ─────────────────────────────────────────────
static const char* SCREEN_VERTEX_SHADER = R"(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec2 aUV;
out vec2 vUV;
void main() {
    gl_Position = vec4(aPosition, 1.0);
    vUV = aUV;
}
)";

static const char* SCREEN_FRAGMENT_SHADER = R"(
#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uScreen;
void main() {
    fragColor = texture(uScreen, vUV);
}
)";

static const char* SKYBOX_FRAGMENT_SHADER = R"(
#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uSkybox;
uniform mat4 uInvView;
uniform mat4 uInvProjection;
uniform vec3 uTint;
uniform float uExposure;
uniform float uRotationRadians;

const float PI = 3.14159265359;

vec3 rotateY(vec3 v, float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return vec3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
}

void main() {
    vec2 ndc = vUV * 2.0 - 1.0;
    vec4 clip = vec4(ndc, 1.0, 1.0);
    vec4 view = uInvProjection * clip;
    view = vec4(normalize(view.xyz / max(view.w, 0.0001)), 0.0);
    vec3 dir = normalize((uInvView * view).xyz);
    dir = rotateY(dir, uRotationRadians);

    float u = atan(dir.z, dir.x) / (2.0 * PI) + 0.5;
    float v = 0.5 - asin(clamp(dir.y, -1.0, 1.0)) / PI;
    vec3 color = texture(uSkybox, vec2(u, v)).rgb;
    color *= exp2(uExposure) * uTint;
    fragColor = vec4(color, 1.0);
}
)";

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
    m_PS1Shader = Shader(PS1_VERTEX_SHADER, PS1_FRAGMENT_SHADER);
    if (!m_PS1Shader.IsValid()) {
        MIPSYNC_FATAL("Failed to create PS1 shader!");
    }

    m_PS1SkinnedShader = Shader(PS1_SKINNED_VERTEX_SHADER, PS1_FRAGMENT_SHADER);
    if (!m_PS1SkinnedShader.IsValid()) {
        MIPSYNC_FATAL("Failed to create PS1 skinned shader!");
    }
    if (const GLint boneLoc =
            glGetUniformLocation(m_PS1SkinnedShader.GetID(), "uBones[0]");
        boneLoc < 0) {
        MIPSYNC_ERROR("Skinned shader: uBones[] uniform array not found (GPU skinning will fail)");
    }

    m_OutlineMaskShader = Shader(OUTLINE_MASK_VERTEX_SHADER, OUTLINE_MASK_FRAGMENT_SHADER);
    if (!m_OutlineMaskShader.IsValid()) {
        MIPSYNC_FATAL("Failed to create outline mask shader!");
    }

    m_OutlineComposeShader = Shader(OUTLINE_COMPOSE_VERTEX_SHADER, OUTLINE_COMPOSE_FRAGMENT_SHADER);
    if (!m_OutlineComposeShader.IsValid()) {
        MIPSYNC_FATAL("Failed to create outline compose shader!");
    }

    m_ScreenShader = Shader(SCREEN_VERTEX_SHADER, SCREEN_FRAGMENT_SHADER);
    if (!m_ScreenShader.IsValid()) {
        MIPSYNC_FATAL("Failed to create screen shader!");
    }

    m_SkyboxShader = Shader(SCREEN_VERTEX_SHADER, SKYBOX_FRAGMENT_SHADER);
    if (!m_SkyboxShader.IsValid()) {
        MIPSYNC_FATAL("Failed to create skybox shader!");
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
        m_ScreenShader.Bind();
        m_ScreenShader.SetInt("uScreen", 0);
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
