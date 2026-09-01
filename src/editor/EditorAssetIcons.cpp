#include "EditorAssetIcons.h"
#include "AssetBrowserPanel.h"
#include "EditorTheme.h"
#include "../core/RuntimePaths.h"
#include "../renderer/Texture.h"
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <unordered_map>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif
#include <vector>

namespace MipsyncEngine {

#ifdef _WIN32
std::shared_ptr<Texture> GetWindowsSystemIcon(const std::wstring& extension) {
    SHFILEINFOW sfi = {0};
    DWORD_PTR hr = SHGetFileInfoW(
        extension.c_str(),
        FILE_ATTRIBUTE_NORMAL,
        &sfi,
        sizeof(sfi),
        SHGFI_ICON | SHGFI_USEFILEATTRIBUTES | SHGFI_LARGEICON
    );

    if (hr == 0 || !sfi.hIcon) {
        return nullptr;
    }

    ICONINFO iconInfo = {0};
    if (!GetIconInfo(sfi.hIcon, &iconInfo)) {
        DestroyIcon(sfi.hIcon);
        return nullptr;
    }

    BITMAP bmColor = {0};
    GetObject(iconInfo.hbmColor, sizeof(bmColor), &bmColor);

    int width = bmColor.bmWidth;
    int height = bmColor.bmHeight;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, iconInfo.hbmColor);

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    std::vector<unsigned char> pixels(width * height * 4);
    GetDIBits(hdcScreen, iconInfo.hbmColor, 0, height, pixels.data(), &bmi, DIB_RGB_COLORS);

    bool hasAlpha = false;
    for (int i = 0; i < width * height; ++i) {
        if (pixels[i * 4 + 3] != 0) {
            hasAlpha = true;
            break;
        }
    }

    if (!hasAlpha && iconInfo.hbmMask) {
        BITMAP bmMask = {0};
        GetObject(iconInfo.hbmMask, sizeof(bmMask), &bmMask);
        
        HDC hdcMask = CreateCompatibleDC(hdcScreen);
        HBITMAP hbmMaskOld = (HBITMAP)SelectObject(hdcMask, iconInfo.hbmMask);
        
        std::vector<unsigned char> maskPixels(width * height * 4);
        GetDIBits(hdcScreen, iconInfo.hbmMask, 0, height, maskPixels.data(), &bmi, DIB_RGB_COLORS);
        
        for (int i = 0; i < width * height; ++i) {
            if (maskPixels[i * 4] == 0) {
                pixels[i * 4 + 3] = 255;
            } else {
                pixels[i * 4 + 3] = 0;
            }
        }
        
        SelectObject(hdcMask, hbmMaskOld);
        DeleteDC(hdcMask);
    }

    for (int i = 0; i < width * height; ++i) {
        unsigned char b = pixels[i * 4 + 0];
        unsigned char r = pixels[i * 4 + 2];
        pixels[i * 4 + 0] = r;
        pixels[i * 4 + 2] = b;
    }

    // Flip rows vertically so row 0 is at the bottom (matching STB flip for OpenGL / ImGui rendering)
    const int rowBytes = width * 4;
    std::vector<unsigned char> tempRow(rowBytes);
    for (int y = 0; y < height / 2; ++y) {
        unsigned char* rowTop = pixels.data() + y * rowBytes;
        unsigned char* rowBottom = pixels.data() + (height - 1 - y) * rowBytes;
        std::memcpy(tempRow.data(), rowTop, rowBytes);
        std::memcpy(rowTop, rowBottom, rowBytes);
        std::memcpy(rowBottom, tempRow.data(), rowBytes);
    }

    SelectObject(hdcMem, hbmOld);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    
    if (iconInfo.hbmColor) DeleteObject(iconInfo.hbmColor);
    if (iconInfo.hbmMask) DeleteObject(iconInfo.hbmMask);
    DestroyIcon(sfi.hIcon);

    TextureParams params;
    params.nearestFilter = false;
    params.flipVerticallyOnLoad = false;
    auto tex = std::make_shared<Texture>(width, height, pixels.data(), 4, params);
    if (tex->GetID() == 0) return nullptr;
    return tex;
}
#endif

namespace {

std::filesystem::path ResolveProjectIconPath(const char* filename) {
    const std::filesystem::path candidate =
        GetBundledResourcesDirectory() / "icons" / "project" / filename;
    if (std::filesystem::exists(candidate))
        return candidate;
    return {};
}

const char* IconFileNameForKind(AssetKind kind) {
    switch (kind) {
    case AssetKind::Folder: return "folder.png";
    case AssetKind::Script: return "script.png";
    // Not all kinds have dedicated PNGs yet. Map the rest to existing assets so
    // the Project window always shows an icon instead of a text label.
    case AssetKind::Scene: return "app_icon.png";
    case AssetKind::Prefab: return "script.png";
    case AssetKind::AnimatorController: return "animator_controller.png";
    case AssetKind::AnimationClip: return "animation_clip.png";
    case AssetKind::Audio: return "script.png";
    case AssetKind::Texture: return "script.png";
    case AssetKind::Material: return "script.png";
    case AssetKind::Model: return "script.png";
    case AssetKind::Other: return "script.png";
    }
    return "other.png";
}

class ProjectIconCache {
public:
    std::shared_ptr<Texture> Get(AssetKind kind, const std::string& path = "") {
        if (kind == AssetKind::Other && !path.empty()) {
            std::filesystem::path fsPath(path);
            std::string ext = fsPath.extension().string();
            if (!ext.empty()) {
                const auto it = m_ExtTextures.find(ext);
                if (it != m_ExtTextures.end())
                    return it->second;

#ifdef _WIN32
                std::wstring wext(ext.begin(), ext.end());
                auto systemTex = GetWindowsSystemIcon(wext);
                if (systemTex) {
                    m_ExtTextures.emplace(ext, systemTex);
                    return systemTex;
                }
#endif
            }
        }

        const auto it = m_Textures.find(kind);
        if (it != m_Textures.end())
            return it->second;

        const char* fileName = IconFileNameForKind(kind);
        const std::filesystem::path filePath = kind == AssetKind::Scene
            ? GetBundledResourcesDirectory() / "icons" / "app_icon.png"
            : ResolveProjectIconPath(fileName);
        if (filePath.empty()) {
            m_Textures.emplace(kind, nullptr);
            return nullptr;
        }

        TextureParams params;
        params.nearestFilter = false;
        params.maxSize = 0; // keep source pixel dimensions / aspect
        params.colorKeyTopLeft = kind == AssetKind::Folder;
        auto tex = std::make_shared<Texture>(filePath.string(), params);
        if (tex->GetID() == 0)
            tex = nullptr;
        m_Textures.emplace(kind, tex);
        return tex;
    }

private:
    std::unordered_map<AssetKind, std::shared_ptr<Texture>> m_Textures;
    std::unordered_map<std::string, std::shared_ptr<Texture>> m_ExtTextures;
};

ProjectIconCache& Icons() {
    static ProjectIconCache cache;
    return cache;
}

ImVec2 AspectFitImageRect(ImVec2 cellMin, ImVec2 cellMax, int texW, int texH, ImVec2& outMax) {
    const float cellW = cellMax.x - cellMin.x;
    const float cellH = cellMax.y - cellMin.y;
    if (texW <= 0 || texH <= 0 || cellW <= 0.0f || cellH <= 0.0f) {
        outMax = cellMax;
        return cellMin;
    }

    const float texAspect = static_cast<float>(texW) / static_cast<float>(texH);
    const float cellAspect = cellW / cellH;
    float displayW = cellW;
    float displayH = cellH;
    if (texAspect > cellAspect)
        displayH = cellW / texAspect;
    else
        displayW = cellH * texAspect;

    const float offsetX = (cellW - displayW) * 0.5f;
    const float offsetY = (cellH - displayH) * 0.5f;
    const ImVec2 imageMin(cellMin.x + offsetX, cellMin.y + offsetY);
    outMax = ImVec2(imageMin.x + displayW, imageMin.y + displayH);
    return imageMin;
}

} // namespace

void DrawTexturedImageAspectFit(ImDrawList* drawList, const Texture& tex, ImVec2 cellMin,
                                ImVec2 cellMax) {
    if (!drawList || tex.GetID() == 0)
        return;

    ImVec2 imageMax;
    const ImVec2 imageMin = AspectFitImageRect(cellMin, cellMax, tex.GetWidth(), tex.GetHeight(), imageMax);
    drawList->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(tex.GetID())), imageMin,
                       imageMax, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
}

bool TryDrawProjectAssetIcon(ImDrawList* drawList, AssetKind kind, ImVec2 min, ImVec2 max,
                             const std::string& path) {
    if (!drawList)
        return false;

    const auto tex = Icons().Get(kind, path);
    if (!tex || tex->GetID() == 0)
        return false;

    DrawTexturedImageAspectFit(drawList, *tex, min, max);
    return true;
}

void DrawFolderIcon(ImDrawList* drawList, ImVec2 min, ImVec2 max) {
    const float w = max.x - min.x;
    const float h = max.y - min.y;
    const float pad = w * 0.14f;
    const float stroke = 1.25f;
    const float tabR = 2.5f;
    const float bodyR = 3.5f;

    const ImVec2 bodyMin(min.x + pad, min.y + h * 0.30f);
    const ImVec2 bodyMax(max.x - pad, max.y - pad);

    const float tabW = w * 0.46f;
    const ImVec2 tabMin(bodyMin.x, min.y + pad);
    const ImVec2 tabMax(bodyMin.x + tabW, bodyMin.y);

    const ImU32 fillTab = ImGui::ColorConvertFloat4ToU32(UiTokens::BrandTertiary);
    const ImU32 fillBody = ImGui::ColorConvertFloat4ToU32(EditorTheme::Selection);
    const ImU32 outline = ImGui::ColorConvertFloat4ToU32(EditorTheme::BorderLight);

    drawList->AddRectFilled(tabMin, tabMax, fillTab, tabR);
    drawList->AddRectFilled(bodyMin, bodyMax, fillBody, bodyR);
    drawList->AddRect(bodyMin, bodyMax, outline, bodyR, 0, stroke);

    drawList->PathClear();
    drawList->PathLineTo(tabMin);
    drawList->PathLineTo(ImVec2(tabMax.x, tabMin.y));
    drawList->PathLineTo(tabMax);
    drawList->PathStroke(outline, ImDrawFlags_None, stroke);

    drawList->AddLine(tabMax, ImVec2(bodyMax.x, bodyMin.y), outline, stroke);
}

void DrawScriptIcon(ImDrawList* drawList, ImVec2 min, ImVec2 max) {
    const float w = max.x - min.x;
    const float h = max.y - min.y;
    const float pad = w * 0.16f;
    const ImVec2 pageMin(min.x + pad, min.y + pad);
    const ImVec2 pageMax(max.x - pad, max.y - pad);

    const ImU32 fill = ImGui::ColorConvertFloat4ToU32(UiTokens::SuccessBg);
    const ImU32 fold = ImGui::ColorConvertFloat4ToU32(EditorTheme::Success);
    const ImU32 outline = ImGui::ColorConvertFloat4ToU32(EditorTheme::BorderLight);
    const ImU32 lines = ImGui::ColorConvertFloat4ToU32(EditorTheme::TextPrimary);

    const float foldSz = w * 0.22f;
    const ImVec2 foldMin(pageMax.x - foldSz, pageMin.y);
    const ImVec2 foldMax(pageMax.x, pageMin.y + foldSz);

    drawList->AddRectFilled(pageMin, pageMax, fill, 3.0f);
    drawList->AddTriangleFilled(foldMin, foldMax, ImVec2(pageMax.x, pageMin.y + foldSz), fold);
    drawList->AddRect(pageMin, pageMax, outline, 3.0f, 0, 1.5f);

    const float lineY0 = pageMin.y + h * 0.38f;
    const float lineStep = h * 0.11f;
    for (int i = 0; i < 3; ++i) {
        const float y = lineY0 + lineStep * static_cast<float>(i);
        drawList->AddLine(ImVec2(pageMin.x + w * 0.22f, y),
                          ImVec2(pageMax.x - w * 0.18f, y), lines, 1.5f);
    }
}

void DrawShapePresetIcon(ImDrawList* drawList, ImVec2 min, ImVec2 max, int shapeType) {
    const float w = max.x - min.x;
    const float h = max.y - min.y;
    const float pad = w * 0.25f;
    const ImU32 color = ImGui::ColorConvertFloat4ToU32(EditorTheme::TextPrimary);
    const float thick = 1.5f;

    const float cx = min.x + w * 0.5f;
    const float cy = min.y + h * 0.5f;

    if (shapeType == 0) { // Box (Isometric wireframe cube)
        const float r = w * 0.28f;
        // Points
        const ImVec2 c(cx, cy);
        const ImVec2 t(cx, cy - r);
        const ImVec2 b(cx, cy + r);
        const ImVec2 tl(cx - r * 0.866f, cy - r * 0.5f);
        const ImVec2 tr(cx + r * 0.866f, cy - r * 0.5f);
        const ImVec2 bl(cx - r * 0.866f, cy + r * 0.5f);
        const ImVec2 br(cx + r * 0.866f, cy + r * 0.5f);

        // Outer borders
        drawList->AddLine(t, tr, color, thick);
        drawList->AddLine(tr, br, color, thick);
        drawList->AddLine(br, b, color, thick);
        drawList->AddLine(b, bl, color, thick);
        drawList->AddLine(bl, tl, color, thick);
        drawList->AddLine(tl, t, color, thick);

        // Inner Y lines
        drawList->AddLine(c, t, color, thick);
        drawList->AddLine(c, bl, color, thick);
        drawList->AddLine(c, br, color, thick);
    }
    else if (shapeType == 1) { // Plane (Flat rectangle perspective)
        const float rx = w * 0.35f;
        const float ry = h * 0.18f;
        const ImVec2 p0(cx - rx, cy + ry * 0.5f);
        const ImVec2 p1(cx + rx * 0.6f, cy + ry);
        const ImVec2 p2(cx + rx, cy - ry * 0.5f);
        const ImVec2 p3(cx - rx * 0.6f, cy - ry);

        drawList->AddQuad(p0, p1, p2, p3, color, thick);
    }
    else if (shapeType == 2) { // Ramp (Wedge shape)
        const float rx = w * 0.32f;
        const float ry = h * 0.32f;
        const ImVec2 bl(cx - rx, cy + ry);
        const ImVec2 br(cx + rx, cy + ry);
        const ImVec2 tl(cx - rx, cy - ry);
        const ImVec2 tr(cx + rx, cy - ry);
        const ImVec2 bl_front(cx - rx * 0.3f, cy + ry * 0.3f);
        const ImVec2 br_front(cx + rx * 0.7f, cy + ry * 0.3f);

        // Left triangle
        drawList->AddTriangle(bl, br, tl, color, thick);
        // Extrude to right
        const ImVec2 r_bl(cx + rx * 0.4f, cy + ry * 0.4f);
        const ImVec2 r_br(cx + rx * 1.0f, cy + ry * 0.4f);
        const ImVec2 r_tl(cx + rx * 0.4f, cy - ry * 0.4f);

        drawList->AddLine(br, r_br, color, thick);
        drawList->AddLine(tl, r_tl, color, thick);
        drawList->AddLine(r_tl, r_br, color, thick);
    }
    else if (shapeType == 3) { // Stairs (Step outline)
        const float rx = w * 0.35f;
        const float ry = h * 0.35f;
        const ImVec2 start(cx - rx, cy + ry);
        const ImVec2 p1(cx - rx, cy + ry * 0.33f);
        const ImVec2 p2(cx - rx * 0.33f, cy + ry * 0.33f);
        const ImVec2 p3(cx - rx * 0.33f, cy - ry * 0.33f);
        const ImVec2 p4(cx + rx * 0.33f, cy - ry * 0.33f);
        const ImVec2 p5(cx + rx * 0.33f, cy - ry);
        const ImVec2 end(cx + rx, cy - ry);
        const ImVec2 br(cx + rx, cy + ry);

        drawList->AddLine(start, p1, color, thick);
        drawList->AddLine(p1, p2, color, thick);
        drawList->AddLine(p2, p3, color, thick);
        drawList->AddLine(p3, p4, color, thick);
        drawList->AddLine(p4, p5, color, thick);
        drawList->AddLine(p5, end, color, thick);
        drawList->AddLine(end, br, color, thick);
        drawList->AddLine(br, start, color, thick);
    }
    else if (shapeType == 4) { // Cylinder
        const float rx = w * 0.28f;
        const float ry = h * 0.12f;
        const float topY = cy - h * 0.23f;
        const float bottomY = cy + h * 0.23f;
        drawList->AddEllipse(ImVec2(cx, topY), ImVec2(rx, ry), color,
                             0.0f, 20, thick);
        drawList->PathEllipticalArcTo(ImVec2(cx, bottomY), ImVec2(rx, ry),
                                      0.0f, 0.0f, 3.14159265f, 12);
        drawList->PathStroke(color, 0, thick);
        drawList->AddLine(ImVec2(cx - rx, topY), ImVec2(cx - rx, bottomY),
                          color, thick);
        drawList->AddLine(ImVec2(cx + rx, topY), ImVec2(cx + rx, bottomY),
                          color, thick);
    }
}

void DrawEditModeIcon(ImDrawList* drawList, ImVec2 min, ImVec2 max, int mode) {
    const float w = max.x - min.x;
    const float h = max.y - min.y;
    const float cx = min.x + w * 0.5f;
    const float cy = min.y + h * 0.5f;
    const ImU32 white = ImGui::ColorConvertFloat4ToU32(EditorTheme::TextPrimary);
    const ImU32 accent = ImGui::ColorConvertFloat4ToU32(EditorTheme::Accent);
    const ImU32 gray = ImGui::ColorConvertFloat4ToU32(EditorTheme::TextDisabled);
    const float thick = 1.5f;

    if (mode == 0) { // Object Mode (Whole cube highlighted)
        const float r = w * 0.25f;
        const ImVec2 t(cx, cy - r);
        const ImVec2 b(cx, cy + r);
        const ImVec2 tl(cx - r * 0.866f, cy - r * 0.5f);
        const ImVec2 tr(cx + r * 0.866f, cy - r * 0.5f);
        const ImVec2 bl(cx - r * 0.866f, cy + r * 0.5f);
        const ImVec2 br(cx + r * 0.866f, cy + r * 0.5f);

        drawList->AddLine(t, tr, accent, thick + 0.5f);
        drawList->AddLine(tr, br, accent, thick + 0.5f);
        drawList->AddLine(br, b, accent, thick + 0.5f);
        drawList->AddLine(b, bl, accent, thick + 0.5f);
        drawList->AddLine(bl, tl, accent, thick + 0.5f);
        drawList->AddLine(tl, t, accent, thick + 0.5f);

        const ImVec2 c(cx, cy);
        drawList->AddLine(c, t, accent, thick + 0.5f);
        drawList->AddLine(c, bl, accent, thick + 0.5f);
        drawList->AddLine(c, br, accent, thick + 0.5f);
    }
    else if (mode == 1) { // Vertex Mode (Dots at vertices)
        const float r = w * 0.25f;
        const ImVec2 t(cx, cy - r);
        const ImVec2 b(cx, cy + r);
        const ImVec2 tl(cx - r * 0.866f, cy - r * 0.5f);
        const ImVec2 tr(cx + r * 0.866f, cy - r * 0.5f);
        const ImVec2 bl(cx - r * 0.866f, cy + r * 0.5f);
        const ImVec2 br(cx + r * 0.866f, cy + r * 0.5f);
        const ImVec2 c(cx, cy);

        // Cube wireframe in dim gray
        drawList->AddLine(t, tr, gray, thick);
        drawList->AddLine(tr, br, gray, thick);
        drawList->AddLine(br, b, gray, thick);
        drawList->AddLine(b, bl, gray, thick);
        drawList->AddLine(bl, tl, gray, thick);
        drawList->AddLine(tl, t, gray, thick);
        drawList->AddLine(c, t, gray, thick);
        drawList->AddLine(c, bl, gray, thick);
        drawList->AddLine(c, br, gray, thick);

        // Accent dots at vertices
        drawList->AddCircleFilled(t, 3.0f, accent);
        drawList->AddCircleFilled(tr, 3.0f, accent);
        drawList->AddCircleFilled(tl, 3.0f, accent);
        drawList->AddCircleFilled(c, 3.0f, accent);
    }
    else if (mode == 2) { // Edge Mode (Highlit edge)
        const float r = w * 0.25f;
        const ImVec2 t(cx, cy - r);
        const ImVec2 b(cx, cy + r);
        const ImVec2 tl(cx - r * 0.866f, cy - r * 0.5f);
        const ImVec2 tr(cx + r * 0.866f, cy - r * 0.5f);
        const ImVec2 bl(cx - r * 0.866f, cy + r * 0.5f);
        const ImVec2 br(cx + r * 0.866f, cy + r * 0.5f);
        const ImVec2 c(cx, cy);

        drawList->AddLine(t, tr, gray, thick);
        drawList->AddLine(tr, br, gray, thick);
        drawList->AddLine(br, b, gray, thick);
        drawList->AddLine(b, bl, gray, thick);
        drawList->AddLine(bl, tl, gray, thick);
        drawList->AddLine(tl, t, gray, thick);
        drawList->AddLine(c, t, gray, thick);
        drawList->AddLine(c, bl, gray, thick);
        drawList->AddLine(c, br, gray, thick);

        // Highlight top-front edge
        drawList->AddLine(t, tr, accent, thick + 1.0f);
    }
    else if (mode == 3) { // Face Mode (Filled face)
        const float r = w * 0.25f;
        const ImVec2 t(cx, cy - r);
        const ImVec2 b(cx, cy + r);
        const ImVec2 tl(cx - r * 0.866f, cy - r * 0.5f);
        const ImVec2 tr(cx + r * 0.866f, cy - r * 0.5f);
        const ImVec2 bl(cx - r * 0.866f, cy + r * 0.5f);
        const ImVec2 br(cx + r * 0.866f, cy + r * 0.5f);
        const ImVec2 c(cx, cy);

        // Fill top face
        drawList->AddQuadFilled(t, tr, c, tl, ImGui::ColorConvertFloat4ToU32(UiTokens::WithAlpha(EditorTheme::Accent, 0.4f)));

        drawList->AddLine(t, tr, gray, thick);
        drawList->AddLine(tr, br, gray, thick);
        drawList->AddLine(br, b, gray, thick);
        drawList->AddLine(b, bl, gray, thick);
        drawList->AddLine(bl, tl, gray, thick);
        drawList->AddLine(tl, t, gray, thick);
        drawList->AddLine(c, t, gray, thick);
        drawList->AddLine(c, bl, gray, thick);
        drawList->AddLine(c, br, gray, thick);

        drawList->AddQuad(t, tr, c, tl, accent, thick);
    }
}

void DrawExtrudeIcon(ImDrawList* drawList, ImVec2 min, ImVec2 max, int directionIndex) {
    const float w = max.x - min.x;
    const float h = max.y - min.y;
    const float cx = min.x + w * 0.5f;
    const float cy = min.y + h * 0.5f;
    const ImU32 color = ImGui::ColorConvertFloat4ToU32(EditorTheme::TextPrimary);
    const float thick = 2.0f;

    // Draw an arrow indicating direction
    if (directionIndex == 0) { // Up (+Y)
        drawList->AddLine(ImVec2(cx, cy + h * 0.3f), ImVec2(cx, cy - h * 0.3f), color, thick);
        drawList->AddLine(ImVec2(cx - w * 0.15f, cy - h * 0.1f), ImVec2(cx, cy - h * 0.3f), color, thick);
        drawList->AddLine(ImVec2(cx + w * 0.15f, cy - h * 0.1f), ImVec2(cx, cy - h * 0.3f), color, thick);
    }
    else if (directionIndex == 1) { // Right (+X)
        drawList->AddLine(ImVec2(cx - w * 0.3f, cy), ImVec2(cx + w * 0.3f, cy), color, thick);
        drawList->AddLine(ImVec2(cx + w * 0.1f, cy - h * 0.15f), ImVec2(cx + w * 0.3f, cy), color, thick);
        drawList->AddLine(ImVec2(cx + w * 0.1f, cy + h * 0.15f), ImVec2(cx + w * 0.3f, cy), color, thick);
    }
    else if (directionIndex == 2) { // Forward (+Z, diagonal down-left in 2D perspective representation)
        drawList->AddLine(ImVec2(cx + w * 0.2f, cy - h * 0.2f), ImVec2(cx - w * 0.2f, cy + h * 0.2f), color, thick);
        drawList->AddLine(ImVec2(cx - w * 0.2f, cy), ImVec2(cx - w * 0.2f, cy + h * 0.2f), color, thick);
        drawList->AddLine(ImVec2(cx, cy + h * 0.2f), ImVec2(cx - w * 0.2f, cy + h * 0.2f), color, thick);
    }
}

} // namespace MipsyncEngine
