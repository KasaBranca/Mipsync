#include "EditorDropdown.h"
#include "EditorTheme.h"
#include "EditorMenu.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cstring>
#include <vector>

namespace MipsyncEngine::EditorDropdown {
namespace {
constexpr ImVec4 kMenuText = EditorMenu::Text;
constexpr ImVec4 kMenuDisabled = EditorMenu::Disabled;
constexpr float kVerticalPadding = 6.0f;

float RowHeight() { return ImGui::GetTextLineHeight() + 8.0f; }

void ConstrainHeight(int rows) {
    if (!(ImGui::GetCurrentContext()->NextWindowData.HasFlags & ImGuiNextWindowDataFlags_HasSizeConstraint))
        ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0),
            ImVec2(FLT_MAX, rows > 0 ? RowHeight() * rows + kVerticalPadding * 2.0f : FLT_MAX));
}
} // namespace

bool BeginCombo(const char* label, const char* preview, ImGuiComboFlags flags) {
    const int rows = (flags & ImGuiComboFlags_HeightSmall) ? 4 :
        (flags & ImGuiComboFlags_HeightLarge) ? 20 : (flags & ImGuiComboFlags_HeightLargest) ? -1 : 8;
    ConstrainHeight(rows);
    // Popup appearance is captured by Begin; these overrides leave the closed field unchanged.
    // BeginCombo renders the closed field before beginning its popup. Keep
    // the editor's field border here; repaint only the opened popup below.
    EditorMenu::PushPopupStyle(false);
    const bool open = ImGui::BeginCombo(label, preview, flags);
    EditorMenu::PopPopupStyle();
    if (!open) return false;
    ImGuiWindow* popup = ImGui::GetCurrentWindow();
    popup->DrawList->AddRect(popup->Pos, ImVec2(popup->Pos.x + popup->Size.x, popup->Pos.y + popup->Size.y),
        ImGui::ColorConvertFloat4ToU32(EditorMenu::Border), popup->WindowRounding, 0, 1.0f);
    EditorMenu::PushItemsStyle();
    return true;
}

void EndCombo() {
    EditorMenu::PopItemsStyle();
    ImGui::EndCombo();
}

bool Selectable(const char* label, bool selected, ImGuiSelectableFlags flags, const ImVec2& size) {
    ImGuiContext& context = *ImGui::GetCurrentContext();
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (context.BeginComboDepth == 0 || std::strncmp(window->Name, "##Combo_", 8) != 0)
        return ImGui::Selectable(label, selected, flags, size);

    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float fontSize = ImGui::GetFontSize();
    const float gutter = fontSize + 12.0f;
    const ImVec2 text = ImGui::CalcTextSize(label, nullptr, true);
    const float height = size.y > 0.0f ? size.y : RowHeight();
    const float width = size.x > 0.0f ? size.x : std::max(ImGui::GetContentRegionAvail().x, text.x + gutter + 8.0f);
    const bool disabled = (flags & ImGuiSelectableFlags_Disabled) ||
        (context.CurrentItemFlags & ImGuiItemFlags_Disabled);
    ImGui::PushID(label);
    const bool pressed = ImGui::Selectable("##DropdownItem", false, flags, ImVec2(width, height));
    ImGui::PopID();
    const bool hovered = ImGui::IsItemHovered();
    const bool navigationEnabled = (context.IO.ConfigFlags &
        (ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad)) != 0;
    const bool highlighted = !disabled && (hovered || (navigationEnabled && ImGui::IsItemFocused()));
    if (highlighted && !hovered)
        window->DrawList->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
            ImGui::GetColorU32(UiTokens::Brand));
    const ImU32 color = ImGui::GetColorU32(disabled ? kMenuDisabled :
        highlighted ? UiTokens::TextOnBrand : kMenuText);
    const ImVec2 textMin(start.x + gutter, start.y + (height - text.y) * 0.5f);
    const char* textEnd = ImGui::FindRenderedTextEnd(label);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::RenderTextClipped(textMin, ImVec2(start.x + width, start.y + height), label, textEnd,
        &text, ImVec2(0, 0), nullptr);
    ImGui::PopStyleColor();
    if (selected)
        ImGui::RenderCheckMark(window->DrawList, ImVec2(start.x + 2.0f,
            start.y + (height - fontSize * 0.7f) * 0.5f), color, fontSize * 0.7f);
    return pressed;
}

bool Combo(const char* label, int* currentItem, const char* const items[], int count, int popupMaxHeightInItems) {
    const ImGuiID id = ImGui::GetCurrentWindow()->GetID(label);
    const char* preview = *currentItem >= 0 && *currentItem < count ? items[*currentItem] : "";
    if (popupMaxHeightInItems > 0) ConstrainHeight(popupMaxHeightInItems);
    if (!BeginCombo(label, preview)) return false;
    bool changed = false;
    for (int index = 0; index < count; ++index) {
        ImGui::PushID(index);
        const bool selected = index == *currentItem;
        if (Selectable(items[index], selected) && !selected) { *currentItem = index; changed = true; }
        if (selected) ImGui::SetItemDefaultFocus();
        ImGui::PopID();
    }
    EndCombo();
    if (changed) ImGui::MarkItemEdited(id);
    return changed;
}

bool Combo(const char* label, int* currentItem, const char* itemsSeparatedByZeros, int popupMaxHeightInItems) {
    std::vector<const char*> items;
    for (const char* item = itemsSeparatedByZeros; *item; item += std::strlen(item) + 1)
        items.push_back(item);
    return Combo(label, currentItem, items.data(), static_cast<int>(items.size()), popupMaxHeightInItems);
}
} // namespace MipsyncEngine::EditorDropdown
