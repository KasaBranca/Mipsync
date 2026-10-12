#pragma once
#include "EditorTheme.h"
#include <imgui.h>

namespace MipsyncEngine::EditorMenu {
bool BeginHoverMenu(const char* label, bool enabled = true);
inline constexpr ImVec4 Face = UiTokens::Hex(0xF7F7F8);
inline constexpr ImVec4 Text = UiTokens::Hex(0x202124);
inline constexpr ImVec4 Disabled = UiTokens::Hex(0x8A8D92);
inline constexpr ImVec4 Border = UiTokens::Hex(0xD5D6D8);

void PushPopupStyle(bool applyBorder = true);
void PopPopupStyle();
void PushItemsStyle(bool nativeMenuRows = false);
void PopItemsStyle();
bool BeginPopup(const char* id, ImGuiWindowFlags flags = 0);
bool BeginPopupContextItem(const char* id = nullptr,
                          ImGuiPopupFlags flags = ImGuiPopupFlags_MouseButtonRight);
bool BeginPopupContextWindow(const char* id = nullptr,
                            ImGuiPopupFlags flags = ImGuiPopupFlags_MouseButtonRight);
// Also accepts ordinary ImGui popups so mixed popup/dialog code can safely
// share the closing function without inheriting a menu's appearance.
void EndPopup();
bool BeginMenu(const char* label, bool enabled = true);
void EndMenu();
bool MenuItem(const char* label, const char* shortcut = nullptr, bool selected = false, bool enabled = true);
bool MenuItem(const char* label, const char* shortcut, bool* selected, bool enabled = true);
}
