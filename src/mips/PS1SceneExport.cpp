#include "PS1SceneExport.h"
#include "../audio/Ps1VagEncoder.h"
#include "PS1TextureExport.h"
#include "MipsRuntime.h"
#include "../assets/AssetManager.h"
#include "../assets/Material.h"
#include "../animation/AnimatorControllerIO.h"
#include "../animation/AnimationTypes.h"
#include "../animation/SkeletalModel.h"
#include "../core/Log.h"
#include "../renderer/Mesh.h"
#include "../renderer/Texture.h"
#include <stb_image.h>
#include <stb_image_write.h>
#include <ufbx.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <iomanip>
#include <limits>
#include <nlohmann/json.hpp>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace MipsyncEngine::Mips {
namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

bool ReadVec3(const json& j, float out[3]) {
    if (!j.is_array() || j.size() < 3) return false;
    out[0] = j[0].get<float>();
    out[1] = j[1].get<float>();
    out[2] = j[2].get<float>();
    return true;
}

bool ReadVec2(const json& j, float out[2]) {
    if (!j.is_array() || j.size() < 2) return false;
    out[0] = j[0].get<float>();
    out[1] = j[1].get<float>();
    return true;
}

static float Clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static uint8_t ToByte01(float v) {
    const float c = Clamp01(v);
    return static_cast<uint8_t>(c * 255.0f + 0.5f);
}

static int16_t ClampI16(int v) {
    if (v < -32768) return -32768;
    if (v > 32767) return 32767;
    return static_cast<int16_t>(v);
}

static int16_t ToQ8(float value) {
    const long scaled = std::lround(static_cast<double>(value) * 256.0);
    if (scaled < -32768l) return -32768;
    if (scaled > 32767l) return 32767;
    return static_cast<int16_t>(scaled);
}

static std::string EscapeCString(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 32 || c >= 127) {
                char buf[5];
                std::snprintf(buf, sizeof(buf), "\\%03o", c);
                out += buf;
            } else {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    return out;
}

static float Fract01(float v) {
    v -= std::floor(v);
    if (v < 0.0f) v += 1.0f;
    return v;
}

static bool IsCameraTriggerTag(const json& ent) {
    std::string tag;
    if (ent.contains("unityTag") && ent["unityTag"].is_string())
        tag = ent["unityTag"].get<std::string>();
    else if (ent.contains("tag") && ent["tag"].is_string())
        tag = ent["tag"].get<std::string>();
    std::transform(tag.begin(), tag.end(), tag.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return tag == "mipsynccameratrigger" ||
           tag == "mipsyncshottrigger" ||
           tag == "cameratrigger" ||
           tag == "shottrigger";
}

static uint8_t UvToPsxU(float u, int texSize) {
    if (texSize < 2)
        texSize = 2;
    /* setUVWH uses 0..width (64 for 64x64); not width-1. */
    const int uv = static_cast<int>(Fract01(u) * static_cast<float>(texSize) + 0.5f);
    return static_cast<uint8_t>(std::min(uv, 255));
}

static uint8_t UvToPsxV(float v, int texSize) {
    if (texSize < 2)
        texSize = 2;
    const int uv = static_cast<int>(Fract01(v) * static_cast<float>(texSize) + 0.5f);
    return static_cast<uint8_t>(std::min(uv, 255));
}

static constexpr float kPs1UiLayoutWidth = 1024.0f;
static constexpr float kPs1UiLayoutHeight = 768.0f;
static constexpr int kPs1UiCjkGlyphSize = 16;

static bool DecodeUtf8Next(const std::string& text, size_t& index, uint32_t& outCodepoint) {
    if (index >= text.size())
        return false;

    const uint8_t c0 = static_cast<uint8_t>(text[index++]);
    if (c0 < 0x80) {
        outCodepoint = c0;
        return true;
    }

    uint32_t cp = 0;
    int extra = 0;
    if ((c0 & 0xE0u) == 0xC0u) {
        cp = c0 & 0x1Fu;
        extra = 1;
    } else if ((c0 & 0xF0u) == 0xE0u) {
        cp = c0 & 0x0Fu;
        extra = 2;
    } else if ((c0 & 0xF8u) == 0xF0u) {
        cp = c0 & 0x07u;
        extra = 3;
    } else {
        outCodepoint = '?';
        return true;
    }

    if (index + static_cast<size_t>(extra) > text.size()) {
        outCodepoint = '?';
        return true;
    }

    for (int i = 0; i < extra; ++i) {
        const uint8_t cx = static_cast<uint8_t>(text[index++]);
        if ((cx & 0xC0u) != 0x80u) {
            outCodepoint = '?';
            return true;
        }
        cp = (cp << 6) | (cx & 0x3Fu);
    }

    outCodepoint = cp;
    return true;
}

static std::wstring CodepointToUtf16(uint32_t cp) {
    std::wstring out;
    if (cp <= 0xFFFFu) {
        out.push_back(static_cast<wchar_t>(cp));
    } else if (cp <= 0x10FFFFu) {
        cp -= 0x10000u;
        out.push_back(static_cast<wchar_t>(0xD800u + (cp >> 10)));
        out.push_back(static_cast<wchar_t>(0xDC00u + (cp & 0x3FFu)));
    }
    return out;
}

static bool RenderUiGlyphBitmap(uint32_t cp, std::vector<uint16_t>& rows) {
    rows.assign(kPs1UiCjkGlyphSize, 0);

#ifdef _WIN32
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = kPs1UiCjkGlyphSize;
    bmi.bmiHeader.biHeight = -kPs1UiCjkGlyphSize;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC hdc = CreateCompatibleDC(nullptr);
    if (!hdc)
        return false;
    HBITMAP bitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        if (bitmap) DeleteObject(bitmap);
        DeleteDC(hdc);
        return false;
    }

    HGDIOBJ oldBitmap = SelectObject(hdc, bitmap);
    HFONT font = CreateFontW(-kPs1UiCjkGlyphSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             NONANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Meiryo");
    HGDIOBJ oldFont = font ? SelectObject(hdc, font) : nullptr;

    RECT rect{ 0, 0, kPs1UiCjkGlyphSize, kPs1UiCjkGlyphSize };
    HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hdc, &rect, black);
    DeleteObject(black);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    const std::wstring text = CodepointToUtf16(cp);
    if (!text.empty()) {
        DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &rect,
                  DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOCLIP);
    }
    GdiFlush();

    const uint32_t* px = static_cast<const uint32_t*>(bits);
    for (int y = 0; y < kPs1UiCjkGlyphSize; ++y) {
        uint16_t row = 0;
        for (int x = 0; x < kPs1UiCjkGlyphSize; ++x) {
            const uint32_t p = px[y * kPs1UiCjkGlyphSize + x];
            const uint8_t b = static_cast<uint8_t>(p & 0xFFu);
            const uint8_t g = static_cast<uint8_t>((p >> 8) & 0xFFu);
            const uint8_t r = static_cast<uint8_t>((p >> 16) & 0xFFu);
            if (static_cast<int>(r) + static_cast<int>(g) + static_cast<int>(b) > 96)
                row |= static_cast<uint16_t>(1u << (kPs1UiCjkGlyphSize - 1 - x));
        }
        rows[y] = row;
    }

    if (oldFont) SelectObject(hdc, oldFont);
    if (font) DeleteObject(font);
    SelectObject(hdc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(hdc);
    return true;
#else
    (void)cp;
    return false;
#endif
}

static uint8_t RegisterTexturePath(std::vector<std::string>& texturePaths,
                                   std::unordered_map<std::string, uint8_t>& texIndexByPath,
                                   const std::string& texPath) {
    if (texPath.empty())
        return 0;

    auto it = texIndexByPath.find(texPath);
    if (it != texIndexByPath.end())
        return it->second;

    const uint8_t idx = static_cast<uint8_t>(texturePaths.size() + 1u);
    if (idx == 0 || texturePaths.size() >= 255) {
        MIPSYNC_WARN("PS1 texture table full; skipping {}", texPath);
        return 0;
    }
    texturePaths.push_back(texPath);
    texIndexByPath[texPath] = idx;
    return idx;
}

static bool ResolvePrerenderedBackgroundPath(const fs::path& projectRoot, const fs::path& scenePath,
                                             const std::string& rawPathUtf8, std::string& outProjectRel);

static bool IsSkinnedAnimMeshKey(const std::string& key);
static bool SameSkinnedAnimSequence(const std::string& a, const std::string& b);

void ResolveEntityAppearances(const std::string& projectRoot, const fs::path& scenePath,
                              Ps1SceneExportResult& data) {
    const fs::path root = PathUtf8::FromString(projectRoot);
    std::unordered_map<std::string, uint8_t> texIndexByPath;
    const std::string requestedSceneBackground = data.backgroundImagePath;
    data.texturePaths.clear();
    data.backgroundImagePath.clear();
    data.backgroundImagePaths.clear();

    for (auto& e : data.entities) {
        std::string texPath;
        Material mat;
        if (!e.materialPath.empty()) {
            std::string err;
            const fs::path matAbs = root / e.materialPath;
            if (Material::Load(PathUtf8::ToString(matAbs), mat, err)) {
                e.color[0] = ToByte01(mat.color.r);
                e.color[1] = ToByte01(mat.color.g);
                e.color[2] = ToByte01(mat.color.b);
                texPath = mat.texturePath;
                e.textureTiling[0] = mat.mainTextureTiling.x;
                e.textureTiling[1] = mat.mainTextureTiling.y;
                e.textureOffset[0] = mat.mainTextureOffset.x;
                e.textureOffset[1] = mat.mainTextureOffset.y;
            } else {
                MIPSYNC_WARN("PS1 material load failed '{}': {}", e.materialPath, err);
            }
        } else {
            // Read-only compatibility for scenes saved before renderers became
            // material-only. The editor migrates these bindings to .nmat files.
            texPath = e.texturePath;
        }

        e.textureIndex = 0;
        e.textureIndex = RegisterTexturePath(data.texturePaths, texIndexByPath, texPath);
    }

    for (auto& ui : data.uiElements) {
        ui.textureIndex = RegisterTexturePath(data.texturePaths, texIndexByPath, ui.texturePath);
    }
    for (auto& group : data.uiButtonGroups) {
        group.cursorTextureIndex =
            RegisterTexturePath(data.texturePaths, texIndexByPath, group.cursorTexturePath);
    }

    data.backgroundTextureIndex = 0;
    for (auto& e : data.entities) {
        e.cameraBackgroundTextureIndex = 0;
        if (!e.hasCamera || e.prerenderedBackgroundPath.empty())
            continue;

        std::string resolvedBackground;
        if (!ResolvePrerenderedBackgroundPath(root, scenePath, e.prerenderedBackgroundPath,
                                              resolvedBackground)) {
            MIPSYNC_WARN("PS1 pre-rendered background not found '{}'; using draw-env clear",
                         e.prerenderedBackgroundPath);
            continue;
        }

        e.cameraBackgroundTextureIndex =
            RegisterTexturePath(data.texturePaths, texIndexByPath, resolvedBackground);
        if (std::find(data.backgroundImagePaths.begin(), data.backgroundImagePaths.end(),
                      resolvedBackground) == data.backgroundImagePaths.end()) {
            data.backgroundImagePaths.push_back(resolvedBackground);
        }
        if (data.backgroundTextureIndex == 0 &&
            data.cameraEntityIndex >= 0 &&
            static_cast<size_t>(data.cameraEntityIndex) < data.entities.size() &&
            data.entities[static_cast<size_t>(data.cameraEntityIndex)].id == e.id) {
            data.backgroundImagePath = resolvedBackground;
            data.backgroundTextureIndex = e.cameraBackgroundTextureIndex;
        }
    }

    if (data.backgroundTextureIndex == 0 && !requestedSceneBackground.empty()) {
        std::string resolvedBackground;
        if (ResolvePrerenderedBackgroundPath(root, scenePath, requestedSceneBackground,
                                             resolvedBackground)) {
            data.backgroundTextureIndex =
                RegisterTexturePath(data.texturePaths, texIndexByPath, resolvedBackground);
            if (data.backgroundTextureIndex != 0) {
                data.backgroundImagePath = resolvedBackground;
                if (std::find(data.backgroundImagePaths.begin(), data.backgroundImagePaths.end(),
                              resolvedBackground) == data.backgroundImagePaths.end()) {
                    data.backgroundImagePaths.push_back(resolvedBackground);
                }
            }
        } else {
            MIPSYNC_WARN("PS1 scene background not found '{}'; using draw-env clear",
                         requestedSceneBackground);
        }
    }
}

bool FindMeshUvTransform(const Ps1SceneExportResult& data, const std::string& meshPath,
                         float tiling[2], float offset[2], bool& outExportUvs) {
    outExportUvs = false;
    tiling[0] = 1.0f;
    tiling[1] = 1.0f;
    offset[0] = 0.0f;
    offset[1] = 0.0f;
    for (const auto& e : data.entities) {
        if (e.meshKind != 4 || e.textureIndex == 0)
            continue;
        if (e.meshPath != meshPath && !SameSkinnedAnimSequence(e.meshPath, meshPath))
            continue;
        tiling[0] = e.textureTiling[0];
        tiling[1] = e.textureTiling[1];
        offset[0] = e.textureOffset[0];
        offset[1] = e.textureOffset[1];
        outExportUvs = true;
        return true;
    }
    return false;
}

static void EulerToForwardUnity(const float eulerDeg[3], float outForward[3]) {
    // Matches TransformComponent::RotationMatrixFromEuler * (0,0,-1).
    const float pitch = eulerDeg[0] * 3.14159265f / 180.0f;
    const float yaw   = eulerDeg[1] * 3.14159265f / 180.0f;
    const float sp = std::sin(pitch);
    const float cp = std::cos(pitch);
    const float sy = std::sin(yaw);
    const float cy = std::cos(yaw);
    outForward[0] = -(cp * sy);
    outForward[1] = sp;
    outForward[2] = -(cp * cy);
}

static void Normalize3(float v[3]) {
    const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len < 1e-6f) { v[0] = 0; v[1] = -1; v[2] = 0; return; }
    v[0] /= len; v[1] /= len; v[2] /= len;
}

static float Dot3(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void Cross3(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

int MeshKindFromJson(const json& mr, const std::string& entityName) {
    std::string prim = mr.value("primitive", std::string{"Cube"});
    if (prim == "sphere" || prim == "Sphere") return 3;
    if (prim == "plane" || prim == "Plane") return 2;
    if (prim == "cube"  || prim == "Cube")  return 1;
    if (prim == "terrain" || prim == "Terrain") return 4;
    if (prim == "probuilder" || prim == "ProBuilder" || prim == "promodeler" || prim == "ProModeler") return 4;
    if (entityName == "Sphere") return 3;
    if (entityName == "Plane" || entityName == "Floor") return 2;
    if (entityName == "Cube") return 1;
    if (mr.contains("mesh") && mr["mesh"].is_string()) return 4; // file mesh → custom mesh
    return 0;
}

static std::string TerrainMeshKeyFromJson(const json& terrain) {
    const float size = terrain.value("size", 32.0f);
    const int subdivisions = std::clamp(terrain.value("subdivisions", 32), 1, 128);
    const float heightScale = terrain.value("heightScale", 2.0f);
    const float noiseScale = terrain.value("noiseScale", 0.18f);
    const int seed = terrain.value("seed", 1337);
    const int flat = terrain.value("flat", false) ? 1 : 0;
    char buf[160];
    std::snprintf(buf, sizeof(buf), "terrain://%.4f|%d|%.4f|%.5f|%d|%d",
                  size, subdivisions, heightScale, noiseScale, seed, flat);
    return buf;
}

static glm::vec4 TerrainPackedColorToVec4(uint32_t color) {
    constexpr float inv = 1.0f / 255.0f;
    return {
        static_cast<float>((color >> 24u) & 0xFFu) * inv,
        static_cast<float>((color >> 16u) & 0xFFu) * inv,
        static_cast<float>((color >> 8u) & 0xFFu) * inv,
        static_cast<float>(color & 0xFFu) * inv,
    };
}

static std::string RegisterTerrainMeshFromJson(const json& terrain, Ps1SceneExportResult& result) {
    Ps1TerrainMeshData data;
    data.key = "terraindata://" + std::to_string(result.terrainMeshes.size());
    data.size = terrain.value("size", 32.0f);
    data.subdivisions = std::clamp(terrain.value("subdivisions", 32), 1, 128);
    data.heightScale = terrain.value("heightScale", 2.0f);
    data.noiseScale = terrain.value("noiseScale", 0.18f);
    data.seed = terrain.value("seed", 1337);
    data.flat = terrain.value("flat", false);

    const int exportSubdivisions = std::clamp(data.subdivisions, 1, 24);
    const int sourceRow = data.subdivisions + 1;
    const int exportRow = exportSubdivisions + 1;
    const size_t sourceExpected = static_cast<size_t>(sourceRow * sourceRow);

    if (terrain.contains("heights") && terrain["heights"].is_array() &&
        terrain["heights"].size() == sourceExpected) {
        std::vector<float> sourceHeights;
        sourceHeights.reserve(sourceExpected);
        for (const auto& h : terrain["heights"])
            sourceHeights.push_back(h.is_number() ? h.get<float>() : 0.0f);

        data.heights.assign(static_cast<size_t>(exportRow * exportRow), 0.0f);
        for (int z = 0; z < exportRow; ++z) {
            const int sz = std::clamp(static_cast<int>(std::lround(
                                      (static_cast<float>(z) / exportSubdivisions) * data.subdivisions)),
                                      0, data.subdivisions);
            for (int x = 0; x < exportRow; ++x) {
                const int sx = std::clamp(static_cast<int>(std::lround(
                                          (static_cast<float>(x) / exportSubdivisions) * data.subdivisions)),
                                          0, data.subdivisions);
                data.heights[static_cast<size_t>(z * exportRow + x)] =
                    sourceHeights[static_cast<size_t>(sz * sourceRow + sx)];
            }
        }
    }
    if (data.heights.empty())
        data.heights.assign(static_cast<size_t>(exportRow * exportRow), 0.0f);

    if (terrain.contains("colors") && terrain["colors"].is_array() &&
        terrain["colors"].size() == sourceExpected) {
        std::vector<uint32_t> sourceColors;
        sourceColors.reserve(sourceExpected);
        for (const auto& c : terrain["colors"]) {
            if (c.is_number_unsigned())
                sourceColors.push_back(c.get<uint32_t>());
            else if (c.is_number_integer())
                sourceColors.push_back(static_cast<uint32_t>(c.get<int64_t>()));
            else
                sourceColors.push_back(0xFFFFFFFFu);
        }

        data.colors.assign(static_cast<size_t>(exportRow * exportRow), 0xFFFFFFFFu);
        for (int z = 0; z < exportRow; ++z) {
            const int sz = std::clamp(static_cast<int>(std::lround(
                                      (static_cast<float>(z) / exportSubdivisions) * data.subdivisions)),
                                      0, data.subdivisions);
            for (int x = 0; x < exportRow; ++x) {
                const int sx = std::clamp(static_cast<int>(std::lround(
                                          (static_cast<float>(x) / exportSubdivisions) * data.subdivisions)),
                                          0, data.subdivisions);
                data.colors[static_cast<size_t>(z * exportRow + x)] =
                    sourceColors[static_cast<size_t>(sz * sourceRow + sx)];
            }
        }
    }
    if (data.colors.empty())
        data.colors.assign(static_cast<size_t>(exportRow * exportRow), 0xFFFFFFFFu);

    data.subdivisions = exportSubdivisions;
    result.terrainMeshes.push_back(std::move(data));
    return result.terrainMeshes.back().key;
}

static uint32_t PackedColorFromJson(const json& color) {
    if (color.is_number_unsigned())
        return color.get<uint32_t>();
    if (color.is_number_integer())
        return static_cast<uint32_t>(color.get<int64_t>());
    if (color.is_array() && color.size() >= 3) {
        const uint32_t r = ToByte01(color[0].get<float>());
        const uint32_t g = ToByte01(color[1].get<float>());
        const uint32_t b = ToByte01(color[2].get<float>());
        const uint32_t a = color.size() >= 4 ? ToByte01(color[3].get<float>()) : 255u;
        return (r << 24u) | (g << 16u) | (b << 8u) | a;
    }
    return 0xFFFFFFFFu;
}

static bool IsPrerenderOccluderEntity(const json& ent, const std::string& name) {
    if (ent.contains("meshRenderer") && ent["meshRenderer"].is_object() &&
        ent["meshRenderer"].value("prerenderOccluder", false))
        return true;

    static constexpr const char* kMarker = "_PrerenderOccluder";
    return name.find(kMarker) != std::string::npos;
}

static std::string RegisterProModelerMeshFromJson(const json& proModeler, Ps1SceneExportResult& result) {
    Ps1ProModelerMeshData data;
    data.key = "promodelerdata://" + std::to_string(result.proModelerMeshes.size());
    if (proModeler.contains("vertices") && proModeler["vertices"].is_array()) {
        for (const json& vj : proModeler["vertices"]) {
            float pos[3] = { 0.0f, 0.0f, 0.0f };
            float nrm[3] = { 0.0f, 1.0f, 0.0f };
            float uv[2] = { 0.0f, 0.0f };
            if (vj.contains("position")) ReadVec3(vj["position"], pos);
            if (vj.contains("normal")) ReadVec3(vj["normal"], nrm);
            if (vj.contains("uv")) ReadVec2(vj["uv"], uv);
            data.positions.insert(data.positions.end(), { pos[0], pos[1], pos[2] });
            data.normals.insert(data.normals.end(), { nrm[0], nrm[1], nrm[2] });
            data.uvs.insert(data.uvs.end(), { uv[0], uv[1] });
            data.colors.push_back(PackedColorFromJson(vj.value("color", json::array({ 1, 1, 1, 1 }))));
        }
    }
    if (proModeler.contains("indices") && proModeler["indices"].is_array()) {
        for (const json& index : proModeler["indices"]) {
            if (index.is_number_unsigned())
                data.indices.push_back(index.get<uint32_t>());
            else if (index.is_number_integer())
                data.indices.push_back(static_cast<uint32_t>(std::max<int64_t>(0, index.get<int64_t>())));
        }
    }
    result.proModelerMeshes.push_back(std::move(data));
    return result.proModelerMeshes.back().key;
}

static std::string RegisterPrimitiveSphereMesh(float size, Ps1SceneExportResult& result) {
    const float radius = std::max(size * 0.5f, 0.001f);
    constexpr int sectors = 18;
    constexpr int stacks = 12;
    char keyBuf[96];
    std::snprintf(keyBuf, sizeof(keyBuf), "primitivesphere://%.5f/%d/%d", radius, sectors, stacks);
    const std::string key = keyBuf;
    for (const Ps1ProModelerMeshData& existing : result.proModelerMeshes) {
        if (existing.key == key)
            return key;
    }

    constexpr float pi = 3.14159265358979323846f;
    Ps1ProModelerMeshData data;
    data.key = key;
    data.positions.reserve(static_cast<size_t>((stacks + 1) * (sectors + 1)) * 3u);
    data.normals.reserve(data.positions.capacity());
    data.uvs.reserve(static_cast<size_t>((stacks + 1) * (sectors + 1)) * 2u);
    data.colors.reserve(static_cast<size_t>((stacks + 1) * (sectors + 1)));
    for (int i = 0; i <= stacks; ++i) {
        const float stackAngle = pi * 0.5f - static_cast<float>(i) * pi / static_cast<float>(stacks);
        const float xz = radius * std::cos(stackAngle);
        const float y = radius * std::sin(stackAngle);
        for (int j = 0; j <= sectors; ++j) {
            const float sectorAngle = static_cast<float>(j) * 2.0f * pi / static_cast<float>(sectors);
            const float x = xz * std::cos(sectorAngle);
            const float z = xz * std::sin(sectorAngle);
            glm::vec3 normal = glm::normalize(glm::vec3(x, y, z));
            if (!std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z))
                normal = glm::vec3(0.0f, 1.0f, 0.0f);
            data.positions.insert(data.positions.end(), { x, y, z });
            data.normals.insert(data.normals.end(), { normal.x, normal.y, normal.z });
            data.uvs.insert(data.uvs.end(), {
                static_cast<float>(j) / static_cast<float>(sectors),
                static_cast<float>(i) / static_cast<float>(stacks),
            });
            data.colors.push_back(0xFFFFFFFFu);
        }
    }
    for (int i = 0; i < stacks; ++i) {
        int k1 = i * (sectors + 1);
        int k2 = k1 + sectors + 1;
        for (int j = 0; j < sectors; ++j, ++k1, ++k2) {
            if (i != 0) {
                data.indices.push_back(static_cast<uint32_t>(k1));
                data.indices.push_back(static_cast<uint32_t>(k1 + 1));
                data.indices.push_back(static_cast<uint32_t>(k2));
            }
            if (i != stacks - 1) {
                data.indices.push_back(static_cast<uint32_t>(k1 + 1));
                data.indices.push_back(static_cast<uint32_t>(k2 + 1));
                data.indices.push_back(static_cast<uint32_t>(k2));
            }
        }
    }
    result.proModelerMeshes.push_back(std::move(data));
    return key;
}

static uint32_t ColorToPackedFromVec4(const glm::vec4& c) {
    return (static_cast<uint32_t>(ToByte01(c.r)) << 24u) |
           (static_cast<uint32_t>(ToByte01(c.g)) << 16u) |
           (static_cast<uint32_t>(ToByte01(c.b)) << 8u) |
           static_cast<uint32_t>(ToByte01(c.a));
}

static glm::vec3 Ps1SkinPositionInGeometry(const SkinnedVertex& sv, const glm::mat4 bones[kMaxBones],
                                           int maxBoneIndex) {
    glm::vec4 pos(0.0f);
    for (int i = 0; i < 4; ++i) {
        const float weight = sv.boneWeights[i];
        if (weight < 1e-6f)
            continue;
        const int boneIndex = std::clamp(static_cast<int>(sv.boneIndices[i] + 0.5f), 0, maxBoneIndex);
        pos += weight * (bones[boneIndex] * glm::vec4(sv.position, 1.0f));
    }
    return glm::vec3(pos);
}

static glm::vec3 Ps1SkinNormalInGeometry(const SkinnedVertex& sv, const glm::mat4 bones[kMaxBones],
                                         int maxBoneIndex) {
    glm::vec3 normal(0.0f);
    for (int i = 0; i < 4; ++i) {
        const float weight = sv.boneWeights[i];
        if (weight < 1e-6f)
            continue;
        const int boneIndex = std::clamp(static_cast<int>(sv.boneIndices[i] + 0.5f), 0, maxBoneIndex);
        const glm::mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(bones[boneIndex])));
        normal += weight * (normalMatrix * sv.normal);
    }
    const float len = glm::length(normal);
    return len > 1e-6f ? (normal / len) : glm::vec3(0.0f, 1.0f, 0.0f);
}

static Vertex BakeSkinnedVertexForPs1(const SkeletalModelAsset& model, uint32_t sourceVertexIndex,
                                      const glm::mat4 bones[kMaxBones]) {
    if (sourceVertexIndex >= model.sourceVertices.size())
        return Vertex{ glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(0.0f), glm::vec4(1.0f) };

    const SkinnedVertex& sv = model.sourceVertices[sourceVertexIndex];
    const int maxBoneIndex = static_cast<int>(model.bones.size()) - 1;
    const glm::mat4 geometryToWorld =
        sourceVertexIndex < model.vertexGeometryToWorld.size()
        ? model.vertexGeometryToWorld[sourceVertexIndex]
        : model.meshGeometryToWorld;
    const glm::mat3 normalGeometry =
        sourceVertexIndex < model.vertexNormalGeo.size()
        ? model.vertexNormalGeo[sourceVertexIndex]
        : glm::transpose(glm::inverse(glm::mat3(geometryToWorld)));

    const float weightSum = sv.boneWeights.x + sv.boneWeights.y + sv.boneWeights.z + sv.boneWeights.w;
    const glm::vec3 geometryPos = weightSum > 1e-6f && maxBoneIndex >= 0
        ? Ps1SkinPositionInGeometry(sv, bones, maxBoneIndex)
        : sv.position;
    const glm::vec3 geometryNormal = weightSum > 1e-6f && maxBoneIndex >= 0
        ? Ps1SkinNormalInGeometry(sv, bones, maxBoneIndex)
        : sv.normal;

    const glm::vec3 worldPos = glm::vec3(geometryToWorld * glm::vec4(geometryPos, 1.0f));
    const glm::vec3 worldNormal = glm::normalize(normalGeometry * geometryNormal);
    const glm::vec3 displayPos = (worldPos - model.displayCenter) * model.displayScale;
    return Vertex{ displayPos, worldNormal, sv.uv, sv.color };
}

static void ReduceSkinnedTopologyForPs1(const std::vector<Vertex>& sampleVerts,
                                        std::vector<uint32_t>& usedVertices,
                                        std::vector<uint32_t>& compactIndices,
                                        size_t maxTris,
                                        size_t maxVerts,
                                        std::vector<std::vector<uint32_t>>* outClusterSourceVertices = nullptr) {
    if (outClusterSourceVertices)
        outClusterSourceVertices->clear();
    const size_t triCount = compactIndices.size() / 3;
    if (triCount == 0)
        return;
    if (triCount <= maxTris && usedVertices.size() <= maxVerts)
        return;

    glm::vec3 bmin = sampleVerts[compactIndices[0]].position;
    glm::vec3 bmax = bmin;
    for (uint32_t idx : compactIndices) {
        if (idx >= sampleVerts.size())
            continue;
        bmin = glm::min(bmin, sampleVerts[idx].position);
        bmax = glm::max(bmax, sampleVerts[idx].position);
    }
    const glm::vec3 ext = glm::max(bmax - bmin, glm::vec3(1e-6f));
    const int grid = 18;
    auto packCell = [](int x, int y, int z) -> uint64_t {
        return (static_cast<uint64_t>(x & 0x3ff) << 20u) |
               (static_cast<uint64_t>(y & 0x3ff) << 10u) |
               static_cast<uint64_t>(z & 0x3ff);
    };

    auto buildClusteredTopology = [&](int grid,
                                      std::vector<uint32_t>& outUsedVertices,
                                      std::vector<uint32_t>& outIndices,
                                      std::vector<std::vector<uint32_t>>& outSourceVerticesByCluster) -> bool {
        struct SkinnedCluster {
            glm::vec3 posSum{ 0.0f };
            glm::vec3 centroid{ 0.0f };
            uint32_t count = 0;
            uint32_t representative = UINT32_MAX;
            float representativeDist2 = std::numeric_limits<float>::max();
        };

        std::unordered_map<uint64_t, uint32_t> cellToCluster;
        cellToCluster.reserve(maxVerts * 2);
        std::vector<SkinnedCluster> clusters;
        clusters.reserve(maxVerts);
        std::vector<uint32_t> vertexToCluster(usedVertices.size(), UINT32_MAX);

        for (size_t vi = 0; vi < usedVertices.size(); ++vi) {
            const glm::vec3& p = sampleVerts[vi].position;
            const int ix = std::clamp(static_cast<int>((p.x - bmin.x) / ext.x * grid), 0, grid - 1);
            const int iy = std::clamp(static_cast<int>((p.y - bmin.y) / ext.y * grid), 0, grid - 1);
            const int iz = std::clamp(static_cast<int>((p.z - bmin.z) / ext.z * grid), 0, grid - 1);
            const uint64_t key = packCell(ix, iy, iz);
            auto it = cellToCluster.find(key);
            if (it == cellToCluster.end()) {
                const uint32_t clusterIndex = static_cast<uint32_t>(clusters.size());
                it = cellToCluster.emplace(key, clusterIndex).first;
                clusters.push_back({});
            }
            SkinnedCluster& cluster = clusters[it->second];
            cluster.posSum += p;
            ++cluster.count;
            vertexToCluster[vi] = it->second;
        }

        if (clusters.empty() || clusters.size() > maxVerts)
            return false;

        for (SkinnedCluster& cluster : clusters)
            cluster.centroid = cluster.count > 0 ? (cluster.posSum / static_cast<float>(cluster.count)) : glm::vec3(0.0f);

        for (size_t vi = 0; vi < usedVertices.size(); ++vi) {
            const uint32_t clusterIndex = vertexToCluster[vi];
            if (clusterIndex >= clusters.size())
                continue;
            SkinnedCluster& cluster = clusters[clusterIndex];
            const glm::vec3 delta = sampleVerts[vi].position - cluster.centroid;
            const float dist2 = glm::dot(delta, delta);
            if (dist2 < cluster.representativeDist2) {
                cluster.representativeDist2 = dist2;
                cluster.representative = static_cast<uint32_t>(vi);
            }
        }

        std::vector<uint32_t> reducedIndices;
        reducedIndices.reserve(compactIndices.size());
        std::unordered_set<uint64_t> emittedTriangles;
        emittedTriangles.reserve(maxTris * 2);
        auto triKey = [](uint32_t a, uint32_t b, uint32_t c) -> uint64_t {
            uint32_t s0 = a, s1 = b, s2 = c;
            if (s1 < s0) std::swap(s0, s1);
            if (s2 < s1) std::swap(s1, s2);
            if (s1 < s0) std::swap(s0, s1);
            return (static_cast<uint64_t>(s0 & 0x1fffff) << 42u) |
                   (static_cast<uint64_t>(s1 & 0x1fffff) << 21u) |
                   static_cast<uint64_t>(s2 & 0x1fffff);
        };

        for (size_t ti = 0; ti + 2 < compactIndices.size(); ti += 3) {
            const uint32_t originalA = compactIndices[ti + 0];
            const uint32_t originalB = compactIndices[ti + 1];
            const uint32_t originalC = compactIndices[ti + 2];
            if (originalA >= vertexToCluster.size() || originalB >= vertexToCluster.size() || originalC >= vertexToCluster.size())
                continue;
            const uint32_t a = vertexToCluster[originalA];
            const uint32_t b = vertexToCluster[originalB];
            const uint32_t c = vertexToCluster[originalC];
            if (a == b || b == c || a == c)
                continue;
            const uint64_t key = triKey(a, b, c);
            if (!emittedTriangles.insert(key).second)
                continue;
            reducedIndices.push_back(a);
            reducedIndices.push_back(b);
            reducedIndices.push_back(c);
        }

        if (reducedIndices.size() < 3 || (reducedIndices.size() / 3) > maxTris)
            return false;

        std::vector<std::vector<uint32_t>> sourceVerticesByCluster(clusters.size());
        for (size_t vi = 0; vi < usedVertices.size(); ++vi) {
            const uint32_t clusterIndex = vertexToCluster[vi];
            if (clusterIndex < sourceVerticesByCluster.size())
                sourceVerticesByCluster[clusterIndex].push_back(usedVertices[vi]);
        }

        outUsedVertices.resize(clusters.size());
        for (size_t ci = 0; ci < clusters.size(); ++ci) {
            const uint32_t representative = clusters[ci].representative;
            if (representative == UINT32_MAX || representative >= usedVertices.size() || sourceVerticesByCluster[ci].empty())
                return false;
            outUsedVertices[ci] = usedVertices[representative];
        }
        outIndices = std::move(reducedIndices);
        outSourceVerticesByCluster = std::move(sourceVerticesByCluster);
        return true;
    };

    std::vector<uint32_t> bestClusteredUsedVertices;
    std::vector<uint32_t> bestClusteredIndices;
    std::vector<std::vector<uint32_t>> bestClusteredSourceVertices;
    for (int gridCandidate = 3; gridCandidate <= 32; ++gridCandidate) {
        std::vector<uint32_t> candidateUsedVertices;
        std::vector<uint32_t> candidateIndices;
        std::vector<std::vector<uint32_t>> candidateSourceVertices;
        if (!buildClusteredTopology(gridCandidate, candidateUsedVertices, candidateIndices, candidateSourceVertices))
            continue;
        const size_t candidateTris = candidateIndices.size() / 3;
        const size_t bestTris = bestClusteredIndices.size() / 3;
        if (candidateTris > bestTris ||
            (candidateTris == bestTris && candidateUsedVertices.size() > bestClusteredUsedVertices.size())) {
            bestClusteredUsedVertices = std::move(candidateUsedVertices);
            bestClusteredIndices = std::move(candidateIndices);
            bestClusteredSourceVertices = std::move(candidateSourceVertices);
        }
    }
    if (!bestClusteredUsedVertices.empty() && bestClusteredIndices.size() >= 3) {
        usedVertices = std::move(bestClusteredUsedVertices);
        compactIndices = std::move(bestClusteredIndices);
        if (outClusterSourceVertices)
            *outClusterSourceVertices = std::move(bestClusteredSourceVertices);
        return;
    }

    struct RankedTri {
        size_t triIndex = 0;
        float area = 0.0f;
    };
    std::vector<RankedTri> ranked;
    ranked.reserve(triCount);
    std::unordered_map<uint64_t, RankedTri> bestPerCell;
    bestPerCell.reserve(maxTris * 2);

    for (size_t ti = 0; ti < triCount; ++ti) {
        const uint32_t i0 = compactIndices[ti * 3 + 0];
        const uint32_t i1 = compactIndices[ti * 3 + 1];
        const uint32_t i2 = compactIndices[ti * 3 + 2];
        if (i0 >= sampleVerts.size() || i1 >= sampleVerts.size() || i2 >= sampleVerts.size())
            continue;
        const glm::vec3& a = sampleVerts[i0].position;
        const glm::vec3& b = sampleVerts[i1].position;
        const glm::vec3& c = sampleVerts[i2].position;
        const float area = glm::length(glm::cross(b - a, c - a)) * 0.5f;
        if (!(area > 1e-12f))
            continue;
        ranked.push_back({ ti, area });

        const glm::vec3 center = (a + b + c) * (1.0f / 3.0f);
        const int ix = std::clamp(static_cast<int>((center.x - bmin.x) / ext.x * grid), 0, grid - 1);
        const int iy = std::clamp(static_cast<int>((center.y - bmin.y) / ext.y * grid), 0, grid - 1);
        const int iz = std::clamp(static_cast<int>((center.z - bmin.z) / ext.z * grid), 0, grid - 1);
        const uint64_t key = packCell(ix, iy, iz);
        auto it = bestPerCell.find(key);
        if (it == bestPerCell.end() || area > it->second.area)
            bestPerCell[key] = { ti, area };
    }

    std::sort(ranked.begin(), ranked.end(), [](const RankedTri& a, const RankedTri& b) {
        return a.area > b.area;
    });

    std::vector<size_t> candidateTris;
    candidateTris.reserve(ranked.size());
    std::unordered_set<size_t> chosen;
    chosen.reserve(maxTris * 2);
    for (const auto& [key, tri] : bestPerCell) {
        (void)key;
        if (chosen.insert(tri.triIndex).second)
            candidateTris.push_back(tri.triIndex);
    }
    for (const RankedTri& tri : ranked) {
        if (chosen.insert(tri.triIndex).second)
            candidateTris.push_back(tri.triIndex);
    }

    std::vector<size_t> selectedTris;
    selectedTris.reserve(maxTris);
    std::vector<uint8_t> selectedVert(usedVertices.size(), 0);
    size_t selectedVertCount = 0;
    for (size_t ti : candidateTris) {
        if (selectedTris.size() >= maxTris)
            break;
        uint32_t tri[3] = {
            compactIndices[ti * 3 + 0],
            compactIndices[ti * 3 + 1],
            compactIndices[ti * 3 + 2],
        };
        if (tri[0] >= usedVertices.size() || tri[1] >= usedVertices.size() || tri[2] >= usedVertices.size())
            continue;
        size_t newVerts = 0;
        for (uint32_t idx : tri)
            newVerts += selectedVert[idx] ? 0 : 1;
        if (selectedVertCount + newVerts > maxVerts)
            continue;
        selectedTris.push_back(ti);
        for (uint32_t idx : tri) {
            if (!selectedVert[idx]) {
                selectedVert[idx] = 1;
                ++selectedVertCount;
            }
        }
    }

    if (selectedTris.empty())
        return;

    std::vector<uint32_t> remap(usedVertices.size(), UINT32_MAX);
    std::vector<uint32_t> reducedUsedVertices;
    std::vector<uint32_t> reducedIndices;
    reducedUsedVertices.reserve(selectedVertCount);
    reducedIndices.reserve(selectedTris.size() * 3);
    for (size_t ti : selectedTris) {
        for (int corner = 0; corner < 3; ++corner) {
            const uint32_t oldCompact = compactIndices[ti * 3 + static_cast<size_t>(corner)];
            uint32_t& mapped = remap[oldCompact];
            if (mapped == UINT32_MAX) {
                mapped = static_cast<uint32_t>(reducedUsedVertices.size());
                reducedUsedVertices.push_back(usedVertices[oldCompact]);
            }
            reducedIndices.push_back(mapped);
        }
    }

    usedVertices = std::move(reducedUsedVertices);
    compactIndices = std::move(reducedIndices);
}

static std::string RegisterSkinnedAnimMeshesFromJson(const json& skinned, Ps1SceneExportResult& result,
                                                     uint16_t& outFrameCount, uint8_t& outFps) {
    outFrameCount = 0;
    outFps = 0;

    const std::string modelPath = skinned.value("model", std::string{});
    if (modelPath.empty())
        return {};

    const int ps1Mode = std::clamp(skinned.value("ps1ExportMode", 0), 0, 2);
    if (ps1Mode <= 0)
        return {};

    const fs::path modelAbs = PathUtf8::FromString(AssetManager::Get().ToAbsolute(modelPath));
    std::error_code sizeEc;
    const uintmax_t modelFileSize = fs::file_size(modelAbs, sizeEc);
    const float maxSourceMb = std::clamp(skinned.value("ps1VertexAnimMaxSourceMB", 4.0f), 0.25f, 64.0f);
    const uintmax_t warnSourceBytes =
        static_cast<uintmax_t>(maxSourceMb * 1024.0f * 1024.0f);
    if (!sizeEc && modelFileSize > warnSourceBytes) {
        static std::unordered_set<std::string> warnedLargeSkinnedModels;
        if (warnedLargeSkinnedModels.insert(modelPath).second) {
            MIPSYNC_WARN(
                "PS1 vertex animation source '{}' is large ({:.2f} MB FBX); "
                "export will bake only the configured low-poly PS1 budget.",
                modelPath, static_cast<double>(modelFileSize) / (1024.0 * 1024.0));
        }
    }

    auto model = AssetManager::Get().GetSkeletalModel(modelPath);
    if (!model || model->sourceVertices.empty() || model->sourceIndices.empty()) {
        MIPSYNC_WARN("PS1 vertex animation: skeletal model unavailable '{}'", modelPath);
        return {};
    }
    if (model->sourceVertices.size() > 5000 || (model->sourceIndices.size() / 3) > 2500) {
        MIPSYNC_WARN(
            "PS1 vertex animation source '{}' is high-poly ({} verts / {} tris); "
            "export will decimate to the configured PS1 target.",
            modelPath, model->sourceVertices.size(), model->sourceIndices.size() / 3);
    }

    const int ps1Fps = ps1Mode == 2
        ? std::clamp(skinned.value("ps1VertexAnimFps", 15), 1, 30)
        : 1;
    const int ps1MaxFrames = ps1Mode == 2
        ? std::clamp(skinned.value("ps1VertexAnimMaxFrames", 30), 1, 120)
        : 1;
    const int stackIndex = ps1Mode == 2 && !model->animationStackIndices.empty()
        ? static_cast<int>(model->animationStackIndices[0])
        : -1;
    const double duration = stackIndex >= 0 ? model->GetClipDurationByStackIndex(stackIndex) : 0.0;
    const int requestedFrames = duration > 0.0
        ? static_cast<int>(std::ceil(duration * static_cast<double>(ps1Fps)))
        : 1;
    const int frameCount = std::clamp(requestedFrames, 1, ps1MaxFrames);
    const bool cappedFrames = duration > 0.0 && requestedFrames > frameCount;
    const int playbackFps = cappedFrames
        ? std::clamp(static_cast<int>(std::lround(static_cast<double>(frameCount) / duration)), 1, ps1Fps)
        : ps1Fps;

    const int meshPartIndex = skinned.value("meshPart", -1);
    uint32_t indexOffset = 0;
    uint32_t indexCount = static_cast<uint32_t>(model->sourceIndices.size());
    if (meshPartIndex >= 0 && static_cast<size_t>(meshPartIndex) < model->meshParts.size()) {
        const auto& part = model->meshParts[static_cast<size_t>(meshPartIndex)];
        indexOffset = part.indexOffset;
        indexCount = part.indexCount;
    }
    if (indexOffset >= model->sourceIndices.size())
        return {};
    indexCount = std::min<uint32_t>(indexCount, static_cast<uint32_t>(model->sourceIndices.size() - indexOffset));
    if (indexCount < 3)
        return {};

    std::vector<uint32_t> remap(model->sourceVertices.size(), UINT32_MAX);
    std::vector<uint32_t> usedVertices;
    usedVertices.reserve(std::min<size_t>(model->sourceVertices.size(), indexCount));
    std::vector<uint32_t> compactIndices;
    compactIndices.reserve(indexCount);
    for (uint32_t ii = 0; ii < indexCount; ++ii) {
        const uint32_t oldIndex = model->sourceIndices[indexOffset + ii];
        if (oldIndex >= remap.size())
            continue;
        if (remap[oldIndex] == UINT32_MAX) {
            remap[oldIndex] = static_cast<uint32_t>(usedVertices.size());
            usedVertices.push_back(oldIndex);
        }
        compactIndices.push_back(remap[oldIndex]);
    }
    if (usedVertices.empty() || compactIndices.size() < 3)
        return {};

    std::vector<std::vector<uint32_t>> clusteredSourceVertices;
    {
        glm::mat4 sampleBones[kMaxBones];
        if (stackIndex >= 0 && duration > 0.0)
            model->EvaluateBoneMatricesByStackIndex(stackIndex, 0.0, sampleBones);
        else
            model->EvaluateBoneMatrices({}, 0.0, sampleBones);

        std::vector<Vertex> sampleVerts;
        sampleVerts.reserve(usedVertices.size());
        for (uint32_t oldIndex : usedVertices)
            sampleVerts.push_back(BakeSkinnedVertexForPs1(*model, oldIndex, sampleBones));

        const size_t configuredTargetTris =
            static_cast<size_t>(std::clamp(skinned.value("ps1VertexAnimTargetTris", 640), 32, 4000));
        const size_t configuredTargetVerts =
            static_cast<size_t>(std::clamp(skinned.value("ps1VertexAnimTargetVerts", 2000), 64, 5000));
        size_t targetTris = configuredTargetTris;
        size_t targetVerts = configuredTargetVerts;
        if (meshPartIndex >= 0 && model->meshParts.size() > 1) {
            const size_t partCount = std::max<size_t>(model->meshParts.size(), 1);
            targetTris = std::clamp<size_t>(
                (configuredTargetTris * 4 + partCount - 1) / partCount,
                96,
                configuredTargetTris);
            targetVerts = std::clamp<size_t>(
                targetTris * 3,
                64,
                configuredTargetVerts);
        }
        const size_t beforeVerts = usedVertices.size();
        const size_t beforeTris = compactIndices.size() / 3;
        ReduceSkinnedTopologyForPs1(sampleVerts, usedVertices, compactIndices, targetTris, targetVerts, &clusteredSourceVertices);
        MIPSYNC_INFO("PS1 vertex animation topology: {}v/{}t -> {}v/{}t (stable across frames, budget {}v/{}t)",
                     beforeVerts, beforeTris, usedVertices.size(), compactIndices.size() / 3,
                     targetVerts, targetTris);
    }

    const size_t sequenceStart = result.skinnedAnimMeshes.size();
    const std::string baseKey = "skinnedanim://" + std::to_string(sequenceStart);
    glm::mat4 bones[kMaxBones];
    for (int frame = 0; frame < frameCount; ++frame) {
        const double time = frameCount > 1
            ? (cappedFrames
                ? (duration * static_cast<double>(frame) / static_cast<double>(frameCount))
                : (static_cast<double>(frame) / static_cast<double>(ps1Fps)))
            : 0.0;
        if (stackIndex >= 0 && duration > 0.0)
            model->EvaluateBoneMatricesByStackIndex(stackIndex, std::fmod(time, duration), bones);
        else
            model->EvaluateBoneMatrices({}, 0.0, bones);

        Ps1SkinnedAnimMeshData mesh;
        mesh.key = baseKey + "#" + std::to_string(frame);
        const size_t bakedVertexCount = !clusteredSourceVertices.empty()
            ? clusteredSourceVertices.size()
            : usedVertices.size();
        mesh.positions.reserve(bakedVertexCount * 3);
        mesh.normals.reserve(bakedVertexCount * 3);
        mesh.uvs.reserve(bakedVertexCount * 2);
        mesh.colors.reserve(bakedVertexCount);

        auto appendVertex = [&](const Vertex& v) {
            mesh.positions.insert(mesh.positions.end(), { v.position.x, v.position.y, v.position.z });
            mesh.normals.insert(mesh.normals.end(), { v.normal.x, v.normal.y, v.normal.z });
            mesh.uvs.insert(mesh.uvs.end(), { v.uv.x, v.uv.y });
            mesh.colors.push_back(ColorToPackedFromVec4(v.color));
        };

        if (!clusteredSourceVertices.empty()) {
            for (const auto& sourceCluster : clusteredSourceVertices) {
                glm::vec3 positionSum(0.0f);
                glm::vec3 normalSum(0.0f);
                glm::vec2 uvSum(0.0f);
                glm::vec4 colorSum(0.0f);
                uint32_t count = 0;
                for (uint32_t oldIndex : sourceCluster) {
                    if (oldIndex >= model->sourceVertices.size())
                        continue;
                    const Vertex v = BakeSkinnedVertexForPs1(*model, oldIndex, bones);
                    positionSum += v.position;
                    normalSum += v.normal;
                    uvSum += v.uv;
                    colorSum += v.color;
                    ++count;
                }
                if (count == 0) {
                    appendVertex(Vertex{});
                    continue;
                }
                const float invCount = 1.0f / static_cast<float>(count);
                Vertex averaged{};
                averaged.position = positionSum * invCount;
                const float normalLen2 = glm::dot(normalSum, normalSum);
                averaged.normal = normalLen2 > 1e-8f
                    ? glm::normalize(normalSum)
                    : glm::vec3(0.0f, 1.0f, 0.0f);
                averaged.uv = uvSum * invCount;
                averaged.color = colorSum * invCount;
                appendVertex(averaged);
            }
        } else {
            for (uint32_t oldIndex : usedVertices) {
                const Vertex v = BakeSkinnedVertexForPs1(*model, oldIndex, bones);
                appendVertex(v);
            }
        }
        mesh.indices = compactIndices;

        result.skinnedAnimMeshes.push_back(std::move(mesh));
    }

    const size_t generated = result.skinnedAnimMeshes.size() - sequenceStart;
    if (generated == 0)
        return {};
    outFrameCount = static_cast<uint16_t>(std::min<size_t>(generated, 65535));
    outFps = static_cast<uint8_t>(playbackFps);
    MIPSYNC_INFO("PS1 vertex animation export: {} -> {} frames @ {}fps (low-budget PS1 mode)",
                 modelPath, outFrameCount, static_cast<int>(outFps));
    return baseKey + "#0";
}

static int DominantBoneIndex(const SkinnedVertex& sv, int maxBoneIndex) {
    int best = 0;
    float bestWeight = sv.boneWeights.x;
    for (int i = 1; i < 4; ++i) {
        if (sv.boneWeights[i] > bestWeight) {
            bestWeight = sv.boneWeights[i];
            best = i;
        }
    }
    return std::clamp(static_cast<int>(sv.boneIndices[best] + 0.5f), 0, maxBoneIndex);
}

static glm::mat4 DisplayBoneMatrixForPs1(const SkeletalModelAsset& model, int boneIndex,
                                         const glm::mat4& geometryToWorld,
                                         const glm::mat4 bones[kMaxBones]) {
    const int safeBone = std::clamp(boneIndex, 0, kMaxBones - 1);
    const glm::mat4 boneToGeometry =
        static_cast<size_t>(safeBone) < model.bones.size()
        ? glm::inverse(model.bones[static_cast<size_t>(safeBone)].bindGeometryToBone)
        : glm::mat4(1.0f);
    glm::mat4 out = geometryToWorld * bones[safeBone] * boneToGeometry;
    out[3] = glm::vec4((glm::vec3(out[3]) - model.displayCenter) * model.displayScale, 1.0f);
    return out;
}

static int16_t ToMatrix12_4(float v) {
    return ClampI16(static_cast<int>(std::lround(v * 4096.0f)));
}

static glm::vec3 ExtractAxisScale(const glm::mat4& m) {
    glm::vec3 scale(1.0f);
    for (int col = 0; col < 3; ++col) {
        const glm::vec3 axis(m[col]);
        const float len = glm::length(axis);
        scale[col] = (std::isfinite(len) && len > 1e-8f) ? len : 1.0f;
    }
    return scale;
}

static glm::mat3 ExtractOrthonormalBasis(const glm::mat4& m) {
    glm::vec3 x(m[0]);
    glm::vec3 y(m[1]);
    glm::vec3 z(m[2]);

    if (!std::isfinite(glm::dot(x, x)) || glm::length(x) <= 1e-8f)
        x = glm::vec3(1.0f, 0.0f, 0.0f);
    else
        x = glm::normalize(x);

    y -= x * glm::dot(y, x);
    if (!std::isfinite(glm::dot(y, y)) || glm::length(y) <= 1e-8f) {
        const glm::vec3 fallback = std::abs(x.y) < 0.9f
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::vec3(0.0f, 0.0f, 1.0f);
        y = glm::normalize(fallback - x * glm::dot(fallback, x));
    } else {
        y = glm::normalize(y);
    }

    z = glm::cross(x, y);
    if (!std::isfinite(glm::dot(z, z)) || glm::length(z) <= 1e-8f)
        z = glm::vec3(0.0f, 0.0f, 1.0f);
    else
        z = glm::normalize(z);

    return glm::mat3(x, y, z);
}

static glm::mat3 Ps1RotationMatrixFromEuler(const glm::vec3& eulerDegrees) {
    const glm::mat4 rot =
        glm::rotate(glm::mat4(1.0f), glm::radians(eulerDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f)) *
        glm::rotate(glm::mat4(1.0f), glm::radians(eulerDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f)) *
        glm::rotate(glm::mat4(1.0f), glm::radians(eulerDegrees.z), glm::vec3(0.0f, 0.0f, 1.0f));
    return glm::mat3(rot);
}

static Ps1RigidAnimFrame RigidAnimFrameFromMatrix(const glm::mat4& m,
                                                  const glm::vec3& rootPos,
                                                  const glm::vec3& rootRotation,
                                                  const glm::vec3& rootScale,
                                                  const glm::vec3& localPivot,
                                                  const glm::vec3& axisScale,
                                                  float displayScale) {
    Ps1RigidAnimFrame out;

    const glm::mat3 rootBasis = Ps1RotationMatrixFromEuler(rootRotation);
    const glm::mat3 localBasis = ExtractOrthonormalBasis(m);
    const glm::mat3 basis = rootBasis * localBasis;
    const glm::vec3 pivotDisplay = localPivot * axisScale * displayScale;
    const glm::vec3 localPos = glm::vec3(m[3]) + localBasis * pivotDisplay;
    const glm::vec3 pos = rootPos + rootBasis * (localPos * rootScale);
    out.position[0] = pos.x; out.position[1] = pos.y; out.position[2] = pos.z;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col)
            out.matrix[row][col] = ToMatrix12_4(basis[col][row] * rootScale[row]);
    }
    return out;
}

static int GetParentBoneIndex(const SkeletalModelAsset& model, int boneIndex) {
    if (boneIndex < 0 || static_cast<size_t>(boneIndex) >= model.bones.size())
        return -1;
    ufbx_node* parentNode = model.bones[boneIndex].node ? model.bones[boneIndex].node->parent : nullptr;
    if (!parentNode)
        return -1;
    for (size_t i = 0; i < model.bones.size(); ++i) {
        if (model.bones[i].node == parentNode) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

static bool BoneNameContains(const SkeletalModelAsset& model, int boneIndex,
                             const char* needle) {
    if (boneIndex < 0 || static_cast<size_t>(boneIndex) >= model.bones.size())
        return false;
    std::string name = model.bones[static_cast<size_t>(boneIndex)].name;
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return name.find(needle) != std::string::npos;
}

// A Mixamo shoulder is a short helper joint inside the torso silhouette.
// Attaching its surface weights to UpperArm makes the collar rotate with the
// arm and protrude above the shoulder. Fold it into the nearest torso ancestor;
// the subsequent triangle split creates a clean torso/upper-arm boundary.
static int CollapseShoulderToTorso(const SkeletalModelAsset& model, int boneIndex) {
    if (!BoneNameContains(model, boneIndex, "shoulder"))
        return boneIndex;

    int current = GetParentBoneIndex(model, boneIndex);
    int nearestParent = current;
    while (current >= 0) {
        if (BoneNameContains(model, current, "spine") ||
            BoneNameContains(model, current, "chest"))
            return current;
        current = GetParentBoneIndex(model, current);
    }
    return nearestParent >= 0 ? nearestParent : boneIndex;
}

static int CanonicalRigidBone(const SkeletalModelAsset& model, int boneIndex) {
    boneIndex = CollapseShoulderToTorso(model, boneIndex);
    if (!BoneNameContains(model, boneIndex, "spine"))
        return boneIndex;

    // A classic segmented character uses one rigid torso shell. Mapping the
    // short Spine/Spine1/Spine2 helper chain to its middle joint prevents the
    // jacket/chest from being cut into several unrelated rigid fragments.
    int firstSpine = -1;
    int middleSpine = -1;
    for (size_t i = 0; i < model.bones.size(); ++i) {
        if (!BoneNameContains(model, static_cast<int>(i), "spine"))
            continue;
        if (firstSpine < 0)
            firstSpine = static_cast<int>(i);
        std::string name = model.bones[i].name;
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (name.find("spine1") != std::string::npos)
            middleSpine = static_cast<int>(i);
    }
    return middleSpine >= 0 ? middleSpine : (firstSpine >= 0 ? firstSpine : boneIndex);
}

static int DominantCanonicalRigidBone(const SkeletalModelAsset& model,
                                      const SkinnedVertex& vertex,
                                      int maxBoneIndex) {
    float weights[kMaxBones] = { 0.0f };
    for (int i = 0; i < 4; ++i) {
        if (vertex.boneWeights[i] <= 0.0f)
            continue;
        int bone = std::clamp(
            static_cast<int>(vertex.boneIndices[i] + 0.5f), 0, maxBoneIndex);
        bone = CanonicalRigidBone(model, bone);
        weights[bone] += vertex.boneWeights[i];
    }
    int bestBone = 0;
    float bestWeight = -1.0f;
    for (int bone = 0; bone <= maxBoneIndex; ++bone) {
        if (weights[bone] > bestWeight) {
            bestWeight = weights[bone];
            bestBone = bone;
        }
    }
    return bestBone;
}

struct RigidSplitGeometry {
    std::vector<SkinnedVertex> vertices;
    std::vector<glm::mat4> geometryToWorld;
    std::unordered_map<int, std::vector<uint32_t>> triangles;
    size_t mixedTriangleCount = 0;
    size_t splitTriangleCount = 0;
};

static RigidSplitGeometry BuildRigidSplitGeometry(const SkeletalModelAsset& model,
                                                   uint32_t indexOffset,
                                                   uint32_t indexCount,
                                                   int maxBoneIndex,
                                                   bool seamFill) {
    RigidSplitGeometry out;
    out.vertices = model.sourceVertices;
    out.geometryToWorld = model.vertexGeometryToWorld;
    if (out.geometryToWorld.size() < out.vertices.size())
        out.geometryToWorld.resize(out.vertices.size(), model.meshGeometryToWorld);

    // Keep authored faces intact for the torso and other mostly rigid areas,
    // but make a clean geometric cut at articulating limb joints. Whole-face
    // assignment at an elbow creates a long triangular spike while walking;
    // splitting every blended face everywhere is unnecessarily expensive.
    auto dominantTriangleBone = [&](const uint32_t indices[3]) {
        float scores[kMaxBones] = { 0.0f };
        for (int corner = 0; corner < 3; ++corner) {
            const SkinnedVertex& vertex = model.sourceVertices[indices[corner]];
            for (int influence = 0; influence < 4; ++influence) {
                const float weight = vertex.boneWeights[influence];
                if (weight <= 0.0f)
                    continue;
                int bone = std::clamp(
                    static_cast<int>(vertex.boneIndices[influence] + 0.5f),
                    0, maxBoneIndex);
                bone = CanonicalRigidBone(model, bone);
                scores[bone] += weight;
            }
        }
        int bestBone = 0;
        float bestScore = -1.0f;
        for (int bone = 0; bone <= maxBoneIndex; ++bone) {
            if (scores[bone] > bestScore) {
                bestScore = scores[bone];
                bestBone = bone;
            }
        }
        return bestBone;
    };

    auto isArmBone = [&](int bone) {
        return BoneNameContains(model, bone, "arm") ||
               BoneNameContains(model, bone, "hand");
    };

    auto appendInterpolated = [&](uint32_t a, uint32_t b, float t) {
        const SkinnedVertex& va = out.vertices[a];
        const SkinnedVertex& vb = out.vertices[b];
        SkinnedVertex vertex{};
        vertex.position = glm::mix(va.position, vb.position, t);
        vertex.normal = glm::mix(va.normal, vb.normal, t);
        if (glm::dot(vertex.normal, vertex.normal) > 1e-8f)
            vertex.normal = glm::normalize(vertex.normal);
        vertex.uv = glm::mix(va.uv, vb.uv, t);
        vertex.color = glm::mix(va.color, vb.color, t);
        vertex.boneIndices = va.boneIndices;
        vertex.boneWeights = va.boneWeights;
        const uint32_t index = static_cast<uint32_t>(out.vertices.size());
        out.vertices.push_back(vertex);
        out.geometryToWorld.push_back(out.geometryToWorld[a]);
        return index;
    };

    auto appendCentroid = [&](uint32_t a, uint32_t b, uint32_t c) {
        const SkinnedVertex& va = out.vertices[a];
        const SkinnedVertex& vb = out.vertices[b];
        const SkinnedVertex& vc = out.vertices[c];
        SkinnedVertex vertex{};
        vertex.position = (va.position + vb.position + vc.position) / 3.0f;
        vertex.normal = va.normal + vb.normal + vc.normal;
        if (glm::dot(vertex.normal, vertex.normal) > 1e-8f)
            vertex.normal = glm::normalize(vertex.normal);
        vertex.uv = (va.uv + vb.uv + vc.uv) / 3.0f;
        vertex.color = (va.color + vb.color + vc.color) / 3.0f;
        vertex.boneIndices = va.boneIndices;
        vertex.boneWeights = va.boneWeights;
        const uint32_t index = static_cast<uint32_t>(out.vertices.size());
        out.vertices.push_back(vertex);
        out.geometryToWorld.push_back(out.geometryToWorld[a]);
        return index;
    };

    for (uint32_t ii = 0; ii + 2 < indexCount; ii += 3) {
        const uint32_t original[3] = {
            model.sourceIndices[indexOffset + ii + 0],
            model.sourceIndices[indexOffset + ii + 1],
            model.sourceIndices[indexOffset + ii + 2],
        };
        if (original[0] >= model.sourceVertices.size() ||
            original[1] >= model.sourceVertices.size() ||
            original[2] >= model.sourceVertices.size())
            continue;

        const int bone[3] = {
            DominantCanonicalRigidBone(model, model.sourceVertices[original[0]], maxBoneIndex),
            DominantCanonicalRigidBone(model, model.sourceVertices[original[1]], maxBoneIndex),
            DominantCanonicalRigidBone(model, model.sourceVertices[original[2]], maxBoneIndex),
        };
        const bool mixed = bone[0] != bone[1] || bone[1] != bone[2];
        if (mixed)
            ++out.mixedTriangleCount;
        const bool cleanArmCut = mixed &&
            (isArmBone(bone[0]) || isArmBone(bone[1]) || isArmBone(bone[2]));
        if (!cleanArmCut) {
            const int groupBone = dominantTriangleBone(original);
            auto& dst = out.triangles[groupBone];
            dst.insert(dst.end(), { original[0], original[1], original[2] });
            continue;
        }

        ++out.splitTriangleCount;
        const glm::vec3 sourceNormal = glm::cross(
            out.vertices[original[1]].position - out.vertices[original[0]].position,
            out.vertices[original[2]].position - out.vertices[original[0]].position);
        auto pushTriangle = [&](int group, uint32_t a, uint32_t b, uint32_t c) {
            const glm::vec3 normal = glm::cross(
                out.vertices[b].position - out.vertices[a].position,
                out.vertices[c].position - out.vertices[a].position);
            if (glm::dot(normal, sourceNormal) < 0.0f)
                std::swap(b, c);
            auto& dst = out.triangles[group];
            dst.insert(dst.end(), { a, b, c });
        };

        if (bone[0] == bone[1] || bone[1] == bone[2] || bone[2] == bone[0]) {
            int singleton = 0;
            if (bone[0] == bone[1]) singleton = 2;
            else if (bone[1] == bone[2]) singleton = 0;
            else singleton = 1;
            const int other0 = (singleton + 1) % 3;
            const int other1 = (singleton + 2) % 3;
            const uint32_t mid0 = appendInterpolated(
                original[singleton], original[other0], 0.5f);
            const uint32_t mid1 = appendInterpolated(
                original[singleton], original[other1], 0.5f);
            pushTriangle(bone[singleton], original[singleton], mid0, mid1);
            pushTriangle(bone[other0], original[other0], original[other1], mid1);
            pushTriangle(bone[other0], original[other0], mid1, mid0);
            continue;
        }

        const uint32_t center = appendCentroid(original[0], original[1], original[2]);
        for (int corner = 0; corner < 3; ++corner) {
            const int next = (corner + 1) % 3;
            const int previous = (corner + 2) % 3;
            const uint32_t midNext = appendInterpolated(
                original[corner], original[next], 0.5f);
            const uint32_t midPrevious = appendInterpolated(
                original[corner], original[previous], 0.5f);
            pushTriangle(bone[corner], original[corner], midNext, center);
            pushTriangle(bone[corner], original[corner], center, midPrevious);
        }
    }
    (void)seamFill;
    return out;
}

struct Ps1AnimatorClipSelection {
    std::shared_ptr<SkeletalModelAsset> clipModel;
    int stackIndex = -1;
    double duration = 0.0;
    float speed = 1.0f;
    std::string stateName;
    std::string clipName;
    std::string clipModelPath;
};

static const AnimatorStateDef* FindAnimatorStateForPs1(const AnimatorControllerAsset& controller,
                                                       const std::string& stateName) {
    if (!stateName.empty()) {
        for (const AnimatorStateDef& state : controller.states) {
            if (state.name == stateName)
                return &state;
        }
    }
    if (!controller.defaultState.empty()) {
        for (const AnimatorStateDef& state : controller.states) {
            if (state.name == controller.defaultState)
                return &state;
        }
    }
    return controller.states.empty() ? nullptr : &controller.states[0];
}

static Ps1AnimatorClipSelection ResolvePs1AnimatorClip(const std::string& meshModelPath,
                                                       const std::shared_ptr<SkeletalModelAsset>& meshModel,
                                                       const json* animator) {
    Ps1AnimatorClipSelection out;
    out.clipModel = meshModel;
    out.clipModelPath = meshModelPath;
    if (meshModel && !meshModel->animationStackIndices.empty()) {
        out.stackIndex = static_cast<int>(meshModel->animationStackIndices[0]);
        out.duration = meshModel->GetClipDurationByStackIndex(out.stackIndex);
    }

    if (!animator || !animator->value("enabled", true))
        return out;

    const std::string controllerPath = animator->value("controller", std::string{});
    if (controllerPath.empty())
        return out;

    std::string error;
    auto controller = AssetManager::Get().GetAnimatorController(controllerPath, error);
    if (!controller) {
        MIPSYNC_WARN("PS1 rigid bone export: animator controller '{}' unavailable: {}",
                     controllerPath, error);
        return out;
    }

    const AnimatorStateDef* state =
        FindAnimatorStateForPs1(*controller, animator->value("state", std::string{}));
    if (!state)
        return out;

    const std::string animatorModelPath = animator->value("model", std::string{});
    std::string clipModelPath = !state->clipSourceModelPath.empty()
        ? state->clipSourceModelPath
        : (!controller->sourceModelPath.empty()
            ? controller->sourceModelPath
            : (!animatorModelPath.empty() ? animatorModelPath : meshModelPath));

    auto clipModel = (clipModelPath == meshModelPath)
        ? meshModel
        : AssetManager::Get().GetSkeletalModel(clipModelPath);
    if (!clipModel) {
        MIPSYNC_WARN("PS1 rigid bone export: state '{}' clip model '{}' unavailable; using mesh model clip",
                     state->name, clipModelPath);
        return out;
    }

    const int stackIndex = clipModel->ResolveClipStackIndex(state->clipName, state->clipStackIndex);
    if (stackIndex < 0) {
        MIPSYNC_WARN("PS1 rigid bone export: state '{}' clip '{}' has no stack; using mesh model clip",
                     state->name, state->clipName);
        return out;
    }

    out.clipModel = clipModel;
    out.clipModelPath = clipModelPath;
    out.stackIndex = stackIndex;
    out.duration = clipModel->GetClipDurationByStackIndex(stackIndex);
    out.speed = std::isfinite(state->speed) && state->speed > 0.001f ? state->speed : 1.0f;
    out.stateName = state->name;
    out.clipName = state->clipName;
    MIPSYNC_INFO("PS1 rigid bone export: animator state '{}' clip '{}' model '{}' stack {}",
                 out.stateName, out.clipName, out.clipModelPath, out.stackIndex);
    return out;
}

static Ps1AnimatorClipSelection ResolvePs1AnimatorStateClip(
    const std::string& meshModelPath,
    const std::shared_ptr<SkeletalModelAsset>& meshModel,
    const json* animator,
    const AnimatorControllerAsset& controller,
    const AnimatorStateDef* state) {
    Ps1AnimatorClipSelection out;
    if (!state)
        return out;

    const std::string animatorModelPath =
        animator ? animator->value("model", std::string{}) : std::string{};
    const std::string clipModelPath = !state->clipSourceModelPath.empty()
        ? state->clipSourceModelPath
        : (!controller.sourceModelPath.empty()
            ? controller.sourceModelPath
            : (!animatorModelPath.empty() ? animatorModelPath : meshModelPath));
    auto clipModel = clipModelPath == meshModelPath
        ? meshModel
        : AssetManager::Get().GetSkeletalModel(clipModelPath);
    if (!clipModel)
        return out;

    const int stackIndex =
        clipModel->ResolveClipStackIndex(state->clipName, state->clipStackIndex);
    if (stackIndex < 0)
        return out;

    out.clipModel = clipModel;
    out.clipModelPath = clipModelPath;
    out.stackIndex = stackIndex;
    out.duration = clipModel->GetClipDurationByStackIndex(stackIndex);
    out.speed = std::isfinite(state->speed) && state->speed > 0.001f
        ? state->speed
        : 1.0f;
    out.stateName = state->name;
    out.clipName = state->clipName;
    return out;
}

struct Ps1AnimatorClipSet {
    Ps1AnimatorClipSelection idle;
    Ps1AnimatorClipSelection walk;
    Ps1AnimatorClipSelection aim;
};

static Ps1AnimatorClipSet ResolvePs1AnimatorClips(
    const std::string& meshModelPath,
    const std::shared_ptr<SkeletalModelAsset>& meshModel,
    const json* animator) {
    Ps1AnimatorClipSet clips;
    clips.idle = ResolvePs1AnimatorClip(meshModelPath, meshModel, animator);
    if (!animator || !animator->value("enabled", true))
        return clips;

    const std::string controllerPath = animator->value("controller", std::string{});
    if (controllerPath.empty())
        return clips;

    std::string error;
    auto controller = AssetManager::Get().GetAnimatorController(controllerPath, error);
    if (!controller)
        return clips;

    const AnimatorStateDef* walkState = nullptr;
    const AnimatorStateDef* aimState = nullptr;
    for (const AnimatorTransitionDef& transition : controller->transitions) {
        for (const AnimatorTransitionCondition& condition : transition.conditions) {
            const bool positiveCondition =
                condition.mode == AnimatorConditionMode::Greater ||
                condition.mode == AnimatorConditionMode::IfTrue ||
                (condition.mode == AnimatorConditionMode::Equals && condition.threshold > 0.5f);
            if (!positiveCondition)
                continue;
            if (!walkState && (condition.parameter == "Speed" || condition.parameter == "Moving")) {
                walkState = FindAnimatorStateForPs1(*controller, transition.toState);
            } else if (!aimState && (condition.parameter == "Aim" ||
                                     condition.parameter == "Aiming")) {
                aimState = FindAnimatorStateForPs1(*controller, transition.toState);
            }
        }
    }

    clips.walk = ResolvePs1AnimatorStateClip(
        meshModelPath, meshModel, animator, *controller, walkState);
    clips.aim = ResolvePs1AnimatorStateClip(
        meshModelPath, meshModel, animator, *controller, aimState);
    MIPSYNC_INFO("PS1 rigid animator clips: idle='{}', walk='{}', aim='{}'",
                 clips.idle.stateName,
                 clips.walk.stateName,
                 clips.aim.stateName);
    return clips;
}

static bool RegisterRigidBonePartsFromJson(const json& skinned, Ps1SceneExportResult& result,
                                           uint32_t& nextId,
                                           const Ps1ExportedEntity& baseEntity,
                                           const glm::vec3& rootPosition,
                                           const glm::vec3& rootRotation,
                                           const glm::vec3& rootScale,
                                           uint32_t controlRootEntityId,
                                           const json* animator) {
    const std::string modelPath = skinned.value("model", std::string{});
    if (modelPath.empty())
        return false;

    auto model = AssetManager::Get().GetSkeletalModel(modelPath);
    if (!model || model->sourceVertices.empty() || model->sourceIndices.empty() || model->bones.empty())
        return false;

    const int ps1ExportMode = std::clamp(skinned.value("ps1ExportMode", 0), 0, 2);
    const Ps1AnimatorClipSet clips = ResolvePs1AnimatorClips(modelPath, model, animator);
    const Ps1AnimatorClipSelection& clip = clips.idle;
    const int stackIndex = ps1ExportMode == 2 ? clip.stackIndex : -1;
    const double duration = stackIndex >= 0 ? clip.duration : 0.0;
    const int requestedRigidFps =
        skinned.value("ps1RigidAnimFps", skinned.value("ps1VertexAnimFps", 30));
    const int animFps = std::clamp(requestedRigidFps <= 12 ? 30 : requestedRigidFps, 12, 30);
    const int maxFrames = std::clamp(
        std::max(skinned.value("ps1VertexAnimMaxFrames", 96), 96),
        1,
        240);
    const int frameCount = (ps1ExportMode == 2 && duration > 0.0)
        ? std::clamp(static_cast<int>(std::ceil(duration * static_cast<double>(animFps))), 1, maxFrames)
        : 1;

    const int meshPartIndex = skinned.value("meshPart", -1);
    uint32_t indexOffset = 0;
    uint32_t indexCount = static_cast<uint32_t>(model->sourceIndices.size());
    if (meshPartIndex >= 0 && static_cast<size_t>(meshPartIndex) < model->meshParts.size()) {
        const auto& part = model->meshParts[static_cast<size_t>(meshPartIndex)];
        indexOffset = part.indexOffset;
        indexCount = part.indexCount;
    }
    if (indexOffset >= model->sourceIndices.size())
        return false;
    indexCount = std::min<uint32_t>(indexCount, static_cast<uint32_t>(model->sourceIndices.size() - indexOffset));
    if (indexCount < 3)
        return false;

    const int maxBoneIndex = static_cast<int>(std::min<size_t>(model->bones.size(), kMaxBones)) - 1;
    if (maxBoneIndex < 0)
        return false;

    RigidSplitGeometry splitGeometry =
        BuildRigidSplitGeometry(*model, indexOffset, indexCount, maxBoneIndex,
                                baseEntity.seamFill);
    std::unordered_map<int, std::vector<uint32_t>> initialTriangles =
        std::move(splitGeometry.triangles);
    MIPSYNC_INFO(
        "PS1 rigid partition: {} mixed-weight triangles, {} clean arm cuts (Seam FX {})",
        splitGeometry.mixedTriangleCount, splitGeometry.splitTriangleCount,
        baseEntity.seamFill ? "ON" : "OFF");

    struct TempBoneCount {
        int bone = 0;
        size_t triCount = 0;
    };
    std::vector<TempBoneCount> activeBones;
    for (auto& [b, tris] : initialTriangles) {
        if (!tris.empty()) {
            activeBones.push_back({ b, tris.size() / 3 });
        }
    }
    std::sort(activeBones.begin(), activeBones.end(), [](const TempBoneCount& a, const TempBoneCount& b) {
        return a.triCount > b.triCount;
    });

    // A standard Mixamo body needs enough rigid parts to retain the major
    // spine, upper/lower arm, hand, upper/lower leg and foot bones. Shoulder
    // helper weights are folded into UpperArm before this budget is applied;
    // fingers and toes still collapse into their nearest retained parent.
    const size_t maxRigidPartsPerSkinnedMesh =
        static_cast<size_t>(std::clamp(skinned.value("ps1RigidBoneMaxParts", 20), 1, 32));
    std::unordered_set<int> keptBones;
    auto lowerBoneName = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return text;
    };
    auto contains = [](const std::string& text, const char* needle) {
        return text.find(needle) != std::string::npos;
    };
    auto rigidBonePriority = [&](int bone) {
        if (bone < 0 || static_cast<size_t>(bone) >= model->bones.size())
            return 0;
        const std::string name = lowerBoneName(model->bones[static_cast<size_t>(bone)].name);
        if (contains(name, "thumb") || contains(name, "index") || contains(name, "middle") ||
            contains(name, "ring") || contains(name, "pinky"))
            return 0;
        if (contains(name, "leftarm") || contains(name, "rightarm"))
            return 9000;
        if (contains(name, "forearm") || contains(name, "lowerarm"))
            return 8900;
        if (contains(name, "lefthand") || contains(name, "righthand") ||
            contains(name, "wrist"))
            return 8800;
        if (contains(name, "hips") || contains(name, "pelvis"))
            return 8600;
        if (contains(name, "spine"))
            return 8500;
        if (contains(name, "neck") || contains(name, "head"))
            return 8400;
        if (contains(name, "upleg") || contains(name, "thigh"))
            return 8200;
        if (contains(name, "leftleg") || contains(name, "rightleg") ||
            contains(name, "calf") || contains(name, "shin"))
            return 8100;
        if (contains(name, "foot"))
            return 8000;
        if (contains(name, "shoulder"))
            return 7600;
        return 0;
    };
    std::vector<TempBoneCount> selectedBones = activeBones;
    std::sort(selectedBones.begin(), selectedBones.end(), [&](const TempBoneCount& a, const TempBoneCount& b) {
        const int pa = rigidBonePriority(a.bone);
        const int pb = rigidBonePriority(b.bone);
        if (pa != pb)
            return pa > pb;
        return a.triCount > b.triCount;
    });
    const size_t numKept = std::min<size_t>(selectedBones.size(), maxRigidPartsPerSkinnedMesh);
    for (size_t i = 0; i < numKept; ++i) {
        keptBones.insert(selectedBones[i].bone);
    }

    // Redirect discarded bone triangles recursively up the parent hierarchy to their closest kept ancestor
    std::unordered_map<int, int> boneRedirect;
    for (int b = 0; b <= maxBoneIndex; ++b) {
        if (keptBones.count(b)) {
            boneRedirect[b] = b;
        } else {
            int curr = b;
            int redirect = -1;
            while (curr >= 0) {
                int parent = GetParentBoneIndex(*model, curr);
                if (parent < 0)
                    break;
                if (keptBones.count(parent)) {
                    redirect = parent;
                    break;
                }
                curr = parent;
            }
            if (redirect < 0) {
                redirect = activeBones.empty() ? 0 : activeBones[0].bone;
            }
            boneRedirect[b] = redirect;
        }
    }

    struct BoneGroup {
        int bone = 0;
        uint32_t triCount = 0;
        uint16_t targetTris = 64;
        uint16_t targetVerts = 192;
        std::vector<uint32_t> indices;
    };
    std::unordered_map<int, BoneGroup> groups;
    for (auto& [b, tris] : initialTriangles) {
        if (tris.empty())
            continue;
        int redirect = boneRedirect[b];
        BoneGroup& group = groups[redirect];
        group.bone = redirect;
        group.triCount += static_cast<uint32_t>(tris.size() / 3);
        group.indices.insert(group.indices.end(), tris.begin(), tris.end());
    }

    std::vector<BoneGroup> ordered;
    ordered.reserve(groups.size());
    for (auto& [_, group] : groups) {
        if (group.triCount >= 1)
            ordered.push_back(std::move(group));
    }
    std::sort(ordered.begin(), ordered.end(), [](const BoneGroup& a, const BoneGroup& b) {
        return a.triCount > b.triCount;
    });

    if (ordered.empty())
        return false;

    const size_t configuredTargetTris =
        static_cast<size_t>(std::clamp(skinned.value("ps1VertexAnimTargetTris", 800), 32, 2000));
    const size_t totalSourceTris = std::accumulate(
        ordered.begin(), ordered.end(), size_t{0},
        [](size_t sum, const BoneGroup& group) { return sum + static_cast<size_t>(group.triCount); });
    const size_t originalSourceTris = indexCount / 3u;
    // Clean cuts are limited to moving limbs, so a low-poly source stays well
    // below the former all-bones split. Preserve that topology; simplifying
    // separate rigid parts opens holes at UV islands and joint boundaries.
    const bool preserveLowPolyRigidMesh =
        totalSourceTris <= 1000u ||
        (originalSourceTris <= 1200u && totalSourceTris <= 1600u);
    const size_t performanceBudget = std::clamp<size_t>(
        std::min<size_t>(configuredTargetTris, std::max<size_t>(originalSourceTris, 480u)),
        480u, 1000u);
    const size_t rigidTotalTargetTris = preserveLowPolyRigidMesh
        ? totalSourceTris
        : performanceBudget;
    size_t assignedTargetTris = 0;
    for (BoneGroup& group : ordered) {
        size_t targetTris = group.triCount;
        if (!preserveLowPolyRigidMesh) {
            const double weight = totalSourceTris > 0
                ? static_cast<double>(group.triCount) / static_cast<double>(totalSourceTris)
                : 1.0 / static_cast<double>(ordered.size());
            const size_t weightedTarget = static_cast<size_t>(
                std::lround(static_cast<double>(rigidTotalTargetTris) * weight));
            targetTris = std::min<size_t>(
                group.triCount,
                std::clamp<size_t>(weightedTarget, 8, 320));
        }
        group.targetTris = static_cast<uint16_t>(targetTris);
        group.targetVerts = static_cast<uint16_t>(std::clamp<size_t>(targetTris * 3, 36, 2048));
        assignedTargetTris += targetTris;
    }
    MIPSYNC_INFO("PS1 rigid bone budget: {} generated tris ({} original) -> {} target tris across {} parts{}",
                 totalSourceTris, originalSourceTris, assignedTargetTris, ordered.size(),
                 preserveLowPolyRigidMesh ? " (preserve low-poly)" : "");

    size_t generated = 0;
    for (const BoneGroup& group : ordered) {
        const glm::mat4 geometryToWorld =
            group.indices[0] < splitGeometry.geometryToWorld.size()
            ? splitGeometry.geometryToWorld[group.indices[0]]
            : model->meshGeometryToWorld;

        glm::mat4 firstBones[kMaxBones];
        if (stackIndex >= 0 && duration > 0.0 && clip.clipModel)
            EvaluateRetargetedBoneMatrices(
                *model, *clip.clipModel, stackIndex, 0.0, firstBones);
        else
            model->EvaluateBoneMatrices({}, 0.0, firstBones);
        const glm::mat4 firstBoneDisplay =
            DisplayBoneMatrixForPs1(*model, group.bone, geometryToWorld, firstBones);
        const glm::vec3 axisScale = ExtractAxisScale(firstBoneDisplay);

        struct RigidLocalVertex {
            uint32_t sourceIndex = 0;
            glm::vec3 position{ 0.0f };
            glm::vec3 normal{ 0.0f, 1.0f, 0.0f };
        };
        std::vector<RigidLocalVertex> localVertices;
        localVertices.reserve(group.indices.size());

        std::unordered_map<uint32_t, uint32_t> remap;
        remap.reserve(group.indices.size());
        Ps1RigidBoneMeshData mesh;
        mesh.key = "rigidbone://" + std::to_string(result.rigidBoneMeshes.size());

        for (uint32_t oldIndex : group.indices) {
            auto remapIt = remap.find(oldIndex);
            if (remapIt != remap.end()) {
                mesh.indices.push_back(remapIt->second);
                continue;
            }
            const SkinnedVertex& sv = splitGeometry.vertices[oldIndex];
            const uint32_t newIndex = static_cast<uint32_t>(mesh.positions.size() / 3);
            remap[oldIndex] = newIndex;
            mesh.indices.push_back(newIndex);
            const glm::mat4 geometryToBone =
                static_cast<size_t>(group.bone) < model->bones.size()
                ? model->bones[static_cast<size_t>(group.bone)].bindGeometryToBone
                : glm::mat4(1.0f);
            const glm::vec3 localPos =
                glm::vec3(geometryToBone * glm::vec4(sv.position, 1.0f));
            glm::vec3 localNormal = glm::mat3(glm::transpose(glm::inverse(geometryToBone))) * sv.normal;
            if (glm::dot(localNormal, localNormal) > 1e-8f)
                localNormal = glm::normalize(localNormal);
            else
                localNormal = glm::vec3(0.0f, 1.0f, 0.0f);
            localVertices.push_back(RigidLocalVertex{ oldIndex, localPos, localNormal });
            mesh.positions.insert(mesh.positions.end(), { 0.0f, 0.0f, 0.0f });
            mesh.normals.insert(mesh.normals.end(), { localNormal.x, localNormal.y, localNormal.z });
            mesh.uvs.insert(mesh.uvs.end(), { sv.uv.x, sv.uv.y });
            mesh.colors.push_back(ColorToPackedFromVec4(sv.color));
        }
        if (mesh.positions.empty() || mesh.indices.size() < 3)
            continue;

        glm::vec3 localMin(0.0f);
        glm::vec3 localMax(0.0f);
        bool firstLocal = true;
        for (const RigidLocalVertex& lv : localVertices) {
            if (firstLocal) {
                localMin = localMax = lv.position;
                firstLocal = false;
            } else {
                localMin = glm::min(localMin, lv.position);
                localMax = glm::max(localMax, lv.position);
            }
        }
        const glm::vec3 localPivot = (localMin + localMax) * 0.5f;
        const float seamScale = baseEntity.seamFill ? 1.025f : 1.0f;
        for (size_t vi = 0; vi < localVertices.size(); ++vi) {
            const glm::vec3 p =
                (localVertices[vi].position - localPivot) * axisScale *
                model->displayScale * seamScale;
            mesh.positions[vi * 3 + 0] = p.x;
            mesh.positions[vi * 3 + 1] = p.y;
            mesh.positions[vi * 3 + 2] = p.z;
        }

        Ps1ExportedEntity part = baseEntity;
        const std::string boneName =
            static_cast<size_t>(group.bone) < model->bones.size()
            ? model->bones[static_cast<size_t>(group.bone)].name
            : std::string("Bone");
        part.id = nextId++;
        part.name = baseEntity.name + "_" + boneName;
        part.meshEnabled = true;
        part.meshKind = 4;
        part.meshPath = mesh.key;
        part.meshIndex = 0;
        part.vertexAnimFirstMeshIndex = 0;
        part.vertexAnimFrameCount = 0;
        part.vertexAnimFps = 0;
        part.vertexAnimTargetTris = group.targetTris;
        part.vertexAnimTargetVerts = group.targetVerts;
        part.rigidRootEntityId =
            controlRootEntityId != 0 ? controlRootEntityId : baseEntity.id;

        glm::mat4 bones[kMaxBones];
        auto appendClipFrames = [&](const Ps1AnimatorClipSelection& selected,
                                    uint16_t& firstOut,
                                    uint16_t& countOut,
                                    uint8_t& fpsOut) {
            if (ps1ExportMode != 2 || selected.stackIndex < 0 ||
                selected.duration <= 0.0 || !selected.clipModel) {
                firstOut = 0;
                countOut = 0;
                fpsOut = 0;
                return;
            }
            const int selectedFrameCount = std::clamp(
                static_cast<int>(std::ceil(
                    selected.duration * static_cast<double>(animFps))),
                1,
                maxFrames);
            if (result.rigidAnimFrames.size() >= 65535u) {
                firstOut = 0;
                countOut = 0;
                fpsOut = 0;
                return;
            }
            firstOut = static_cast<uint16_t>(result.rigidAnimFrames.size());
            countOut = static_cast<uint16_t>(std::min<int>(
                selectedFrameCount,
                65535 - static_cast<int>(result.rigidAnimFrames.size())));
            fpsOut = static_cast<uint8_t>(animFps);
            for (int frame = 0; frame < static_cast<int>(countOut); ++frame) {
                const double time = countOut > 1
                    ? selected.duration * static_cast<double>(frame) /
                        static_cast<double>(countOut)
                    : 0.0;
                EvaluateRetargetedBoneMatrices(
                    *model, *selected.clipModel, selected.stackIndex,
                    std::fmod(
                        time * static_cast<double>(selected.speed),
                        selected.duration),
                    bones);
                const glm::mat4 boneDisplay =
                    DisplayBoneMatrixForPs1(*model, group.bone, geometryToWorld, bones);
                result.rigidAnimFrames.push_back(
                    RigidAnimFrameFromMatrix(
                        boneDisplay, rootPosition, rootRotation, rootScale,
                        localPivot, axisScale, model->displayScale));
            }
        };

        appendClipFrames(
            clips.idle,
            part.rigidIdleFirstFrame,
            part.rigidIdleFrameCount,
            part.rigidIdleFps);
        appendClipFrames(
            clips.walk,
            part.rigidWalkFirstFrame,
            part.rigidWalkFrameCount,
            part.rigidWalkFps);
        appendClipFrames(
            clips.aim,
            part.rigidAimFirstFrame,
            part.rigidAimFrameCount,
            part.rigidAimFps);

        if (part.rigidIdleFrameCount == 0) {
            part.rigidIdleFirstFrame =
                static_cast<uint16_t>(std::min<size_t>(result.rigidAnimFrames.size(), 65535));
            part.rigidIdleFrameCount = static_cast<uint16_t>(frameCount);
            part.rigidIdleFps = static_cast<uint8_t>(animFps);
            for (int frame = 0; frame < frameCount; ++frame) {
                const double time = (frameCount > 1 && duration > 0.0)
                    ? (duration * static_cast<double>(frame) /
                        static_cast<double>(frameCount))
                    : 0.0;
                if (stackIndex >= 0 && duration > 0.0 && clip.clipModel) {
                    EvaluateRetargetedBoneMatrices(
                        *model, *clip.clipModel, stackIndex,
                        std::fmod(time * static_cast<double>(clip.speed), duration),
                        bones);
                } else {
                    model->EvaluateBoneMatrices({}, 0.0, bones);
                }
                const glm::mat4 boneDisplay =
                    DisplayBoneMatrixForPs1(*model, group.bone, geometryToWorld, bones);
                result.rigidAnimFrames.push_back(
                    RigidAnimFrameFromMatrix(
                        boneDisplay, rootPosition, rootRotation, rootScale,
                        localPivot, axisScale, model->displayScale));
            }
        }
        part.rigidAnimFirstFrame = part.rigidIdleFirstFrame;
        part.rigidAnimFrameCount = part.rigidIdleFrameCount;
        part.rigidAnimFps = part.rigidIdleFps;

        const Ps1RigidAnimFrame& first = result.rigidAnimFrames[part.rigidAnimFirstFrame];
        part.position[0] = first.position[0];
        part.position[1] = first.position[1];
        part.position[2] = first.position[2];
        part.rotation[0] = 0.0f;
        part.rotation[1] = 0.0f;
        part.rotation[2] = 0.0f;
        part.scale[0] = 1.0f;
        part.scale[1] = 1.0f;
        part.scale[2] = 1.0f;

        result.rigidBoneMeshes.push_back(std::move(mesh));
        result.entities.push_back(std::move(part));
        ++generated;
    }

    if (generated > 0) {
        MIPSYNC_INFO("PS1 rigid bone export: {} -> {} rigid parts, {} transform keys @ {}fps{}{}",
                     modelPath, generated, generated * static_cast<size_t>(frameCount), animFps,
                     clip.stateName.empty() ? "" : " from state ",
                     clip.stateName.empty() ? "" : clip.stateName);
    }
    return generated > 0;
}

static void ApplySkinnedPartMaterialFromModel(const json& skinned, Ps1ExportedEntity& entity) {
    const std::string modelPath = skinned.value("model", std::string{});
    if (modelPath.empty())
        return;
    const int meshPartIndex = skinned.value("meshPart", -1);
    auto model = AssetManager::Get().GetSkeletalModel(modelPath);
    if (!model || model->materials.empty())
        return;

    uint32_t materialIndex = 0;
    bool foundMaterial = false;
    if (meshPartIndex >= 0 && static_cast<size_t>(meshPartIndex) < model->meshParts.size()) {
        const SkeletalModelMeshPart& part = model->meshParts[static_cast<size_t>(meshPartIndex)];
        auto lower = [](std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return text;
        };
        std::string partKey = lower(part.name);
        if (!partKey.empty() && partKey.back() == 's')
            partKey.pop_back();
        for (size_t mi = 0; mi < model->materials.size(); ++mi) {
            const std::string materialName = lower(model->materials[mi].name);
            if (!partKey.empty() && materialName.find(partKey) != std::string::npos) {
                materialIndex = static_cast<uint32_t>(mi);
                foundMaterial = true;
                break;
            }
        }
        if (!foundMaterial) {
            uint32_t bestOverlap = 0;
            for (const SkeletalModelSubmesh& sub : model->submeshes) {
                const uint32_t subBegin = sub.indexOffset;
                const uint32_t subEnd = sub.indexOffset + sub.indexCount;
                const uint32_t partBegin = part.indexOffset;
                const uint32_t partEnd = part.indexOffset + part.indexCount;
                const uint32_t overlapBegin = std::max(subBegin, partBegin);
                const uint32_t overlapEnd = std::min(subEnd, partEnd);
                if (overlapEnd <= overlapBegin || sub.materialIndex >= model->materials.size())
                    continue;
                const uint32_t overlap = overlapEnd - overlapBegin;
                if (overlap > bestOverlap) {
                    bestOverlap = overlap;
                    materialIndex = sub.materialIndex;
                    foundMaterial = true;
                }
            }
        }
        if (!foundMaterial)
            materialIndex = part.defaultMaterialIndex;
    }
    if (materialIndex >= model->materials.size())
        materialIndex = 0;

    const SkeletalModelMaterial& material = model->materials[materialIndex];
    if (entity.texturePath.empty() && !material.texturePath.empty())
        entity.texturePath = material.texturePath;
    if (entity.materialPath.empty()) {
        entity.color[0] = ToByte01(material.color.r);
        entity.color[1] = ToByte01(material.color.g);
        entity.color[2] = ToByte01(material.color.b);
        entity.textureTiling[0] = material.textureTiling.x;
        entity.textureTiling[1] = material.textureTiling.y;
        entity.textureOffset[0] = material.textureOffset.x;
        entity.textureOffset[1] = material.textureOffset.y;
    }
}

static bool IsTerrainMeshKey(const std::string& key) {
    return key.rfind("terrain://", 0) == 0 || key.rfind("terraindata://", 0) == 0;
}

static bool IsProModelerMeshKey(const std::string& key) {
    return key.rfind("promodelerdata://", 0) == 0 ||
           key.rfind("probuilderdata://", 0) == 0 ||
           key.rfind("primitivesphere://", 0) == 0;
}

static bool IsSkinnedAnimMeshKey(const std::string& key) {
    return key.rfind("skinnedanim://", 0) == 0;
}

static bool IsRigidBoneMeshKey(const std::string& key) {
    return key.rfind("rigidbone://", 0) == 0;
}

static uint8_t RigidLightDirectionBin(glm::vec3 normal) {
    const float length2 = glm::dot(normal, normal);
    int x;
    int y;
    int z;
    if (length2 > 1e-8f)
        normal /= std::sqrt(length2);
    else
        normal = glm::vec3(0.0f, 1.0f, 0.0f);

    const auto quantize = [](float value) {
        if (value > 0.35f) return 1;
        if (value < -0.35f) return -1;
        return 0;
    };
    x = quantize(normal.x);
    y = quantize(normal.y);
    z = quantize(normal.z);
    if (x == 0 && y == 0 && z == 0) {
        const glm::vec3 absolute = glm::abs(normal);
        if (absolute.x >= absolute.y && absolute.x >= absolute.z)
            x = normal.x >= 0.0f ? 1 : -1;
        else if (absolute.y >= absolute.z)
            y = normal.y >= 0.0f ? 1 : -1;
        else
            z = normal.z >= 0.0f ? 1 : -1;
    }
    return static_cast<uint8_t>((x + 1) * 9 + (y + 1) * 3 + (z + 1));
}

static bool SameSkinnedAnimSequence(const std::string& a, const std::string& b) {
    if (!IsSkinnedAnimMeshKey(a) || !IsSkinnedAnimMeshKey(b))
        return false;
    const size_t ah = a.find('#');
    const size_t bh = b.find('#');
    return a.substr(0, ah) == b.substr(0, bh);
}

static void FindSkinnedAnimBudget(const Ps1SceneExportResult& data, const std::string& meshPath,
                                  size_t& outMaxTris, size_t& outMaxVerts) {
    outMaxTris = 320;
    outMaxVerts = 1000;
    for (const Ps1ExportedEntity& entity : data.entities) {
        if (entity.meshKind != 4 || !SameSkinnedAnimSequence(entity.meshPath, meshPath))
            continue;
        outMaxTris = std::clamp<size_t>(entity.vertexAnimTargetTris, 32, 2000);
        outMaxVerts = std::clamp<size_t>(entity.vertexAnimTargetVerts, 64, 2600);
        return;
    }
}

static void FindRigidBoneBudget(const Ps1SceneExportResult& data, const std::string& meshPath,
                                size_t& outMaxTris, size_t& outMaxVerts) {
    outMaxTris = 64;
    outMaxVerts = 220;
    for (const Ps1ExportedEntity& entity : data.entities) {
        if (entity.meshKind != 4 || entity.meshPath != meshPath)
            continue;
        outMaxTris = std::clamp<size_t>(entity.vertexAnimTargetTris, 8, 320);
        outMaxVerts = std::clamp<size_t>(entity.vertexAnimTargetVerts, 24, 2048);
        return;
    }
}

static Mesh MeshFromProModelerKey(const std::string& key, const Ps1SceneExportResult& data) {
    for (const Ps1ProModelerMeshData& pb : data.proModelerMeshes) {
        if (pb.key != key)
            continue;
        std::vector<Vertex> vertices;
        const size_t vertexCount = pb.positions.size() / 3;
        vertices.reserve(vertexCount);
        for (size_t i = 0; i < vertexCount; ++i) {
            Vertex v{};
            v.position = {
                pb.positions[i * 3 + 0],
                pb.positions[i * 3 + 1],
                pb.positions[i * 3 + 2],
            };
            if (pb.normals.size() >= (i + 1) * 3) {
                v.normal = {
                    pb.normals[i * 3 + 0],
                    pb.normals[i * 3 + 1],
                    pb.normals[i * 3 + 2],
                };
            } else {
                v.normal = { 0.0f, 1.0f, 0.0f };
            }
            if (pb.uvs.size() >= (i + 1) * 2)
                v.uv = { pb.uvs[i * 2 + 0], pb.uvs[i * 2 + 1] };
            else
                v.uv = { 0.0f, 0.0f };
            if (pb.colors.size() > i)
                v.color = TerrainPackedColorToVec4(pb.colors[i]);
            else
                v.color = glm::vec4(1.0f);
            vertices.push_back(v);
        }
        return Mesh(vertices, pb.indices, false, true);
    }
    return Mesh::CreateCube(1.0f);
}

static Mesh MeshFromSkinnedAnimKey(const std::string& key, const Ps1SceneExportResult& data) {
    for (const Ps1SkinnedAnimMeshData& anim : data.skinnedAnimMeshes) {
        if (anim.key != key)
            continue;
        std::vector<Vertex> vertices;
        const size_t vertexCount = anim.positions.size() / 3;
        vertices.reserve(vertexCount);
        for (size_t i = 0; i < vertexCount; ++i) {
            Vertex v{};
            v.position = {
                anim.positions[i * 3 + 0],
                anim.positions[i * 3 + 1],
                anim.positions[i * 3 + 2],
            };
            if (anim.normals.size() >= (i + 1) * 3) {
                v.normal = {
                    anim.normals[i * 3 + 0],
                    anim.normals[i * 3 + 1],
                    anim.normals[i * 3 + 2],
                };
            } else {
                v.normal = { 0.0f, 1.0f, 0.0f };
            }
            if (anim.uvs.size() >= (i + 1) * 2)
                v.uv = { anim.uvs[i * 2 + 0], anim.uvs[i * 2 + 1] };
            else
                v.uv = { 0.0f, 0.0f };
            if (anim.colors.size() > i)
                v.color = TerrainPackedColorToVec4(anim.colors[i]);
            else
                v.color = glm::vec4(1.0f);
            vertices.push_back(v);
        }
        return Mesh(vertices, anim.indices, false, true);
    }
    return Mesh::CreateCube(1.0f);
}

static Mesh MeshFromRigidBoneKey(const std::string& key, const Ps1SceneExportResult& data) {
    for (const Ps1RigidBoneMeshData& rigid : data.rigidBoneMeshes) {
        if (rigid.key != key)
            continue;
        std::vector<Vertex> vertices;
        const size_t vertexCount = rigid.positions.size() / 3;
        vertices.reserve(vertexCount);
        for (size_t i = 0; i < vertexCount; ++i) {
            Vertex v{};
            v.position = {
                rigid.positions[i * 3 + 0],
                rigid.positions[i * 3 + 1],
                rigid.positions[i * 3 + 2],
            };
            if (rigid.normals.size() >= (i + 1) * 3) {
                v.normal = {
                    rigid.normals[i * 3 + 0],
                    rigid.normals[i * 3 + 1],
                    rigid.normals[i * 3 + 2],
                };
            } else {
                v.normal = { 0.0f, 1.0f, 0.0f };
            }
            if (rigid.uvs.size() >= (i + 1) * 2)
                v.uv = { rigid.uvs[i * 2 + 0], rigid.uvs[i * 2 + 1] };
            else
                v.uv = { 0.0f, 0.0f };
            if (rigid.colors.size() > i)
                v.color = TerrainPackedColorToVec4(rigid.colors[i]);
            else
                v.color = glm::vec4(1.0f);
            vertices.push_back(v);
        }
        return Mesh(vertices, rigid.indices, false, true);
    }
    return Mesh::CreateCube(1.0f);
}

static Mesh MeshFromTerrainKey(const std::string& key, const Ps1SceneExportResult& data) {
    if (key.rfind("terraindata://", 0) == 0) {
        for (const Ps1TerrainMeshData& terrain : data.terrainMeshes) {
            if (terrain.key != key)
                continue;
            std::vector<glm::vec4> colors;
            colors.reserve(terrain.colors.size());
            for (uint32_t color : terrain.colors)
                colors.push_back(TerrainPackedColorToVec4(color));
            if (!terrain.heights.empty())
                return Mesh::CreateTerrainFromData(
                    terrain.size, terrain.subdivisions, terrain.heights, colors, true);
            return Mesh::CreateTerrain(terrain.size, terrain.subdivisions,
                                       terrain.heightScale, terrain.noiseScale,
                                       terrain.seed, terrain.flat, true);
        }
    }

    float size = 32.0f;
    int subdivisions = 32;
    float heightScale = 2.0f;
    float noiseScale = 0.18f;
    int seed = 1337;
    int flat = 0;
    std::sscanf(key.c_str(), "terrain://%f|%d|%f|%f|%d|%d",
                &size, &subdivisions, &heightScale, &noiseScale, &seed, &flat);
    subdivisions = std::clamp(subdivisions, 1, 24);
    return Mesh::CreateTerrain(size, subdivisions, heightScale, noiseScale, seed, flat != 0, true);
}

static uint8_t TerrainTriShade(const glm::vec3& normal, const glm::vec3& center,
                               float minY, float maxY) {
    glm::vec3 n = normal;
    const float len = glm::length(n);
    if (len > 1e-6f) n /= len;
    else n = { 0.0f, 1.0f, 0.0f };
    if (n.y < 0.0f)
        n = -n;

    const glm::vec3 lightDir = glm::normalize(glm::vec3(-0.58f, 0.58f, -0.58f));
    const float diffuse = std::max(0.0f, glm::dot(n, lightDir));
    const float slope = 1.0f - std::clamp(n.y, 0.0f, 1.0f);
    const float slopeContrast = slope * slope * 135.0f;
    float heightContrast = 0.0f;
    if (maxY > minY + 1e-5f) {
        const float ht = std::clamp((center.y - minY) / (maxY - minY), 0.0f, 1.0f);
        heightContrast = (ht - 0.5f) * 52.0f;
    }

    const int shade = static_cast<int>(std::lround(38.0f + diffuse * 88.0f + slopeContrast + heightContrast));
    return static_cast<uint8_t>(std::clamp(shade, 34, 190));
}

void ExtractColor(const json& mr, uint8_t out[3]) {
    if (mr.contains("color") && mr["color"].is_array() && mr["color"].size() >= 3) {
        auto clampByte = [](float v) {
            if (v <= 0.0f) return static_cast<uint8_t>(0);
            if (v >= 1.0f) return static_cast<uint8_t>(255);
            return static_cast<uint8_t>(v * 255.0f);
        };
        out[0] = clampByte(mr["color"][0].get<float>());
        out[1] = clampByte(mr["color"][1].get<float>());
        out[2] = clampByte(mr["color"][2].get<float>());
    }
}

struct Ps1ResolvedTransform {
    glm::vec3 position{ 0.0f };
    glm::vec3 rotation{ 0.0f };
    glm::vec3 scale{ 1.0f };
    glm::mat4 matrix{ 1.0f };
};

struct Ps1LocalTransform {
    uint32_t parent = 0;
    Ps1ResolvedTransform transform;
};

struct Ps1ResolvedAnimator {
    bool has = false;
    json animator;
};

struct Ps1LocalAnimator {
    uint32_t parent = 0;
    bool has = false;
    json animator;
};

static Ps1ResolvedTransform ReadEntityLocalTransform(const json& ent) {
    Ps1ResolvedTransform transform;
    if (ent.contains("transform")) {
        float values[3];
        if (ReadVec3(ent["transform"].value("position", json::array({ 0, 0, 0 })), values))
            transform.position = { values[0], values[1], values[2] };
        if (ReadVec3(ent["transform"].value("rotation", json::array({ 0, 0, 0 })), values))
            transform.rotation = { values[0], values[1], values[2] };
        if (ReadVec3(ent["transform"].value("scale", json::array({ 1, 1, 1 })), values))
            transform.scale = { values[0], values[1], values[2] };
    }
    return transform;
}

static glm::mat4 Ps1TransformMatrix(const Ps1ResolvedTransform& transform) {
    const glm::vec3 radians = glm::radians(transform.rotation);
    const glm::mat4 rotation =
        glm::rotate(glm::mat4(1.0f), radians.y, glm::vec3(0.0f, 1.0f, 0.0f)) *
        glm::rotate(glm::mat4(1.0f), radians.x, glm::vec3(1.0f, 0.0f, 0.0f)) *
        glm::rotate(glm::mat4(1.0f), radians.z, glm::vec3(0.0f, 0.0f, 1.0f));
    return glm::translate(glm::mat4(1.0f), transform.position) *
           rotation *
           glm::scale(glm::mat4(1.0f), transform.scale);
}

static Ps1ResolvedTransform DecomposePs1Transform(const glm::mat4& matrix) {
    Ps1ResolvedTransform transform;
    glm::vec3 skew;
    glm::vec4 perspective;
    glm::quat orientation;
    if (!glm::decompose(matrix, transform.scale, orientation, transform.position,
                        skew, perspective)) {
        transform.matrix = matrix;
        transform.position = glm::vec3(matrix[3]);
        return transform;
    }

    orientation = glm::normalize(orientation);
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    glm::extractEulerAngleYXZ(glm::mat4_cast(orientation), yaw, pitch, roll);
    transform.rotation = glm::degrees(glm::vec3(pitch, yaw, roll));
    transform.matrix = matrix;
    return transform;
}

static void BuildFlatWorldTransforms(const json& entities,
                                     std::unordered_map<uint32_t, Ps1ResolvedTransform>& outTransforms) {
    std::unordered_map<uint32_t, Ps1LocalTransform> locals;
    locals.reserve(entities.size());
    for (const json& ent : entities) {
        const uint32_t id = ent.value("id", 0u);
        if (id == 0)
            continue;
        Ps1LocalTransform local;
        local.parent = ent.value("parent", 0u);
        local.transform = ReadEntityLocalTransform(ent);
        local.transform.matrix = Ps1TransformMatrix(local.transform);
        locals[id] = local;
    }

    std::unordered_set<uint32_t> resolving;
    std::function<Ps1ResolvedTransform(uint32_t)> resolve = [&](uint32_t id) -> Ps1ResolvedTransform {
        auto cached = outTransforms.find(id);
        if (cached != outTransforms.end())
            return cached->second;
        auto localIt = locals.find(id);
        if (localIt == locals.end() || !resolving.insert(id).second)
            return {};

        const Ps1LocalTransform& local = localIt->second;
        const glm::mat4 parentMatrix =
            local.parent != 0 ? resolve(local.parent).matrix : glm::mat4(1.0f);
        resolving.erase(id);

        Ps1ResolvedTransform world =
            DecomposePs1Transform(parentMatrix * local.transform.matrix);
        outTransforms[id] = world;
        return world;
    };

    for (const auto& [id, local] : locals) {
        (void)local;
        resolve(id);
    }
}

static void BuildFlatAnimatorContexts(const json& entities,
                                      std::unordered_map<uint32_t, Ps1ResolvedAnimator>& outAnimators) {
    std::unordered_map<uint32_t, Ps1LocalAnimator> locals;
    locals.reserve(entities.size());
    for (const json& ent : entities) {
        const uint32_t id = ent.value("id", 0u);
        if (id == 0)
            continue;
        Ps1LocalAnimator local;
        local.parent = ent.value("parent", 0u);
        if (ent.contains("animator") && ent["animator"].is_object()) {
            local.has = true;
            local.animator = ent["animator"];
        }
        locals[id] = std::move(local);
    }

    std::unordered_set<uint32_t> resolving;
    std::function<Ps1ResolvedAnimator(uint32_t)> resolve = [&](uint32_t id) -> Ps1ResolvedAnimator {
        auto cached = outAnimators.find(id);
        if (cached != outAnimators.end())
            return cached->second;
        auto localIt = locals.find(id);
        if (localIt == locals.end() || !resolving.insert(id).second)
            return {};

        const Ps1LocalAnimator& local = localIt->second;
        Ps1ResolvedAnimator inherited =
            local.parent != 0 ? resolve(local.parent) : Ps1ResolvedAnimator{};
        resolving.erase(id);

        if (local.has) {
            inherited.has = true;
            inherited.animator = local.animator;
        }
        outAnimators[id] = inherited;
        return inherited;
    };

    for (const auto& [id, local] : locals) {
        (void)local;
        resolve(id);
    }
}

static bool RegisterTransformAnimationFromAnimator(const json& animator,
                                                   const glm::mat4& parentMatrix,
                                                   Ps1ExportedEntity& entity,
                                                   Ps1SceneExportResult& result) {
    if (!animator.value("enabled", true))
        return false;

    const std::string controllerPath = animator.value("controller", std::string{});
    if (controllerPath.empty())
        return false;

    std::string controllerError;
    const auto controller =
        AssetManager::Get().GetAnimatorController(controllerPath, controllerError);
    if (!controller) {
        MIPSYNC_WARN("PS1 transform animation: controller '{}' unavailable: {}",
                     controllerPath, controllerError);
        return false;
    }

    const AnimatorStateDef* state =
        FindAnimatorStateForPs1(*controller, animator.value("state", std::string{}));
    if (!state)
        return false;

    std::string clipPath = state->clipSourceModelPath;
    if (clipPath.empty())
        clipPath = controller->sourceModelPath;
    if (clipPath.empty())
        clipPath = animator.value("model", std::string{});

    std::string extension = PathUtf8::ToString(PathUtf8::FromString(clipPath).extension());
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".nanim")
        return false;

    std::ifstream file(PathUtf8::FromString(AssetManager::Get().ToAbsolute(clipPath)));
    if (!file.is_open()) {
        MIPSYNC_WARN("PS1 transform animation: cannot open '{}'", clipPath);
        return false;
    }

    json clipJson;
    try {
        file >> clipJson;
    } catch (const std::exception& ex) {
        MIPSYNC_WARN("PS1 transform animation: invalid '{}': {}", clipPath, ex.what());
        return false;
    }
    if (!clipJson.contains("keys") || !clipJson["keys"].is_array() ||
        clipJson["keys"].empty()) {
        MIPSYNC_WARN("PS1 transform animation: '{}' has no keys", clipPath);
        return false;
    }

    std::vector<Ps1TransformAnimKey> keys;
    keys.reserve(clipJson["keys"].size());
    for (const json& keyJson : clipJson["keys"]) {
        if (!keyJson.is_object())
            continue;

        Ps1ResolvedTransform local;
        float values[3];
        if (ReadVec3(keyJson.value("position", json::array({ 0, 0, 0 })), values))
            local.position = { values[0], values[1], values[2] };
        if (ReadVec3(keyJson.value("rotation", json::array({ 0, 0, 0 })), values))
            local.rotation = { values[0], values[1], values[2] };
        if (ReadVec3(keyJson.value("scale", json::array({ 1, 1, 1 })), values))
            local.scale = { values[0], values[1], values[2] };

        const Ps1ResolvedTransform world =
            DecomposePs1Transform(parentMatrix * Ps1TransformMatrix(local));
        Ps1TransformAnimKey key;
        key.frame = static_cast<uint16_t>(std::clamp(keyJson.value("frame", 0), 0, 65535));
        for (int axis = 0; axis < 3; ++axis) {
            key.position[axis] = world.position[axis];
            key.rotation[axis] = world.rotation[axis];
            key.scale[axis] = world.scale[axis];
        }
        keys.push_back(key);
    }
    if (keys.empty())
        return false;

    std::sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) {
        return a.frame < b.frame;
    });
    if (result.transformAnimKeys.size() + keys.size() > 65535u) {
        MIPSYNC_WARN("PS1 transform animation: key table limit exceeded by '{}'", clipPath);
        return false;
    }

    entity.transformAnimFirstKey =
        static_cast<uint16_t>(result.transformAnimKeys.size());
    entity.transformAnimKeyCount = static_cast<uint16_t>(keys.size());
    const int declaredLength = std::max(1, clipJson.value("lengthFrames", 1));
    entity.transformAnimLengthFrames = static_cast<uint16_t>(
        std::clamp(std::max(declaredLength, static_cast<int>(keys.back().frame)), 1, 65535));
    const float effectiveFps = static_cast<float>(std::max(1, clipJson.value("fps", 30))) *
                               std::max(0.0f, state->speed) *
                               std::max(0.0f, animator.value("speed", 1.0f));
    entity.transformAnimFps = static_cast<uint8_t>(
        std::clamp(static_cast<int>(std::lround(effectiveFps)), 0, 255));
    entity.transformAnimLoop = state->loop;
    result.transformAnimKeys.insert(result.transformAnimKeys.end(), keys.begin(), keys.end());

    MIPSYNC_INFO("PS1 transform animation: '{}' state '{}' -> {} keys, {} frames @ {} fps",
                 clipPath, state->name, keys.size(), entity.transformAnimLengthFrames,
                 static_cast<int>(entity.transformAnimFps));
    return true;
}

void FlattenEntityJson(const json& ent, Ps1SceneExportResult& result, uint32_t& nextId,
                       const glm::vec3& inheritedPosition = glm::vec3(0.0f),
                       const glm::vec3& inheritedRotation = glm::vec3(0.0f),
                       const glm::vec3& inheritedScale = glm::vec3(1.0f),
                       const std::unordered_map<uint32_t, Ps1ResolvedTransform>* flatWorldTransforms = nullptr,
                       const std::unordered_map<uint32_t, Ps1ResolvedAnimator>* flatAnimatorContexts = nullptr,
                       const json* inheritedAnimator = nullptr,
                       uint32_t inheritedControlRootEntityId = 0,
                       std::unordered_map<uint32_t, uint32_t>* flatControlRoots = nullptr) {
    Ps1ExportedEntity e;
    e.id = nextId++;
    e.name = ent.value("name", std::string{"Entity"});
    const uint32_t sourceId = ent.value("id", 0u);
    uint32_t controlRootEntityId = inheritedControlRootEntityId;
    if (controlRootEntityId == 0 && flatControlRoots) {
        const uint32_t sourceParentId = ent.value("parent", 0u);
        auto parentIt = flatControlRoots->find(sourceParentId);
        if (parentIt != flatControlRoots->end())
            controlRootEntityId = parentIt->second;
    }
    const bool hasEnabledScripts =
        (ent.contains("mipsScripts") && ent["mipsScripts"].is_array() &&
         std::any_of(ent["mipsScripts"].begin(), ent["mipsScripts"].end(),
                     [](const json& script) { return script.is_object() && script.value("enabled", true); })) ||
        (ent.contains("mipsScript") && ent["mipsScript"].is_object() &&
         ent["mipsScript"].value("enabled", true));
    if (hasEnabledScripts) {
        controlRootEntityId = e.id;
    }
    if (sourceId != 0 && flatControlRoots)
        (*flatControlRoots)[sourceId] = controlRootEntityId;

    glm::vec3 localPosition(0.0f);
    glm::vec3 localRotation(0.0f);
    glm::vec3 localScale(1.0f);
    if (ent.contains("transform")) {
        ReadVec3(ent["transform"].value("position", json::array({ 0, 0, 0 })), e.position);
        ReadVec3(ent["transform"].value("rotation", json::array({ 0, 0, 0 })), e.rotation);
        ReadVec3(ent["transform"].value("scale",    json::array({ 1, 1, 1 })), e.scale);
        localPosition = { e.position[0], e.position[1], e.position[2] };
        localRotation = { e.rotation[0], e.rotation[1], e.rotation[2] };
        localScale = { e.scale[0], e.scale[1], e.scale[2] };
    }

    glm::vec3 worldPosition = inheritedPosition + localPosition * inheritedScale;
    glm::vec3 worldRotation = inheritedRotation + localRotation;
    glm::vec3 worldScale = inheritedScale * localScale;
    if (flatWorldTransforms && sourceId != 0) {
        auto worldIt = flatWorldTransforms->find(sourceId);
        if (worldIt != flatWorldTransforms->end()) {
            worldPosition = worldIt->second.position;
            worldRotation = worldIt->second.rotation;
            worldScale = worldIt->second.scale;
        }
    }
    e.position[0] = worldPosition.x;
    e.position[1] = worldPosition.y;
    e.position[2] = worldPosition.z;
    e.rotation[0] = worldRotation.x;
    e.rotation[1] = worldRotation.y;
    e.rotation[2] = worldRotation.z;
    e.scale[0] = worldScale.x;
    e.scale[1] = worldScale.y;
    e.scale[2] = worldScale.z;

    // Transform clips animate the entity that owns the Animator. Animator
    // inheritance below exists for rigid skinned-mesh children and must not
    // accidentally attach the parent's .nanim clip to every descendant.
    if (ent.contains("animator") && ent["animator"].is_object()) {
        glm::mat4 parentMatrix(1.0f);
        if (flatWorldTransforms && sourceId != 0) {
            const auto worldIt = flatWorldTransforms->find(sourceId);
            if (worldIt != flatWorldTransforms->end()) {
                const glm::mat4 localMatrix = Ps1TransformMatrix({
                    localPosition, localRotation, localScale, glm::mat4(1.0f)
                });
                parentMatrix = worldIt->second.matrix * glm::inverse(localMatrix);
            }
        } else {
            Ps1ResolvedTransform parentTransform;
            parentTransform.position = inheritedPosition;
            parentTransform.rotation = inheritedRotation;
            parentTransform.scale = inheritedScale;
            parentMatrix = Ps1TransformMatrix(parentTransform);
        }
        RegisterTransformAnimationFromAnimator(
            ent["animator"], parentMatrix, e, result);
    }

    const json* currentAnimator = inheritedAnimator;
    if (ent.contains("animator") && ent["animator"].is_object())
        currentAnimator = &ent["animator"];
    if (flatAnimatorContexts && sourceId != 0) {
        auto animIt = flatAnimatorContexts->find(sourceId);
        if (animIt != flatAnimatorContexts->end() && animIt->second.has)
            currentAnimator = &animIt->second.animator;
    }

    if (ent.contains("meshRenderer")) {
        const json& mr = ent["meshRenderer"];
        e.prerenderOccluder = IsPrerenderOccluderEntity(ent, e.name);
        if (!mr.value("editorOnly", false) || e.prerenderOccluder) {
            e.meshEnabled = mr.value("enabled", true);
            e.meshKind = MeshKindFromJson(mr, e.name);
            e.meshSize = mr.value("size", 1.0f);
            ExtractColor(mr, e.color);
            e.materialPath = mr.value("material", std::string{});
            e.texturePath = mr.value("texture", std::string{});
            e.viewModel = mr.value("viewModel", false);
            e.seamFill = mr.value("ps1SeamFill", false);
            if (mr.contains("tiling"))
                ReadVec2(mr["tiling"], e.textureTiling);
            if (mr.contains("offset"))
                ReadVec2(mr["offset"], e.textureOffset);
            if (e.meshKind == 3) {
                e.meshKind = 4;
                e.meshPath = RegisterPrimitiveSphereMesh(e.meshSize, result);
            } else if (e.meshKind == 4 && mr.contains("mesh") && mr["mesh"].is_string())
                e.meshPath = mr["mesh"].get<std::string>();
            else if (e.meshKind == 4 && ent.contains("terrain"))
                e.meshPath = RegisterTerrainMeshFromJson(ent["terrain"], result);
            else if (e.meshKind == 4 && (ent.contains("proModeler") || ent.contains("proBuilder"))) {
                const std::string pmKey = ent.contains("proModeler") ? "proModeler" : "proBuilder";
                e.meshPath = RegisterProModelerMeshFromJson(ent[pmKey], result);
            }
        }
    }

    if (!e.meshEnabled && ent.contains("skinnedMeshRenderer")) {
        const json& sm = ent["skinnedMeshRenderer"];
        e.meshEnabled = sm.value("enabled", true);
        const int ps1ExportMode = std::clamp(sm.value("ps1ExportMode", 0), 0, 2);
        if (e.meshEnabled && ps1ExportMode > 0) {
            e.meshKind = 4;
            e.meshSize = 1.0f;
            ExtractColor(sm, e.color);
            e.materialPath = sm.value("material", std::string{});
            e.texturePath = sm.value("texture", std::string{});
            if (sm.contains("tiling"))
                ReadVec2(sm["tiling"], e.textureTiling);
            if (sm.contains("offset"))
                ReadVec2(sm["offset"], e.textureOffset);
            ApplySkinnedPartMaterialFromModel(sm, e);
            e.seamFill = sm.value("ps1SeamFill", false);
            e.vertexAnimTargetTris =
                static_cast<uint16_t>(std::clamp(sm.value("ps1VertexAnimTargetTris", 640), 32, 4000));
            e.vertexAnimTargetVerts =
                static_cast<uint16_t>(std::clamp(sm.value("ps1VertexAnimTargetVerts", 2000), 64, 5000));

            const bool rigidOk = RegisterRigidBonePartsFromJson(
                sm, result, nextId, e, worldPosition, worldRotation, worldScale,
                controlRootEntityId, currentAnimator);
            if (rigidOk) {
                e.meshEnabled = false;
                e.meshKind = 0;
            } else {
                MIPSYNC_WARN("PS1 rigid bone export failed for '{}'; skipping mesh", e.name);
                e.meshEnabled = false;
                e.meshKind = 0;
            }
        } else if (e.meshEnabled) {
            e.meshEnabled = false;
        }
    }

    if (ent.contains("collider") && ent["collider"].is_object()) {
        const json& col = ent["collider"];
        e.colliderShape = col.value("shape", -1);
        if (col.contains("center"))
            ReadVec3(col["center"], e.colliderCenter);
        if (col.contains("halfExtents"))
            ReadVec3(col["halfExtents"], e.colliderHalfExtents);
        e.colliderRadius = col.value("radius", 0.0f);
        e.colliderCapsuleHeight = col.value("capsuleHeight", 0.0f);
        e.colliderIsTrigger = col.value("isTrigger", false);
        e.colliderCameraShotTrigger =
            col.value("cameraTrigger", col.value("shotTrigger", IsCameraTriggerTag(ent)));
        e.colliderCameraTargetId = col.value("cameraTarget", 0u);
        if (e.colliderCameraShotTrigger)
            e.colliderIsTrigger = true;
    }

    if (ent.contains("camera")) {
        const json& cam = ent["camera"];
        e.hasCamera = cam.value("enabled", true);
        e.cameraPrimary = cam.value("primary", false);
        e.cameraFov = cam.value("fov", 60.0f);
        e.cameraNear = cam.value("nearClip", 0.1f);
        e.cameraFar = cam.value("farClip", 100.0f);
        e.prerenderedBackgroundPath = cam.value("prerenderedBackground", std::string{});
        if (cam.contains("shot") && cam["shot"].is_object()) {
            const json& shot = cam["shot"];
            e.cameraShotTriggerId = shot.value("trigger", 0u);
            e.cameraShotPriority = shot.value("priority", 0);
        }
    }

    if (ent.contains("audioSource") && ent["audioSource"].is_object()) {
        const json& audio = ent["audioSource"];
        e.audioEnabled = audio.value("enabled", true);
        e.audioClipPath = audio.value("clip", std::string{});
        e.audioPlayOnAwake = audio.value("playOnAwake", true);
        e.audioLoop = audio.value("loop", false);
        e.audioMute = audio.value("mute", false);
        e.audioVolume = std::clamp(audio.value("volume", 1.0f), 0.0f, 1.0f);
    }

    auto appendScript = [&](const json& sc) {
        if (sc.value("enabled", true)) {
            Ps1ExportedScript script;
            script.path = sc.value("path", std::string{});
            if (sc.contains("fields") && sc["fields"].is_object()) {
                for (auto it = sc["fields"].begin(); it != sc["fields"].end(); ++it) {
                    if (it.value().is_number())
                        script.fieldOverrides[it.key()] = it.value().get<double>();
                    else if (it.value().is_string())
                        script.assetFieldOverrides[it.key()] = it.value().get<std::string>();
                }
            }
            if (!script.path.empty()) e.scripts.push_back(std::move(script));
        }
    };
    if (ent.contains("mipsScripts") && ent["mipsScripts"].is_array()) {
        for (const auto& sc : ent["mipsScripts"])
            if (sc.is_object()) appendScript(sc);
    } else if (ent.contains("mipsScript") && ent["mipsScript"].is_object()) {
        appendScript(ent["mipsScript"]);
    }

    result.entities.push_back(e);

    if (ent.contains("children") && ent["children"].is_array()) {
        for (const json& child : ent["children"])
            FlattenEntityJson(child, result, nextId, worldPosition, worldRotation, worldScale,
                              flatWorldTransforms, flatAnimatorContexts, currentAnimator,
                              controlRootEntityId, flatControlRoots);
    }
}

struct Ps1UiRect {
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
};

struct Ps1RectTransformJson {
    glm::vec2 anchorMin{ 0.5f, 0.5f };
    glm::vec2 anchorMax{ 0.5f, 0.5f };
    glm::vec2 pivot{ 0.5f, 0.5f };
    glm::vec2 anchoredPosition{ 0.0f, 0.0f };
    glm::vec2 sizeDelta{ 100.0f, 100.0f };
};

struct Ps1UiNode {
    uint32_t id = 0;
    uint32_t parent = 0;
    int sortOrder = 0;
    int renderMode = 0;
    glm::vec2 referenceResolution{ 1920.0f, 1080.0f };
    bool hasCanvas = false;
    bool hasRect = false;
    Ps1RectTransformJson rect;
    float rotationZDegrees = 0.0f;
    bool hasImage = false;
    glm::vec4 imageColor{ 1.0f };
    std::string imageTexturePath;
    bool imagePreserveAspect = false;
    bool hasButton = false;
    glm::vec4 buttonColor{ 1.0f };
    std::string buttonTexturePath;
    bool buttonPreserveAspect = false;
    bool hasButtonGroup = false;
    int buttonGroupSelectedIndex = 0;
    bool buttonGroupWrapNavigation = true;
    bool buttonGroupGamepadNavigation = true;
    std::string cursorTexturePath;
    glm::vec2 cursorOffset{ -28.0f, 0.0f };
    glm::vec2 cursorSize{ 24.0f, 24.0f };
    bool hasText = false;
    std::string text;
    glm::vec4 textColor{ 1.0f };
    float fontSize = 18.0f;
    uint8_t alignment = 1;
    bool hasSpectrum = false;
    glm::vec4 spectrumColor{ 0.1f, 0.9f, 0.65f, 1.0f };
    glm::vec4 spectrumBackground{ 0.02f, 0.03f, 0.04f, 0.65f };
    uint8_t spectrumBars = 16;
    float spectrumGap = 4.0f;
    float spectrumSensitivity = 1.4f;
    std::vector<uint32_t> children;
};

static bool ReadVec2Json(const json& j, glm::vec2& out) {
    if (!j.is_array() || j.size() < 2)
        return false;
    out.x = j[0].get<float>();
    out.y = j[1].get<float>();
    return true;
}

static bool ReadVec4Json(const json& j, glm::vec4& out) {
    if (!j.is_array() || j.size() < 4)
        return false;
    out.r = j[0].get<float>();
    out.g = j[1].get<float>();
    out.b = j[2].get<float>();
    out.a = j[3].get<float>();
    return true;
}

static Ps1UiRect CalcUiRectInParent(const Ps1UiRect& parent, const Ps1RectTransformJson& rect) {
    const glm::vec2 parentSize{ parent.maxX - parent.minX, parent.maxY - parent.minY };
    const glm::vec2 anchorMinPos{ parent.minX + parentSize.x * rect.anchorMin.x,
                                  parent.minY + parentSize.y * rect.anchorMin.y };
    const glm::vec2 anchorMaxPos{ parent.minX + parentSize.x * rect.anchorMax.x,
                                  parent.minY + parentSize.y * rect.anchorMax.y };
    const glm::vec2 anchorCenter = (anchorMinPos + anchorMaxPos) * 0.5f;
    const glm::vec2 size{ parentSize.x * (rect.anchorMax.x - rect.anchorMin.x) + rect.sizeDelta.x,
                          parentSize.y * (rect.anchorMax.y - rect.anchorMin.y) + rect.sizeDelta.y };
    const glm::vec2 pivotPos = anchorCenter + rect.anchoredPosition;
    const glm::vec2 rectMin = pivotPos - glm::vec2(size.x * rect.pivot.x, size.y * rect.pivot.y);
    return { rectMin.x, rectMin.y, rectMin.x + size.x, rectMin.y + size.y };
}

static void AddUiNodeFromJson(const json& ent, uint32_t inheritedParent,
                              std::unordered_map<uint32_t, Ps1UiNode>& nodes,
                              std::vector<uint32_t>& order) {
    if (!ent.is_object())
        return;

    Ps1UiNode node;
    node.id = ent.value("id", 0u);
    if (node.id == 0)
        return;
    node.parent = ent.value("parent", inheritedParent);

    if (ent.contains("canvas") && ent["canvas"].is_object()) {
        const json& c = ent["canvas"];
        node.hasCanvas = true;
        node.renderMode = c.value("renderMode", 0);
        node.sortOrder = c.value("sortOrder", 0);
        ReadVec2Json(c.value("referenceResolution", json::array({ 1920.0f, 1080.0f })),
                     node.referenceResolution);
    }

    if (ent.contains("rectTransform") && ent["rectTransform"].is_object()) {
        const json& rt = ent["rectTransform"];
        node.hasRect = true;
        ReadVec2Json(rt.value("anchorMin", json::array({ 0.5f, 0.5f })), node.rect.anchorMin);
        ReadVec2Json(rt.value("anchorMax", json::array({ 0.5f, 0.5f })), node.rect.anchorMax);
        ReadVec2Json(rt.value("pivot", json::array({ 0.5f, 0.5f })), node.rect.pivot);
        ReadVec2Json(rt.value("anchoredPosition", json::array({ 0.0f, 0.0f })),
                     node.rect.anchoredPosition);
        ReadVec2Json(rt.value("sizeDelta", json::array({ 100.0f, 100.0f })), node.rect.sizeDelta);
    }
    if (ent.contains("transform") && ent["transform"].is_object()) {
        float values[3] = { 0.0f, 0.0f, 0.0f };
        if (ReadVec3(ent["transform"].value("rotation", json::array({ 0, 0, 0 })), values))
            node.rotationZDegrees = values[2];
    }

    if (ent.contains("uiImage") && ent["uiImage"].is_object()) {
        const json& im = ent["uiImage"];
        node.hasImage = im.value("enabled", true);
        ReadVec4Json(im.value("color", json::array({ 1.0f, 1.0f, 1.0f, 1.0f })), node.imageColor);
        node.imageTexturePath = im.value("texture", std::string{});
        node.imagePreserveAspect = im.value("preserveAspect", false);
    }

    if (ent.contains("uiButton") && ent["uiButton"].is_object()) {
        const json& bt = ent["uiButton"];
        node.hasButton = bt.value("enabled", true);
        ReadVec4Json(bt.value("normalColor", json::array({ 1.0f, 1.0f, 1.0f, 1.0f })), node.buttonColor);
        node.buttonTexturePath = bt.value("backgroundTexture", std::string{});
        node.buttonPreserveAspect = bt.value("preserveAspect", false);
    }

    if (ent.contains("uiButtonGroup") && ent["uiButtonGroup"].is_object()) {
        const json& bg = ent["uiButtonGroup"];
        node.hasButtonGroup = true;
        node.buttonGroupSelectedIndex = bg.value("selectedIndex", 0);
        node.buttonGroupWrapNavigation = bg.value("wrapNavigation", true);
        node.buttonGroupGamepadNavigation = bg.value("gamepadNavigation", true);
        node.cursorTexturePath = bg.value("cursorTexture", std::string{});
        ReadVec2Json(bg.value("cursorOffset", json::array({ -28.0f, 0.0f })), node.cursorOffset);
        ReadVec2Json(bg.value("cursorSize", json::array({ 24.0f, 24.0f })), node.cursorSize);
    }

    if (ent.contains("uiText") && ent["uiText"].is_object()) {
        const json& tx = ent["uiText"];
        node.hasText = tx.value("enabled", true);
        node.text = tx.value("text", std::string{});
        ReadVec4Json(tx.value("color", json::array({ 1.0f, 1.0f, 1.0f, 1.0f })), node.textColor);
        node.fontSize = tx.value("fontSize", 18.0f);
        node.alignment = static_cast<uint8_t>(std::clamp(tx.value("alignment", 1), 0, 2));
    }

    if (ent.contains("uiAudioSpectrum") && ent["uiAudioSpectrum"].is_object()) {
        const json& sp = ent["uiAudioSpectrum"];
        node.hasSpectrum = sp.value("enabled", true);
        ReadVec4Json(sp.value("color", json::array({ 0.1f, 0.9f, 0.65f, 1.0f })), node.spectrumColor);
        ReadVec4Json(sp.value("backgroundColor", json::array({ 0.02f, 0.03f, 0.04f, 0.65f })),
                     node.spectrumBackground);
        node.spectrumBars = static_cast<uint8_t>(std::clamp(sp.value("barCount", 16), 4, 32));
        node.spectrumGap = sp.value("barGap", 4.0f);
        node.spectrumSensitivity = sp.value("sensitivity", 1.4f);
    }

    order.push_back(node.id);
    nodes[node.id] = std::move(node);

    if (ent.contains("children") && ent["children"].is_array()) {
        for (const json& child : ent["children"])
            AddUiNodeFromJson(child, node.id, nodes, order);
    }
}

static bool ConvertPs1UiRect(const Ps1UiRect& rect, float layoutW, float layoutH,
                             int16_t& outX, int16_t& outY, int16_t& outW, int16_t& outH) {
    const float safeW = std::max(layoutW, 1.0f);
    const float safeH = std::max(layoutH, 1.0f);
    const int x0 = static_cast<int>(std::floor((rect.minX / safeW) * 320.0f + 0.5f));
    const int x1 = static_cast<int>(std::floor((rect.maxX / safeW) * 320.0f + 0.5f));
    const int y0 = static_cast<int>(std::floor((1.0f - rect.maxY / safeH) * 240.0f + 0.5f));
    const int y1 = static_cast<int>(std::floor((1.0f - rect.minY / safeH) * 240.0f + 0.5f));
    const int w = x1 - x0;
    const int h = y1 - y0;
    if (w <= 0 || h <= 0)
        return false;
    outX = ClampI16(x0);
    outY = ClampI16(y0);
    outW = ClampI16(w);
    outH = ClampI16(h);
    return true;
}

static void AppendPs1UiElement(Ps1SceneExportResult& outResult, const Ps1UiNode& node,
                               const Ps1UiRect& rect, float layoutW, float layoutH, uint8_t kind) {
    const float safeW = std::max(layoutW, 1.0f);
    const float safeH = std::max(layoutH, 1.0f);

    Ps1ExportedUiElement elem;
    if (!ConvertPs1UiRect(rect, layoutW, layoutH, elem.x, elem.y, elem.w, elem.h))
        return;
    elem.kind = kind;
    elem.rotationDegrees = ClampI16(static_cast<int>(std::round(node.rotationZDegrees)));

    const glm::vec4& c = (kind == 1) ? node.imageColor :
                         (kind == 3 ? node.spectrumColor : node.textColor);
    elem.color[0] = ToByte01(c.r);
    elem.color[1] = ToByte01(c.g);
    elem.color[2] = ToByte01(c.b);
    elem.color[3] = ToByte01(c.a);

    if (kind == 1) {
        elem.texturePath = node.imageTexturePath;
        elem.preserveAspect = node.imagePreserveAspect ? 1 : 0;
    } else if (kind == 2) {
        elem.alignment = node.alignment;
        elem.fontSize = static_cast<uint8_t>(std::clamp(static_cast<int>(std::round(node.fontSize * 240.0f / safeH)),
                                                       4, 64));
        elem.textOffset = static_cast<uint16_t>(std::min<size_t>(outResult.uiTextBlob.size(), 65535));
        const size_t remaining = 65535u - elem.textOffset;
        const size_t len = std::min(node.text.size(), remaining);
        outResult.uiTextBlob.append(node.text.data(), len);
        elem.textLength = static_cast<uint16_t>(len);
    } else if (kind == 3) {
        for (int i = 0; i < 4; ++i)
            elem.spectrumBackground[i] = ToByte01(node.spectrumBackground[i]);
        elem.spectrumBars = node.spectrumBars;
        elem.spectrumGap = static_cast<uint8_t>(std::clamp(
            static_cast<int>(std::round(node.spectrumGap * 320.0f / safeW)), 0, 32));
        elem.spectrumSensitivityQ8 = static_cast<uint16_t>(std::clamp(
            static_cast<int>(std::round(node.spectrumSensitivity * 256.0f)), 1, 4095));
    }

    outResult.uiElements.push_back(std::move(elem));
}

static void ExtractUiElements(const json& root, Ps1SceneExportResult& outResult) {
    std::unordered_map<uint32_t, Ps1UiNode> nodes;
    std::vector<uint32_t> order;
    if (!root.contains("entities") || !root["entities"].is_array())
        return;

    for (const json& ent : root["entities"])
        AddUiNodeFromJson(ent, 0, nodes, order);

    for (uint32_t id : order) {
        auto nodeIt = nodes.find(id);
        if (nodeIt != nodes.end() && nodeIt->second.parent != 0) {
            auto parentIt = nodes.find(nodeIt->second.parent);
            if (parentIt != nodes.end())
                parentIt->second.children.push_back(id);
        }
    }

    std::vector<uint32_t> canvases;
    for (uint32_t id : order) {
        auto it = nodes.find(id);
        if (it != nodes.end() && it->second.hasCanvas && it->second.renderMode == 0)
            canvases.push_back(id);
    }
    std::stable_sort(canvases.begin(), canvases.end(), [&](uint32_t a, uint32_t b) {
        return nodes[a].sortOrder < nodes[b].sortOrder;
    });

    for (uint32_t canvasId : canvases) {
        const Ps1UiNode& canvas = nodes[canvasId];
        const float layoutW = kPs1UiLayoutWidth;
        const float layoutH = kPs1UiLayoutHeight;
        Ps1UiRect rootRect{ 0.0f, 0.0f, layoutW, layoutH };
        if (canvas.hasRect)
            rootRect = CalcUiRectInParent(rootRect, canvas.rect);

        std::function<void(uint32_t, Ps1UiRect)> visit = [&](uint32_t id, Ps1UiRect parentRect) {
            const Ps1UiNode& node = nodes[id];
            Ps1UiRect rect = parentRect;
            if (node.hasRect)
                rect = CalcUiRectInParent(parentRect, node.rect);
            if (node.hasButtonGroup) {
                std::vector<std::pair<uint32_t, Ps1UiRect>> buttonChildren;
                for (uint32_t child : node.children) {
                    const Ps1UiNode& childNode = nodes[child];
                    if (!childNode.hasButton)
                        continue;
                    Ps1UiRect childRect = rect;
                    if (childNode.hasRect)
                        childRect = CalcUiRectInParent(rect, childNode.rect);
                    buttonChildren.push_back({ child, childRect });
                }
                if (!buttonChildren.empty()) {
                    Ps1ExportedUiButtonGroup group;
                    group.selectedIndex = static_cast<uint8_t>(std::clamp(
                        node.buttonGroupSelectedIndex, 0,
                        static_cast<int>(buttonChildren.size()) - 1));
                    group.wrapNavigation = node.buttonGroupWrapNavigation ? 1 : 0;
                    group.gamepadNavigation = node.buttonGroupGamepadNavigation ? 1 : 0;
                    group.buttonRectOffset = static_cast<uint16_t>(
                        std::min<size_t>(outResult.uiButtonRects.size(), 65535));
                    group.buttonCount = static_cast<uint8_t>(
                        std::min<size_t>(buttonChildren.size(), 255));
                    group.cursorOffsetX = ClampI16(static_cast<int>(std::round(
                        node.cursorOffset.x * 320.0f / layoutW)));
                    group.cursorOffsetY = ClampI16(static_cast<int>(std::round(
                        -node.cursorOffset.y * 240.0f / layoutH)));
                    group.cursorW = ClampI16(static_cast<int>(std::round(
                        std::max(node.cursorSize.x, 1.0f) * 320.0f / layoutW)));
                    group.cursorH = ClampI16(static_cast<int>(std::round(
                        std::max(node.cursorSize.y, 1.0f) * 240.0f / layoutH)));
                    group.cursorTexturePath = node.cursorTexturePath;
                    for (size_t bi = 0; bi < buttonChildren.size() && bi < 255; ++bi) {
                        Ps1ExportedUiButtonRect rr;
                        if (ConvertPs1UiRect(buttonChildren[bi].second, layoutW, layoutH,
                                             rr.x, rr.y, rr.w, rr.h)) {
                            outResult.uiButtonRects.push_back(rr);
                        } else if (group.buttonCount > 0) {
                            --group.buttonCount;
                        }
                    }
                    if (group.buttonCount > 0)
                        outResult.uiButtonGroups.push_back(std::move(group));
                }
            }
            if (node.hasImage)
                AppendPs1UiElement(outResult, node, rect, layoutW, layoutH, 1);
            if (node.hasButton) {
                Ps1UiNode buttonImage = node;
                buttonImage.imageColor = node.buttonColor;
                buttonImage.imageTexturePath = node.buttonTexturePath;
                buttonImage.imagePreserveAspect = node.buttonPreserveAspect;
                AppendPs1UiElement(outResult, buttonImage, rect, layoutW, layoutH, 1);
            }
            if (node.hasText && !node.text.empty())
                AppendPs1UiElement(outResult, node, rect, layoutW, layoutH, 2);
            if (node.hasSpectrum)
                AppendPs1UiElement(outResult, node, rect, layoutW, layoutH, 3);
            for (uint32_t child : node.children)
                visit(child, rect);
        };

        if (canvas.hasImage)
            AppendPs1UiElement(outResult, canvas, rootRect, layoutW, layoutH, 1);
        if (canvas.hasButton) {
            Ps1UiNode buttonImage = canvas;
            buttonImage.imageColor = canvas.buttonColor;
            buttonImage.imageTexturePath = canvas.buttonTexturePath;
            buttonImage.imagePreserveAspect = canvas.buttonPreserveAspect;
            AppendPs1UiElement(outResult, buttonImage, rootRect, layoutW, layoutH, 1);
        }
        if (canvas.hasText && !canvas.text.empty())
            AppendPs1UiElement(outResult, canvas, rootRect, layoutW, layoutH, 2);
        if (canvas.hasSpectrum)
            AppendPs1UiElement(outResult, canvas, rootRect, layoutW, layoutH, 3);
        for (uint32_t child : canvas.children)
            visit(child, rootRect);
    }
}

static void ExtractSkyboxBackground(const json& root, Ps1SceneExportResult& outResult) {
    std::string bestTexture;
    int bestPriority = std::numeric_limits<int>::min();
    if (!root.contains("entities") || !root["entities"].is_array())
        return;

    auto scan = [&](auto&& self, const json& ent) -> void {
        if (ent.is_object() && ent.contains("postProcessVolume") &&
            ent["postProcessVolume"].is_object()) {
            const json& pp = ent["postProcessVolume"];
            const bool enabled = pp.value("enabled", true);
            const bool skyboxEnabled = pp.value("skyboxEnabled", false);
            const std::string texture = pp.value("skyboxTexture", std::string{});
            const int priority = pp.value("priority", 0);
            if (enabled && skyboxEnabled && !texture.empty() && priority >= bestPriority) {
                bestPriority = priority;
                bestTexture = texture;
            }
        }
        if (ent.is_object() && ent.contains("children") && ent["children"].is_array()) {
            for (const json& child : ent["children"])
                self(self, child);
        }
    };

    for (const json& ent : root["entities"])
        scan(scan, ent);

    if (!bestTexture.empty())
        outResult.backgroundImagePath = bestTexture;
}

static void ExtractPostProcessFog(const json& root, Ps1SceneExportResult& outResult) {
    int bestPriority = std::numeric_limits<int>::min();
    bool found = false;
    if (!root.contains("entities") || !root["entities"].is_array())
        return;

    auto scan = [&](auto&& self, const json& ent) -> void {
        if (ent.is_object() && ent.contains("postProcessVolume") &&
            ent["postProcessVolume"].is_object()) {
            const json& pp = ent["postProcessVolume"];
            const bool componentEnabled = pp.value("enabled", true);
            const int priority = pp.value("priority", 0);
            if (componentEnabled && priority >= bestPriority) {
                bestPriority = priority;
                found = true;
                outResult.fogEnabled = pp.value("fogEnabled", true);
                outResult.fogStart = std::max(0.0f, pp.value("fogStart", 10.0f));
                outResult.fogEnd = std::max(outResult.fogStart + 0.1f,
                                            pp.value("fogEnd", 40.0f));
                if (pp.contains("fogColor") && pp["fogColor"].is_array() &&
                    pp["fogColor"].size() >= 3) {
                    outResult.fogColor[0] = ToByte01(pp["fogColor"][0].get<float>());
                    outResult.fogColor[1] = ToByte01(pp["fogColor"][1].get<float>());
                    outResult.fogColor[2] = ToByte01(pp["fogColor"][2].get<float>());
                }
            }
        }
        if (ent.is_object() && ent.contains("children") && ent["children"].is_array()) {
            for (const json& child : ent["children"])
                self(self, child);
        }
    };

    for (const json& ent : root["entities"])
        scan(scan, ent);
    if (!found)
        outResult.fogEnabled = false;
}

static bool ResolveTexturePathForBake(const fs::path& projectRoot, const fs::path& scenePath,
                                      const std::string& rawPathUtf8, fs::path& outAbs) {
    outAbs.clear();
    if (rawPathUtf8.empty())
        return false;
    const fs::path rawPath = PathUtf8::FromString(rawPathUtf8);
    std::vector<fs::path> candidates;
    if (rawPath.is_absolute()) {
        candidates.push_back(rawPath);
    } else {
        candidates.push_back(scenePath.parent_path() / rawPath);
        candidates.push_back(projectRoot / rawPath);
        candidates.push_back(projectRoot / "assets" / rawPath);
    }
    for (const fs::path& candidate : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec)) {
            outAbs = candidate;
            return true;
        }
    }
    return false;
}

static uint8_t SampleSkyboxByte(const uint8_t* pixels, int width, int height, int channels,
                                float u, float v, int c) {
    u = u - std::floor(u);
    v = std::clamp(v, 0.0f, 1.0f);
    const int x = std::clamp(static_cast<int>(std::floor(u * static_cast<float>(width))), 0, width - 1);
    const int y = std::clamp(static_cast<int>(std::floor(v * static_cast<float>(height))), 0, height - 1);
    return pixels[(static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) *
                  static_cast<size_t>(channels) + static_cast<size_t>(c)];
}

static void BakeSkyboxBackgroundForPs1(const json& root, const std::string& projectRootUtf8,
                                       const fs::path& scenePath, const std::string& generatedDir,
                                       Ps1SceneExportResult& outResult) {
    std::string bestTexture;
    float bestRotation = 0.0f;
    float bestExposure = 0.0f;
    glm::vec3 bestTint{ 1.0f };
    int bestPriority = std::numeric_limits<int>::min();
    if (!root.contains("entities") || !root["entities"].is_array())
        return;

    auto scan = [&](auto&& self, const json& ent) -> void {
        if (ent.is_object() && ent.contains("postProcessVolume") &&
            ent["postProcessVolume"].is_object()) {
            const json& pp = ent["postProcessVolume"];
            const bool enabled = pp.value("enabled", true);
            const bool skyboxEnabled = pp.value("skyboxEnabled", false);
            const std::string texture = pp.value("skyboxTexture", std::string{});
            const int priority = pp.value("priority", 0);
            if (enabled && skyboxEnabled && !texture.empty() && priority >= bestPriority) {
                bestPriority = priority;
                bestTexture = texture;
                bestRotation = pp.value("skyboxRotation", 0.0f);
                bestExposure = pp.value("skyboxExposure", 0.0f);
                glm::vec3 tint{ 1.0f };
                if (pp.contains("skyboxTint") && pp["skyboxTint"].is_array() &&
                    pp["skyboxTint"].size() >= 3) {
                    tint.r = pp["skyboxTint"][0].get<float>();
                    tint.g = pp["skyboxTint"][1].get<float>();
                    tint.b = pp["skyboxTint"][2].get<float>();
                }
                bestTint = tint;
            }
        }
        if (ent.is_object() && ent.contains("children") && ent["children"].is_array()) {
            for (const json& child : ent["children"])
                self(self, child);
        }
    };
    for (const json& ent : root["entities"])
        scan(scan, ent);
    if (bestTexture.empty())
        return;

    const fs::path projectRoot = PathUtf8::FromString(projectRootUtf8);
    fs::path srcAbs;
    if (!ResolveTexturePathForBake(projectRoot, scenePath, bestTexture, srcAbs)) {
        outResult.backgroundImagePath = bestTexture;
        return;
    }

    int srcW = 0, srcH = 0, srcC = 0;
    uint8_t* src = stbi_load(PathUtf8::ToString(srcAbs).c_str(), &srcW, &srcH, &srcC, 4);
    if (!src || srcW <= 0 || srcH <= 0) {
        if (src) stbi_image_free(src);
        outResult.backgroundImagePath = bestTexture;
        return;
    }

    constexpr int outW = 256;
    constexpr int outH = 192;
    std::vector<uint8_t> out(static_cast<size_t>(outW) * static_cast<size_t>(outH) * 4u, 255u);

    const Ps1ExportedEntity* camera = nullptr;
    if (outResult.cameraEntityIndex >= 0 &&
        static_cast<size_t>(outResult.cameraEntityIndex) < outResult.entities.size()) {
        camera = &outResult.entities[static_cast<size_t>(outResult.cameraEntityIndex)];
    }
    const float fov = camera ? camera->cameraFov : 60.0f;
    const float nearPlane = camera ? std::max(camera->cameraNear, 0.001f) : 0.1f;
    const float farPlane = camera ? std::max(camera->cameraFar, nearPlane + 1.0f) : 100.0f;
    const glm::mat4 invProjection = glm::inverse(glm::perspective(
        glm::radians(fov), 4.0f / 3.0f, nearPlane, farPlane));
    const glm::mat3 invView = camera
        ? Ps1RotationMatrixFromEuler(glm::vec3(camera->rotation[0], camera->rotation[1], camera->rotation[2]))
        : glm::mat3(1.0f);

    const float rot = glm::radians(bestRotation);
    const float s = std::sin(rot);
    const float c = std::cos(rot);
    const float exposureScale = std::exp2(bestExposure);
    constexpr float pi = 3.14159265358979323846f;

    for (int y = 0; y < outH; ++y) {
        for (int x = 0; x < outW; ++x) {
            const glm::vec2 uv{
                (static_cast<float>(x) + 0.5f) / static_cast<float>(outW),
                (static_cast<float>(outH - 1 - y) + 0.5f) / static_cast<float>(outH),
            };
            const glm::vec2 ndc = uv * 2.0f - glm::vec2(1.0f);
            glm::vec4 view = invProjection * glm::vec4(ndc, 1.0f, 1.0f);
            view = glm::vec4(glm::normalize(glm::vec3(view) / std::max(view.w, 0.0001f)), 0.0f);
            glm::vec3 dir = glm::normalize(invView * glm::vec3(view));
            dir = glm::normalize(glm::vec3(c * dir.x + s * dir.z, dir.y, -s * dir.x + c * dir.z));

            const float sampleU = std::atan2(dir.z, dir.x) / (2.0f * pi) + 0.5f;
            const float sampleV = 0.5f - std::asin(std::clamp(dir.y, -1.0f, 1.0f)) / pi;
            const size_t dst = (static_cast<size_t>(y) * outW + static_cast<size_t>(x)) * 4u;
            for (int ch = 0; ch < 3; ++ch) {
                const float tint = ch == 0 ? bestTint.r : (ch == 1 ? bestTint.g : bestTint.b);
                const float color = static_cast<float>(SampleSkyboxByte(src, srcW, srcH, 4, sampleU, sampleV, ch)) *
                                    exposureScale * tint;
                out[dst + static_cast<size_t>(ch)] =
                    static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(color)), 0, 255));
            }
            out[dst + 3u] = 255u;
        }
    }

    stbi_image_free(src);

    const fs::path outPath = PathUtf8::FromString(generatedDir) / "ps1_skybox_background.png";
    std::error_code ec;
    fs::create_directories(outPath.parent_path(), ec);
    if (stbi_write_png(PathUtf8::ToString(outPath).c_str(), outW, outH, 4,
                       out.data(), outW * 4) != 0) {
        outResult.backgroundImagePath = PathUtf8::ToString(outPath);
    } else {
        outResult.backgroundImagePath = bestTexture;
    }
}

static void BuildUiGlyphs(Ps1SceneExportResult& outResult) {
    std::vector<uint32_t> codepoints;
    std::unordered_set<uint32_t> seen;

    size_t index = 0;
    while (index < outResult.uiTextBlob.size()) {
        uint32_t cp = 0;
        if (!DecodeUtf8Next(outResult.uiTextBlob, index, cp))
            break;
        if (cp < 0x80u)
            continue;
        if (seen.insert(cp).second)
            codepoints.push_back(cp);
    }

    std::sort(codepoints.begin(), codepoints.end());
    outResult.uiGlyphs.clear();
    outResult.uiGlyphRows.clear();

    for (uint32_t cp : codepoints) {
        std::vector<uint16_t> rows;
        if (!RenderUiGlyphBitmap(cp, rows)) {
            MIPSYNC_WARN("PS1 UI font: failed to rasterize U+{:04X}", cp);
            continue;
        }

        Ps1ExportedUiGlyph glyph;
        glyph.codepoint = cp;
        glyph.width = static_cast<uint8_t>(kPs1UiCjkGlyphSize);
        glyph.height = static_cast<uint8_t>(kPs1UiCjkGlyphSize);
        glyph.advance = static_cast<uint8_t>(kPs1UiCjkGlyphSize);
        glyph.rowOffset = static_cast<uint16_t>(std::min<size_t>(outResult.uiGlyphRows.size(), 65535));

        outResult.uiGlyphRows.insert(outResult.uiGlyphRows.end(), rows.begin(), rows.end());
        outResult.uiGlyphs.push_back(glyph);
    }

    if (!outResult.uiGlyphs.empty()) {
        MIPSYNC_INFO("PS1 UI font export: {} glyphs, {} rows",
                     outResult.uiGlyphs.size(), outResult.uiGlyphRows.size());
    }
}

std::string SanitizeSymbol(const std::string& name) {
    std::string out;
    for (char c : name) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_')
            out.push_back(c);
        else
            out.push_back('_');
    }
    if (out.empty() || (out[0] >= '0' && out[0] <= '9'))
        out.insert(out.begin(), '_');
    return out;
}

static bool ResolvePrerenderedBackgroundPath(const fs::path& projectRoot, const fs::path& scenePath,
                                             const std::string& rawPathUtf8, std::string& outProjectRel) {
    outProjectRel.clear();
    if (rawPathUtf8.empty())
        return false;

    const fs::path rawPath = PathUtf8::FromString(rawPathUtf8);
    std::vector<fs::path> candidates;
    if (rawPath.is_absolute()) {
        candidates.push_back(rawPath);
    } else {
        candidates.push_back(scenePath.parent_path() / rawPath);
        candidates.push_back(projectRoot / rawPath);
        candidates.push_back(projectRoot / "assets" / rawPath);
    }

    for (const fs::path& candidate : candidates) {
        std::error_code existsEc;
        if (!fs::is_regular_file(candidate, existsEc))
            continue;

        std::error_code relEc;
        const fs::path rel = fs::relative(candidate, projectRoot, relEc);
        outProjectRel = PathUtf8::ToString(relEc ? candidate : rel);
        return true;
    }

    return false;
}

bool EmitSceneDataC(const Ps1SceneExportResult& data, const std::string& outCFile,
                    std::string& outError) {
    try {
        const fs::path cPath = PathUtf8::FromString(outCFile);
        if (cPath.has_parent_path())
            fs::create_directories(cPath.parent_path());

        std::ofstream out(cPath, std::ios::trunc);
        if (!out.is_open()) {
            outError = "cannot open scene_data.c: " + outCFile;
            return false;
        }

        out << "/* Auto-generated by Mipsync PS1SceneExport. Do not edit. */\n"
               "#include \"../runtime/scene.h\"\n\n";

        out << "static const ps1_entity k_ps1_entities[] = {\n";
        for (size_t i = 0; i < data.entities.size(); ++i) {
            const auto& e = data.entities[i];
            out << "    { " << e.id << "u, "
                << "\"" << e.name << "\", "
                << "{" << ToFixed16(e.position[0]) << ", "
                << ToFixed16(e.position[1]) << ", "
                << ToFixed16(e.position[2]) << "}, "
                << "{" << ToFixed16(e.rotation[0]) << ", "
                << ToFixed16(e.rotation[1]) << ", "
                << ToFixed16(e.rotation[2]) << "}, "
                << "{" << ToFixed16(e.scale[0]) << ", "
                << ToFixed16(e.scale[1]) << ", "
                << ToFixed16(e.scale[2]) << "}, "
                << static_cast<int>(e.meshKind) << ", "
                << static_cast<int>(e.meshIndex) << "u, "
                << ToFixed16(e.meshSize) << ", "
                << static_cast<int>(e.color[0]) << ", "
                << static_cast<int>(e.color[1]) << ", "
                << static_cast<int>(e.color[2]) << ", "
                << static_cast<int>(e.textureIndex) << ", "
                << ToQ8(e.textureTiling[0]) << ", "
                << ToQ8(e.textureTiling[1]) << ", "
                << ToQ8(e.textureOffset[0]) << ", "
                << ToQ8(e.textureOffset[1]) << ", "
                << (e.meshEnabled ? 1 : 0) << ", "
                << (e.viewModel ? 1 : 0) << ", "
                << (e.prerenderOccluder ? 1 : 0) << ", "
                << (e.seamFill ? 1 : 0) << ", "
                << e.vertexAnimFirstMeshIndex << "u, "
                << e.vertexAnimFrameCount << "u, "
                << static_cast<int>(e.vertexAnimFps) << ", "
                << "0u, 0, "
                << e.rigidAnimFirstFrame << "u, "
                << e.rigidAnimFrameCount << "u, "
                << static_cast<int>(e.rigidAnimFps) << ", "
                << "0u, 0u, 0, "
                << e.rigidIdleFirstFrame << "u, "
                << e.rigidIdleFrameCount << "u, "
                << static_cast<int>(e.rigidIdleFps) << ", "
                << e.rigidWalkFirstFrame << "u, "
                << e.rigidWalkFrameCount << "u, "
                << static_cast<int>(e.rigidWalkFps) << ", "
                << e.rigidAimFirstFrame << "u, "
                << e.rigidAimFrameCount << "u, "
                << static_cast<int>(e.rigidAimFps) << ", "
                << e.rigidRootEntityIndex << ", "
                << "0, 0, "
                << e.transformAnimFirstKey << "u, "
                << e.transformAnimKeyCount << "u, "
                << e.transformAnimLengthFrames << "u, "
                << static_cast<int>(e.transformAnimFps) << ", "
                << (e.transformAnimLoop ? 1 : 0) << ", "
                << (e.hasCamera ? 1 : 0) << ", "
                << (e.cameraPrimary ? 1 : 0) << ", "
                << ToFixed16(e.cameraFov) << ", "
                << ToFixed16(e.cameraNear) << ", "
                << ToFixed16(e.cameraFar) << ", "
                << static_cast<int>(e.cameraBackgroundTextureIndex) << ", "
                << e.cameraShotTriggerEntityIndex << ", "
                << e.cameraShotPriority << ", "
                << e.colliderShape << ", "
                << "{" << ToFixed16(e.colliderCenter[0]) << ", "
                << ToFixed16(e.colliderCenter[1]) << ", "
                << ToFixed16(e.colliderCenter[2]) << "}, "
                << "{" << ToFixed16(e.colliderHalfExtents[0]) << ", "
                << ToFixed16(e.colliderHalfExtents[1]) << ", "
                << ToFixed16(e.colliderHalfExtents[2]) << "}, "
                << ToFixed16(e.colliderRadius) << ", "
                << ToFixed16(e.colliderCapsuleHeight) << ", "
                << (e.colliderIsTrigger ? 1 : 0) << ", "
                << (e.colliderCameraShotTrigger ? 1 : 0) << ", "
                << e.colliderCameraTargetEntityIndex << ", "
                << static_cast<int>(e.audioClipIndex) << ", "
                << (e.audioEnabled ? 1 : 0) << ", "
                << (e.audioPlayOnAwake ? 1 : 0) << ", "
                << (e.audioLoop ? 1 : 0) << ", "
                << (e.audioMute ? 1 : 0) << ", "
                << std::clamp(static_cast<int>(std::lround(e.audioVolume * 255.0f)), 0, 255)
                << " },\n";
        }
        out << "};\n\n";

        out << "static const ps1_rigid_anim_frame k_ps1_rigid_anim_frames[] = {\n";
        for (const auto& f : data.rigidAnimFrames) {
            out << "    { { "
                << ToFixed16(f.position[0]) << ", "
                << ToFixed16(f.position[1]) << ", "
                << ToFixed16(f.position[2]) << " }, { "
                << "{ " << f.matrix[0][0] << ", " << f.matrix[0][1] << ", " << f.matrix[0][2] << " }, "
                << "{ " << f.matrix[1][0] << ", " << f.matrix[1][1] << ", " << f.matrix[1][2] << " }, "
                << "{ " << f.matrix[2][0] << ", " << f.matrix[2][1] << ", " << f.matrix[2][2] << " }"
                << " } },\n";
        }
        out << "};\n\n";

        out << "static const ps1_transform_anim_key k_ps1_transform_anim_keys[] = {\n";
        for (const auto& key : data.transformAnimKeys) {
            out << "    { " << key.frame << "u, { "
                << ToFixed16(key.position[0]) << ", "
                << ToFixed16(key.position[1]) << ", "
                << ToFixed16(key.position[2]) << " }, { "
                << ToFixed16(key.rotation[0]) << ", "
                << ToFixed16(key.rotation[1]) << ", "
                << ToFixed16(key.rotation[2]) << " }, { "
                << ToFixed16(key.scale[0]) << ", "
                << ToFixed16(key.scale[1]) << ", "
                << ToFixed16(key.scale[2]) << " } },\n";
        }
        out << "};\n\n";

        out << "static const ps1_script_binding k_ps1_bindings[] = {\n";
        std::unordered_map<std::string, int> classToModule;
        for (size_t i = 0; i < data.moduleClassNames.size(); ++i)
            classToModule[data.moduleClassNames[i]] = static_cast<int>(i);

        for (size_t ei = 0; ei < data.entities.size(); ++ei) {
            const auto& e = data.entities[ei];
          for (const auto& script : e.scripts) {
            if (script.className.empty()) continue;
            auto it = classToModule.find(script.className);
            if (it == classToModule.end()) continue;
            out << "    { " << ei << "u, " << it->second << "u, "
                << static_cast<int>(script.fieldValuesQ16.size()) << "u, { ";
            for (size_t fi = 0; fi < script.fieldValuesQ16.size(); ++fi) {
                out << script.fieldValuesQ16[fi];
                if (fi + 1 < script.fieldValuesQ16.size()) out << ", ";
            }
            out << " } },\n";
          }
        }
        out << "};\n\n";

        out << "static const char k_ps1_ui_text[] = \""
            << EscapeCString(data.uiTextBlob) << "\";\n\n";

        out << "static const ps1_ui_element k_ps1_ui_elements[] = {\n";
        for (const auto& ui : data.uiElements) {
            out << "    { "
                << static_cast<int>(ui.kind) << ", "
                << ui.x << ", " << ui.y << ", " << ui.w << ", " << ui.h << ", "
                << static_cast<int>(ui.color[0]) << ", "
                << static_cast<int>(ui.color[1]) << ", "
                << static_cast<int>(ui.color[2]) << ", "
                << static_cast<int>(ui.color[3]) << ", "
                << static_cast<int>(ui.textureIndex) << ", "
                << static_cast<int>(ui.preserveAspect) << ", "
                << static_cast<int>(ui.alignment) << ", "
                << static_cast<int>(ui.fontSize) << ", "
                << ui.textOffset << "u, "
                << ui.textLength << "u, { "
                << static_cast<int>(ui.spectrumBackground[0]) << ", "
                << static_cast<int>(ui.spectrumBackground[1]) << ", "
                << static_cast<int>(ui.spectrumBackground[2]) << ", "
                << static_cast<int>(ui.spectrumBackground[3]) << " }, "
                << static_cast<int>(ui.spectrumBars) << ", "
                << static_cast<int>(ui.spectrumGap) << ", "
                << ui.spectrumSensitivityQ8 << "u, "
                << ui.rotationDegrees << " },\n";
        }
        out << "};\n\n";

        out << "static const ps1_ui_glyph k_ps1_ui_glyphs[] = {\n";
        for (const auto& glyph : data.uiGlyphs) {
            out << "    { 0x" << std::hex << glyph.codepoint << std::dec << "u, "
                << static_cast<int>(glyph.width) << ", "
                << static_cast<int>(glyph.height) << ", "
                << static_cast<int>(glyph.advance) << ", "
                << glyph.rowOffset << "u },\n";
        }
        out << "};\n\n";

        out << "static const uint16_t k_ps1_ui_glyph_rows[] = {\n";
        for (size_t i = 0; i < data.uiGlyphRows.size(); ++i) {
            if (i % 8 == 0)
                out << "    ";
            out << "0x" << std::hex << data.uiGlyphRows[i] << std::dec;
            if (i + 1 < data.uiGlyphRows.size())
                out << ", ";
            if (i % 8 == 7 || i + 1 == data.uiGlyphRows.size())
                out << "\n";
        }
        out << "};\n\n";

        out << "static const ps1_ui_button_rect k_ps1_ui_button_rects[] = {\n";
        for (const auto& rect : data.uiButtonRects) {
            out << "    { " << rect.x << ", " << rect.y << ", "
                << rect.w << ", " << rect.h << " },\n";
        }
        out << "};\n\n";

        out << "static const ps1_ui_button_group k_ps1_ui_button_groups[] = {\n";
        for (const auto& group : data.uiButtonGroups) {
            out << "    { "
                << static_cast<int>(group.selectedIndex) << ", "
                << static_cast<int>(group.wrapNavigation) << ", "
                << static_cast<int>(group.gamepadNavigation) << ", 0, "
                << group.buttonRectOffset << "u, "
                << static_cast<int>(group.buttonCount) << ", "
                << group.cursorOffsetX << ", "
                << group.cursorOffsetY << ", "
                << group.cursorW << ", "
                << group.cursorH << ", "
                << static_cast<int>(group.cursorTextureIndex) << " },\n";
        }
        out << "};\n\n";

        out << "const ps1_scene g_ps1_scene = {\n"
               "    k_ps1_entities,\n"
               "    " << data.entities.size() << "u,\n"
               "    " << data.cameraEntityIndex << ",\n"
               "    k_ps1_bindings,\n"
               "    " << data.bindingCount << "u,\n"
               "    { "
            << (data.hasLight ? 1 : 0) << ", "
            << data.lightDirXYZ12_4[0] << ", " << data.lightDirXYZ12_4[1] << ", " << data.lightDirXYZ12_4[2] << ", "
            << static_cast<int>(data.lightColor[0]) << ", "
            << static_cast<int>(data.lightColor[1]) << ", "
            << static_cast<int>(data.lightColor[2]) << ", "
            << static_cast<int>(data.ambientColor[0]) << ", "
            << static_cast<int>(data.ambientColor[1]) << ", "
            << static_cast<int>(data.ambientColor[2])
            << " },\n"
               "    " << (data.fogEnabled ? 1 : 0) << ", "
            << static_cast<int>(data.fogColor[0]) << ", "
            << static_cast<int>(data.fogColor[1]) << ", "
            << static_cast<int>(data.fogColor[2]) << ",\n"
               "    " << ToFixed16(data.fogStart) << ", "
            << ToFixed16(data.fogEnd) << ",\n"
               "    g_ps1_meshes,\n"
               "    " << data.meshPaths.size() << "u,\n"
               "    k_ps1_rigid_anim_frames,\n"
               "    " << data.rigidAnimFrames.size() << "u,\n"
               "    k_ps1_transform_anim_keys,\n"
               "    " << data.transformAnimKeys.size() << "u,\n"
               "    k_ps1_ui_elements,\n"
               "    " << data.uiElements.size() << "u,\n"
               "    k_ps1_ui_text,\n"
               "    " << data.uiTextBlob.size() << "u,\n"
               "    k_ps1_ui_glyphs,\n"
               "    " << data.uiGlyphs.size() << "u,\n"
               "    k_ps1_ui_glyph_rows,\n"
               "    " << data.uiGlyphRows.size() << "u,\n"
               "    k_ps1_ui_button_groups,\n"
               "    " << data.uiButtonGroups.size() << "u,\n"
               "    k_ps1_ui_button_rects,\n"
               "    " << data.uiButtonRects.size() << "u,\n"
               "    " << static_cast<int>(data.backgroundTextureIndex) << "u\n"
               "};\n";
        return out.good();
    } catch (const std::exception& ex) {
        outError = ex.what();
        return false;
    }
}

// Slightly below ONE (4096) to reduce GTE lighting MAC overflow (black sparkles).
static constexpr int kNorm12_4One = 3800;

static int16_t float_to_norm12_4(float v) {
    const float c = std::clamp(v, -1.0f, 1.0f);
    const int iv = static_cast<int>(std::round(c * static_cast<float>(kNorm12_4One)));
    return static_cast<int16_t>(std::clamp(iv, -kNorm12_4One, kNorm12_4One));
}

static void NormalizeNormals(std::vector<Vertex>& verts) {
    for (auto& v : verts) {
        const float len = std::sqrt(v.normal.x * v.normal.x + v.normal.y * v.normal.y + v.normal.z * v.normal.z);
        if (!(len > 1e-6f))
            v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        else
            v.normal /= len;
    }
}

static void ComputeTriNormal(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, glm::vec3& out) {
    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;
    out = glm::normalize(glm::cross(ab, ac));
    if (!std::isfinite(out.x) || !std::isfinite(out.y) || !std::isfinite(out.z))
        out = glm::vec3(0.0f, 1.0f, 0.0f);
}

/// Max |component| from origin (legacy; prefer bbox half for export scale).
static float MeshLocalHalfExtent(const std::vector<Vertex>& verts) {
    float half = 0.0f;
    for (const auto& v : verts) {
        half = std::max(half, std::abs(v.position.x));
        half = std::max(half, std::abs(v.position.y));
        half = std::max(half, std::abs(v.position.z));
    }
    return half;
}

/// Longest axis of the mesh AABB / 2 (matches cube template sizing).
static float MeshBBoxHalfExtent(const std::vector<Vertex>& verts) {
    if (verts.empty())
        return 0.0f;
    glm::vec3 bmin = verts[0].position;
    glm::vec3 bmax = verts[0].position;
    for (const auto& v : verts) {
        bmin = glm::min(bmin, v.position);
        bmax = glm::max(bmax, v.position);
    }
    const glm::vec3 ext = bmax - bmin;
    return 0.5f * std::max({ ext.x, ext.y, ext.z });
}

static uint64_t PackQ(int xi, int yi, int zi) {
    auto pack21 = [](int v) -> uint64_t {
        const uint64_t u = static_cast<uint64_t>(static_cast<uint32_t>(v) & 0x1FFFFFu);
        return u;
    };
    return (pack21(xi) << 42) | (pack21(yi) << 21) | pack21(zi);
}

struct PosUvWeldKey {
    uint64_t pos = 0;
    uint32_t uv = 0;
    bool operator==(const PosUvWeldKey& o) const { return pos == o.pos && uv == o.uv; }
};

struct PosUvWeldKeyHash {
    size_t operator()(const PosUvWeldKey& k) const {
        return std::hash<uint64_t>()(k.pos) ^ (std::hash<uint32_t>()(k.uv) << 1);
    }
};

/// FBX import keeps 3 unique verts per triangle; weld before decimation/export.
/// When `separateUvSeams` is true, only merge verts with the same position AND UV.
static void WeldMeshVerts(std::vector<Vertex>& verts, std::vector<uint32_t>& indices,
                          float eps = 1e-5f, bool separateUvSeams = true) {
    if (verts.empty() || indices.empty() || !(eps > 0.0f))
        return;

    const float inv = 1.0f / eps;
    std::unordered_map<PosUvWeldKey, uint32_t, PosUvWeldKeyHash> keyToIndex;
    keyToIndex.reserve(std::min<size_t>(verts.size(), 16384u));

    std::vector<Vertex> welded;
    welded.reserve(verts.size() / 3);
    std::vector<uint32_t> newIndices;
    newIndices.reserve(indices.size());

    auto makeKey = [&](const Vertex& v) -> PosUvWeldKey {
        PosUvWeldKey k;
        const int xi = static_cast<int>(std::lround(v.position.x * inv));
        const int yi = static_cast<int>(std::lround(v.position.y * inv));
        const int zi = static_cast<int>(std::lround(v.position.z * inv));
        k.pos = PackQ(xi, yi, zi);
        if (separateUvSeams) {
            const int ui = static_cast<int>(std::lround(Fract01(v.uv.x) * 4096.0f));
            const int vi = static_cast<int>(std::lround(Fract01(v.uv.y) * 4096.0f));
            k.uv = (static_cast<uint32_t>(ui & 0xFFFF) << 16) |
                   static_cast<uint32_t>(vi & 0xFFFF);
        }
        return k;
    };

    auto weldIndex = [&](uint32_t oldIdx) -> uint32_t {
        if (oldIdx >= verts.size())
            return UINT32_MAX;
        const PosUvWeldKey k = makeKey(verts[oldIdx]);
        auto it = keyToIndex.find(k);
        if (it != keyToIndex.end())
            return it->second;
        const uint32_t ni = static_cast<uint32_t>(welded.size());
        keyToIndex.emplace(k, ni);
        welded.push_back(verts[oldIdx]);
        return ni;
    };

    for (uint32_t oldIdx : indices) {
        const uint32_t ni = weldIndex(oldIdx);
        if (ni != UINT32_MAX)
            newIndices.push_back(ni);
    }

    verts = std::move(welded);
    indices = std::move(newIndices);
}

/// Sit mesh on local y=0 (bottom-center pivot). Avoids floor plane eating the base in OT.
static void AlignMeshBottom(std::vector<Vertex>& verts) {
    if (verts.empty())
        return;
    float minY = verts[0].position.y;
    for (const auto& v : verts)
        minY = std::min(minY, v.position.y);
    for (auto& v : verts)
        v.position.y -= minY;
}

/// Restore AABB size after decimation (stride can shrink the bbox).
static void RefitMeshHalfExtent(std::vector<Vertex>& verts, float targetHalf) {
    if (targetHalf < 1e-6f || verts.empty())
        return;
    const float current = MeshBBoxHalfExtent(verts);
    if (current < 1e-6f)
        return;
    const float s = targetHalf / current;
    if (std::abs(s - 1.0f) < 1e-5f)
        return;
    for (auto& v : verts)
        v.position *= s;
}

struct SimplifiedMesh {
    std::vector<Vertex> verts;
    std::vector<uint32_t> indices; // tri list
};

struct ClusterAccum {
    glm::vec3 posSum{ 0.0f };
    glm::vec3 nrmSum{ 0.0f };
    glm::vec2 uvSum{ 0.0f };
    glm::vec4 colorSum{ 0.0f };
    int count = 0;
};

static float TriangleArea(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    return glm::length(glm::cross(b - a, c - a)) * 0.5f;
}

static void CompactMeshVertices(SimplifiedMesh& mesh) {
    if (mesh.indices.empty())
        return;

    std::vector<uint32_t> remap(mesh.verts.size(), UINT32_MAX);
    std::vector<Vertex> compact;
    compact.reserve(mesh.verts.size());

    auto mapIndex = [&](uint32_t oldIdx) -> uint32_t {
        if (oldIdx >= mesh.verts.size())
            return UINT32_MAX;
        uint32_t& slot = remap[oldIdx];
        if (slot == UINT32_MAX) {
            slot = static_cast<uint32_t>(compact.size());
            compact.push_back(mesh.verts[oldIdx]);
        }
        return slot;
    };

    size_t write = 0;
    for (size_t i = 0; i < mesh.indices.size(); ++i) {
        const uint32_t ni = mapIndex(mesh.indices[i]);
        if (ni == UINT32_MAX)
            continue;
        mesh.indices[write++] = ni;
    }
    mesh.indices.resize(write);
    mesh.verts = std::move(compact);
}

static void CullDegenerateTriangles(SimplifiedMesh& mesh) {
    if (mesh.indices.size() < 3)
        return;

    std::vector<uint32_t> kept;
    kept.reserve(mesh.indices.size());
    for (size_t ti = 0; ti + 2 < mesh.indices.size(); ti += 3) {
        const uint32_t i0 = mesh.indices[ti + 0];
        const uint32_t i1 = mesh.indices[ti + 1];
        const uint32_t i2 = mesh.indices[ti + 2];
        if (i0 >= mesh.verts.size() || i1 >= mesh.verts.size() || i2 >= mesh.verts.size())
            continue;
        if (i0 == i1 || i1 == i2 || i0 == i2)
            continue;
        const glm::vec3& a = mesh.verts[i0].position;
        const glm::vec3& b = mesh.verts[i1].position;
        const glm::vec3& c = mesh.verts[i2].position;
        if (a == b || b == c || a == c)
            continue;
        const glm::vec3 ab = b - a;
        const glm::vec3 ac = c - a;
        if (glm::length(glm::cross(ab, ac)) < 1e-10f)
            continue;
        kept.push_back(i0);
        kept.push_back(i1);
        kept.push_back(i2);
    }
    mesh.indices = std::move(kept);
    CompactMeshVertices(mesh);
}

/// Keep the largest triangles by area (solid coverage on curved surfaces).
static SimplifiedMesh TopAreaDecimateTriangles(const std::vector<Vertex>& inVerts,
                                               const std::vector<uint32_t>& inIndices,
                                               size_t maxTris) {
    SimplifiedMesh out;
    const size_t triCount = inIndices.size() / 3;
    if (triCount == 0)
        return out;
    if (triCount <= maxTris) {
        out.verts = inVerts;
        out.indices = inIndices;
        return out;
    }

    std::vector<std::pair<float, size_t>> ranked;
    ranked.reserve(triCount);
    for (size_t ti = 0; ti < triCount; ++ti) {
        const uint32_t i0 = inIndices[ti * 3 + 0];
        const uint32_t i1 = inIndices[ti * 3 + 1];
        const uint32_t i2 = inIndices[ti * 3 + 2];
        if (i0 >= inVerts.size() || i1 >= inVerts.size() || i2 >= inVerts.size())
            continue;
        const float area = TriangleArea(inVerts[i0].position, inVerts[i1].position, inVerts[i2].position);
        if (area > 1e-12f)
            ranked.emplace_back(area, ti);
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    const size_t keep = std::min(maxTris, ranked.size());

    out.verts = inVerts;
    out.indices.reserve(keep * 3);
    for (size_t i = 0; i < keep; ++i) {
        const size_t ti = ranked[i].second;
        out.indices.push_back(inIndices[ti * 3 + 0]);
        out.indices.push_back(inIndices[ti * 3 + 1]);
        out.indices.push_back(inIndices[ti * 3 + 2]);
    }
    return out;
}

/// Keep the largest triangle per spatial cell so decimation covers the whole mesh.
/// Does NOT compact vertices — indices keep welded connectivity.
static SimplifiedMesh SpatialDecimateTriangles(const std::vector<Vertex>& inVerts,
                                               const std::vector<uint32_t>& inIndices,
                                               size_t maxTris) {
    SimplifiedMesh out;
    const size_t triCount = inIndices.size() / 3;
    if (triCount == 0)
        return out;
    if (triCount <= maxTris) {
        out.verts = inVerts;
        out.indices = inIndices;
        return out;
    }

    glm::vec3 bmin = inVerts[inIndices[0]].position;
    glm::vec3 bmax = bmin;
    for (uint32_t idx : inIndices) {
        if (idx >= inVerts.size())
            continue;
        bmin = glm::min(bmin, inVerts[idx].position);
        bmax = glm::max(bmax, inVerts[idx].position);
    }
    const glm::vec3 ext = glm::max(bmax - bmin, glm::vec3(1e-6f));
    const int grid = 28;

    struct TriPick {
        size_t ti;
        float area;
    };
    std::unordered_map<uint64_t, TriPick> bestPerCell;
    bestPerCell.reserve(maxTris * 2);

    std::vector<float> triArea(triCount, 0.0f);
    std::vector<std::pair<float, size_t>> ranked;
    ranked.reserve(triCount);

    for (size_t ti = 0; ti < triCount; ++ti) {
        const uint32_t i0 = inIndices[ti * 3 + 0];
        const uint32_t i1 = inIndices[ti * 3 + 1];
        const uint32_t i2 = inIndices[ti * 3 + 2];
        if (i0 >= inVerts.size() || i1 >= inVerts.size() || i2 >= inVerts.size())
            continue;
        const glm::vec3& a = inVerts[i0].position;
        const glm::vec3& b = inVerts[i1].position;
        const glm::vec3& c = inVerts[i2].position;
        const float area = TriangleArea(a, b, c);
        if (!(area > 1e-12f))
            continue;
        triArea[ti] = area;
        ranked.emplace_back(area, ti);

        const glm::vec3 cen = (a + b + c) * (1.0f / 3.0f);
        const int ix = std::clamp(static_cast<int>((cen.x - bmin.x) / ext.x * grid), 0, grid - 1);
        const int iy = std::clamp(static_cast<int>((cen.y - bmin.y) / ext.y * grid), 0, grid - 1);
        const int iz = std::clamp(static_cast<int>((cen.z - bmin.z) / ext.z * grid), 0, grid - 1);
        const uint64_t key = PackQ(ix, iy, iz);
        auto it = bestPerCell.find(key);
        if (it == bestPerCell.end() || area > it->second.area)
            bestPerCell[key] = { ti, area };
    }

    std::unordered_set<size_t> chosen;
    chosen.reserve(maxTris * 2);
    std::vector<size_t> picked;
    picked.reserve(maxTris);
    for (const auto& [k, pick] : bestPerCell) {
        (void)k;
        if (chosen.insert(pick.ti).second)
            picked.push_back(pick.ti);
    }

    if (picked.size() > maxTris) {
        std::sort(picked.begin(), picked.end(), [&](size_t a, size_t b) {
            return triArea[a] > triArea[b];
        });
        picked.resize(maxTris);
        chosen.clear();
        for (size_t ti : picked)
            chosen.insert(ti);
    }

    if (picked.size() < maxTris) {
        std::sort(ranked.begin(), ranked.end(),
                  [](const auto& a, const auto& b) { return a.first > b.first; });
        for (const auto& [area, ti] : ranked) {
            (void)area;
            if (chosen.size() >= maxTris)
                break;
            if (chosen.insert(ti).second)
                picked.push_back(ti);
        }
    }

    out.verts = inVerts;
    out.indices.reserve(picked.size() * 3);
    for (size_t ti : picked) {
        out.indices.push_back(inIndices[ti * 3 + 0]);
        out.indices.push_back(inIndices[ti * 3 + 1]);
        out.indices.push_back(inIndices[ti * 3 + 2]);
    }
    return out;
}

/// Legacy stride decimation (kept for fallback only).
static SimplifiedMesh StrideCullTriangles(const std::vector<Vertex>& inVerts,
                                          const std::vector<uint32_t>& inIndices,
                                          size_t maxTris) {
    SimplifiedMesh out;
    const size_t triCount = inIndices.size() / 3;
    if (triCount == 0)
        return out;
    if (triCount <= maxTris) {
        out.verts = inVerts;
        out.indices = inIndices;
        return out;
    }

    const size_t step = (triCount + maxTris - 1) / maxTris;
    out.verts = inVerts;
    out.indices.reserve(maxTris * 3);
    for (size_t ti = 0; ti < triCount && out.indices.size() / 3 < maxTris; ti += step) {
        const uint32_t i0 = inIndices[ti * 3 + 0];
        const uint32_t i1 = inIndices[ti * 3 + 1];
        const uint32_t i2 = inIndices[ti * 3 + 2];
        if (i0 >= inVerts.size() || i1 >= inVerts.size() || i2 >= inVerts.size())
            continue;
        out.indices.push_back(i0);
        out.indices.push_back(i1);
        out.indices.push_back(i2);
    }
    return out;
}

static bool BuildClusteredMesh(const std::vector<Vertex>& inVerts,
                               const std::vector<uint32_t>& inIndices,
                               float cellSize,
                               SimplifiedMesh& out) {
    if (inVerts.empty() || inIndices.size() < 3 || !(cellSize > 0.0f))
        return false;

    out.verts.clear();
    out.indices.clear();

    /* Finer Y cells avoid horizontal "shelf" artifacts from welding a model layer. */
    const float cellY = cellSize * 0.25f;
    const float invCellX = 1.0f / cellSize;
    const float invCellY = 1.0f / cellY;
    const float invCellZ = 1.0f / cellSize;
    auto quantKey = [&](const glm::vec3& p) -> uint64_t {
        const int xi = static_cast<int>(std::floor(p.x * invCellX + 0.5f));
        const int yi = static_cast<int>(std::floor(p.y * invCellY + 0.5f));
        const int zi = static_cast<int>(std::floor(p.z * invCellZ + 0.5f));
        return PackQ(xi, yi, zi);
    };

    std::unordered_map<uint64_t, ClusterAccum> clusters;
    clusters.reserve(std::min<size_t>(inVerts.size(), 8192u));

    struct TriKeys { uint64_t a, b, c; };
    std::vector<TriKeys> triKeys;
    triKeys.reserve(inIndices.size() / 3);

    auto accumulate = [&](uint32_t oldIndex) -> uint64_t {
        const Vertex& src = inVerts[oldIndex];
        const uint64_t k = quantKey(src.position);
        ClusterAccum& c = clusters[k];
        c.posSum += src.position;
        c.nrmSum += src.normal;
        c.uvSum += src.uv;
        c.colorSum += src.color;
        ++c.count;
        return k;
    };

    for (size_t ti = 0; ti + 2 < inIndices.size(); ti += 3) {
        const uint32_t i0 = inIndices[ti + 0];
        const uint32_t i1 = inIndices[ti + 1];
        const uint32_t i2 = inIndices[ti + 2];
        if (i0 >= inVerts.size() || i1 >= inVerts.size() || i2 >= inVerts.size())
            continue;
        triKeys.push_back({ accumulate(i0), accumulate(i1), accumulate(i2) });
    }

    if (triKeys.empty() || clusters.empty())
        return false;

    std::unordered_map<uint64_t, uint32_t> keyToIndex;
    keyToIndex.reserve(clusters.size());
    out.verts.reserve(clusters.size());

    for (const auto& [k, acc] : clusters) {
        if (acc.count <= 0)
            continue;
        const float inv = 1.0f / static_cast<float>(acc.count);
        Vertex v{};
        v.position = acc.posSum * inv;
        glm::vec3 n = acc.nrmSum * inv;
        const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        v.normal = (len > 1e-6f) ? (n / len) : glm::vec3(0.0f, 1.0f, 0.0f);
        v.uv = acc.uvSum * inv;
        v.color = acc.colorSum * inv;
        keyToIndex.emplace(k, static_cast<uint32_t>(out.verts.size()));
        out.verts.push_back(v);
    }

    out.indices.reserve(triKeys.size() * 3);
    std::unordered_set<uint64_t> emittedTriangles;
    emittedTriangles.reserve(std::min<size_t>(triKeys.size(), 8192u));
    for (const TriKeys& tk : triKeys) {
        auto ita = keyToIndex.find(tk.a);
        auto itb = keyToIndex.find(tk.b);
        auto itc = keyToIndex.find(tk.c);
        if (ita == keyToIndex.end() || itb == keyToIndex.end() || itc == keyToIndex.end())
            continue;
        const uint32_t a = ita->second;
        const uint32_t b = itb->second;
        const uint32_t c = itc->second;
        if (a == b || b == c || a == c)
            continue;
        uint32_t s0 = a;
        uint32_t s1 = b;
        uint32_t s2 = c;
        if (s1 < s0) std::swap(s0, s1);
        if (s2 < s1) std::swap(s1, s2);
        if (s1 < s0) std::swap(s0, s1);
        const uint64_t triangleKey =
            (static_cast<uint64_t>(s0 & 0x1fffff) << 42u) |
            (static_cast<uint64_t>(s1 & 0x1fffff) << 21u) |
            static_cast<uint64_t>(s2 & 0x1fffff);
        if (!emittedTriangles.insert(triangleKey).second)
            continue;
        out.indices.push_back(a);
        out.indices.push_back(b);
        out.indices.push_back(c);
    }

    return !out.verts.empty() && out.indices.size() >= 3;
}

bool EmitAudioDataC(const std::string& projectRoot, Ps1SceneExportResult& data,
                    const std::string& outCFile, std::string& outError) {
    data.audioClips.clear();
    std::unordered_map<std::string, uint8_t> clipIndex;
    size_t totalSpuBytes = 0;

    auto ensureClip = [&](const std::string& path, uint8_t& outIndex) -> bool {
        outIndex = 0;
        if (path.empty()) return true;
        auto found = clipIndex.find(path);
        if (found != clipIndex.end()) { outIndex = found->second; return true; }
        if (data.audioClips.size() >= 63) {
            outError = "PS1 audio supports at most 63 unique clips";
            return false;
        }
        const fs::path absolute = PathUtf8::FromString(projectRoot) /
                                  PathUtf8::FromString(path);
        Ps1VagData encoded;
        std::string encodeError;
        if (!EncodeAudioForPs1(PathUtf8::ToString(absolute), false,
                               encoded, encodeError)) {
            outError = "PS1 audio encode failed for '" + path + "': " + encodeError;
            return false;
        }
        encoded.frames.resize((encoded.frames.size() + 63u) & ~size_t(63u), 0);
        totalSpuBytes += encoded.frames.size();
        Ps1ExportedAudioClip clip;
        clip.path = path;
        clip.loop = false;
        clip.sampleRate = encoded.sampleRate;
        clip.data = std::move(encoded.frames);
        data.audioClips.push_back(std::move(clip));
        const uint8_t index = static_cast<uint8_t>(data.audioClips.size());
        clipIndex[path] = index;
        outIndex = index;
        return true;
    };

    for (auto& entity : data.entities) {
        if (entity.audioEnabled && !ensureClip(entity.audioClipPath, entity.audioClipIndex))
            return false;
      for (auto& script : entity.scripts) {
        for (size_t i = 0; i < script.assetFieldPaths.size(); ++i) {
            uint8_t index = 0;
            if (!ensureClip(script.assetFieldPaths[i], index))
                return false;
            if (index != 0 && i < script.fieldValuesQ16.size())
                script.fieldValuesQ16[i] = ToFixed16(static_cast<double>(index));
        }
      }
    }

    /* Audio is streamed through two 32 KiB SPU buffers at runtime, so clips
       no longer need to be downsampled to fit all at once in SPU RAM. */

    const fs::path cPath = PathUtf8::FromString(outCFile);
    if (cPath.has_parent_path()) fs::create_directories(cPath.parent_path());
    const fs::path ps1SourceDir = cPath.parent_path().parent_path();
    const fs::path audioDir = ps1SourceDir / "audio";
    fs::create_directories(audioDir);

    std::vector<std::string> cdFileNames;
    cdFileNames.reserve(data.audioClips.size());
    for (size_t i = 0; i < data.audioClips.size(); ++i) {
        std::ostringstream name;
        name << "AUD" << std::setfill('0') << std::setw(2) << i << ".VAG";
        cdFileNames.push_back(name.str());
        const fs::path audioPath = audioDir / name.str();
        std::ofstream audioOut(audioPath, std::ios::binary | std::ios::trunc);
        if (!audioOut.is_open()) {
            outError = "cannot create PS1 audio file: " + PathUtf8::ToString(audioPath);
            return false;
        }
        const auto& bytes = data.audioClips[i].data;
        audioOut.write(reinterpret_cast<const char*>(bytes.data()),
                       static_cast<std::streamsize>(bytes.size()));
        if (!audioOut.good()) {
            outError = "cannot write PS1 audio file: " + PathUtf8::ToString(audioPath);
            return false;
        }
    }

    if (!data.audioClips.empty()) {
        const fs::path isoPath = ps1SourceDir / "iso.xml";
        std::ifstream isoIn(isoPath, std::ios::binary);
        if (!isoIn.is_open()) {
            outError = "cannot open PS1 iso.xml: " + PathUtf8::ToString(isoPath);
            return false;
        }
        std::string isoText((std::istreambuf_iterator<char>(isoIn)),
                            std::istreambuf_iterator<char>());
        const std::string marker = "            <dummy sectors=\"1024\"/>";
        const size_t markerPos = isoText.find(marker);
        if (markerPos == std::string::npos) {
            outError = "cannot add audio files to PS1 iso.xml";
            return false;
        }
        std::ostringstream audioEntries;
        for (const std::string& name : cdFileNames) {
            audioEntries << "            <file name=\"" << name
                         << "\" type=\"data\" source=\"${PROJECT_SOURCE_DIR}/audio/"
                         << name << "\" />\n";
        }
        isoText.insert(markerPos, audioEntries.str());
        std::ofstream isoOut(isoPath, std::ios::binary | std::ios::trunc);
        if (!isoOut.is_open()) {
            outError = "cannot update PS1 iso.xml: " + PathUtf8::ToString(isoPath);
            return false;
        }
        isoOut.write(isoText.data(), static_cast<std::streamsize>(isoText.size()));
        if (!isoOut.good()) {
            outError = "cannot write PS1 iso.xml: " + PathUtf8::ToString(isoPath);
            return false;
        }
    }

    std::ofstream out(cPath, std::ios::trunc);
    if (!out.is_open()) { outError = "cannot open audio_data.c: " + outCFile; return false; }
    out << "/* Auto-generated PS1 CD audio metadata. */\n#include \"../runtime/audio.h\"\n\n";
    out << "const ps1_audio_clip g_ps1_audio_clips[] = {\n";
    if (data.audioClips.empty()) {
        out << "    { 0, 0u, 0u },\n";
    } else {
        for (size_t i = 0; i < data.audioClips.size(); ++i) {
            out << "    { \"\\\\" << cdFileNames[i] << "\", "
                << data.audioClips[i].data.size()
                << "u, " << data.audioClips[i].sampleRate << "u },\n";
        }
    }
    out << "};\nconst unsigned int g_ps1_audio_clip_count = "
        << data.audioClips.size() << "u;\n";
    MIPSYNC_INFO("PS1 audio export: {} streamed clip(s), {} KiB CD data",
                 data.audioClips.size(), totalSpuBytes / 1024u);
    return true;
}

static SimplifiedMesh ClusterDecimateToBudget(const std::vector<Vertex>& inVerts,
                                              const std::vector<uint32_t>& inIndices,
                                              size_t maxVerts,
                                              size_t maxTris) {
    SimplifiedMesh best;
    if (inVerts.empty() || inIndices.size() < 3 || maxVerts < 3 || maxTris == 0)
        return best;

    glm::vec3 bmin = inVerts[inIndices[0]].position;
    glm::vec3 bmax = bmin;
    for (uint32_t index : inIndices) {
        if (index >= inVerts.size())
            continue;
        bmin = glm::min(bmin, inVerts[index].position);
        bmax = glm::max(bmax, inVerts[index].position);
    }
    const glm::vec3 extent = glm::max(bmax - bmin, glm::vec3(1e-6f));
    const float longestAxis = std::max({ extent.x, extent.y, extent.z });

    for (int grid = 64; grid >= 2; --grid) {
        SimplifiedMesh trial;
        if (!BuildClusteredMesh(inVerts, inIndices, longestAxis / static_cast<float>(grid), trial))
            continue;
        CullDegenerateTriangles(trial);
        if (trial.indices.empty())
            continue;

        if ((trial.indices.size() / 3) > maxTris) {
            trial = SpatialDecimateTriangles(trial.verts, trial.indices, maxTris);
            CompactMeshVertices(trial);
            CullDegenerateTriangles(trial);
        }
        if (trial.verts.size() > maxVerts || (trial.indices.size() / 3) > maxTris)
            continue;

        if (trial.indices.size() > best.indices.size() ||
            (trial.indices.size() == best.indices.size() && trial.verts.size() > best.verts.size())) {
            best = std::move(trial);
        }
    }

    if (!best.indices.empty()) {
        MIPSYNC_INFO("ClusterDecimateToBudget: succeeded with {}v/{}t (budget {}v/{}t)",
                     best.verts.size(), best.indices.size() / 3, maxVerts, maxTris);
    }
    return best;
}

static bool MeshWithinBudget(const SimplifiedMesh& mesh, size_t maxVerts, size_t maxTris) {
    return mesh.verts.size() <= maxVerts && (mesh.indices.size() / 3) <= maxTris;
}

static SimplifiedMesh SelectTrianglesToBudget(const std::vector<Vertex>& inVerts,
                                              const std::vector<uint32_t>& inIndices,
                                              size_t maxVerts,
                                              size_t maxTris) {
    SimplifiedMesh out;
    if (inVerts.empty() || inIndices.size() < 3 || maxVerts < 3 || maxTris == 0)
        return out;

    struct RankedTri {
        float area = 0.0f;
        size_t tri = 0;
    };

    std::vector<RankedTri> ranked;
    ranked.reserve(inIndices.size() / 3);
    for (size_t ti = 0; ti + 2 < inIndices.size(); ti += 3) {
        const uint32_t i0 = inIndices[ti + 0];
        const uint32_t i1 = inIndices[ti + 1];
        const uint32_t i2 = inIndices[ti + 2];
        if (i0 >= inVerts.size() || i1 >= inVerts.size() || i2 >= inVerts.size())
            continue;
        if (i0 == i1 || i1 == i2 || i0 == i2)
            continue;
        const float area = TriangleArea(inVerts[i0].position, inVerts[i1].position, inVerts[i2].position);
        if (area > 1e-12f)
            ranked.push_back({ area, ti });
    }

    std::sort(ranked.begin(), ranked.end(),
              [](const RankedTri& a, const RankedTri& b) {
                  if (a.area == b.area)
                      return a.tri < b.tri;
                  return a.area > b.area;
              });

    std::unordered_map<uint32_t, uint32_t> remap;
    remap.reserve(maxVerts);
    out.verts.reserve(maxVerts);
    out.indices.reserve(maxTris * 3);

    auto canAddTriangle = [&](uint32_t i0, uint32_t i1, uint32_t i2) {
        size_t needed = 0;
        if (remap.find(i0) == remap.end()) ++needed;
        if (remap.find(i1) == remap.end()) ++needed;
        if (remap.find(i2) == remap.end()) ++needed;
        return out.verts.size() + needed <= maxVerts;
    };

    auto addIndex = [&](uint32_t oldIndex) {
        auto it = remap.find(oldIndex);
        if (it != remap.end()) {
            out.indices.push_back(it->second);
            return;
        }
        const uint32_t newIndex = static_cast<uint32_t>(out.verts.size());
        remap.emplace(oldIndex, newIndex);
        out.verts.push_back(inVerts[oldIndex]);
        out.indices.push_back(newIndex);
    };

    for (const RankedTri& tri : ranked) {
        if ((out.indices.size() / 3) >= maxTris)
            break;
        const uint32_t i0 = inIndices[tri.tri + 0];
        const uint32_t i1 = inIndices[tri.tri + 1];
        const uint32_t i2 = inIndices[tri.tri + 2];
        if (!canAddTriangle(i0, i1, i2))
            continue;
        addIndex(i0);
        addIndex(i1);
        addIndex(i2);
    }

    CullDegenerateTriangles(out);
    return out;
}

static SimplifiedMesh SpatialDecimateToBudgetPreserveUvs(const std::vector<Vertex>& inVerts,
                                                         const std::vector<uint32_t>& inIndices,
                                                         size_t maxVerts,
                                                         size_t maxTris) {
    size_t triBudget = maxTris;
    SimplifiedMesh best;

    while (triBudget >= 64) {
        SimplifiedMesh trial = SpatialDecimateTriangles(inVerts, inIndices, triBudget);
        CompactMeshVertices(trial);
        CullDegenerateTriangles(trial);
        if (!trial.indices.empty())
            best = trial;
        if (MeshWithinBudget(trial, maxVerts, maxTris)) {
            MIPSYNC_INFO("SpatialDecimateToBudgetPreserveUvs: succeeded with {}v/{}t",
                         trial.verts.size(), trial.indices.size() / 3);
            return trial;
        }

        const size_t nextBudget = std::max<size_t>(64, (triBudget * 9) / 10);
        if (nextBudget == triBudget)
            break;
        triBudget = nextBudget;
    }

    if (!best.indices.empty()) {
        MIPSYNC_WARN("SpatialDecimateToBudgetPreserveUvs: best result still over budget: {}v/{}t (budget {}v/{}t)",
                     best.verts.size(), best.indices.size() / 3, maxVerts, maxTris);
    }
    return best;
}

static size_t CountVerticesUsed(const SimplifiedMesh& mesh) {
    std::vector<uint8_t> used(mesh.verts.size(), 0);
    size_t count = 0;
    for (uint32_t idx : mesh.indices) {
        if (idx < used.size() && !used[idx]) {
            used[idx] = 1;
            ++count;
        }
    }
    return count;
}

static void FinalizeExportedMesh(SimplifiedMesh& mesh) {
    NormalizeNormals(mesh.verts);
    CullDegenerateTriangles(mesh);
}

/// Align winding with smoothed normals only when the whole mesh appears inverted.
/// Per-triangle fixes are unsafe for hard-surface FBX files with mirrored UVs or
/// mixed smoothing, because they can punch holes into otherwise closed parts.
static bool FixTriangleWindingForPs1(std::vector<Vertex>& verts, std::vector<uint32_t>& indices, bool forceFlip = false, bool useForceFlip = false) {
    size_t flipCount = 0;
    size_t totalCount = 0;

    for (size_t ti = 0; ti + 2 < indices.size(); ti += 3) {
        const uint32_t i0 = indices[ti + 0];
        const uint32_t i1 = indices[ti + 1];
        const uint32_t i2 = indices[ti + 2];
        if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size())
            continue;
        const glm::vec3& a = verts[i0].position;
        const glm::vec3& b = verts[i1].position;
        const glm::vec3& c = verts[i2].position;
        glm::vec3 faceN = glm::cross(b - a, c - a);
        const float flen = glm::length(faceN);
        if (!(flen > 1e-10f))
            continue;
        faceN /= flen;
        glm::vec3 smooth = verts[i0].normal + verts[i1].normal + verts[i2].normal;
        const float slen = glm::length(smooth);
        if (slen > 1e-6f) {
            smooth /= slen;
            totalCount++;
            if (glm::dot(faceN, smooth) < 0.0f)
                flipCount++;
        }
    }

    const bool globallyInverted = useForceFlip ? forceFlip : (totalCount > 0 && flipCount > (totalCount / 2));
    if (globallyInverted) {
        for (size_t ti = 0; ti + 2 < indices.size(); ti += 3)
            std::swap(indices[ti + 1], indices[ti + 2]);
    }

    MIPSYNC_INFO("FixTriangleWindingForPs1: {} mesh winding ({} / {} triangles opposed normals, total triangles {})",
                 globallyInverted ? "flipped global" : "kept",
                 flipCount, totalCount, indices.size() / 3);
    return globallyInverted;
}


static SimplifiedMesh SimplifyToBudget(const std::vector<Vertex>& inVerts,
                                      const std::vector<uint32_t>& inIndices,
                                      size_t maxVerts,
                                      size_t maxTris,
                                      float meshHalf,
                                      bool preserveUvSeams) {
    SimplifiedMesh full;
    full.verts = inVerts;
    full.indices = inIndices;
    if (MeshWithinBudget(full, maxVerts, maxTris))
        return full;

    const float maxSafeEps = meshHalf * 0.03f;

    // 1. Try progressive welding with separateUvSeams = true (preserve texture UV boundaries)
    float eps = meshHalf * 0.001f;
    for (int attempt = 0; attempt < 15; ++attempt) {
        if (eps > maxSafeEps)
            break;
        SimplifiedMesh trial = full;
        WeldMeshVerts(trial.verts, trial.indices, eps, true);
        CullDegenerateTriangles(trial);
        if (!trial.indices.empty() && MeshWithinBudget(trial, maxVerts, maxTris)) {
            MIPSYNC_INFO("SimplifyToBudget: succeeded with separateUvSeams=true, eps={:.5f}", eps);
            return trial;
        }
        eps *= 1.3f;
    }

    if (preserveUvSeams) {
        MIPSYNC_WARN("SimplifyToBudget: preserving UV seams; using spatial decimation instead of cross-UV welding");
        return SpatialDecimateToBudgetPreserveUvs(inVerts, inIndices, maxVerts, maxTris);
    }

    // 2. Fallback to progressive welding with separateUvSeams = false (allow merging across seams to fit budget)
    eps = meshHalf * 0.001f;
    for (int attempt = 0; attempt < 15; ++attempt) {
        if (eps > maxSafeEps)
            break;
        SimplifiedMesh trial = full;
        WeldMeshVerts(trial.verts, trial.indices, eps, false);
        CullDegenerateTriangles(trial);
        if (!trial.indices.empty() && MeshWithinBudget(trial, maxVerts, maxTris)) {
            MIPSYNC_INFO("SimplifyToBudget: succeeded with separateUvSeams=false, eps={:.5f}", eps);
            return trial;
        }
        eps *= 1.3f;
    }

    // 3. Last fallback: SpatialDecimateTriangles
    MIPSYNC_WARN("SimplifyToBudget: welding failed to reach budget with safe eps, falling back to SpatialDecimate");
    return SpatialDecimateTriangles(inVerts, inIndices, maxTris);
}

/// Helper to detect if a mesh is an open thin shell (contains boundary edges shared by only 1 triangle).
/// Welds vertices purely by position ignoring UV boundaries to check actual topology manifoldness.
static bool IsOpenShellMesh(const std::vector<Vertex>& verts, const std::vector<uint32_t>& indices) {
    if (indices.empty() || verts.empty())
        return false;

    // Weld vertices purely by position to build topological connectivity
    const float eps = 1e-4f;
    const float inv = 1.0f / eps;

    std::unordered_map<uint64_t, uint32_t> posToWeldedIdx;
    std::vector<uint32_t> weldedIndices;
    weldedIndices.reserve(indices.size());

    auto getWeldedIdx = [&](uint32_t origIdx) -> uint32_t {
        if (origIdx >= verts.size())
            return origIdx;
        const glm::vec3& p = verts[origIdx].position;
        const int xi = static_cast<int>(std::lround(p.x * inv));
        const int yi = static_cast<int>(std::lround(p.y * inv));
        const int zi = static_cast<int>(std::lround(p.z * inv));
        uint64_t key = PackQ(xi, yi, zi);
        auto it = posToWeldedIdx.find(key);
        if (it != posToWeldedIdx.end())
            return it->second;
        uint32_t newIdx = static_cast<uint32_t>(posToWeldedIdx.size());
        posToWeldedIdx[key] = newIdx;
        return newIdx;
    };

    for (uint32_t idx : indices) {
        weldedIndices.push_back(getWeldedIdx(idx));
    }

    std::unordered_map<uint64_t, int> edgeShareCount;
    edgeShareCount.reserve(weldedIndices.size());

    const size_t triCount = weldedIndices.size() / 3;
    for (size_t ti = 0; ti < triCount; ++ti) {
        uint32_t i0 = weldedIndices[ti * 3 + 0];
        uint32_t i1 = weldedIndices[ti * 3 + 1];
        uint32_t i2 = weldedIndices[ti * 3 + 2];

        if (i0 == i1 || i1 == i2 || i2 == i0)
            continue;

        auto addEdge = [&](uint32_t u, uint32_t v) {
            uint32_t mn = std::min(u, v);
            uint32_t mx = std::max(u, v);
            uint64_t key = (static_cast<uint64_t>(mn) << 32) | mx;
            edgeShareCount[key]++;
        };

        addEdge(i0, i1);
        addEdge(i1, i2);
        addEdge(i2, i0);
    }

    size_t boundaryCount = 0;
    for (const auto& [edge, count] : edgeShareCount) {
        if (count == 1) {
            boundaryCount++;
        }
    }

    if (edgeShareCount.empty())
        return false;

    const float ratio = static_cast<float>(boundaryCount) / static_cast<float>(edgeShareCount.size());
    MIPSYNC_INFO("Topological analysis (position-welded): boundary edges = {} / {}, ratio = {:.3f}", boundaryCount, edgeShareCount.size(), ratio);
    return boundaryCount >= 16 && ratio > 0.002f;
}

static bool IsHighQualityPs1Prop(const std::string& relPath) {
    std::string lower = relPath;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find("phonograph") != std::string::npos ||
           lower.find("phono") != std::string::npos;
}

/// Duplicate every triangle with reversed winding so both front and back faces
/// are rendered.  This is essential for thin shell geometry (e.g. phonograph horn)
/// where the inside would otherwise be culled by the PS1 GTE nclip test.
static void DuplicateTrianglesDoubleSided(std::vector<uint32_t>& indices) {
    const size_t origSize = indices.size();
    indices.reserve(origSize * 2);
    for (size_t ti = 0; ti + 2 < origSize; ti += 3) {
        // Append reversed winding copy (swap i1 and i2)
        indices.push_back(indices[ti + 0]);
        indices.push_back(indices[ti + 2]);
        indices.push_back(indices[ti + 1]);
    }
    MIPSYNC_INFO("DuplicateTrianglesDoubleSided: {} -> {} triangles",
                 origSize / 3, indices.size() / 3);
}

std::shared_ptr<Mesh> BuildPs1PreviewMeshInternal(const Mesh& sourceMesh, bool preserveUvSeams) {
    constexpr size_t kMaxTris  = 1800;
    constexpr size_t kMaxVerts = 2600;

    const auto& verts = sourceMesh.GetVertices();
    const auto& indices = sourceMesh.GetIndices();
    if (verts.empty() || indices.size() < 3)
        return nullptr;

    std::vector<Vertex> workVerts = verts;
    std::vector<uint32_t> workIndices = indices;
    AlignMeshBottom(workVerts);
    WeldMeshVerts(workVerts, workIndices, 1e-5f, true);
    NormalizeNormals(workVerts);

    const float sourceHalf = MeshBBoxHalfExtent(workVerts);
    if (sourceHalf < 1e-6f)
        return nullptr;

    const bool needsDoubleSided = IsOpenShellMesh(workVerts, workIndices);
    const size_t meshMaxTris  = needsDoubleSided ? 6500 : kMaxTris;
    const size_t meshMaxVerts = needsDoubleSided ? 6500 : kMaxVerts;
    const size_t simpMaxTris  = needsDoubleSided ? (meshMaxTris / 2) : meshMaxTris;

    const bool overBudget =
        (workVerts.size() > meshMaxVerts) || ((workIndices.size() / 3) > simpMaxTris);
    SimplifiedMesh simp = overBudget
        ? SimplifyToBudget(workVerts, workIndices, meshMaxVerts, simpMaxTris, sourceHalf,
                           preserveUvSeams)
        : SimplifiedMesh{ workVerts, workIndices };
    if (simp.verts.empty() || simp.indices.empty()) {
        simp = preserveUvSeams
            ? SpatialDecimateToBudgetPreserveUvs(workVerts, workIndices, meshMaxVerts, simpMaxTris)
            : SpatialDecimateTriangles(workVerts, workIndices, simpMaxTris);
    }
    if (simp.verts.empty() || simp.indices.empty())
        return nullptr;

    RefitMeshHalfExtent(simp.verts, sourceHalf);
    FinalizeExportedMesh(simp);
    FixTriangleWindingForPs1(simp.verts, simp.indices);
    if (needsDoubleSided)
        DuplicateTrianglesDoubleSided(simp.indices);
    if (simp.verts.empty() || simp.indices.empty())
        return nullptr;

    const float previewHalf = std::max(MeshBBoxHalfExtent(simp.verts), 1e-6f);
    const float toRuntimeWorld = 0.5f / previewHalf;
    for (auto& v : simp.verts)
        v.position *= toRuntimeWorld;

    return std::make_shared<Mesh>(simp.verts, simp.indices);
}

bool EmitMeshDataC(const std::string& projectRoot, Ps1SceneExportResult& data,
                   const std::string& outCFile, std::string& outError) {
    try {
        const fs::path cPath = PathUtf8::FromString(outCFile);
        if (cPath.has_parent_path())
            fs::create_directories(cPath.parent_path());

        std::ofstream out(cPath, std::ios::trunc);
        if (!out.is_open()) {
            outError = "cannot open mesh_data.c: " + outCFile;
            return false;
        }

        out << "/* Auto-generated by Mipsync PS1SceneExport. Do not edit. */\n"
               "#include \"../runtime/scene.h\"\n\n";

        // Ensure asset manager root is correct (Build PS1 may run from installs).
        // NOTE: SetProjectRoot() clears caches when changed, so keep it stable.
        if (AssetManager::Get().GetProjectRoot() != projectRoot)
            AssetManager::Get().SetProjectRoot(projectRoot);

        // PS1 custom meshes are fitted to +/-1024 to preserve vertex precision.
        constexpr float kPs1TemplateHalf = 1024.0f;
        constexpr float kPs1UnitsPerWorld = 64.0f;
        constexpr float kPs1Q12One = 4096.0f;
        // PS1-friendly budgets per mesh. When meshes exceed these, we automatically
        // simplify when over budget. FBX is welded by position before simplification.
        // Per-mesh export budget (u16 in PS1 runtime).
        const size_t kMaxTris  = 1800;
        const size_t kMaxVerts = 2600;

        // Map mesh path + render role to indices (1-based in entities; 0 = none).
        // A first-person view model can need a heavier/double-sided export, but
        // sharing that with normal world meshes makes the whole scene expensive.
        std::unordered_map<std::string, uint16_t> meshIndex;
        std::unordered_map<std::string, std::pair<uint16_t, uint16_t>> vertexAnimIndex;
        std::unordered_map<std::string, bool> skinnedAnimFlippedCache;
        data.meshPaths.clear();
        data.meshViewModelVariants.clear();
        data.meshOccluderVariants.clear();

        for (auto& e : data.entities) {
            if (e.meshKind != 4 || e.meshPath.empty()) continue;
            if (e.vertexAnimFrameCount > 0 && IsSkinnedAnimMeshKey(e.meshPath)) {
                const size_t hash = e.meshPath.find('#');
                const std::string baseKey = hash == std::string::npos ? e.meshPath : e.meshPath.substr(0, hash);
                const std::string roleKey = baseKey + (e.viewModel ? "\nvm" : "\nworld");
                auto existing = vertexAnimIndex.find(roleKey);
                if (existing != vertexAnimIndex.end()) {
                    e.meshIndex = existing->second.first;
                    e.vertexAnimFirstMeshIndex = existing->second.first;
                    e.vertexAnimFrameCount = existing->second.second;
                    continue;
                }

                const uint16_t firstIdx = static_cast<uint16_t>(data.meshPaths.size() + 1u);
                uint16_t added = 0;
                for (uint16_t frame = 0; frame < e.vertexAnimFrameCount; ++frame) {
                    const std::string frameKey = baseKey + "#" + std::to_string(frame);
                    data.meshPaths.push_back(frameKey);
                    data.meshViewModelVariants.push_back(e.viewModel);
                    data.meshOccluderVariants.push_back(e.prerenderOccluder);
                    ++added;
                }
                e.meshIndex = firstIdx;
                e.vertexAnimFirstMeshIndex = firstIdx;
                e.vertexAnimFrameCount = added;
                vertexAnimIndex[roleKey] = { firstIdx, added };
                continue;
            }
            const std::string key =
                e.meshPath + (e.viewModel ? "\nvm" :
                              (e.prerenderOccluder ? "\noccluder" : "\nworld"));
            auto it = meshIndex.find(key);
            if (it == meshIndex.end()) {
                const uint16_t idx = static_cast<uint16_t>(data.meshPaths.size() + 1u);
                data.meshPaths.push_back(e.meshPath);
                data.meshViewModelVariants.push_back(e.viewModel);
                data.meshOccluderVariants.push_back(e.prerenderOccluder);
                meshIndex[key] = idx;
                e.meshIndex = idx;
            } else {
                e.meshIndex = it->second;
            }
        }

        std::vector<int> meshScaleQ12(data.meshPaths.size(), 128);

        // Emit each mesh as a vertex array + tri list with face normals.
        for (size_t mi = 0; mi < data.meshPaths.size(); ++mi) {
            const std::string& rel = data.meshPaths[mi];
            const bool isTerrainMesh = IsTerrainMeshKey(rel);
            const bool isProModelerMesh = IsProModelerMeshKey(rel);
            const bool isSkinnedAnimMesh = IsSkinnedAnimMeshKey(rel);
            const bool isRigidBoneMesh = IsRigidBoneMeshKey(rel);
            const Mesh loadedMesh = isTerrainMesh
                ? MeshFromTerrainKey(rel, data)
                : (isProModelerMesh
                    ? MeshFromProModelerKey(rel, data)
                    : (isSkinnedAnimMesh
                        ? MeshFromSkinnedAnimKey(rel, data)
                        : (isRigidBoneMesh
                            ? MeshFromRigidBoneKey(rel, data)
                            : Mesh::LoadFromFileCpu(AssetManager::Get().ToAbsolute(rel)))));
            if (loadedMesh.GetVertexCount() == 0) continue;
            const auto& verts = loadedMesh.GetVertices();
            const auto& indices = loadedMesh.GetIndices();
            if (verts.empty() || indices.size() < 3) continue;

            std::vector<Vertex> workVerts = verts;
            std::vector<uint32_t> workIndices = indices;
            if (!isTerrainMesh && !isProModelerMesh && !isSkinnedAnimMesh && !isRigidBoneMesh)
                AlignMeshBottom(workVerts);
            const size_t vertsBeforeWeld = workVerts.size();
            if (!isSkinnedAnimMesh && !isRigidBoneMesh)
                WeldMeshVerts(workVerts, workIndices, 1e-5f, true);
            NormalizeNormals(workVerts);

            const float sourceHalf = MeshBBoxHalfExtent(workVerts);
            if (sourceHalf < 1e-6f) {
                MIPSYNC_WARN("PS1 mesh '{}' has zero extent; skipping", rel);
                continue;
            }
            meshScaleQ12[mi] = std::clamp(
                static_cast<int>(std::lround((sourceHalf * kPs1UnitsPerWorld / kPs1TemplateHalf) * kPs1Q12One)),
                1,
                32767);

            const bool usedAsViewModel =
                mi < data.meshViewModelVariants.size() && data.meshViewModelVariants[mi];
            const bool usedAsOccluder =
                mi < data.meshOccluderVariants.size() && data.meshOccluderVariants[mi];
            const bool highQualityProp = IsHighQualityPs1Prop(rel);
            size_t skinnedAnimMaxTris = 320;
            size_t skinnedAnimMaxVerts = 1000;
            if (isSkinnedAnimMesh)
                FindSkinnedAnimBudget(data, rel, skinnedAnimMaxTris, skinnedAnimMaxVerts);
            if (isRigidBoneMesh)
                FindRigidBoneBudget(data, rel, skinnedAnimMaxTris, skinnedAnimMaxVerts);
            const bool skinnedAnimDoubleSided = isSkinnedAnimMesh;
            const bool rigidBoneDoubleSided = false;
            // Determine if the mesh requires double-sided rendering (e.g. thin shells).
            // View models are often authored as thin/partial meshes; force a
            // reversed copy so close first-person weapons do not lose faces.
            const bool needsDoubleSided =
                !usedAsOccluder && !isTerrainMesh && !isSkinnedAnimMesh && !isRigidBoneMesh &&
                (usedAsViewModel || IsOpenShellMesh(workVerts, workIndices));

            // Dynamic budgets per mesh type to keep PS1 performance usable.
            // View models stay denser than world meshes, but cannot consume an
            // entire PS1 frame by themselves; double-sided meshes are budgeted
            // after considering the reversed winding copy.
            const size_t meshMaxTris =
                usedAsOccluder ? 192 :
                (isRigidBoneMesh ? skinnedAnimMaxTris :
                 (isTerrainMesh ? 1200 : (isSkinnedAnimMesh ? std::min<size_t>(skinnedAnimMaxTris * 2, 4000) : (usedAsViewModel ? 5000 : (highQualityProp ? 12000 : (needsDoubleSided ? 6500 : kMaxTris))))));
            const size_t meshMaxVerts =
                usedAsOccluder ? 320 :
                (isRigidBoneMesh ? skinnedAnimMaxVerts :
                 (isTerrainMesh ? 1300 : (isSkinnedAnimMesh ? skinnedAnimMaxVerts : (usedAsViewModel ? 5000 : (highQualityProp ? 8000 : (needsDoubleSided ? 6500 : kMaxVerts))))));
            const size_t simpMaxTris =
                (skinnedAnimDoubleSided || rigidBoneDoubleSided) ? skinnedAnimMaxTris : (needsDoubleSided ? (meshMaxTris / 2) : meshMaxTris);

            float meshTiling[2] = { 1.0f, 1.0f };
            float meshOffset[2] = { 0.0f, 0.0f };
            bool exportUvs = false;
            FindMeshUvTransform(data, rel, meshTiling, meshOffset, exportUvs);
            if (usedAsOccluder)
                exportUvs = false;

            const bool overBudget =
                (workVerts.size() > meshMaxVerts) || ((workIndices.size() / 3) > simpMaxTris);
            SimplifiedMesh simp;
            if (!overBudget) {
                simp = SimplifiedMesh{ workVerts, workIndices };
            } else {
                simp = SimplifyToBudget(
                    workVerts, workIndices, meshMaxVerts, simpMaxTris,
                    sourceHalf, exportUvs);
            }
            if (simp.verts.empty() || simp.indices.empty()) {
                MIPSYNC_WARN("PS1 mesh '{}' simplify failed; retrying spatial decimation", rel);
                simp = exportUvs
                    ? SpatialDecimateToBudgetPreserveUvs(workVerts, workIndices, meshMaxVerts, simpMaxTris)
                    : SpatialDecimateTriangles(workVerts, workIndices, simpMaxTris);
            }
            if ((simp.verts.empty() || simp.indices.empty() || !MeshWithinBudget(simp, meshMaxVerts, simpMaxTris)) &&
                isRigidBoneMesh) {
                MIPSYNC_WARN("PS1 mesh '{}' using clustered rigid-mesh fallback", rel);
                simp = ClusterDecimateToBudget(workVerts, workIndices, meshMaxVerts, simpMaxTris);
            }
            if ((simp.verts.empty() || simp.indices.empty() || !MeshWithinBudget(simp, meshMaxVerts, simpMaxTris)) &&
                (isSkinnedAnimMesh || isRigidBoneMesh)) {
                MIPSYNC_WARN("PS1 mesh '{}' using strict triangle-budget fallback", rel);
                simp = SelectTrianglesToBudget(workVerts, workIndices, meshMaxVerts, simpMaxTris);
            }
            if (simp.verts.empty() || simp.indices.empty()) {
                MIPSYNC_ERROR("PS1 mesh '{}' could not be decimated; skipped", rel);
                continue;
            }

            RefitMeshHalfExtent(simp.verts, sourceHalf);
            FinalizeExportedMesh(simp);
            bool forceFlip = false;
            bool useForceFlip = false;
            std::string animBaseKey;
            if (isSkinnedAnimMesh) {
                const size_t hash = rel.find('#');
                animBaseKey = hash == std::string::npos ? rel : rel.substr(0, hash);
                auto it = skinnedAnimFlippedCache.find(animBaseKey);
                if (it != skinnedAnimFlippedCache.end()) {
                    forceFlip = it->second;
                    useForceFlip = true;
                }
            }

            const bool globallyInverted = FixTriangleWindingForPs1(simp.verts, simp.indices, forceFlip, useForceFlip);

            if (isSkinnedAnimMesh && !useForceFlip) {
                skinnedAnimFlippedCache[animBaseKey] = globallyInverted;
            }
            if (needsDoubleSided || skinnedAnimDoubleSided || rigidBoneDoubleSided) {
                DuplicateTrianglesDoubleSided(simp.indices);
            }
            if (simp.verts.empty() || simp.indices.empty()) {
                MIPSYNC_ERROR("PS1 mesh '{}' has no valid triangles after cull; skipped", rel);
                continue;
            }

            // Fit longest AABB axis to +/-64 PSX template (cube contract).
            const float exportHalf = std::max(MeshBBoxHalfExtent(simp.verts), 1e-6f);
            const float toTemplate = kPs1TemplateHalf / exportHalf;

            const size_t vcount = std::min(simp.verts.size(), meshMaxVerts);
            const size_t tcount = std::min(simp.indices.size() / 3, meshMaxTris);
            if (vcount == 0 || tcount == 0) {
                MIPSYNC_ERROR("PS1 mesh '{}' has no triangles after decimation; skipped", rel);
                continue;
            }

            if (overBudget || workVerts.size() != vertsBeforeWeld) {
                MIPSYNC_INFO("PS1 mesh '{}' export: {}v/{}t -> welded {}v/{}t -> {}v/{}t (budget {}v/{}t)",
                            rel,
                            verts.size(), (indices.size() / 3),
                            workVerts.size(), (workIndices.size() / 3),
                            vcount, tcount,
                            meshMaxVerts, meshMaxTris);
            }

            out << "static const SVECTOR k_ps1_mesh_" << mi << "_verts[] = {\n";
            int clamped = 0;
            for (size_t vi = 0; vi < vcount; ++vi) {
                const glm::vec3& p = simp.verts[vi].position;
                const int rawx = static_cast<int>(std::round(p.x * toTemplate));
                const int rawy = static_cast<int>(std::round(p.y * toTemplate));
                const int rawz = static_cast<int>(std::round(p.z * toTemplate));
                const int vx = ClampI16(rawx);
                const int vy = ClampI16(rawy);
                const int vz = ClampI16(rawz);
                if (vx != rawx || vy != rawy || vz != rawz) ++clamped;
                out << "    { " << vx << ", " << vy << ", " << vz << ", 0 },\n";
            }
            out << "};\n\n";
            if (clamped > 0) {
                MIPSYNC_WARN("PS1 mesh '{}' SVECTOR clamped {} / {} vertices (sourceHalf={:.4f})",
                             rel, clamped, vcount, sourceHalf);
            }

            if (isRigidBoneMesh) {
                std::unordered_map<uint64_t, uint16_t> firstPositionIndex;
                firstPositionIndex.reserve(vcount);
                out << "static const uint16_t k_ps1_mesh_" << mi << "_position_refs[] = {\n";
                for (size_t vi = 0; vi < vcount; ++vi) {
                    const glm::vec3& p = simp.verts[vi].position;
                    const int vx = ClampI16(static_cast<int>(std::round(p.x * toTemplate)));
                    const int vy = ClampI16(static_cast<int>(std::round(p.y * toTemplate)));
                    const int vz = ClampI16(static_cast<int>(std::round(p.z * toTemplate)));
                    const uint64_t key =
                        static_cast<uint64_t>(static_cast<uint16_t>(vx)) |
                        (static_cast<uint64_t>(static_cast<uint16_t>(vy)) << 16u) |
                        (static_cast<uint64_t>(static_cast<uint16_t>(vz)) << 32u);
                    const auto [it, inserted] =
                        firstPositionIndex.emplace(key, static_cast<uint16_t>(vi));
                    out << "    " << it->second << "u,\n";
                }
                out << "};\n\n";
            }

            // Vertex normals (12.4 fixed; ONE=4096). Keep in mesh local space.
            out << "static const SVECTOR k_ps1_mesh_" << mi << "_norms[] = {\n";
            for (size_t vi = 0; vi < vcount; ++vi) {
                glm::vec3 n = simp.verts[vi].normal;
                const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
                if (!(len > 1e-6f)) n = glm::vec3(0.0f, 1.0f, 0.0f);
                else n /= len;
                const int nx = float_to_norm12_4(n.x);
                const int ny = float_to_norm12_4(n.y);
                const int nz = float_to_norm12_4(n.z);
                out << "    { " << nx << ", " << ny << ", " << nz << ", 0 },\n";
            }
            out << "};\n\n";

            if (exportUvs) {
                int texWidth = 128;
                int texHeight = 128;

                std::string texPath;
                for (const auto& e : data.entities) {
                    if (e.meshKind == 4 && e.textureIndex > 0 &&
                        (e.meshPath == rel || SameSkinnedAnimSequence(e.meshPath, rel))) {
                        if (e.textureIndex <= data.texturePaths.size()) {
                            texPath = data.texturePaths[e.textureIndex - 1];
                        }
                        break;
                    }
                }

                if (!texPath.empty()) {
                    std::string absPath = AssetManager::Get().ToAbsolute(texPath);
                    int w = 0, h = 0, comp = 0;
                    if (stbi_info(absPath.c_str(), &w, &h, &comp)) {
                        // Clamp to max size 128 (same as PS1TextureExport)
                        if (w > 128 || h > 128) {
                            float scale = 128.0f / static_cast<float>(std::max(w, h));
                            w = std::max(1, static_cast<int>(std::lround(static_cast<float>(w) * scale)));
                            h = std::max(1, static_cast<int>(std::lround(static_cast<float>(h) * scale)));
                        }
                        // Round even up
                        w = (w < 2) ? 2 : ((w + 1) & ~1);
                        h = (h < 2) ? 2 : ((h + 1) & ~1);
                        texWidth = w;
                        texHeight = h;
                    } else {
                        // Fallback if stbi_info fails
                        auto texPtr = AssetManager::Get().GetTexture(texPath);
                        if (texPtr) {
                            int w2 = texPtr->GetWidth();
                            int h2 = texPtr->GetHeight();
                            if (w2 > 128 || h2 > 128) {
                                float scale = 128.0f / static_cast<float>(std::max(w2, h2));
                                w2 = std::max(1, static_cast<int>(std::lround(static_cast<float>(w2) * scale)));
                                h2 = std::max(1, static_cast<int>(std::lround(static_cast<float>(h2) * scale)));
                            }
                            w2 = (w2 < 2) ? 2 : ((w2 + 1) & ~1);
                            h2 = (h2 < 2) ? 2 : ((h2 + 1) & ~1);
                            texWidth = w2;
                            texHeight = h2;
                        }
                    }
                }

                out << "static const ps1_uv8 k_ps1_mesh_" << mi << "_uvs[] = {\n";
                for (size_t vi = 0; vi < vcount; ++vi) {
                    const glm::vec2& uv = simp.verts[vi].uv;
                    const float fu = uv.x * meshTiling[0] + meshOffset[0];
                    const float fv = uv.y * meshTiling[1] + meshOffset[1];
                    out << "    { " << static_cast<int>(UvToPsxU(fu, texWidth)) << ", "
                        << static_cast<int>(UvToPsxV(fv, texHeight)) << " },\n";
                }
                out << "};\n\n";
            }

            // The first half of simp.indices are original triangles; the second
            // half are reversed-winding copies from DuplicateTrianglesDoubleSided.
            // Both copies must use the OUTWARD-facing normal for GTE lighting.
            // The reversed copies' cross product points inward, so we negate it.
            float terrainMinY = 0.0f;
            float terrainMaxY = 0.0f;
            if (isTerrainMesh && !simp.verts.empty()) {
                terrainMinY = terrainMaxY = simp.verts[0].position.y;
                for (const auto& v : simp.verts) {
                    terrainMinY = std::min(terrainMinY, v.position.y);
                    terrainMaxY = std::max(terrainMaxY, v.position.y);
                }
            }
            out << "static const ps1_mesh_tri k_ps1_mesh_" << mi << "_tris[] = {\n";
            for (size_t ti = 0; ti < tcount; ++ti) {
                const uint32_t i0 = simp.indices[ti * 3 + 0];
                const uint32_t i1 = simp.indices[ti * 3 + 1];
                const uint32_t i2 = simp.indices[ti * 3 + 2];
                if (i0 >= vcount || i1 >= vcount || i2 >= vcount) continue;
                glm::vec3 n;
                const glm::vec3 a = simp.verts[i0].position * toTemplate;
                const glm::vec3 b = simp.verts[i1].position * toTemplate;
                const glm::vec3 c = simp.verts[i2].position * toTemplate;
                ComputeTriNormal(a, b, c, n);
                const glm::vec3 center = (simp.verts[i0].position + simp.verts[i1].position + simp.verts[i2].position) / 3.0f;
                const uint8_t shade =
                    isTerrainMesh ? TerrainTriShade(n, center, terrainMinY, terrainMaxY) :
                    (isRigidBoneMesh ? RigidLightDirectionBin(n) : 128);
                const glm::vec4 triColor = isTerrainMesh
                    ? (simp.verts[i0].color + simp.verts[i1].color + simp.verts[i2].color) / 3.0f
                    : glm::vec4(1.0f);
                // The cross product of the swapped vertices for reversed-winding copies
                // naturally points inward, which is correct for interior lighting.
                out << "    { " << i0 << "u, " << i1 << "u, " << i2 << "u, "
                    << float_to_norm12_4(n.x) << ", "
                    << float_to_norm12_4(n.y) << ", "
                    << float_to_norm12_4(n.z) << ", "
                    << static_cast<int>(shade) << ", "
                    << static_cast<int>(ToByte01(triColor.r)) << ", "
                    << static_cast<int>(ToByte01(triColor.g)) << ", "
                    << static_cast<int>(ToByte01(triColor.b)) << " },\n";
            }
            out << "};\n\n";
        }

        out << "const ps1_mesh g_ps1_meshes[] = {\n";
        for (size_t mi = 0; mi < data.meshPaths.size(); ++mi) {
            const std::string& rel = data.meshPaths[mi];
            float tiling[2] = { 1.0f, 1.0f };
            float offset[2] = { 0.0f, 0.0f };
            bool exportUvs = false;
            FindMeshUvTransform(data, rel, tiling, offset, exportUvs);
            const bool isTerrainMesh = IsTerrainMeshKey(rel);
            const bool isSmoothMesh = rel.rfind("primitivesphere://", 0) == 0;
            std::string meshFlags = "0";
            if (isTerrainMesh)
                meshFlags = "PS1_MESH_FLAG_TERRAIN";
            if (isSmoothMesh)
                meshFlags = meshFlags == "0" ? "PS1_MESH_FLAG_SMOOTH"
                                             : meshFlags + " | PS1_MESH_FLAG_SMOOTH";
            out << "    { k_ps1_mesh_" << mi << "_verts, "
                << " k_ps1_mesh_" << mi << "_norms, "
                << " (uint16_t)(sizeof(k_ps1_mesh_" << mi << "_verts)/sizeof(k_ps1_mesh_" << mi << "_verts[0])), "
                << (exportUvs ? ("k_ps1_mesh_" + std::to_string(mi) + "_uvs") : std::string("0"))
                << ", "
                << (IsRigidBoneMeshKey(rel) ? ("k_ps1_mesh_" + std::to_string(mi) + "_position_refs") : std::string("0"))
                << ", "
                << " k_ps1_mesh_" << mi << "_tris, "
                << " (uint16_t)(sizeof(k_ps1_mesh_" << mi << "_tris)/sizeof(k_ps1_mesh_" << mi << "_tris[0])), "
                << meshScaleQ12[mi] << ", "
                << meshFlags << " },\n";
        }
        out << "};\n";
        out << "const unsigned int g_ps1_mesh_count = " << data.meshPaths.size() << "u;\n";
        return out.good();
    } catch (const std::exception& ex) {
        outError = ex.what();
        return false;
    }
}

} // namespace

std::shared_ptr<Mesh> BuildPs1PreviewMesh(const Mesh& sourceMesh, bool preserveUvSeams) {
    return BuildPs1PreviewMeshInternal(sourceMesh, preserveUvSeams);
}

std::shared_ptr<MipsyncEngine::SkinnedMesh> BuildPs1RigidBonePreview(
    const MipsyncEngine::SkeletalModelAsset& model, int meshPartIndex, bool seamFill,
    int targetTris, int targetVerts) {
    
    if (model.sourceVertices.empty() || model.sourceIndices.empty() || model.bones.empty())
        return nullptr;

    uint32_t indexOffset = 0;
    uint32_t indexCount = static_cast<uint32_t>(model.sourceIndices.size());
    if (meshPartIndex >= 0 && static_cast<size_t>(meshPartIndex) < model.meshParts.size()) {
        const auto& part = model.meshParts[static_cast<size_t>(meshPartIndex)];
        indexOffset = part.indexOffset;
        indexCount = part.indexCount;
    }
    if (indexOffset >= model.sourceIndices.size())
        return nullptr;
    indexCount = std::min<uint32_t>(indexCount, static_cast<uint32_t>(model.sourceIndices.size() - indexOffset));
    if (indexCount < 3)
        return nullptr;

    const int maxBoneIndex = static_cast<int>(std::min<size_t>(model.bones.size(), kMaxBones)) - 1;
    if (maxBoneIndex < 0)
        return nullptr;

    RigidSplitGeometry splitGeometry =
        BuildRigidSplitGeometry(model, indexOffset, indexCount, maxBoneIndex, seamFill);
    std::unordered_map<int, std::vector<uint32_t>> initialTriangles =
        std::move(splitGeometry.triangles);

    struct TempBoneCount {
        int bone = 0;
        size_t triCount = 0;
    };
    std::vector<TempBoneCount> activeBones;
    for (auto& [b, tris] : initialTriangles) {
        if (!tris.empty()) {
            activeBones.push_back({ b, tris.size() / 3 });
        }
    }
    std::sort(activeBones.begin(), activeBones.end(), [](const TempBoneCount& a, const TempBoneCount& b) {
        return a.triCount > b.triCount;
    });

    // Keep this in sync with RegisterRigidBonePartsFromJson(). The editor
    // preview and the exported PS1 character must make the same bone cuts.
    const size_t maxRigidPartsPerSkinnedMesh = 20;
    std::unordered_set<int> keptBones;
    auto lowerBoneName = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return text;
    };
    auto contains = [](const std::string& text, const char* needle) {
        return text.find(needle) != std::string::npos;
    };
    auto rigidBonePriority = [&](int bone) {
        if (bone < 0 || static_cast<size_t>(bone) >= model.bones.size())
            return 0;
        const std::string name = lowerBoneName(model.bones[static_cast<size_t>(bone)].name);
        if (contains(name, "thumb") || contains(name, "index") || contains(name, "middle") ||
            contains(name, "ring") || contains(name, "pinky"))
            return 0;
        if (contains(name, "leftarm") || contains(name, "rightarm"))
            return 9000;
        if (contains(name, "forearm") || contains(name, "lowerarm"))
            return 8900;
        if (contains(name, "lefthand") || contains(name, "righthand") ||
            contains(name, "wrist"))
            return 8800;
        if (contains(name, "hips") || contains(name, "pelvis"))
            return 8600;
        if (contains(name, "spine"))
            return 8500;
        if (contains(name, "neck") || contains(name, "head"))
            return 8400;
        if (contains(name, "upleg") || contains(name, "thigh"))
            return 8200;
        if (contains(name, "leftleg") || contains(name, "rightleg") ||
            contains(name, "calf") || contains(name, "shin"))
            return 8100;
        if (contains(name, "foot"))
            return 8000;
        if (contains(name, "shoulder"))
            return 7600;
        return 0;
    };
    std::vector<TempBoneCount> selectedBones = activeBones;
    std::sort(selectedBones.begin(), selectedBones.end(), [&](const TempBoneCount& a, const TempBoneCount& b) {
        const int pa = rigidBonePriority(a.bone);
        const int pb = rigidBonePriority(b.bone);
        if (pa != pb)
            return pa > pb;
        return a.triCount > b.triCount;
    });
    const size_t numKept = std::min<size_t>(selectedBones.size(), maxRigidPartsPerSkinnedMesh);
    for (size_t i = 0; i < numKept; ++i) {
        keptBones.insert(selectedBones[i].bone);
    }

    std::unordered_map<int, int> boneRedirect;
    for (int b = 0; b <= maxBoneIndex; ++b) {
        if (keptBones.count(b)) {
            boneRedirect[b] = b;
        } else {
            int curr = b;
            int redirect = -1;
            while (curr >= 0) {
                int parent = GetParentBoneIndex(model, curr);
                if (parent < 0)
                    break;
                if (keptBones.count(parent)) {
                    redirect = parent;
                    break;
                }
                curr = parent;
            }
            if (redirect < 0) {
                redirect = activeBones.empty() ? 0 : activeBones[0].bone;
            }
            boneRedirect[b] = redirect;
        }
    }

    struct BoneGroup {
        int bone = 0;
        uint32_t triCount = 0;
        uint16_t targetTris = 64;
        uint16_t targetVerts = 192;
        std::vector<uint32_t> indices;
    };
    std::unordered_map<int, BoneGroup> groups;
    for (auto& [b, tris] : initialTriangles) {
        if (tris.empty())
            continue;
        int redirect = boneRedirect[b];
        BoneGroup& group = groups[redirect];
        group.bone = redirect;
        group.triCount += static_cast<uint32_t>(tris.size() / 3);
        group.indices.insert(group.indices.end(), tris.begin(), tris.end());
    }

    std::vector<BoneGroup> ordered;
    ordered.reserve(groups.size());
    for (auto& [_, group] : groups) {
        if (group.triCount >= 1)
            ordered.push_back(std::move(group));
    }
    std::sort(ordered.begin(), ordered.end(), [](const BoneGroup& a, const BoneGroup& b) {
        return a.triCount > b.triCount;
    });

    if (ordered.empty())
        return nullptr;

    const size_t totalSourceTris = std::accumulate(
        ordered.begin(), ordered.end(), size_t{0},
        [](size_t sum, const BoneGroup& group) { return sum + static_cast<size_t>(group.triCount); });
    const size_t originalSourceTris = indexCount / 3u;
    const bool preserveLowPolyRigidMesh =
        totalSourceTris <= 1000u ||
        (originalSourceTris <= 1200u && totalSourceTris <= 1600u);
    const size_t performanceBudget = std::clamp<size_t>(
        std::min<size_t>(static_cast<size_t>(std::max(targetTris, 32)),
                         std::max<size_t>(originalSourceTris, 480u)),
        480u, 1000u);
    const size_t rigidTotalTargetTris = preserveLowPolyRigidMesh
        ? totalSourceTris
        : performanceBudget;

    for (BoneGroup& group : ordered) {
        size_t partTargetTris = group.triCount;
        if (!preserveLowPolyRigidMesh) {
            const double weight = totalSourceTris > 0
                ? static_cast<double>(group.triCount) / static_cast<double>(totalSourceTris)
                : 1.0 / static_cast<double>(ordered.size());
            const size_t weightedTarget = static_cast<size_t>(
                std::lround(static_cast<double>(rigidTotalTargetTris) * weight));
            partTargetTris = std::min<size_t>(
                group.triCount,
                std::clamp<size_t>(weightedTarget, 8, 320));
        }
        group.targetTris = static_cast<uint16_t>(partTargetTris);
        group.targetVerts = static_cast<uint16_t>(std::clamp<size_t>(partTargetTris * 3, 36, 2048));
    }

    std::vector<SkinnedVertex> finalVertices;
    std::vector<uint32_t> finalIndices;

    for (const BoneGroup& group : ordered) {
        std::vector<Vertex> localVertices;
        std::vector<uint32_t> localIndices;
        std::unordered_map<uint32_t, uint32_t> remap;

        for (uint32_t oldIndex : group.indices) {
            auto remapIt = remap.find(oldIndex);
            if (remapIt != remap.end()) {
                localIndices.push_back(remapIt->second);
                continue;
            }
            const SkinnedVertex& sv = splitGeometry.vertices[oldIndex];
            const uint32_t newIndex = static_cast<uint32_t>(localVertices.size());
            remap[oldIndex] = newIndex;
            localIndices.push_back(newIndex);

            Vertex v;
            v.position = sv.position;
            v.normal = sv.normal;
            v.uv = sv.uv;
            v.color = sv.color;
            localVertices.push_back(v);
        }

        if (localVertices.empty() || localIndices.size() < 3)
            continue;

        // Simplify to budget
        SimplifiedMesh simp;
        const float sourceHalf = std::max(MeshBBoxHalfExtent(localVertices), 1e-6f);
        const size_t meshMaxTris = group.targetTris;
        const size_t meshMaxVerts = group.targetVerts;

        simp = SimplifyToBudget(localVertices, localIndices, meshMaxVerts, meshMaxTris, sourceHalf, true);
        if (simp.verts.empty() || simp.indices.empty()) {
            simp = SpatialDecimateToBudgetPreserveUvs(localVertices, localIndices, meshMaxVerts, meshMaxTris);
        }
        if (simp.verts.empty() || simp.indices.empty() || !MeshWithinBudget(simp, meshMaxVerts, meshMaxTris)) {
            simp = ClusterDecimateToBudget(localVertices, localIndices, meshMaxVerts, meshMaxTris);
        }
        if (simp.verts.empty() || simp.indices.empty() || !MeshWithinBudget(simp, meshMaxVerts, meshMaxTris)) {
            simp = SelectTrianglesToBudget(localVertices, localIndices, meshMaxVerts, meshMaxTris);
        }
        if (simp.verts.empty() || simp.indices.empty())
            continue;

        RefitMeshHalfExtent(simp.verts, sourceHalf);
        FinalizeExportedMesh(simp);
        FixTriangleWindingForPs1(simp.verts, simp.indices);

        if (seamFill && !simp.verts.empty()) {
            glm::vec3 partMin = simp.verts.front().position;
            glm::vec3 partMax = partMin;
            for (const Vertex& vertex : simp.verts) {
                partMin = glm::min(partMin, vertex.position);
                partMax = glm::max(partMax, vertex.position);
            }
            const glm::vec3 partCenter = (partMin + partMax) * 0.5f;
            for (Vertex& vertex : simp.verts)
                vertex.position = partCenter + (vertex.position - partCenter) * 1.025f;
        }

        uint32_t vertexOffset = static_cast<uint32_t>(finalVertices.size());
        for (uint32_t idx : simp.indices) {
            finalIndices.push_back(idx + vertexOffset);
        }

        for (const auto& v : simp.verts) {
            SkinnedVertex sv;
            sv.position = v.position;
            sv.normal = v.normal;
            sv.uv = v.uv;
            sv.color = v.color;
            sv.boneIndices = glm::vec4(static_cast<float>(group.bone), 0.0f, 0.0f, 0.0f);
            sv.boneWeights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            finalVertices.push_back(sv);
        }
    }

    if (finalVertices.empty() || finalIndices.size() < 3)
        return nullptr;

    return std::make_shared<MipsyncEngine::SkinnedMesh>(finalVertices, finalIndices);
}

bool ExportPs1SceneAndScripts(const std::string& projectRoot,
                              const BuildManifest& bootManifest,
                              const std::string& generatedDir,
                              Ps1SceneExportResult& outResult,
                              std::string& outError) {
    outResult = {};
    outError.clear();

    if (AssetManager::Get().GetProjectRoot() != projectRoot)
        AssetManager::Get().SetProjectRoot(projectRoot);

    const std::string sceneRel = bootManifest.StartupScenePath();
    if (sceneRel.empty()) {
        outError = "No startup scene in build settings (Scenes In Build).";
        return false;
    }

    const fs::path scenePath = PathUtf8::FromString(projectRoot) / sceneRel;
    if (!fs::is_regular_file(scenePath)) {
        outError = "Startup scene not found: " + PathUtf8::ToString(scenePath);
        return false;
    }

    try {
        std::ifstream in(scenePath);
        if (!in.is_open()) {
            outError = "Cannot open scene: " + PathUtf8::ToString(scenePath);
            return false;
        }
        json root;
        in >> root;
        if (!root.contains("entities") || !root["entities"].is_array()) {
            outError = "Invalid scene file (missing entities array).";
            return false;
        }

        std::unordered_map<uint32_t, Ps1ResolvedTransform> flatWorldTransforms;
        BuildFlatWorldTransforms(root["entities"], flatWorldTransforms);
        std::unordered_map<uint32_t, Ps1ResolvedAnimator> flatAnimatorContexts;
        BuildFlatAnimatorContexts(root["entities"], flatAnimatorContexts);
        std::unordered_map<uint32_t, uint32_t> flatControlRoots;

        uint32_t nextId = 1;
        for (const json& ent : root["entities"])
            FlattenEntityJson(ent, outResult, nextId,
                              glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f),
                              &flatWorldTransforms, &flatAnimatorContexts, nullptr, 0,
                              &flatControlRoots);
        ExtractUiElements(root, outResult);
        ExtractSkyboxBackground(root, outResult);
        ExtractPostProcessFog(root, outResult);
        BuildUiGlyphs(outResult);

        // Pick first enabled directional light (fallback to first enabled light).
        bool foundAny = false;
        bool foundDir = false;
        float chosenRot[3] = { 0, 0, 0 };
        float chosenColor[3] = { 1, 1, 1 };
        float chosenIntensity = 1.0f;
        auto scanLights = [&](auto&& self, const json& e) -> void {
            if (e.contains("light") && e["light"].is_object()) {
                const json& lj = e["light"];
                const bool enabled = lj.value("enabled", true);
                if (enabled) {
                    const int type = lj.value("type", 1); // 0=Directional, 1=Point, 2=Spot
                    float rot[3] = { 0, 0, 0 };
                    if (e.contains("transform")) {
                        ReadVec3(e["transform"].value("rotation", json::array({ 0, 0, 0 })), rot);
                    }
                    float col[3] = { 1, 1, 1 };
                    if (lj.contains("color") && lj["color"].is_array() && lj["color"].size() >= 3) {
                        col[0] = lj["color"][0].get<float>();
                        col[1] = lj["color"][1].get<float>();
                        col[2] = lj["color"][2].get<float>();
                    }
                    float intensity = lj.value("intensity", 1.0f);

                    if (!foundAny || (!foundDir && type == 0)) {
                        foundAny = true;
                        foundDir = (type == 0);
                        chosenRot[0] = rot[0]; chosenRot[1] = rot[1]; chosenRot[2] = rot[2];
                        chosenColor[0] = col[0]; chosenColor[1] = col[1]; chosenColor[2] = col[2];
                        chosenIntensity = intensity;
                    }
                }
            }
            if (e.contains("children") && e["children"].is_array()) {
                for (const json& c : e["children"])
                    self(self, c);
            }
        };
        for (const json& ent : root["entities"])
            scanLights(scanLights, ent);

        if (foundAny) {
            outResult.hasLight = true;
            float fwd[3];
            EulerToForwardUnity(chosenRot, fwd);
            // Directional light points along -forward (light shining "forward").
            float dir[3] = { -fwd[0], -fwd[1], -fwd[2] };
            Normalize3(dir);
            outResult.lightDirXYZ12_4[0] = static_cast<int16_t>(dir[0] * 4096.0f);
            outResult.lightDirXYZ12_4[1] = static_cast<int16_t>(dir[1] * 4096.0f);
            outResult.lightDirXYZ12_4[2] = static_cast<int16_t>(dir[2] * 4096.0f);

            const float ir = Clamp01(chosenColor[0] * chosenIntensity);
            const float ig = Clamp01(chosenColor[1] * chosenIntensity);
            const float ib = Clamp01(chosenColor[2] * chosenIntensity);
            outResult.lightColor[0] = ToByte01(ir);
            outResult.lightColor[1] = ToByte01(ig);
            outResult.lightColor[2] = ToByte01(ib);

            // Minimal ambient: 12% of directional color, clamped.
            outResult.ambientColor[0] = std::max<uint8_t>(16, static_cast<uint8_t>(outResult.lightColor[0] / 8));
            outResult.ambientColor[1] = std::max<uint8_t>(16, static_cast<uint8_t>(outResult.lightColor[1] / 8));
            outResult.ambientColor[2] = std::max<uint8_t>(16, static_cast<uint8_t>(outResult.lightColor[2] / 8));
        }

        // Compile scripts referenced by entities.
        std::unordered_map<std::string, size_t> moduleIndexByClass;
        std::unordered_map<std::string, std::shared_ptr<CompiledModule>> moduleByPath;

        for (auto& e : outResult.entities) {
          for (auto& script : e.scripts) {
            if (script.path.empty()) continue;

            std::shared_ptr<CompiledModule> mod;
            auto cached = moduleByPath.find(script.path);
            if (cached != moduleByPath.end()) {
                mod = cached->second;
            } else {
                const fs::path absScript =
                    PathUtf8::FromString(projectRoot) / PathUtf8::FromString(script.path);
                std::vector<std::string> compileErrors;
                mod = MipsRuntime::CompileScriptFile(PathUtf8::ToString(absScript), compileErrors);
                if (!mod) {
                    for (const auto& err : compileErrors)
                        MIPSYNC_WARN("PS1 export script {}: {}", script.path, err);
                    continue;
                }
                moduleByPath[script.path] = mod;
            }

            script.className = mod->className;
            if (moduleIndexByClass.find(mod->className) == moduleIndexByClass.end()) {
                moduleIndexByClass[mod->className] = outResult.modules.size();
                outResult.modules.push_back(*mod);
                outResult.moduleClassNames.push_back(mod->className);
            }

            // Field overrides from scene JSON (if present).
            script.fieldValuesQ16.resize(mod->fields.size(), 0);
            script.assetFieldPaths.resize(mod->fields.size());
            for (size_t fi = 0; fi < mod->fields.size(); ++fi) {
                const auto& field = mod->fields[fi];
                if (field.valueKind == FieldValueKind::AudioClip) {
                    auto assetIt = script.assetFieldOverrides.find(field.name);
                    if (assetIt != script.assetFieldOverrides.end())
                        script.assetFieldPaths[fi] = assetIt->second;
                    continue;
                }
                auto overrideIt = script.fieldOverrides.find(field.name);
                if (overrideIt != script.fieldOverrides.end()) {
                    script.fieldValuesQ16[fi] = ToFixed16(overrideIt->second);
                } else if (field.defaultConstIndex < mod->numberConstants.size()) {
                    script.fieldValuesQ16[fi] =
                        ToFixed16(mod->numberConstants[field.defaultConstIndex]);
                }
            }
          }
        }

        std::unordered_map<uint32_t, int> entityIndexById;
        entityIndexById.reserve(outResult.entities.size());
        for (size_t i = 0; i < outResult.entities.size(); ++i)
            entityIndexById[outResult.entities[i].id] = static_cast<int>(i);

        // Resolve camera shot trigger references and pick the initial camera by priority.
        int bestPrimaryCamera = -1;
        int bestAnyCamera = -1;
        for (size_t i = 0; i < outResult.entities.size(); ++i) {
            auto& e = outResult.entities[i];
            if (e.hasCamera && e.cameraShotTriggerId != 0) {
                auto it = entityIndexById.find(e.cameraShotTriggerId);
                if (it != entityIndexById.end())
                    e.cameraShotTriggerEntityIndex = it->second;
            }
            if (e.colliderCameraTargetId != 0) {
                auto it = entityIndexById.find(e.colliderCameraTargetId);
                if (it != entityIndexById.end())
                    e.colliderCameraTargetEntityIndex = it->second;
            }
            if (e.rigidRootEntityId != 0) {
                auto it = entityIndexById.find(e.rigidRootEntityId);
                if (it != entityIndexById.end())
                    e.rigidRootEntityIndex = it->second;
            }
            if (!e.hasCamera)
                continue;

            const int idx = static_cast<int>(i);
            if (bestAnyCamera < 0) {
                bestAnyCamera = idx;
            }
            if (e.cameraPrimary &&
                bestPrimaryCamera < 0) {
                bestPrimaryCamera = idx;
            }
        }
        outResult.cameraEntityIndex = bestPrimaryCamera >= 0 ? bestPrimaryCamera : bestAnyCamera;
        BakeSkyboxBackgroundForPs1(root, projectRoot, scenePath, generatedDir, outResult);

        // Count bindings.
        for (const auto& e : outResult.entities) {
            for (const auto& script : e.scripts)
                if (!script.className.empty()) ++outResult.bindingCount;
        }

        ScriptsDataEmit scriptStats{};
        const fs::path scriptsC = PathUtf8::FromString(generatedDir) / "scripts_data.c";
        if (!EmitScriptsDataC(outResult.modules, PathUtf8::ToString(scriptsC), scriptStats, outError))
            return false;

        ResolveEntityAppearances(projectRoot, scenePath, outResult);

        const fs::path meshC = PathUtf8::FromString(generatedDir) / "mesh_data.c";
        if (!EmitMeshDataC(projectRoot, outResult, PathUtf8::ToString(meshC), outError))
            return false;

        const fs::path texC = PathUtf8::FromString(generatedDir) / "textures_data.c";
        if (!EmitTexturesDataC(outResult.texturePaths, projectRoot, PathUtf8::ToString(texC), outError, outResult.backgroundImagePaths))
            return false;

        const fs::path audioC = PathUtf8::FromString(generatedDir) / "audio_data.c";
        if (!EmitAudioDataC(projectRoot, outResult, PathUtf8::ToString(audioC), outError))
            return false;

        const fs::path sceneC = PathUtf8::FromString(generatedDir) / "scene_data.c";
        if (!EmitSceneDataC(outResult, PathUtf8::ToString(sceneC), outError))
            return false;

        MIPSYNC_INFO("PS1 scene export: {} entities, {} textures, {} ui, {} scripts, {} bindings, camera {}",
                     outResult.entities.size(), outResult.texturePaths.size(),
                     outResult.uiElements.size(), scriptStats.scriptCount, outResult.bindingCount,
                     outResult.cameraEntityIndex);
        return true;
    } catch (const std::exception& ex) {
        outError = ex.what();
        return false;
    }
}

} // namespace MipsyncEngine::Mips
