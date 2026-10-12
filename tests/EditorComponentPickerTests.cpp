#include "editor/EditorComponentPicker.h"
#include "editor/EditorTheme.h"
#include <imgui_internal.h>
#include <cstdio>
#include <stdexcept>
#include <string>

using namespace MipsyncEngine;
ImVec4 EditorTheme::PanelFace = UiTokens::BgSecondary;
ImVec4 EditorTheme::Selection = UiTokens::Brand;
static void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

static void CheckPicker(float scale, bool bottom, float clickFraction, bool scrolled, bool viewports) {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {620, 520};
    io.DeltaTime = 1.0f / 60;
    io.FontGlobalScale = scale;
    io.Fonts->AddFontDefault();
    unsigned char* atlas; int atlasWidth, atlasHeight;
    io.Fonts->GetTexDataAsRGBA32(&atlas, &atlasWidth, &atlasHeight);
    if (viewports) {
        // Match the editor's platform-window behavior without opening native
        // windows. A popup escaping the host can otherwise look fine in the
        // single-viewport test while failing in the real editor.
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_RendererHasViewports;
        auto& platform = ImGui::GetPlatformIO();
        platform.Platform_CreateWindow = [](ImGuiViewport* v) { v->PlatformHandle = v; };
        platform.Platform_DestroyWindow = [](ImGuiViewport* v) { v->PlatformHandle = nullptr; };
        platform.Platform_ShowWindow = [](ImGuiViewport*) {};
        platform.Platform_SetWindowPos = [](ImGuiViewport*, ImVec2) {};
        platform.Platform_GetWindowPos = [](ImGuiViewport* v) { return v->Pos; };
        platform.Platform_SetWindowSize = [](ImGuiViewport*, ImVec2) {};
        platform.Platform_GetWindowSize = [](ImGuiViewport* v) { return v->Size; };
        platform.Platform_SetWindowFocus = [](ImGuiViewport*) {};
        platform.Platform_GetWindowFocus = [](ImGuiViewport*) { return true; };
        platform.Platform_GetWindowMinimized = [](ImGuiViewport*) { return false; };
        platform.Platform_SetWindowTitle = [](ImGuiViewport*, const char*) {};
        ImGuiPlatformMonitor monitor;
        monitor.MainPos = monitor.WorkPos = {0, 0};
        monitor.MainSize = monitor.WorkSize = io.DisplaySize;
        monitor.DpiScale = 1.0f;
        platform.Monitors.push_back(monitor);
        ImGui::GetMainViewport()->PlatformHandle = ImGui::GetMainViewport();
    }
    EditorComponentPicker picker;
    ImRect button;
    ImGuiWindow* results = nullptr;
    int added = 0, begun = 0;
    bool scrollToBottom = false;
    auto frame = [&] {
        results = nullptr;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({330, 0});
        ImGui::SetNextWindowSize({290, io.DisplaySize.y});
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoDecoration);
        if (scrolled) {
            for (int i = 0; i < 40; ++i) {
                ImGui::PushID(i);
                ImGui::Text("Component %d", i);
                ImGui::Dummy({0, 60 * scale});
                ImGui::PopID();
            }
        } else if (bottom) {
            // The bottom of the button extends to the display edge, so its
            // requested popup origin is entirely outside the work area.
            ImGui::SetCursorPosY(io.DisplaySize.y - 30);
        }
        if (ImGui::Button("Add Component", {-1, 28})) picker.Open("picker");
        button = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
        if (scrollToBottom) ImGui::SetScrollHereY(1);
        const auto before = ImGui::GetStyle();
        if (picker.Begin("picker")) {
            ++begun;
            picker.BeginCategory("Animation");
            picker.Add("Animator", [&] { ++added; ImGui::CloseCurrentPopup(); });
            picker.EndCategory();
            picker.BeginCategory("Audio");
            picker.Add("Audio Source", [&] { ++added; ImGui::CloseCurrentPopup(); });
            picker.EndCategory();
            picker.BeginCategory("Scripts");
            for (int i = 0; i < 40; ++i) {
                const std::string label = "Script " + std::to_string(i);
                picker.Add(label.c_str(), [&] { ++added; ImGui::CloseCurrentPopup(); });
            }
            picker.EndCategory();
            picker.AddCreateScript();
            picker.Draw({});
            const auto* popup = ImGui::GetCurrentWindow();
            for (auto* window : ImGui::GetCurrentContext()->Windows)
                if (window->Active && window->ParentWindow == popup &&
                    std::string(window->Name).find("##component_results") != std::string::npos)
                    results = window;
            picker.End();
        }
        Require(ImGui::GetStyle().WindowPadding.x == before.WindowPadding.x &&
                ImGui::GetStyle().PopupRounding == before.PopupRounding, "picker leaked styles");
        ImGui::End();
        ImGui::Render();
        if (viewports) ImGui::UpdatePlatformWindows();
        Require(!ImGui::GetCurrentContext()->ErrorCountCurrentFrame, "ImGui error");
    };
    auto settle = [&] { for (int i = 0; i < 4; ++i) frame(); };
    auto click = [&](ImVec2 pos) {
        io.AddMousePosEvent(pos.x, pos.y); frame();
        io.AddMouseButtonEvent(0, true); frame();
        io.AddMouseButtonEvent(0, false); frame(); settle();
    };
    settle();
    if (scrolled) { scrollToBottom = true; settle(); scrollToBottom = false; settle(); }
    click({button.Min.x + button.GetWidth() * clickFraction, button.GetCenter().y});
    Require(begun > 0 && results && results->Active, "bottom-edge opening never reached picker content");
    Require(added == 0 && !ImGui::GetCurrentContext()->OpenPopupStack.empty(), "opening click triggered an action or closed picker");
    auto* popup = results->ParentWindow;
    Require(popup->Viewport == ImGui::FindWindowByName("Inspector")->Viewport,
            "picker became a separate platform window");
    Require(popup->Pos.x >= 0 && popup->Pos.y >= 0 &&
            popup->Pos.x + popup->Size.x <= io.DisplaySize.x + 0.5f &&
            popup->Pos.y + popup->Size.y <= io.DisplaySize.y + 0.5f,
            "picker escaped viewport");
    const float rowHeight = results->FontRefSize + 9 * results->FontRefSize / 14;
    auto row = [&](int index) { return ImVec2(results->Pos.x + 45, results->Pos.y + (index + 0.5f) * rowHeight); };
    click(row(0)); // Animation category: opening must not have navigated here.
    Require(added == 0 && results, "opening click navigated to a category");
    click(row(0));
    Require(added == 1 && ImGui::GetCurrentContext()->OpenPopupStack.empty(), "component could not be added from bottom-edge popup");
    // A fresh reopen must also work after the old popup had another height.
    click({button.Min.x + button.GetWidth() * clickFraction, button.GetCenter().y});
    Require(results && added == 1, "reopening picker failed");
    click(row(2)); // Scripts: growing the list near the bottom must scroll.
    Require(results && results->ScrollbarY, "large component list did not scroll");
    popup = results->ParentWindow;
    Require(popup->Pos.y + popup->Size.y <= io.DisplaySize.y + 0.5f, "growing popup escaped viewport");
    ImGui::DestroyContext();
}

int main() {
    try {
        for (bool viewports : {false, true})
        for (float scale : {1.0f, 1.5f})
            for (float fraction : {0.1f, 0.5f, 0.95f}) {
                CheckPicker(scale, false, fraction, false, viewports);
                CheckPicker(scale, true, fraction, false, viewports);
                CheckPicker(scale, true, fraction, true, viewports);
            }
        std::puts("Component picker: fresh/repeated bottom-edge opens, scrolled inspectors, left/right clicks, DPI and component actions passed");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
