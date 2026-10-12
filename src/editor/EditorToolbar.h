#pragma once

#include "EditorTheme.h"
#include "EditorDropdown.h"
#include "EditorSurface.h"
#include "EditorIcons.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace MipsyncEngine {

// Toolbar cells have no individual outline. Draw their shared boundaries once,
// after the controls, so adjacent fills cannot cover or double the divider.
class EditorToolbar {
public:
    explicit EditorToolbar(float height = 28.0f)
        : m_Height(std::max(height, ImGui::GetTextLineHeight() + 8.0f)),
          m_ReturnX(ImGui::GetCursorPosX()), m_Y(ImGui::GetCursorPosY()),
          m_Origin(ImGui::GetWindowPos()), m_Width(ImGui::GetWindowWidth()),
          m_Draw(ImGui::GetWindowDrawList()) {
        m_Origin.x -= ImGui::GetScrollX();
        m_Origin.y -= ImGui::GetScrollY();
        if (std::abs(m_Y - ImGui::GetWindowContentRegionMin().y) < 0.5f)
            m_Y -= ImGui::GetStyle().WindowPadding.y;
        ImGui::BeginGroup();
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(8.0f, (m_Height - ImGui::GetTextLineHeight()) * 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, EditorTheme::PanelFace);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, EditorTheme::PanelFace);
        m_Draw->AddRectFilled(ImVec2(m_Origin.x, Top()),
                             ImVec2(m_Origin.x + m_Width, Top() + m_Height),
                             ImGui::ColorConvertFloat4ToU32(EditorTheme::PanelFace));
        Place();
    }

    ~EditorToolbar() { End(); }
    EditorToolbar(const EditorToolbar&) = delete;
    EditorToolbar& operator=(const EditorToolbar&) = delete;

    float Height() const { return m_Height; }
    float Width() const { return m_Width; }
    float CursorX() const { return m_X; }

    void SetX(float x) { m_X = std::max(x, m_X); Place(); }

    bool Button(const char* label, float width = 0.0f) {
        Place();
        const bool pressed = ImGui::Button(label, ImVec2(width, m_Height));
        Item();
        return pressed;
    }

    static float IconButtonWidth(const char* label) {
        return ImGui::CalcTextSize(label, nullptr, true).x + 16.0f + ImGui::GetFontSize() + 4.0f;
    }

    bool IconButton(const char* label, ToolbarIcon icon, float width = 0.0f) {
        Place();
        const float iconSize = ImGui::GetFontSize();
        const ImVec2 padding = ImGui::GetStyle().FramePadding;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padding.x + iconSize + 4.0f, padding.y));
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
        const bool pressed = ImGui::Button(label, ImVec2(width > 0.0f ? width : IconButtonWidth(label), m_Height));
        ImGui::PopStyleVar(2);
        const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        m_Draw->PushClipRect(min, max, true);
        DrawToolbarIcon(icon, m_Draw, ImVec2(min.x + padding.x + iconSize * 0.5f, (min.y + max.y) * 0.5f),
                        iconSize, ImGui::GetColorU32(ImGuiCol_Text));
        m_Draw->PopClipRect();
        Item();
        return pressed;
    }

    bool BeginCombo(const char* id, const char* preview, float width) {
        Place();
        ImGui::SetNextItemWidth(width);
        Record(m_X, m_X + width);
        m_X += width;
        return EditorDropdown::BeginCombo(id, preview);
    }

    bool DragInt(const char* label, int* value, float width, float speed,
                 int min, int max) {
        Place();
        ImGui::SetNextItemWidth(width);
        const bool changed = ImGui::DragInt(label, value, speed, min, max);
        Item();
        return changed;
    }

    void Text(const char* text, const ImVec4& color, float width = 0.0f) {
        Place();
        const ImVec2 textSize = ImGui::CalcTextSize(text);
        const float cellWidth = width > 0.0f ? width : textSize.x + 16.0f;
        ImGui::Dummy(ImVec2(cellWidth, m_Height));
        m_Draw->PushClipRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), true);
        m_Draw->AddText(ImVec2(m_Origin.x + m_X + 8.0f,
                              Top() + (m_Height - textSize.y) * 0.5f),
                        ImGui::ColorConvertFloat4ToU32(color), text);
        m_Draw->PopClipRect();
        Item();
    }

    // Register a custom control immediately after drawing it.
    void Item(bool sharedDivider = true) {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        if (sharedDivider) Record(min.x - m_Origin.x, max.x - m_Origin.x);
        m_X = max.x - m_Origin.x;
        Place();
    }

    // Connected segments share a capsule silhouette, with round corners only at its ends.
    // Call after the hit target, before its icon; register the item with Item(false).
    void CapsuleFace(const ImVec4& face, int index, int count, float inset = 3.0f) {
        ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        min.y += inset;
        max.y -= inset;
        ImDrawFlags corners = ImDrawFlags_RoundCornersNone;
        if (index == 0) corners = ImDrawFlags_RoundCornersLeft;
        if (index == count - 1)
            corners = index == 0 ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersRight;
        EditorSurface::DrawRaisedFace(m_Draw, min, max, ImGui::ColorConvertFloat4ToU32(face),
                                      (max.y - min.y) * 0.5f, 0, 0,
                                      ImGui::IsItemActive(), 1.0f, corners);
        if (index > 0)
            m_Draw->AddLine(ImVec2(min.x + 0.5f, min.y), ImVec2(min.x + 0.5f, max.y),
                            ImGui::ColorConvertFloat4ToU32(EditorTheme::Border));
    }

    void End() {
        if (m_Ended) return;
        m_Ended = true;
        const ImU32 border = ImGui::ColorConvertFloat4ToU32(EditorTheme::Border);
        std::sort(m_Boundaries.begin(), m_Boundaries.end());
        float previous = -1.0f;
        for (float x : m_Boundaries) {
            x = std::floor(x);
            if (x <= 0.0f || x >= m_Width || x == previous) continue;
            previous = x;
            m_Draw->AddLine(ImVec2(m_Origin.x + x + 0.5f, Top()),
                            ImVec2(m_Origin.x + x + 0.5f, Top() + m_Height), border);
        }
        m_Draw->AddLine(ImVec2(m_Origin.x, Top() + m_Height - 0.5f),
                        ImVec2(m_Origin.x + m_Width, Top() + m_Height - 0.5f), border);
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);
        ImGui::EndGroup();
        ImGui::SetCursorPos(ImVec2(m_ReturnX, m_Y + m_Height));
    }

private:
    float Top() const { return m_Origin.y + m_Y; }
    void Place() { ImGui::SetCursorPos(ImVec2(m_X, m_Y)); }
    void Record(float left, float right) {
        m_Boundaries.push_back(left);
        m_Boundaries.push_back(right);
    }
    float m_Height, m_ReturnX, m_Y;
    ImVec2 m_Origin;
    float m_Width, m_X = 0.0f;
    ImDrawList* m_Draw;
    std::vector<float> m_Boundaries;
    bool m_Ended = false;
};

} // namespace MipsyncEngine
