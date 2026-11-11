#pragma once
// Standard sources shared by project migration and the renderer fallback.
namespace MipsyncEngine::RendererDefaults {
inline constexpr const char* PS1_VERTEX_SHADER = R"(
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

inline constexpr const char* PS1_SKINNED_VERTEX_SHADER = R"(
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

inline constexpr const char* PS1_FRAGMENT_SHADER = R"(
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

inline constexpr const char* OUTLINE_MASK_VERTEX_SHADER = R"(
#version 330 core
layout(location = 0) in vec3 aPosition;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
void main() {
    gl_Position = uProjection * uView * uModel * vec4(aPosition, 1.0);
}
)";

inline constexpr const char* OUTLINE_MASK_FRAGMENT_SHADER = R"(
#version 330 core
out vec4 fragColor;
void main() {
    fragColor = vec4(1.0);
}
)";

inline constexpr const char* OUTLINE_COMPOSE_VERTEX_SHADER = R"(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec2 aUV;
out vec2 vUV;
void main() {
    gl_Position = vec4(aPosition, 1.0);
    vUV = aUV;
}
)";

inline constexpr const char* OUTLINE_COMPOSE_FRAGMENT_SHADER = R"(
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

inline constexpr const char* SCREEN_VERTEX_SHADER = R"(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec2 aUV;
out vec2 vUV;
void main() {
    gl_Position = vec4(aPosition, 1.0);
    vUV = aUV;
}
)";

inline constexpr const char* SCREEN_FRAGMENT_SHADER = R"(
#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uScreen;
void main() {
    fragColor = texture(uScreen, vUV);
}
)";

inline constexpr const char* SKYBOX_FRAGMENT_SHADER = R"(
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
}
