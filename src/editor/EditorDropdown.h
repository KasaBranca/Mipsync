#pragma once

#include <imgui.h>

namespace MipsyncEngine::EditorDropdown {
bool BeginCombo(const char* label, const char* preview, ImGuiComboFlags flags = 0);
void EndCombo();
bool Selectable(const char* label, bool selected = false, ImGuiSelectableFlags flags = 0,
                 const ImVec2& size = ImVec2(0, 0));
bool Combo(const char* label, int* currentItem, const char* const items[], int count,
            int popupMaxHeightInItems = -1);
bool Combo(const char* label, int* currentItem, const char* itemsSeparatedByZeros,
            int popupMaxHeightInItems = -1);
} // namespace MipsyncEngine::EditorDropdown
