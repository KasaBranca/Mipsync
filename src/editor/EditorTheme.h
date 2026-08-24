#pragma once
// ─────────────────────────────────────────────────
// Mipsync Engine — Figma UI3-inspired editor design tokens
// ─────────────────────────────────────────────────

#include <imgui.h>

namespace MipsyncEngine {

namespace UiTokens {

constexpr ImVec4 Hex(unsigned rgb) {
    const float r = ((rgb >> 16) & 0xFF) / 255.0f;
    const float g = ((rgb >> 8) & 0xFF) / 255.0f;
    const float b = (rgb & 0xFF) / 255.0f;
    return { r, g, b, 1.0f };
}

constexpr ImVec4 Rgba(unsigned rgb, unsigned a) {
    const float r = ((rgb >> 16) & 0xFF) / 255.0f;
    const float g = ((rgb >> 8) & 0xFF) / 255.0f;
    const float b = (rgb & 0xFF) / 255.0f;
    return { r, g, b, static_cast<float>(a) / 255.0f };
}

constexpr ImVec4 WithAlpha(ImVec4 c, float a) {
    return { c.x, c.y, c.z, a };
}

// ── Figma UI3 dark semantics ─────────────────────
// Canvas-first hierarchy: near-black work area, neutral docked panels, visible
// input fills, and one blue interaction accent. Decorative chrome is avoided.
static constexpr ImVec4 Bg                = Hex(0x1E1E1E);
static constexpr ImVec4 BgSecondary       = Hex(0x2C2C2C);
static constexpr ImVec4 BgTertiary        = Hex(0x383838);
static constexpr ImVec4 BgHover           = Hex(0x444444);
static constexpr ImVec4 BgPressed         = Hex(0x242424);
static constexpr ImVec4 BgSelected        = Hex(0x0D99FF);
static constexpr ImVec4 BgSelectedStrong  = Hex(0x0D99FF);

// brand / interactive
static constexpr ImVec4 Brand             = Hex(0x0D99FF);
static constexpr ImVec4 BrandHover        = Hex(0x3AA9FF);
static constexpr ImVec4 BrandPressed      = Hex(0x0878C9);
static constexpr ImVec4 BrandSecondary    = Hex(0x0D99FF);
static constexpr ImVec4 BrandTertiary     = Hex(0x164B70);

// accent purple (variants / components)
static constexpr ImVec4 Component         = Hex(0x9747FF);
static constexpr ImVec4 ComponentHover    = Hex(0x8638E5);

// text
static constexpr ImVec4 Text              = Hex(0xFFFFFF);
static constexpr ImVec4 TextSecondary     = Hex(0xB3B3B3);
static constexpr ImVec4 TextTertiary      = Hex(0x8C8C8C);
static constexpr ImVec4 TextDisabled      = Hex(0x666666);
static constexpr ImVec4 TextOnBrand       = Hex(0xFFFFFF);
static constexpr ImVec4 TextBrand         = Hex(0x0D99FF);

// borders
static constexpr ImVec4 Border            = Hex(0x444444);
static constexpr ImVec4 BorderHighlight   = Hex(0x5C5C5C);
static constexpr ImVec4 BorderStrong      = Hex(0x707070);
static constexpr ImVec4 BorderSelected    = Hex(0x0D99FF);

// feedback
static constexpr ImVec4 Danger            = Hex(0xF24822);
static constexpr ImVec4 DangerHover       = Hex(0xDC3412);
static constexpr ImVec4 DangerBg          = Hex(0x3A2018);
static constexpr ImVec4 Success           = Hex(0x14AE5C);
static constexpr ImVec4 SuccessHover      = Hex(0x009951);
static constexpr ImVec4 SuccessBg         = Hex(0x142A1E);
static constexpr ImVec4 Warning           = Hex(0xFFCD29);
static constexpr ImVec4 WarningText       = Hex(0xFAB815);

// icons
static constexpr ImVec4 Icon              = Rgba(0xFFFFFF, 0xE5);
static constexpr ImVec4 IconSecondary     = Rgba(0xFFFFFF, 0x80);
static constexpr ImVec4 IconBrand         = Hex(0x007BE5);

} // namespace UiTokens

enum class AeroButtonKind {
    Primary,
    Secondary,
    Success,
    Danger,
};

class EditorTheme {
public:
    static void Apply();
    /// Switch the runtime palette. The engine keeps dark as its default;
    /// individual tools may opt into the light palette before calling Apply().
    static void SetDarkMode(bool dark);
    static bool IsDarkMode();
    /// Returns a font size appropriate for the primary monitor's DPI/resolution.
    /// ~12px on 1080p, ~14px on 1440p, ~16px+ on 4K.
    static float GetDefaultFontSizeForDisplay();
    /// Load fonts at the given pixel size. Call once at startup.
    static void LoadFonts(float fontSize = 0.0f); // 0 = auto-detect
    /// Rebuild the font atlas at a new size (clears old atlas). Must be called
    /// before ImGui renders the next frame; call io.Fonts->Build() afterwards.
    static void RebuildFonts(float fontSize);

    static void DrawCanvasBackground(ImDrawList* drawList, ImVec2 min, ImVec2 max);
    static void DrawTitleBar(ImDrawList* drawList, ImVec2 min, ImVec2 max, const char* title);
    static bool StyledButton(const char* label, const ImVec2& size, AeroButtonKind kind = AeroButtonKind::Primary);
    static bool Checkbox(const char* label, bool* value);

    static ImVec4 GetClearColor();

    // ── Semantic aliases (used across editor panels) ──
    static ImVec4 BtnFace;
    static ImVec4 BtnFaceLight;
    static ImVec4 BtnShadow;
    static ImVec4 BtnDarkShadow;
    static ImVec4 BtnHighlight;

    static ImVec4 PanelFace;
    static ImVec4 PanelAlt;
    static ImVec4 WorkArea;
    static ImVec4 Desktop;
    static ImVec4 InputBg;

    static ImVec4 TitleBarTop;
    static ImVec4 TitleBarBottom;
    static ImVec4 TitleBarText;

    static ImVec4 Selection;
    static ImVec4 SelectionText;

    static ImVec4 PsAccent;
    static ImVec4 LinkCyan;
    static ImVec4 TextBrand;

    static ImVec4 TextPrimary;
    static ImVec4 TextSecondary;
    static ImVec4 TextMuted;
    static ImVec4 TextDisabled;

    static ImVec4 Error;
    static ImVec4 ErrorBg;
    static ImVec4 SuccessLabel;
    static ImVec4 DangerFace;
    static ImVec4 DangerText;
    static ImVec4 Warning;

    static ImVec4 BorderLight;
    static ImVec4 BorderDark;

    static constexpr float ButtonHeight          = 27.0f;
    static constexpr float ShapeCornerExtraSmall = 2.0f;
    static constexpr float ShapeCornerSmall      = 4.0f;
    static constexpr float ShapeCornerMedium     = 6.0f;
    static constexpr float ShapeCornerLarge      = 8.0f;
    static constexpr float ShapeCornerExtraLarge = 12.0f;

    // Legacy aliases
    static ImVec4 Background;
    static ImVec4 Accent;
    static ImVec4 AccentHover;
    static ImVec4 AccentActive;
    static ImVec4 AccentMuted;
    static ImVec4 Primary;
    static ImVec4 OnPrimary;
    static ImVec4 SurfaceContainer;
    static ImVec4 SurfaceContainerHigh;
    static ImVec4 SurfaceRaised;
    static ImVec4 SurfaceHover;
    static ImVec4 OnSurface;
    static ImVec4 OnSurfaceVariant;
    static ImVec4 Outline;
    static ImVec4 OutlineVariant;
    static ImVec4 PanelBackground;
    static ImVec4 GlassPanel;
    static ImVec4 Border;
    static ImVec4 BorderStrong;
    static ImVec4 Success;
    static ImVec4 SuccessHover;
    static ImVec4 Danger;
    static ImVec4 DangerHover;

    static void DrawSkyGradient(ImDrawList* drawList, ImVec2 min, ImVec2 max) {
        DrawCanvasBackground(drawList, min, max);
    }

    static bool AeroButton(const char* label, const ImVec2& size, AeroButtonKind kind = AeroButtonKind::Primary) {
        return StyledButton(label, size, kind);
    }

private:
    static void DrawButtonBevel(ImDrawList* drawList, ImVec2 min, ImVec2 max, bool pressed);
};

} // namespace MipsyncEngine
