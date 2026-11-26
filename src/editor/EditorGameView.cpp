#include "EditorGameView.h"
#include "EditorToolbar.h"
#include "../renderer/Renderer.h"
#include <imgui_internal.h>
#include <cstdio>
#include <optional>

namespace MipsyncEngine {

bool DrawGameViewControls(GameViewSettings& settings) {
    EditorToolbar toolbar;
    bool changed = false;
    const float gizmoWidth = EditorToolbar::IconButtonWidth("Gizmos");
    float statsWidth = EditorToolbar::IconButtonWidth("Statistics");
    float modeWidth = ImGui::CalcTextSize("Play Maximized").x + 16.0f + ImGui::GetFrameHeight();
    const bool compact = modeWidth + statsWidth + gizmoWidth > toolbar.Width();
    if (compact) {
        statsWidth = EditorToolbar::IconButtonWidth("Stats");
        modeWidth = ImGui::CalcTextSize("Maximized").x + 16.0f + ImGui::GetFrameHeight();
    }
    const bool wrap = 60.0f + statsWidth + gizmoWidth > toolbar.Width();
    modeWidth = wrap ? toolbar.Width() : std::max(60.0f, std::min(modeWidth, toolbar.Width() - statsWidth - gizmoWidth));
    const bool maximized = settings.playMode == GameViewPlayMode::Maximized;
    const char* preview = compact ? (maximized ? "Maximized" : "Focused")
                                  : (maximized ? "Play Maximized" : "Play Focused");
    if (toolbar.BeginCombo("##GamePlayPresentation", preview, modeWidth)) {
        for (GameViewPlayMode mode : {GameViewPlayMode::Focused, GameViewPlayMode::Maximized}) {
            if (EditorDropdown::Selectable(mode == GameViewPlayMode::Focused ? "Play Focused" : "Play Maximized",
                                   settings.playMode == mode)) {
                changed |= settings.playMode != mode;
                settings.playMode = mode;
            }
        }
        EditorDropdown::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Play Focused: use the docked Game View\nPlay Maximized: fill the editor workspace during Play Mode");
    std::optional<EditorToolbar> secondRow;
    EditorToolbar* toggles = &toolbar;
    if (wrap) {
        toolbar.End();
        secondRow.emplace();
        toggles = &*secondRow;
    }
    toggles->SetX(std::max(toggles->CursorX(), toggles->Width() - statsWidth - gizmoWidth));
    auto toggle = [&](const char* label, ToolbarIcon icon, bool& enabled, float width, const char* tooltip) {
        if (toggles->CursorX() > 0.0f && width > toggles->Width() - toggles->CursorX() + 0.5f) {
            toggles->End();
            secondRow.emplace();
            toggles = &*secondRow;
        }
        const bool wasEnabled = enabled;
        if (wasEnabled) ImGui::PushStyleColor(ImGuiCol_Button, EditorTheme::Selection);
        if (toggles->IconButton(label, icon, width)) { enabled = !enabled; changed = true; }
        if (wasEnabled) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    };
    toggle(compact ? "Stats" : "Statistics", ToolbarIcon::Statistics, settings.showStatistics, statsWidth, "Toggle rendering statistics");
    toggle("Gizmos", ToolbarIcon::Gizmos, settings.showGizmos, gizmoWidth, "Toggle scene gizmos, light rays, ranges and previews");
    return changed;
}

bool BeginMaximizedGameView(const ImVec2& position, const ImVec2& size, bool focus) {
    ImGui::SetNextWindowPos(position);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
    if (focus) ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const bool visible = ImGui::Begin("Game View###MaximizedGameView", nullptr,
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavInputs);
    ImGui::PopStyleVar(2);
    // Other docked panels stay alive behind this temporary presentation; their layout is untouched.
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    return visible;
}

void DrawGameViewStatistics(const ImVec2& panelMin, const ImVec2& panelSize,
                            const GameViewSettings& settings, const RenderStatistics& statistics,
                            size_t activeObjects, float frameRate) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float line = ImGui::GetTextLineHeight() + 6.0f;
    const float width = std::min(270.0f * ImGui::GetFontSize() / 14.0f, panelSize.x - 16.0f);
    if (width < 80.0f || panelSize.y < line * 2.0f) return;
    const ImVec2 min(panelMin.x + panelSize.x - width - 8.0f, panelMin.y + 8.0f);
    const float rows = 7.0f;
    const ImVec2 max(min.x + width, std::min(min.y + line * rows + 18.0f, panelMin.y + panelSize.y - 8.0f));
    draw->PushClipRect(min, max, true);
    ImVec4 face = EditorTheme::PanelFace;
    face.w = 0.96f;
    draw->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(face), 3.0f);
    draw->AddRect(min, max, ImGui::ColorConvertFloat4ToU32(EditorTheme::Border), 3.0f);
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    ImVec2 cursor(min.x + 10.0f, min.y + 8.0f);
    draw->AddText(cursor, color, "Statistics");
    cursor.y += line;
    draw->AddLine(ImVec2(min.x + 10.0f, cursor.y - 3.0f), ImVec2(max.x - 10.0f, cursor.y - 3.0f),
                  ImGui::ColorConvertFloat4ToU32(EditorTheme::Border));
    const auto row = [&](const char* label, const char* value) {
        const float valueX = std::max(cursor.x, max.x - 10.0f - ImGui::CalcTextSize(value).x);
        const ImVec4 labelClip(cursor.x, cursor.y, std::max(cursor.x, valueX - 8.0f), cursor.y + line);
        draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), cursor, color, label, nullptr, 0.0f, &labelClip);
        draw->AddText(ImVec2(valueX, cursor.y), color, value);
        cursor.y += line;
    };
    char value[80];
    std::snprintf(value, sizeof(value), "%.1f FPS", frameRate); row("Editor frame rate", value);
    std::snprintf(value, sizeof(value), "%.2f ms", frameRate > 0.0f ? 1000.0f / frameRate : 0.0f); row("Editor frame time", value);
    std::snprintf(value, sizeof(value), "%d x %d", settings.renderWidth, settings.renderHeight); row("Render resolution", value);
    std::snprintf(value, sizeof(value), "%llu", static_cast<unsigned long long>(statistics.drawCalls)); row("3D draw calls", value);
    std::snprintf(value, sizeof(value), "%llu", static_cast<unsigned long long>(statistics.triangles)); row("Triangles", value);
    std::snprintf(value, sizeof(value), "%zu", activeObjects); row("Active objects", value);
    draw->PopClipRect();
}

} // namespace MipsyncEngine
