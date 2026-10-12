#include "EditorMenu.h"
#include <imgui_internal.h>
#include <vector>

namespace MipsyncEngine::EditorMenu {
namespace {
std::vector<ImGuiWindow*> s_Popups;
void PushItemText(const char* label, bool enabled) {
    bool highlighted = false;
    if (enabled && !s_Popups.empty() && !(ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled)) {
        auto* window = ImGui::GetCurrentWindow();
        const auto& style = ImGui::GetStyle();
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const ImVec2 min(start.x - style.WindowPadding.x, start.y - style.ItemSpacing.y * .5f);
        const ImVec2 max(window->Pos.x + window->Size.x,
                         start.y + ImGui::GetTextLineHeight() + style.ItemSpacing.y * .5f);
        highlighted = (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                       ImGui::IsMouseHoveringRect(min, max)) ||
                      (window->NavLastIds[ImGuiNavLayer_Main] == window->GetID(label) &&
                       ImGui::GetCurrentContext()->NavWindow == window &&
                       ImGui::GetCurrentContext()->NavCursorVisible);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, highlighted ? UiTokens::TextOnBrand : ImGui::GetStyleColorVec4(ImGuiCol_Text));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, highlighted ? UiTokens::TextOnBrand : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
}
bool FinishBegin(bool open) {
    if (!open) { PopPopupStyle(); return false; }
    PushItemsStyle(true);
    s_Popups.push_back(ImGui::GetCurrentWindow());
    return true;
}
}

void PushPopupStyle(bool applyBorder) {
    ImGui::PushStyleColor(ImGuiCol_PopupBg, Face);
    ImGui::PushStyleColor(ImGuiCol_Border, applyBorder ? Border : ImGui::GetStyleColorVec4(ImGuiCol_Border));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
}
void PopPopupStyle() { ImGui::PopStyleVar(3); ImGui::PopStyleColor(2); }
void PushItemsStyle(bool nativeMenuRows) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, nativeMenuRows ? 8.0f : 0));
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Text, Text);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, Disabled);
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, UiTokens::Brand);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, UiTokens::BrandPressed);
    ImGui::PushStyleColor(ImGuiCol_Separator, Border);
}
void PopItemsStyle() { ImGui::PopStyleColor(6); ImGui::PopStyleVar(2); }
bool BeginPopup(const char* id, ImGuiWindowFlags flags) {
    PushPopupStyle(); return FinishBegin(ImGui::BeginPopup(id, flags));
}
bool BeginPopupContextItem(const char* id, ImGuiPopupFlags flags) {
    PushPopupStyle(); return FinishBegin(ImGui::BeginPopupContextItem(id, flags));
}
bool BeginPopupContextWindow(const char* id, ImGuiPopupFlags flags) {
    PushPopupStyle(); return FinishBegin(ImGui::BeginPopupContextWindow(id, flags));
}
void EndPopup() {
    const bool styled = !s_Popups.empty() && s_Popups.back() == ImGui::GetCurrentWindow();
    if (styled) { s_Popups.pop_back(); PopItemsStyle(); }
    ImGui::EndPopup();
    if (styled) PopPopupStyle();
}
bool BeginHoverMenu(const char* label, bool enabled) {
    auto* window = ImGui::GetCurrentWindow();
    const auto& context = *ImGui::GetCurrentContext();
    const float padding = ImGui::GetStyle().ItemSpacing.x;
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + ImGui::CalcTextSize(label).x + padding,
                     min.y + ImGui::GetFrameHeight());
    if (enabled && window->DC.MenuBarAppending && context.ActiveId == 0 &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
        ImGui::IsMouseHoveringRect(min, max) && !ImGui::IsPopupOpen(label))
        ImGui::OpenPopup(label);
    return BeginMenu(label, enabled);
}

bool BeginMenu(const char* label, bool enabled) {
    PushItemText(label, enabled);
    const bool open = ImGui::BeginMenu(label, enabled);
    ImGui::PopStyleColor(2);
    return open;
}
void EndMenu() { ImGui::EndMenu(); }
bool MenuItem(const char* label, const char* shortcut, bool selected, bool enabled) {
    PushItemText(label, enabled);
    const bool picked = ImGui::MenuItem(label, shortcut, selected, enabled);
    ImGui::PopStyleColor(2);
    return picked;
}
bool MenuItem(const char* label, const char* shortcut, bool* selected, bool enabled) {
    const bool picked = MenuItem(label, shortcut, selected && *selected, enabled);
    if (picked && selected) *selected = !*selected;
    return picked;
}
}
