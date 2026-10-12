#include "EditorComponentPicker.h"
#include "EditorTheme.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cctype>
#include <sstream>

namespace MipsyncEngine {
namespace {
std::string Fold(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
bool Matches(const std::string& text, const char* query) {
    const std::string folded = Fold(text);
    std::istringstream words(Fold(query));
    for (std::string word; words >> word;)
        if (folded.find(word) == std::string::npos) return false;
    return true;
}
std::string Leaf(const std::string& path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}
}

void EditorComponentPicker::Open(const char* popupId) {
    m_Search[0] = '\0';
    m_Path.clear();
    m_CreateScript = false;
    m_InitializeScript = false;
    // Anchor to the button rather than the click position. In a docked
    // inspector the right side may require flipping/clamping the popup;
    // its opening click must stay outside the result rows.
    m_PopupX = ImGui::GetItemRectMin().x;
    m_PopupY = ImGui::GetItemRectMax().y + 4 * ImGui::GetFontSize() / 14.0f;
    m_AnchorY = m_PopupY;
    m_PopupHeight = 0;
    m_AdjustPosition = true;
    m_FocusSearch = true;
    m_OpenFrame = ImGui::GetFrameCount();
    ImGui::OpenPopup(popupId);
}

bool EditorComponentPicker::Begin(const char* popupId) {
    const float scale = ImGui::GetFontSize() / 14.0f;
    const auto* viewport = ImGui::GetWindowViewport();
    // Keep the picker on the inspector's viewport. Otherwise a popup that
    // starts below its bottom edge can become a separate platform window;
    // subsequent work-area calculations then use the popup's own tiny area.
    ImGui::SetNextWindowViewport(viewport->ID);
    const float margin = 4 * scale;
    const float width = std::max(1.0f, std::min(280.0f * scale, viewport->WorkSize.x - 2 * margin));
    const float maxHeight = std::max(1.0f, viewport->WorkSize.y - 2 * margin);
    ImGui::SetNextWindowSize(ImVec2(width, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0), ImVec2(width, maxHeight));
    if (m_AdjustPosition) {
        // BeginPopup can return false for a fully clipped window. Keep even a
        // fresh popup inside the work area BEFORE Begin; Draw's later size
        // correction alone cannot rescue an opening below the screen edge.
        const float estimatedHeight = std::min(maxHeight, m_PopupHeight > 0 ? m_PopupHeight : 300 * scale +
            ImGui::GetFrameHeight() + ImGui::GetFontSize() + 23 * scale +
            ImGui::GetStyle().ItemSpacing.y * 4);
        m_PopupX = std::clamp(m_PopupX, viewport->WorkPos.x + margin,
            viewport->WorkPos.x + viewport->WorkSize.x - width - margin);
        m_PopupY = std::clamp(m_PopupY, viewport->WorkPos.y + margin,
            viewport->WorkPos.y + viewport->WorkSize.y - estimatedHeight - margin);
        ImGui::SetNextWindowPos(ImVec2(m_PopupX, m_PopupY));
        m_AdjustPosition = false;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(7 * scale, 7 * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 3 * scale);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, EditorTheme::PanelFace);
    if (!ImGui::BeginPopup(popupId)) {
        ImGui::PopStyleColor(); ImGui::PopStyleVar(2);
        return false;
    }
    m_Entries.clear();
    m_CategoryStack.clear();
    return true;
}

bool EditorComponentPicker::BeginCategory(const char* label) {
    m_CategoryStack.emplace_back(label);
    return true;
}
void EditorComponentPicker::EndCategory() {
    if (!m_CategoryStack.empty()) m_CategoryStack.pop_back();
}
void EditorComponentPicker::Add(const char* label, std::function<void()> action, bool enabled,
                                const std::string& detail) {
    std::string category;
    for (const auto& part : m_CategoryStack) {
        if (!category.empty()) category += '/';
        category += part;
    }
    m_Entries.push_back({category, label, detail, std::move(action), enabled, false});
}
void EditorComponentPicker::AddCreateScript() {
    Add("New Script", {});
    m_Entries.back().createScript = true;
}

void EditorComponentPicker::Draw(const std::function<void(const char*)>& drawCreateScript) {
    const float scale = ImGui::GetFontSize() / 14.0f;
    const bool opening = m_OpenFrame == ImGui::GetFrameCount();
    const float rowHeight = ImGui::GetFontSize() + 9 * scale;
    const bool focusSearch = m_FocusSearch;
    m_FocusSearch = false;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2 * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20 * scale, 3 * scale));
    ImGui::SetNextItemWidth(-1);
    if (focusSearch) ImGui::SetKeyboardFocusHere();
    if (ImGui::InputTextWithHint("##component_search", "Search components", m_Search, sizeof(m_Search))) {
        m_CreateScript = false;
        m_Path.clear();
    }
    const bool searchActive = ImGui::IsItemActive();
    bool focusFirst = searchActive && ImGui::IsKeyPressed(ImGuiKey_DownArrow);
    const ImVec2 searchMin = ImGui::GetItemRectMin(), searchMax = ImGui::GetItemRectMax();
    auto* searchDraw = ImGui::GetWindowDrawList();
    const ImVec2 lens(searchMin.x + 9 * scale, (searchMin.y + searchMax.y) * 0.5f - scale);
    const ImU32 searchColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    searchDraw->AddCircle(lens, 3 * scale, searchColor, 12, scale);
    searchDraw->AddLine(ImVec2(lens.x + 2 * scale, lens.y + 2 * scale),
                        ImVec2(lens.x + 5 * scale, lens.y + 5 * scale), searchColor, scale);
    if (m_Search[0]) {
        const ImVec2 center(searchMax.x - 9 * scale, (searchMin.y + searchMax.y) * 0.5f);
        searchDraw->AddLine(ImVec2(center.x - 3 * scale, center.y - 3 * scale),
                            ImVec2(center.x + 3 * scale, center.y + 3 * scale), searchColor, scale);
        searchDraw->AddLine(ImVec2(center.x + 3 * scale, center.y - 3 * scale),
                            ImVec2(center.x - 3 * scale, center.y + 3 * scale), searchColor, scale);
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            ImGui::IsMouseHoveringRect(ImVec2(searchMax.x - 19 * scale, searchMin.y), searchMax)) {
            m_Search[0] = '\0';
            m_Path.clear(); m_CreateScript = false; m_FocusSearch = true;
            if (auto* state = ImGui::GetInputTextState(ImGui::GetItemID()))
                state->ReloadUserBufAndMoveToEnd();
            ImGui::ClearActiveID();
        }
    }
    ImGui::PopStyleVar(2);

    const bool searching = m_Search[0] != '\0';
    const bool canGoBack = m_CreateScript || !m_Path.empty();
    if (!ImGui::IsAnyItemActive() && canGoBack && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
        if (m_CreateScript) m_CreateScript = false;
        else {
            const auto slash = m_Path.find_last_of('/');
            m_Path = slash == std::string::npos ? "" : m_Path.substr(0, slash);
        }
    }
    const ImVec2 header = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::InvisibleButton("##component_heading", ImVec2(width, rowHeight));
    if (!opening && canGoBack && ImGui::IsItemClicked()) {
        if (m_CreateScript) m_CreateScript = false;
        else {
            const auto slash = m_Path.find_last_of('/');
            m_Path = slash == std::string::npos ? "" : m_Path.substr(0, slash);
        }
    }
    auto* draw = ImGui::GetWindowDrawList();
    if (canGoBack)
        ImGui::RenderArrow(draw, ImVec2(header.x + 3 * scale, header.y + 5 * scale),
                           ImGui::GetColorU32(ImGuiCol_Text), ImGuiDir_Left, 0.8f);
    const std::string title = m_CreateScript ? "New Script" : searching ? "Search Results"
                                             : m_Path.empty() ? "Component" : Leaf(m_Path);
    const float titleWidth = ImGui::CalcTextSize(title.c_str()).x;
    draw->AddText(ImVec2(header.x + std::max(22 * scale, (width - titleWidth) * 0.5f),
                         header.y + 4 * scale), ImGui::GetColorU32(ImGuiCol_Text), title.c_str());
    ImGui::Separator();

    struct Row { std::string label, path; const Entry* entry = nullptr; };
    std::vector<Row> rows;
    const Entry* newScript = nullptr;
    const std::string prefix = m_Path.empty() ? "" : m_Path + '/';
    for (const Entry& entry : m_Entries) {
        if (entry.createScript) { newScript = &entry; continue; }
        if (searching) {
            if (Matches(entry.category + " " + entry.label + " " + entry.detail, m_Search))
                rows.push_back({entry.label, entry.category, &entry});
        } else if (entry.category == m_Path) {
            rows.push_back({entry.label, entry.category, &entry});
        } else if (entry.category.starts_with(prefix)) {
            const auto end = entry.category.find('/', prefix.size());
            const std::string category = entry.category.substr(0, end);
            if (std::none_of(rows.begin(), rows.end(), [&](const Row& r) { return !r.entry && r.path == category; }))
                rows.push_back({Leaf(category), category, nullptr});
        }
    }
    if (newScript) rows.push_back({newScript->label, "", newScript});
    const auto* viewport = ImGui::GetWindowViewport();
    const float maxHeight = std::max(rowHeight * 2, std::min(300 * scale,
        viewport->WorkSize.y - ImGui::GetCursorPosY() - 20 * scale));
    const float height = m_CreateScript ? std::min(116 * scale, maxHeight)
        : std::clamp(float(rows.size()) * rowHeight, rowHeight * 2, maxHeight);
    const ImVec2 popupPos = ImGui::GetWindowPos();
    const float popupHeight = ImGui::GetCursorPosY() + height + 7 * scale;
    m_PopupHeight = popupHeight;
    m_PopupX = std::max(viewport->WorkPos.x + 4 * scale,
        std::min(popupPos.x, viewport->WorkPos.x + viewport->WorkSize.x - width - 18 * scale));
    m_PopupY = std::max(viewport->WorkPos.y + 4 * scale,
        std::min(m_AnchorY, viewport->WorkPos.y + viewport->WorkSize.y - popupHeight - 4 * scale));
    m_AdjustPosition = m_PopupX != popupPos.x || m_PopupY != popupPos.y;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, EditorTheme::Selection);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, EditorTheme::Selection);
    ImGui::BeginChild("##component_results", ImVec2(0, height), false);
    std::function<void()> chosen;
    std::string nextPath;
    bool createNext = false;
    if (m_CreateScript) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6 * scale, 7 * scale));
        if (drawCreateScript) drawCreateScript(m_InitializeScript ? m_Search : nullptr);
        m_InitializeScript = false;
        ImGui::PopStyleVar();
    } else {
        for (size_t index = 0; index < rows.size(); ++index) {
            const Row& row = rows[index];
            ImGui::PushID(int(index));
            const bool enabled = !row.entry || row.entry->enabled;
            ImGui::BeginDisabled(!enabled);
            if (focusFirst) { ImGui::SetKeyboardFocusHere(); focusFirst = false; }
            const ImVec2 start = ImGui::GetCursorScreenPos();
            const bool picked = ImGui::Selectable("##component_row", false,
                ImGuiSelectableFlags_DontClosePopups, ImVec2(0, rowHeight));
            if (!opening && (picked || (enabled && !row.entry && ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_RightArrow)))) {
                if (!row.entry) nextPath = row.path;
                else if (row.entry->createScript) createNext = true;
                else chosen = row.entry->action;
            }
            const float right = start.x + ImGui::GetContentRegionAvail().x;
            const ImVec4 clip(start.x, start.y, right - 18 * scale, start.y + rowHeight);
            draw = ImGui::GetWindowDrawList();
            draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                           ImVec2(start.x + 9 * scale, start.y + 4 * scale),
                           ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled),
                           row.label.c_str(), nullptr, 0, &clip);
            if (!row.entry || row.entry->createScript)
                ImGui::RenderArrow(draw, ImVec2(right - 15 * scale, start.y + 5 * scale),
                                   ImGui::GetColorU32(ImGuiCol_TextDisabled), ImGuiDir_Right, 0.8f);
            if (ImGui::IsItemHovered() && row.entry && !row.entry->createScript && (searching || !row.entry->detail.empty()))
                ImGui::SetTooltip("%s", (row.entry->detail.empty() ? row.path : row.entry->detail).c_str());
            ImGui::EndDisabled(); ImGui::PopID();
        }
        if (rows.empty()) {
            ImGui::SetCursorPos(ImVec2(9 * scale, 6 * scale));
            ImGui::TextDisabled("No matching components");
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2); ImGui::PopStyleVar();
    if (!nextPath.empty()) m_Path = nextPath;
    if (createNext) { m_CreateScript = true; m_InitializeScript = true; }
    if (chosen) chosen();
}

void EditorComponentPicker::End() {
    ImGui::EndPopup();
    ImGui::PopStyleColor(); ImGui::PopStyleVar(2);
}
} // namespace MipsyncEngine
