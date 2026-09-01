/*
 * Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md).
 * Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "EditorTheme.h"
#include "../core/RuntimePaths.h"
#include <filesystem>
#include <vector>
#include <cmath>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace MipsyncEngine {

ImVec4 EditorTheme::BtnFace = UiTokens::BgTertiary;
ImVec4 EditorTheme::BtnFaceLight = UiTokens::BgHover;
ImVec4 EditorTheme::BtnShadow = UiTokens::Border;
ImVec4 EditorTheme::BtnDarkShadow = UiTokens::Bg;
ImVec4 EditorTheme::BtnHighlight = UiTokens::BorderHighlight;
ImVec4 EditorTheme::PanelFace = UiTokens::BgSecondary;
ImVec4 EditorTheme::PanelAlt = UiTokens::Bg;
ImVec4 EditorTheme::WorkArea = UiTokens::Bg;
ImVec4 EditorTheme::Desktop = UiTokens::Bg;
ImVec4 EditorTheme::InputBg = UiTokens::Bg;
ImVec4 EditorTheme::TitleBarTop = UiTokens::Bg;
ImVec4 EditorTheme::TitleBarBottom = UiTokens::Bg;
ImVec4 EditorTheme::TitleBarText = UiTokens::Text;
ImVec4 EditorTheme::Selection = UiTokens::Brand;
ImVec4 EditorTheme::SelectionText = UiTokens::TextOnBrand;
ImVec4 EditorTheme::PsAccent = UiTokens::Brand;
ImVec4 EditorTheme::LinkCyan = UiTokens::TextBrand;
ImVec4 EditorTheme::TextBrand = UiTokens::TextBrand;
ImVec4 EditorTheme::TextPrimary = UiTokens::Text;
ImVec4 EditorTheme::TextSecondary = UiTokens::TextSecondary;
ImVec4 EditorTheme::TextMuted = UiTokens::TextTertiary;
ImVec4 EditorTheme::TextDisabled = UiTokens::TextDisabled;
ImVec4 EditorTheme::Error = UiTokens::Danger;
ImVec4 EditorTheme::ErrorBg = UiTokens::DangerBg;
ImVec4 EditorTheme::SuccessLabel = UiTokens::Success;
ImVec4 EditorTheme::DangerFace = UiTokens::Danger;
ImVec4 EditorTheme::DangerText = UiTokens::TextOnBrand;
ImVec4 EditorTheme::Warning = UiTokens::Warning;
ImVec4 EditorTheme::BorderLight = UiTokens::Border;
ImVec4 EditorTheme::BorderDark = UiTokens::Bg;
ImVec4 EditorTheme::Background = UiTokens::Bg;
ImVec4 EditorTheme::Accent = UiTokens::Brand;
ImVec4 EditorTheme::AccentHover = UiTokens::BrandHover;
ImVec4 EditorTheme::AccentActive = UiTokens::BrandPressed;
ImVec4 EditorTheme::AccentMuted = UiTokens::WithAlpha(UiTokens::Brand, 0.35f);
ImVec4 EditorTheme::Primary = UiTokens::Brand;
ImVec4 EditorTheme::OnPrimary = UiTokens::TextOnBrand;
ImVec4 EditorTheme::SurfaceContainer = UiTokens::BgSecondary;
ImVec4 EditorTheme::SurfaceContainerHigh = UiTokens::BgTertiary;
ImVec4 EditorTheme::SurfaceRaised = UiTokens::BgTertiary;
ImVec4 EditorTheme::SurfaceHover = UiTokens::BgHover;
ImVec4 EditorTheme::OnSurface = UiTokens::Text;
ImVec4 EditorTheme::OnSurfaceVariant = UiTokens::TextSecondary;
ImVec4 EditorTheme::Outline = UiTokens::Border;
ImVec4 EditorTheme::OutlineVariant = UiTokens::Border;
ImVec4 EditorTheme::PanelBackground = UiTokens::BgSecondary;
ImVec4 EditorTheme::GlassPanel = UiTokens::BgSecondary;
ImVec4 EditorTheme::Border = UiTokens::Border;
ImVec4 EditorTheme::BorderStrong = UiTokens::BorderStrong;
ImVec4 EditorTheme::Success = UiTokens::Success;
ImVec4 EditorTheme::SuccessHover = UiTokens::SuccessHover;
ImVec4 EditorTheme::Danger = UiTokens::Danger;
ImVec4 EditorTheme::DangerHover = UiTokens::DangerHover;

namespace {

bool gDarkMode = true;

ImU32 U32(const ImVec4& c) {
    return ImGui::ColorConvertFloat4ToU32(c);
}

using namespace UiTokens;

std::filesystem::path ResolveBundledFont(const char* filename) {
    const std::filesystem::path candidate =
        GetBundledResourcesDirectory() / "fonts" / filename;
    if (std::filesystem::exists(candidate))
        return candidate;
    return {};
}

} // namespace

bool EditorTheme::IsDarkMode() {
    return gDarkMode;
}

void EditorTheme::SetDarkMode(bool dark) {
    gDarkMode = dark;

    const ImVec4 bg = dark ? UiTokens::Hex(0x1E1E1E) : UiTokens::Hex(0xECECEC);
    const ImVec4 panel = dark ? UiTokens::Hex(0x2C2C2C) : UiTokens::Hex(0xFAFAFA);
    const ImVec4 panelAlt = dark ? UiTokens::Hex(0x1E1E1E) : UiTokens::Hex(0xF4F4F4);
    const ImVec4 control = dark ? UiTokens::Hex(0x383838) : UiTokens::Hex(0xF0F0F0);
    const ImVec4 hover = dark ? UiTokens::Hex(0x444444) : UiTokens::Hex(0xE2E2E2);
    const ImVec4 pressed = dark ? UiTokens::Hex(0x242424) : UiTokens::Hex(0xD8D8D8);
    const ImVec4 text = dark ? UiTokens::Hex(0xFFFFFF) : UiTokens::Hex(0x242424);
    const ImVec4 textSecondary = dark ? UiTokens::Hex(0xB3B3B3) : UiTokens::Hex(0x5F6368);
    const ImVec4 textMuted = dark ? UiTokens::Hex(0x8C8C8C) : UiTokens::Hex(0x858585);
    const ImVec4 textDisabled = dark ? UiTokens::Hex(0x666666) : UiTokens::Hex(0xADADAD);
    const ImVec4 border = dark ? UiTokens::Hex(0x444444) : UiTokens::Hex(0xD2D2D2);
    const ImVec4 borderStrong = dark ? UiTokens::Hex(0x707070) : UiTokens::Hex(0xA9A9A9);

    BtnFace = control;
    BtnFaceLight = hover;
    BtnShadow = border;
    BtnDarkShadow = pressed;
    BtnHighlight = borderStrong;
    PanelFace = panel;
    PanelAlt = panelAlt;
    WorkArea = bg;
    Desktop = bg;
    InputBg = dark ? UiTokens::Hex(0x1E1E1E) : UiTokens::Hex(0xF3F3F3);
    TitleBarTop = panel;
    TitleBarBottom = panel;
    TitleBarText = text;
    Selection = UiTokens::Brand;
    SelectionText = UiTokens::TextOnBrand;
    PsAccent = UiTokens::Brand;
    LinkCyan = UiTokens::TextBrand;
    TextBrand = UiTokens::TextBrand;
    TextPrimary = text;
    TextSecondary = textSecondary;
    TextMuted = textMuted;
    TextDisabled = textDisabled;
    Error = UiTokens::Danger;
    ErrorBg = dark ? UiTokens::DangerBg : UiTokens::Hex(0xFCE8E3);
    SuccessLabel = dark ? UiTokens::Success : UiTokens::Hex(0x07883F);
    DangerFace = UiTokens::Danger;
    DangerText = UiTokens::TextOnBrand;
    Warning = dark ? UiTokens::Warning : UiTokens::Hex(0x9A6700);
    BorderLight = border;
    BorderDark = pressed;

    Background = WorkArea;
    Accent = Selection;
    AccentHover = UiTokens::BrandHover;
    AccentActive = UiTokens::BrandPressed;
    AccentMuted = UiTokens::WithAlpha(UiTokens::Brand, dark ? 0.35f : 0.22f);
    Primary = Selection;
    OnPrimary = SelectionText;
    SurfaceContainer = PanelFace;
    SurfaceContainerHigh = control;
    SurfaceRaised = control;
    SurfaceHover = hover;
    OnSurface = TextPrimary;
    OnSurfaceVariant = TextSecondary;
    Outline = BorderLight;
    OutlineVariant = BorderLight;
    PanelBackground = PanelFace;
    GlassPanel = PanelFace;
    Border = BorderLight;
    BorderStrong = borderStrong;
    Success = SuccessLabel;
    SuccessHover = UiTokens::SuccessHover;
    Danger = DangerFace;
    DangerHover = UiTokens::DangerHover;
}

void EditorTheme::DrawButtonBevel(ImDrawList* drawList, ImVec2 min, ImVec2 max, bool pressed) {
    // Godot StyleBoxFlat button border & focus ring rendering:
    // Crisp 1px border around rounded box (4px radius).
    const ImU32 borderCol = U32(pressed ? BorderStrong : BorderLight);
    drawList->AddRect({min.x + 0.5f, min.y + 0.5f},
                      {max.x - 0.5f, max.y - 0.5f},
                      borderCol, ShapeCornerSmall, 0, 1.0f);
}

void EditorTheme::DrawTitleBar(ImDrawList* drawList, ImVec2 min, ImVec2 max, const char* title) {
    // Godot section panel title header with clean bottom separator
    const float h = 30.0f;
    const ImVec2 barMax(max.x, min.y + h);
    drawList->AddRectFilled(min, barMax, U32(PanelFace));
    drawList->AddLine(ImVec2(min.x, barMax.y), ImVec2(max.x, barMax.y), U32(BorderLight));

    if (title && title[0]) {
        const ImVec2 textPos(min.x + 10.0f, min.y + 7.0f);
        drawList->AddText(textPos, U32(EditorTheme::TextSecondary), title);
    }
}

void EditorTheme::DrawCanvasBackground(ImDrawList* drawList, ImVec2 min, ImVec2 max) {
    drawList->AddRectFilled(min, max, U32(WorkArea));
}

void EditorTheme::DrawFocusRing(ImDrawList* drawList, ImVec2 min, ImVec2 max,
                                float rounding) {
    if (!drawList)
        return;

    // Godot's editor theme uses a separate, expanded StyleBoxFlat for focus
    // instead of replacing the control's normal/hover/pressed style box.
    // ImGui already owns the item geometry, so draw that outside outline here.
    constexpr float expand = 1.0f;
    drawList->AddRect(ImVec2(min.x - expand, min.y - expand),
                      ImVec2(max.x + expand, max.y + expand),
                      U32(Accent), rounding + expand, 0, 2.0f);
}

ImVec4 EditorTheme::GetClearColor() {
    return Desktop;
}

bool EditorTheme::StyledButton(const char* label, const ImVec2& size, AeroButtonKind kind) {
    const float h = size.y > 0.0f ? size.y : ButtonHeight;

    ImVec4 face = BtnFace;
    ImVec4 hover = BtnFaceLight;
    ImVec4 active = BtnDarkShadow;
    ImVec4 text = TextPrimary;
    ImVec4 border = BorderLight;
    float borderSize = 1.0f;
    float rounding = ShapeCornerSmall;

    switch (kind) {
    case AeroButtonKind::Danger:
        face = Danger;
        hover = DangerHover;
        active = DangerHover;
        text = SelectionText;
        border = Danger;
        borderSize = 0.0f;
        break;
    case AeroButtonKind::Success:
        face = Success;
        hover = SuccessHover;
        active = SuccessHover;
        text = SelectionText;
        border = Success;
        borderSize = 0.0f;
        break;
    case AeroButtonKind::Primary:
        face = Brand;
        hover = BrandHover;
        active = BrandPressed;
        text = SelectionText;
        border = Brand;
        borderSize = 0.0f;
        break;
    case AeroButtonKind::Secondary:
    default:
        break;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, std::max(1.0f, borderSize));
    ImGui::PushStyleColor(ImGuiCol_Border, border);
    ImGui::PushStyleColor(ImGuiCol_Button, face);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
    ImGui::PushStyleColor(ImGuiCol_Text, text);

    const bool pressed = ImGui::Button(label, ImVec2(size.x, h));
    DrawButtonBevel(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(),
                    ImGui::GetItemRectMax(), ImGui::IsItemActive());
    if (ImGui::IsItemFocused()) {
        DrawFocusRing(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(),
                      ImGui::GetItemRectMax(), rounding);
    }

    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(3);
    return pressed;
}

bool EditorTheme::Checkbox(const char* label, bool* value) {
    if (!label || !value)
        return false;

    const ImGuiStyle& style = ImGui::GetStyle();
    const float hitHeight = ImGui::GetFrameHeight();
    // Keep a full-height hit target, but use the compact visual size found in
    // Godot's inspector. The old square filled the entire row and collided
    // visually with component-card borders.
    const float size = std::max(14.0f, std::floor(hitHeight * 0.68f));
    const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
    const float labelGap = labelSize.x > 0.0f ? style.ItemInnerSpacing.x : 0.0f;
    const ImVec2 itemMin = ImGui::GetCursorScreenPos();
    const ImVec2 min(itemMin.x, itemMin.y + std::floor((hitHeight - size) * 0.5f));
    const ImVec2 max(min.x + size, min.y + size);
    const ImVec2 itemSize(size + labelGap + labelSize.x, hitHeight);

    const bool pressed = ImGui::InvisibleButton(label, itemSize);
    if (pressed)
        *value = !*value;

    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    ImVec4 fill = *value ? Selection : InputBg;
    if (held)
        fill = *value ? AccentActive : BtnDarkShadow;
    else if (hovered)
        fill = *value ? AccentHover : BtnFaceLight;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(min, max, ImGui::GetColorU32(fill), ShapeCornerExtraSmall);
    drawList->AddRect(min, max,
                      ImGui::GetColorU32(*value ? Selection : BorderLight),
                      ShapeCornerExtraSmall, 0, 1.0f);

    if (*value) {
        const float scale = size / 18.0f;
        const ImVec2 checkA(min.x + 4.5f * scale, min.y + 9.2f * scale);
        const ImVec2 checkB(min.x + 7.4f * scale, min.y + 12.0f * scale);
        const ImVec2 checkC(min.x + 13.5f * scale, min.y + 5.7f * scale);
        const ImU32 checkColor = ImGui::GetColorU32(SelectionText);
        const float thickness = std::max(1.5f, 1.7f * scale);
        drawList->AddLine(checkA, checkB, checkColor, thickness);
        drawList->AddLine(checkB, checkC, checkColor, thickness);
    }

    if (labelSize.x > 0.0f) {
        const char* renderedEnd = label;
        while (*renderedEnd && !(renderedEnd[0] == '#' && renderedEnd[1] == '#'))
            ++renderedEnd;
        const ImVec2 textPos(max.x + labelGap,
                             itemMin.y + (hitHeight - labelSize.y) * 0.5f);
        drawList->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text),
                          label, renderedEnd);
    }

    if (ImGui::IsItemFocused())
        DrawFocusRing(drawList, min, max, ShapeCornerExtraSmall);

    return pressed;
}

void EditorTheme::Apply() {
    // Godot 4 Editor Theme StyleBoxFlat configuration
    ImGuiStyle& s = ImGui::GetStyle();
    ImVec4* c = s.Colors;

    const float rSm = ShapeCornerSmall; // 4.0f

    s.WindowRounding = 0.0f;
    s.ChildRounding = 3.0f;
    s.FrameRounding = rSm;
    s.PopupRounding = ShapeCornerMedium; // 6.0f
    s.ScrollbarRounding = rSm;
    s.GrabRounding = rSm;
    s.TabRounding = rSm;

    s.WindowPadding = ImVec2(8.0f, 8.0f);
    s.FramePadding = ImVec2(8.0f, 4.0f);
    s.ItemSpacing = ImVec2(6.0f, 4.0f);
    s.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    s.CellPadding = ImVec2(6.0f, 3.0f);
    s.ScrollbarSize = 11.0f;
    s.GrabMinSize = 8.0f;
    s.IndentSpacing = 14.0f;

    s.WindowBorderSize = 1.0f;
    s.ChildBorderSize = 1.0f;
    s.PopupBorderSize = 1.0f;
    s.FrameBorderSize = 1.0f;
    s.TabBorderSize = 1.0f;
    s.TabBarBorderSize = 1.0f;
    // Dock tabs get a custom straight, inset overline in EditorApp. ImGui's
    // built-in overline follows TabRounding and produces an unwanted blue arc.
    s.TabBarOverlineSize = 0.0f;
    // Godot's editor exposes parent/child relationships instead of relying on
    // indentation alone. Dear ImGui's native tree-line renderer gives us the
    // same behavior without changing any row or dock geometry.
    s.TreeLinesFlags = ImGuiTreeNodeFlags_DrawLinesToNodes;
    s.TreeLinesSize = 1.0f;
    s.TreeLinesRounding = ShapeCornerSmall;
    s.SeparatorTextBorderSize = 1.0f;
    s.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
    s.SeparatorTextPadding = ImVec2(8.0f, s.FramePadding.y);
    s.CircleTessellationMaxError = 0.20f;
    s.CurveTessellationTol = 1.0f;
    // UI chrome is made from one-pixel rules. Line antialiasing spreads those
    // rules over neighbouring pixels and makes compact inputs/buttons look as
    // though the whole editor was rendered at a lower resolution. Keep filled
    // rounded corners antialiased, but rasterize borders and icon strokes crisply.
    s.AntiAliasedLines = false;
    s.AntiAliasedLinesUseTex = false;
    s.AntiAliasedFill = true;

    c[ImGuiCol_Text] = TextPrimary;
    c[ImGuiCol_TextDisabled] = TextDisabled;
    c[ImGuiCol_WindowBg] = PanelFace;
    c[ImGuiCol_ChildBg] = PanelFace;
    c[ImGuiCol_PopupBg] = PanelFace;
    c[ImGuiCol_Border] = BorderLight;
    c[ImGuiCol_BorderShadow] = BtnHighlight;

    c[ImGuiCol_FrameBg] = SurfaceContainerHigh;
    c[ImGuiCol_FrameBgHovered] = SurfaceHover;
    c[ImGuiCol_FrameBgActive] = SurfaceContainerHigh;

    c[ImGuiCol_TitleBg] = PanelAlt;
    c[ImGuiCol_TitleBgActive] = PanelFace;
    c[ImGuiCol_TitleBgCollapsed] = WorkArea;

    c[ImGuiCol_MenuBarBg] = PanelFace;

    c[ImGuiCol_ScrollbarBg] = PanelAlt;
    c[ImGuiCol_ScrollbarGrab] = BorderStrong;
    c[ImGuiCol_ScrollbarGrabHovered] = SurfaceHover;
    c[ImGuiCol_ScrollbarGrabActive] = Accent;

    c[ImGuiCol_CheckMark] = TextPrimary;
    c[ImGuiCol_SliderGrab] = Accent;
    c[ImGuiCol_SliderGrabActive] = AccentActive;

    c[ImGuiCol_Button] = BtnFace;
    c[ImGuiCol_ButtonHovered] = BtnFaceLight;
    c[ImGuiCol_ButtonActive] = BtnDarkShadow;

    c[ImGuiCol_Header] = Selection;
    c[ImGuiCol_HeaderHovered] = SurfaceHover;
    c[ImGuiCol_HeaderActive] = AccentActive;

    c[ImGuiCol_Separator] = BorderLight;
    c[ImGuiCol_SeparatorHovered] = Accent;
    c[ImGuiCol_SeparatorActive] = AccentHover;

    c[ImGuiCol_ResizeGrip] = SurfaceContainerHigh;
    c[ImGuiCol_ResizeGripHovered] = Accent;
    c[ImGuiCol_ResizeGripActive] = AccentHover;

    c[ImGuiCol_Tab] = PanelAlt;
    c[ImGuiCol_TabHovered] = SurfaceHover;
    c[ImGuiCol_TabSelected] = SurfaceContainerHigh;
    c[ImGuiCol_TabSelectedOverline] = Brand;
    c[ImGuiCol_TabDimmed] = PanelAlt;
    c[ImGuiCol_TabDimmedSelected] = SurfaceContainerHigh;
    c[ImGuiCol_TabDimmedSelectedOverline] = UiTokens::WithAlpha(Brand, 0.6f);

    c[ImGuiCol_TabActive] = SurfaceContainerHigh;
    c[ImGuiCol_TabUnfocused] = PanelAlt;
    c[ImGuiCol_TabUnfocusedActive] = SurfaceContainerHigh;

    c[ImGuiCol_DockingPreview] = UiTokens::WithAlpha(Accent, 0.35f);
    c[ImGuiCol_DockingEmptyBg] = WorkArea;

    c[ImGuiCol_TableHeaderBg] = SurfaceContainerHigh;
    c[ImGuiCol_TableBorderStrong] = BorderStrong;
    c[ImGuiCol_TableBorderLight] = BorderLight;
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = UiTokens::WithAlpha(Accent, 0.04f);

    c[ImGuiCol_PlotLines] = Accent;
    c[ImGuiCol_PlotHistogram] = UiTokens::Component;
    c[ImGuiCol_NavCursor] = Accent;
    c[ImGuiCol_NavWindowingHighlight] = Accent;
    c[ImGuiCol_InputTextCursor] = TextPrimary;
    c[ImGuiCol_TextLink] = LinkCyan;
    c[ImGuiCol_TreeLines] = UiTokens::WithAlpha(BorderStrong, 0.65f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);

    c[ImGuiCol_DragDropTarget] = UiTokens::WithAlpha(Accent, 0.45f);
    c[ImGuiCol_TextSelectedBg] = UiTokens::WithAlpha(Accent, 0.35f);
}

float EditorTheme::GetDefaultFontSizeForDisplay() {
#ifdef _WIN32
    // Query DPI of the primary monitor.
    HDC hdc = GetDC(nullptr);
    if (hdc) {
        const int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(nullptr, hdc);
        // 96 DPI = 100% = 13px baseline
        // Scale proportionally: size = 13 * (dpi / 96)
        const float scale = static_cast<float>(dpiX) / 96.0f;
        // Also factor in vertical resolution for very high-res displays.
        const int screenH = GetSystemMetrics(SM_CYSCREEN);
        float sizeFromRes = 13.0f;
        if (screenH >= 2160)      sizeFromRes = 16.0f; // 4K
        else if (screenH >= 1440) sizeFromRes = 14.0f; // 1440p
        else                      sizeFromRes = 13.0f; // 1080p and below
        // Take the larger of DPI-scaled and resolution-based values.
        return std::max(sizeFromRes, std::round(13.0f * scale));
    }
#endif
    return 13.0f;
}

static void LoadFontsInternal(float fontSize) {
    ImGuiIO& io = ImGui::GetIO();

    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    cfg.PixelSnapH = true;
    cfg.RasterizerMultiply = 1.10f;

    const std::filesystem::path regular = ResolveBundledFont("Inter-Regular.ttf");

    ImFont* font = nullptr;
    if (!regular.empty())
        font = io.Fonts->AddFontFromFileTTF(regular.string().c_str(), fontSize, &cfg);

    if (!font) {
        const char* fallbacks[] = {
            "C:\\Windows\\Fonts\\SegUIVar.ttf",
            "C:\\Windows\\Fonts\\segoeui.ttf",
        };
        for (const char* path : fallbacks) {
            if (std::filesystem::exists(path)) {
                font = io.Fonts->AddFontFromFileTTF(path, fontSize, &cfg);
                break;
            }
        }
    }

    if (!font)
        io.Fonts->AddFontDefault(&cfg);

    const char* cjkPaths[] = {
        "C:\\Windows\\Fonts\\meiryo.ttc",
        "C:\\Windows\\Fonts\\YuGothM.ttc",
        "C:\\Windows\\Fonts\\YuGothR.ttc",
    };
    ImFontConfig cjkCfg;
    cjkCfg.MergeMode = true;
    cjkCfg.OversampleH = 2;
    cjkCfg.OversampleV = 2;
    cjkCfg.PixelSnapH = true;
    cjkCfg.RasterizerMultiply = 1.10f;
    for (const char* path : cjkPaths) {
        if (std::filesystem::exists(path)) {
            io.Fonts->AddFontFromFileTTF(path, fontSize, &cjkCfg,
                                         io.Fonts->GetGlyphRangesJapanese());
            break;
        }
    }

    io.FontGlobalScale = 1.0f;
}

void EditorTheme::LoadFonts(float fontSize) {
    if (fontSize <= 0.0f)
        fontSize = GetDefaultFontSizeForDisplay();
    LoadFontsInternal(fontSize);
}

void EditorTheme::RebuildFonts(float fontSize) {
    if (fontSize <= 0.0f)
        fontSize = GetDefaultFontSizeForDisplay();
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    LoadFontsInternal(fontSize);
    io.Fonts->Build();
}

} // namespace MipsyncEngine
