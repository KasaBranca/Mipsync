#include "editor/EditorPreferences.h"
#include "editor/EditorMenu.h"
#include "editor/EditorSurface.h"
#include "editor/EditorObjectReference.h"
#include <imgui_internal.h>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <unordered_set>

// Font resource lookup only; use the production theme, checkbox, slider and menu code.
namespace MipsyncEngine {
std::filesystem::path GetBundledResourcesDirectory() { return std::filesystem::current_path() / "resources"; }
}
using namespace MipsyncEngine;
static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static void CheckPersistence() {
    EditorPreferences settings;
    settings.Load(nullptr, 14);
    Require(settings.Get("fontSize") == 14 && settings.Enabled("gameViewPSync"), "fresh defaults");
    Require(EditorPreferenceDefinitions().size() >= 35, "settings catalog incomplete");
    std::unordered_set<std::string> keys;
    for (const auto& definition : EditorPreferenceDefinitions()) {
        Require(keys.insert(definition.key).second, "duplicate key");
        settings.Set(definition.key, definition.maximum);
    }
    EditorPreferences restored;
    restored.Load(nlohmann::json::parse(settings.Save().dump()), 13);
    for (const auto& definition : EditorPreferenceDefinitions())
        Require(restored.Get(definition.key) == definition.maximum, "round-trip dropped a setting");
    restored.Load({{"fontSize", 12}, {"gameViewPSync", false}, {"futureSetting", "preserve"}}, 13);
    Require(restored.Get("fontSize") == 13 && !restored.Enabled("gameViewPSync"), "legacy migration changed P-Sync choice");
    Require(restored.Save()["futureSetting"] == "preserve", "unknown settings lost");
    restored.Load({{"fontSize", 12}, {"fontSizeDefaultVersion", 1}}, 13);
    Require(restored.Get("fontSize") == 12, "explicit font choice overwritten");
    restored.Load({{"fontSize", "invalid"}, {"flySpeed", -100}, {"gameViewPSync", nlohmann::json::array()},
                   {"sceneNearClip", 1e99}, {"undoSteps", 42.7}, {"gameViewPlayMode", "Maximized"}}, 13);
    Require(restored.Get("fontSize") == 13 && restored.Get("flySpeed") == .1 &&
        restored.Get("sceneNearClip") == 1 && restored.Get("undoSteps") == 43 && restored.Get("gameViewPlayMode") == 1,
        "validation or legacy play mode failed");
    restored.Set("flySpeed", std::numeric_limits<double>::infinity());
    Require(restored.Get("flySpeed") == 5, "non-finite input survived");
    restored.Set("consoleInfo", 0);
    restored.ResetCategory("Scene View");
    Require(restored.Get("sceneNearClip") == .001 && !restored.Enabled("consoleInfo"), "category reset affected another category");
    Require(restored.Matches(EditorPreferenceDefinitions()[8], "ORBIT"), "case-insensitive search failed");
    settings.ResetCategory("PS1 Controller");
    auto mapping=settings.GetPS1PadMapping();
    Require(mapping.bindings[0]==32 && mapping.bindings[16]=='W' &&
        mapping.bindings[20]==PS1MouseUp && mapping.bindings[21]==PS1MouseDown &&
        mapping.bindings[22]==PS1MouseLeft && mapping.bindings[23]==PS1MouseRight,"PS1 input defaults wrong");
    settings.Set("padCross",1001); settings.Set("padLeftStickUp",'I');
    settings.Set("padRightStickUp",'I'); settings.Set("padInvertMouseY",1);
    restored.Load(settings.Save(),13); mapping=restored.GetPS1PadMapping();
    Require(mapping.bindings[0]==1001 && mapping.bindings[16]=='I' && mapping.bindings[20]=='I' && mapping.invertMouseY,"PS1 bindings not persisted");
    restored.Load({{"padMouseStick",2}, {"padRightStickUp",'I'}, {"padRightStickDown",'K'},
        {"padRightStickLeft",'J'}, {"padRightStickRight",'L'}},13);
    mapping=restored.GetPS1PadMapping();
    Require(mapping.bindings[20]==PS1MouseUp && mapping.bindings[23]==PS1MouseRight &&
        !restored.Save().contains("padMouseStick"),"old right stick defaults were not migrated");
    restored.Load({{"padMouseStick",2}, {"padRightStickUp",'U'}},13);
    Require(restored.GetPS1PadMapping().bindings[20]=='U',"custom right stick key overwritten");
    restored.Load({{"padMouseStick",0}},13);
    Require(restored.GetPS1PadMapping().bindings[20]=='I',"disabled legacy mouse overwritten");
    restored.Load({{"padMouseStick",1}},13);
    Require(restored.GetPS1PadMapping().bindings[16]==PS1MouseUp &&
        restored.GetPS1PadMapping().bindings[20]=='I',"legacy left mouse target lost");
    restored.Set("padCross",58); Require(restored.GetPS1PadMapping().bindings[0]==32,"invalid binding survived");
    for (float height : {17.f, 18.f, 21.f, 22.f, 31.5f, 40.f}) {
        const ImRect rect = EditorSurface::CenteredReferenceIcon({8.25f, 13.5f}, {180.25f, 13.5f + height});
        Require(std::abs(rect.GetCenter().y - (13.5f + height * .5f)) < .0001f, "reference selector not vertically centered");
    }
}
static void CheckWindow(float scale) {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1200, 900};
    io.DeltaTime = .05f;
    io.FontGlobalScale = scale;
    EditorTheme::Apply();
    io.Fonts->AddFontDefault();
    unsigned char* atlas; int width, height;
    io.Fonts->GetTexDataAsRGBA32(&atlas, &width, &height);
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable | ImGuiConfigFlags_DockingEnable;
    io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_RendererHasViewports;
    auto& platform = ImGui::GetPlatformIO();
    int nativeCreated = 0;
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
    monitor.DpiScale = 1;
    platform.Monitors.push_back(monitor);
    ImGui::GetMainViewport()->PlatformHandle = ImGui::GetMainViewport();
    EditorPreferences settings; settings.Load(nullptr, 13);
    EditorPreferencesWindow window;
    ImRect menu;
    ImGuiWindow* root = nullptr;
    bool menuOpen = false, edited = false;
    auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Editor", nullptr, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDecoration);
        if (ImGui::BeginMenuBar()) {
            menuOpen = EditorMenu::BeginHoverMenu("Preferences");
            menu = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
            if (menuOpen) { window.DrawMenu(); EditorMenu::EndMenu(); }
            ImGui::EndMenuBar();
        }
        ImGui::End();
        edited = window.Draw(settings);
        ImGui::Render();
        root = ImGui::FindWindowByName("Preferences");
        ImGui::UpdatePlatformWindows();
        nativeCreated = ImGui::GetPlatformIO().Viewports.Size - 1;
    };
    auto move = [&](ImVec2 point) { io.AddMousePosEvent(point.x, point.y); frame(); frame(); };
    auto click = [&](ImVec2 point) {
        move(point);
        io.AddMouseButtonEvent(0, true); frame();
        io.AddMouseButtonEvent(0, false); frame();
    };
    frame(); frame();
    move(menu.GetCenter());
    Require(menuOpen, "Preferences menu did not open on hover");
    // Click the actual Game View category in the real menu.
    ImGuiWindow* popup = nullptr;
    for (auto* candidate : ImGui::GetCurrentContext()->Windows)
        if (candidate->Active && candidate->Flags & ImGuiWindowFlags_ChildMenu) popup = candidate;
    Require(popup != nullptr, "hover category popup missing");
    click({popup->Pos.x + 50, popup->Pos.y + popup->WindowPadding.y + (popup->FontRefSize + ImGui::GetStyle().ItemSpacing.y) * 4.5f});
    frame(); frame();
    Require(window.IsOpen() && window.Category() == "Game View", "menu selection did not choose category");
    Require(root && root->Viewport != ImGui::GetMainViewport() && root->ViewportOwned && nativeCreated >= 1,
        "Preferences failed to create an independent platform window");
    Require(root->DockId == 0 && (root->Flags & ImGuiWindowFlags_NoDocking), "Preferences entered editor dockspace");
    Require(root->Viewport->Flags & ImGuiViewportFlags_NoAutoMerge, "Preferences can merge back into editor");
    Require(!(root->Viewport->Flags & ImGuiViewportFlags_NoDecoration), "Preferences lost native window decorations");
    Require(root->TitleBarHeight == 0, "Preferences draws a duplicate caption below the native title bar");
    // Select each category and verify all controls render without leaked style/table stacks.
    for (auto* category : EditorPreferenceCategories()) {
        window.Open(category); frame(); frame();
        Require(window.Category() == category && root->Active, "category activation failed");
    }
    window.Open("Game View"); frame(); frame();
    ImGuiWindow* properties = nullptr;
    for (auto* candidate : ImGui::GetCurrentContext()->Windows)
        if (candidate->Active && std::string(candidate->Name).find("Preferences/Settings") == 0) properties = candidate;
    Require(properties != nullptr, "properties pane missing");
    // The first control is P-Sync. Read the actual table column and row geometry.
    ImGuiTable* table = nullptr;
    auto* context = ImGui::GetCurrentContext();
    for (int i = 0; i < context->Tables.GetMapSize(); ++i) {
        auto* candidate = context->Tables.GetByIndex(i);
        if (candidate && candidate->OuterWindow == properties) table = candidate;
    }
    Require(table != nullptr, "property table missing");
    const ImVec2 checkbox(table->Columns[1].WorkMinX + 6,
        properties->Pos.y + properties->WindowPadding.y + properties->FontRefSize + ImGui::GetStyle().ItemSpacing.y * 2 + (properties->FontRefSize + ImGui::GetStyle().FramePadding.y * 2) * .5f);
    click(checkbox);
    Require(!settings.Enabled("gameViewPSync") && edited, "P-Sync control click failed");
    EditorPreferences reopened; reopened.Load(settings.Save(), 13);
    Require(!reopened.Enabled("gameViewPSync"), "UI choice not persisted");
    // Search finds settings in every category.
    click({root->Pos.x + 90, root->Pos.y + root->TitleBarHeight + root->WindowPadding.y + (root->FontRefSize + ImGui::GetStyle().FramePadding.y * 2) * .5f});
    io.AddInputCharactersUTF8("fly"); frame(); frame();
    Require(context->InputTextState.TextA.Size > 0, "search field not editable");
    Require(table->CurrentRow == 0, "search did not filter to Fly speed across categories");
    // Test the production slider in a different category and its category-only reset.
    window.Open("Scene View"); frame(); frame();
    const float rowHeight = properties->FontRefSize + ImGui::GetStyle().FramePadding.y * 2 + 5;
    const float firstRowY = properties->Pos.y + properties->WindowPadding.y + properties->FontRefSize +
        ImGui::GetStyle().ItemSpacing.y * 2;
    click({(table->Columns[1].WorkMinX + table->Columns[1].WorkMaxX) * .5f, firstRowY + rowHeight * .5f});
    Require(settings.Get("orbitSensitivity") != .3, "camera sensitivity slider did not accept input");
    click({root->Pos.x + 65, root->Pos.y + root->Size.y - root->WindowPadding.y - (root->FontRefSize + ImGui::GetStyle().FramePadding.y * 2) * .5f});
    Require(settings.Get("orbitSensitivity") == .3 && !settings.Enabled("gameViewPSync"), "reset category changed an unrelated preference");
    window.Open("PS1 Controller"); frame(); frame();
    const float bindingRowHeight=properties->FontRefSize+ImGui::GetStyle().FramePadding.y*2+5;
    const float bindingStart=properties->Pos.y+properties->WindowPadding.y+properties->FontRefSize+
        ImGui::GetStyle().ItemSpacing.y*2;
    // Four stick settings precede Cross. Use the production binding dropdown.
    click({(table->Columns[1].WorkMinX+table->Columns[1].WorkMaxX)*.5f,bindingStart+bindingRowHeight*4.5f});
    frame(); frame();
    ImGuiWindow* bindingPopup=nullptr;
    for(auto* candidate : context->Windows)
        if(candidate->Active && (candidate->Flags&ImGuiWindowFlags_Popup) &&
            std::string(candidate->Name).find("##Combo_")==0) bindingPopup=candidate;
    Require(bindingPopup!=nullptr,"PS1 binding dropdown did not open");
    click({bindingPopup->Pos.x+40,bindingPopup->Pos.y+bindingPopup->WindowPadding.y+(bindingPopup->FontRefSize+8)*.5f});
    Require(settings.GetPS1PadMapping().bindings[0]==-1,"PS1 binding selection did not apply");
    reopened.Load(settings.Save(),13);
    Require(reopened.GetPS1PadMapping().bindings[0]==-1,"PS1 dropdown selection did not persist");
    // The platform backend forwards the native caption's close request.
    root->Viewport->PlatformRequestClose = true;
    frame();
    Require(!window.IsOpen(), "Preferences close button failed");
    window.Open("Appearance"); frame(); frame();
    Require(window.IsOpen() && window.Category() == "Appearance", "Preferences reopen failed");
    ImGui::DestroyPlatformWindows();
    ImGui::DestroyContext();
}
static void CheckReferenceClicks(float scale) {
    using EditorSurface::ReferenceAction;
    ImGui::CreateContext(); auto& io = ImGui::GetIO(); io.IniFilename = nullptr;
    io.DisplaySize = {700, 700}; io.DeltaTime = .05f; io.FontGlobalScale = scale;
    io.Fonts->AddFontDefault(); unsigned char* pixels; int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    bool assigned = true, disabled = false; ReferenceAction result = ReferenceAction::None;
    ImRect rect;
    auto frame = [&] {
        ImGui::NewFrame(); ImGui::SetNextWindowPos({20, 20}); ImGui::SetNextWindowSize({400, 450});
        ImGui::Begin("Reference"); ImGui::BeginDisabled(disabled);
        result = EditorSurface::ObjectReferenceButton(assigned ? "Player.mips" : "None (Script)", assigned, 220);
        rect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImGui::EndDisabled(); ImGui::End(); ImGui::Render();
    };
    auto click = [&](float x) {
        io.AddMousePosEvent(x, rect.GetCenter().y); frame();
        io.AddMouseButtonEvent(0, true); frame();
        io.AddMouseButtonEvent(0, false); frame();
        return result;
    };
    frame(); frame();
    Require(click(rect.Min.x + 50) == ReferenceAction::Select, "filled reference body did not select target");
    Require(click(rect.Max.x - 10) == ReferenceAction::Pick, "reference icon did not open picker");
    assigned = false; frame();
    Require(click(rect.Min.x + 50) == ReferenceAction::Pick, "empty reference body did not open picker");
    assigned = true; disabled = true; frame();
    Require(click(rect.Min.x + 50) == ReferenceAction::None, "disabled reference accepted click");
    ImGui::DestroyContext();
}
int main() try {
    CheckReferenceClicks(1); CheckReferenceClicks(1.5f);
    CheckPersistence(); CheckWindow(1); CheckWindow(1.5f);
    std::puts("Preferences: persistence, menus, controls, viewport, and reference body/picker/empty/disabled clicks at both font scales passed.");
    return 0;
} catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
