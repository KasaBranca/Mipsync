#include "EditorPreferences.h"
#include "EditorDropdown.h"
#include "EditorTheme.h"
#include "EditorMenu.h"
#include "EditorPreferencesNativeWindow.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cfloat>

namespace MipsyncEngine {
const std::vector<const char*>& EditorPreferenceCategories() {
    static const std::vector<const char*> categories = {
        "General", "Appearance", "Scene View", "Gizmos", "Game View", "Project", "Console", "PS1 Controller"};
    return categories;
}
const std::vector<PreferenceDefinition>& EditorPreferenceDefinitions() {
    using K = PreferenceKind;
    static const auto definitions = [] {
    std::vector<PreferenceDefinition> result = {
        {"focusGameOnPlay", "General", "Focus Game View on Play", K::Toggle, 1, 0, 1},
        {"saveBeforePlay", "General", "Prefer Save before Play", K::Toggle, 0, 0, 1, nullptr,
            "Choose Save as the default action in the save confirmation shown before Play."},
        {"undoSteps", "General", "Undo history limit", K::Integer, 64, 8, 256},
        {"doubleClickTime", "General", "Double-click interval (seconds)", K::Number, .3, .1, .8},
        {"dragThreshold", "General", "Drag threshold (pixels)", K::Number, 6, 1, 20},
        {"fontSize", "Appearance", "Font size (restart required)", K::Integer, 13, 8, 32, nullptr, "Saved now; restart the editor when convenient to apply the new font size."},
        {"scrollbarSize", "Appearance", "Scrollbar width", K::Number, 11, 8, 24},
        {"tooltipDelay", "Appearance", "Tooltip delay (seconds)", K::Number, .4, 0, 2, nullptr, "Delay for preference descriptions and other delayed tooltips."},
        {"orbitSensitivity", "Scene View", "Orbit sensitivity", K::Number, .3, .01, 2},
        {"panSensitivity", "Scene View", "Pan sensitivity", K::Number, .01, .001, .05},
        {"zoomSensitivity", "Scene View", "Zoom sensitivity", K::Number, 1, .1, 4},
        {"flySpeed", "Scene View", "Fly speed", K::Number, 5, .1, 100},
        {"flyBoost", "Scene View", "Shift speed multiplier", K::Number, 3, 1, 10},
        {"invertOrbitY", "Scene View", "Invert orbit Y", K::Toggle, 0, 0, 1},
        {"sceneFov", "Scene View", "Field of view", K::Number, 60, 15, 120},
        {"sceneNearClip", "Scene View", "Near clipping plane", K::Number, .001, .001, 1},
        {"sceneFarClip", "Scene View", "Far clipping plane", K::Number, 2000, 10, 10000},
        {"gizmoColliders", "Gizmos", "Collider outlines", K::Toggle, 1, 0, 1},
        {"gizmoLights", "Gizmos", "Light icons and ranges", K::Toggle, 1, 0, 1},
        {"gizmoCameras", "Gizmos", "Camera frustums", K::Toggle, 1, 0, 1},
        {"gizmoMeshes", "Gizmos", "Mesh subdivision preview", K::Toggle, 1, 0, 1},
        {"gizmoCanvases", "Gizmos", "Canvas outlines", K::Toggle, 1, 0, 1},
        {"gizmoModeler", "Gizmos", "Modeler overlay", K::Toggle, 1, 0, 1},
        {"gameViewPSync", "Game View", "P-Sync", K::Toggle, 1, 0, 1, nullptr, "Run editor simulation at the estimated PS1 frame cadence."},
        {"gameViewPlayMode", "Game View", "Play behavior", K::Choice, 0, 0, 1, "Play Focused\0Play Maximized\0"},
        {"gameViewGizmos", "Game View", "Show gizmos", K::Toggle, 0, 0, 1},
        {"gameViewStatistics", "Game View", "Show statistics", K::Toggle, 0, 0, 1},
        {"gameViewWidth", "Game View", "Render width", K::Integer, 1024, 160, 3840},
        {"gameViewHeight", "Game View", "Render height", K::Integer, 768, 120, 2160},
        {"projectThumbnailSize", "Project", "Thumbnail size", K::Number, 96, 48, 160},
        {"projectTooltips", "Project", "Asset path tooltips", K::Toggle, 1, 0, 1},
        {"projectAutoRefresh", "Project", "Refresh files automatically", K::Toggle, 0, 0, 1},
        {"projectRefreshInterval", "Project", "Refresh interval (seconds)", K::Number, 5, 1, 60},
        {"consoleInfo", "Console", "Show information", K::Toggle, 1, 0, 1},
        {"consoleWarnings", "Console", "Show warnings", K::Toggle, 1, 0, 1},
        {"consoleErrors", "Console", "Show errors", K::Toggle, 1, 0, 1},
        {"consoleDebug", "Console", "Show debug and trace", K::Toggle, 1, 0, 1},
        {"consoleAutoScroll", "Console", "Follow latest messages", K::Toggle, 1, 0, 1},
        {"consoleDisplayLimit", "Console", "Maximum visible messages", K::Integer, 1000, 50, 10000},
        {"padMouseSensitivity", "PS1 Controller", "Mouse stick sensitivity", K::Number, .003, .0001, .02, nullptr, "Mouse speed is converted to a stick value between -1 and 1. Faster motion reaches full stick deflection."},
        {"padInvertMouseX", "PS1 Controller", "Invert mouse X", K::Toggle, 0, 0, 1},
        {"padInvertMouseY", "PS1 Controller", "Invert mouse Y", K::Toggle, 0, 0, 1},
        {"padDeadzone", "PS1 Controller", "Physical stick deadzone", K::Number, .125, 0, .9},
    };
    for (const auto& control : PS1Controls)
        result.push_back({control.preference,"PS1 Controller",control.name,K::Binding,
            double(control.binding),-1,std::strstr(control.name,"Stick") ? 1103.0 : 1004.0,nullptr,"Keyboard or mouse input for this PS1 control in Game View."});
    return result;
    }();
    return definitions;
}
static const PreferenceDefinition* Find(const char* key) {
    for (const auto& d : EditorPreferenceDefinitions()) if (std::strcmp(d.key, key) == 0) return &d;
    return nullptr;
}
double EditorPreferences::Get(const char* key) const {
    const auto* d = Find(key);
    if (!d) return 0;
    double value = std::strcmp(key, "fontSize") == 0 ? m_DefaultFontSize : d->initial;
    auto it = m_Document.find(key);
    if (it != m_Document.end()) {
        if (std::strcmp(key, "gameViewPlayMode") == 0 && it->is_string())
            value = *it == "Maximized" ? 1 : 0;
        else if (it->is_boolean()) value = it->get<bool>() ? 1 : 0;
        else if (it->is_number()) value = it->get<double>();
    }
    if (!std::isfinite(value)) value = d->initial;
    value = std::clamp(value, d->minimum, d->maximum);
    if (d->kind == PreferenceKind::Binding) {
        const int binding = static_cast<int>(std::round(value));
        if (std::none_of(PS1BindingOptions().begin(),PS1BindingOptions().end(),
            [binding](const auto& option) { return option.code==binding; })) value=d->initial;
    }
    return d->kind == PreferenceKind::Number ? value : std::round(value);
}
PS1PadMapping EditorPreferences::GetPS1PadMapping() const {
    PS1PadMapping mapping;
    for (size_t i=0; i<PS1Controls.size(); ++i) mapping.bindings[i]=static_cast<int>(Get(PS1Controls[i].preference));
    mapping.mouseSensitivity=static_cast<float>(Get("padMouseSensitivity"));
    mapping.deadzone=static_cast<float>(Get("padDeadzone"));
    mapping.invertMouseX=Enabled("padInvertMouseX"); mapping.invertMouseY=Enabled("padInvertMouseY");
    return mapping;
}
void EditorPreferences::Set(const char* key, double value) {
    const auto* d = Find(key);
    if (!d) return;
    if (!std::isfinite(value)) value = d->initial;
    value = std::clamp(value, d->minimum, d->maximum);
    if (d->kind != PreferenceKind::Number) value = std::round(value);
    if (std::strcmp(key, "gameViewPlayMode") == 0) m_Document[key] = value == 1 ? "Maximized" : "Focused";
    else if (d->kind == PreferenceKind::Toggle) m_Document[key] = value != 0;
    else m_Document[key] = value;
}
void EditorPreferences::Load(const nlohmann::json& document, float defaultFontSize) {
    m_DefaultFontSize = std::clamp(defaultFontSize, 8.0f, 32.0f);
    m_Document = document.is_object() ? document : nlohmann::json::object();
    // Migrate the old separate mouse destination into visible direction bindings.
    // Keep individually customized keys and only replace the old stick defaults.
    if (!m_Document.contains("padDirectionBindingsVersion") && m_Document.contains("padMouseStick")) {
        const auto& oldTarget=m_Document["padMouseStick"];
        const double value=oldTarget.is_number() ? oldTarget.get<double>() : 2;
        const int target=std::isfinite(value) ? int(std::clamp(value,0.0,2.0)) : 2;
        constexpr int keys[8]={'W','S','A','D','I','K','J','L'};
        constexpr int directions[4]={PS1MouseUp,PS1MouseDown,PS1MouseLeft,PS1MouseRight};
        for (int i=0; i<8; ++i) {
            const char* key=PS1Controls[16+i].preference;
            const auto found=m_Document.find(key);
            if (found!=m_Document.end() && (!found->is_number() || found->get<double>()!=keys[i])) continue;
            m_Document[key]=(target==i/4+1) ? directions[i%4] : keys[i];
        }
    }
    m_Document["padDirectionBindingsVersion"]=1;
    m_Document.erase("padMouseStick");
    auto version = m_Document.find("fontSizeDefaultVersion");
    if (m_Document.contains("fontSize") &&
        (version == m_Document.end() || !version->is_number() || version->get<double>() < 1) && Get("fontSize") == 12)
        Set("fontSize", 13);
    for (const auto& d : EditorPreferenceDefinitions()) Set(d.key, Get(d.key));
}
nlohmann::json EditorPreferences::Save() const {
    auto result = m_Document;
    result["fontSizeDefaultVersion"] = 1;
    result["preferencesVersion"] = 1;
    return result;
}
void EditorPreferences::ResetCategory(const char* category) {
    for (const auto& d : EditorPreferenceDefinitions()) if (std::strcmp(d.category, category) == 0)
        Set(d.key, std::strcmp(d.key, "fontSize") == 0 ? m_DefaultFontSize : d.initial);
}
bool EditorPreferences::Matches(const PreferenceDefinition& d, const char* search) const {
    auto lower = [](std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; };
    return lower(std::string(d.category) + " " + d.label + " " + (d.help ? d.help : "")).find(lower(search)) != std::string::npos;
}
void EditorPreferencesWindow::Open(const char* category) {
    for (auto c : EditorPreferenceCategories()) if (std::strcmp(c, category) == 0) m_Category = c;
    m_Search[0] = '\0';
    if (ImGui::GetCurrentContext()) {
        auto* window = ImGui::FindWindowByName("Preferences");
        if (window) {
            if (auto* state = ImGui::GetInputTextState(window->GetID("##PreferencesSearch")))
                state->ReloadUserBufAndMoveToEnd();
        }
    }
    m_Open = m_Focus = true;
}
void EditorPreferencesWindow::DrawMenu() {
    for (const auto* category : EditorPreferenceCategories())
        if (EditorMenu::MenuItem(category)) Open(category);
}
bool EditorPreferencesWindow::Draw(EditorPreferences& preferences) {
    if (!m_Open) return false;
    ImGuiWindowClass windowClass;
    windowClass.ClassId = ImGui::GetID("PreferencesStandalone");
    windowClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
    windowClass.ViewportFlagsOverrideClear = ImGuiViewportFlags_NoDecoration;
    ImGui::SetNextWindowClass(&windowClass);
    const auto* mainViewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(mainViewport->WorkPos.x + mainViewport->WorkSize.x * .5f,
                                   mainViewport->WorkPos.y + mainViewport->WorkSize.y * .5f),
                            ImGuiCond_FirstUseEver, ImVec2(.5f, .5f));
    ImGui::SetNextWindowSize(ImVec2(900, 650), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(600, 360), ImVec2(FLT_MAX, FLT_MAX));
    if (m_Focus) { ImGui::SetNextWindowFocus(); m_Focus = false; }
    bool changed = false;
    // The native platform window already provides the caption and close button.
    // Keep an ImGui caption only when viewports/native decorations are unavailable.
    const bool nativeCaption = (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse;
    if (nativeCaption) flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove;
    if (ImGui::Begin("Preferences", &m_Open, flags)) {
        if (nativeCaption) PreferencesNativeWindow::Apply(ImGui::GetWindowViewport());
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##PreferencesSearch", "Search preferences...", m_Search, sizeof(m_Search));
        ImGui::Separator();
        const float footer = ImGui::GetFrameHeightWithSpacing() + 6;
        ImGui::BeginChild("Categories", ImVec2(165, -footer), ImGuiChildFlags_Borders);
        for (auto category : EditorPreferenceCategories()) {
            bool hasMatches = false;
            for (const auto& d : EditorPreferenceDefinitions())
                if (std::strcmp(d.category, category) == 0 && preferences.Matches(d, m_Search)) hasMatches = true;
            ImGui::BeginDisabled(!hasMatches);
            if (ImGui::Selectable(category, m_Category == category, 0, ImVec2(0, 27))) Open(category);
            ImGui::EndDisabled();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("Settings", ImVec2(0, -footer));
        // A global search shows matches across categories; selecting a menu category clears it.
        ImGui::TextUnformatted(m_Search[0] ? "Search results" : m_Category.c_str());
        ImGui::Spacing();
        int count = 0;
        if (ImGui::BeginTable("Properties", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) {
            ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch, .48f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, .52f);
            for (const auto& d : EditorPreferenceDefinitions()) {
                if ((!m_Search[0] && m_Category != d.category) || !preferences.Matches(d, m_Search)) continue;
                ++count;
                ImGui::PushID(d.key);
                ImGui::TableNextRow(0, ImGui::GetFrameHeight() + 5);
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                if (m_Search[0]) ImGui::TextDisabled("%s", d.category);
                ImGui::TextWrapped("%s", d.label);
                if (d.help && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", d.help);
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                double value = preferences.Get(d.key);
                bool edited = false;
                if (d.kind == PreferenceKind::Toggle) {
                    bool on = value != 0;
                    edited = EditorTheme::Checkbox("##Value", &on); value = on;
                } else if (d.kind == PreferenceKind::Choice) {
                    int index = static_cast<int>(value);
                    edited = EditorDropdown::Combo("##Value", &index, d.choices); value = index;
                } else if (d.kind == PreferenceKind::Binding) {
                    const int binding=static_cast<int>(value);
                    const char* label="None";
                    for (const auto& option : PS1BindingOptions()) if (option.code==binding) label=option.label.c_str();
                    if (EditorDropdown::BeginCombo("##Value",label)) {
                        for (const auto& option : PS1BindingOptions()) {
                            if (option.code>d.maximum) continue;
                            if (EditorDropdown::Selectable(option.label.c_str(),option.code==binding)) {
                                value=option.code; edited=true;
                            }
                        }
                        EditorDropdown::EndCombo();
                    }
                } else if (d.kind == PreferenceKind::Integer) {
                    int number = static_cast<int>(value);
                    edited = ImGui::InputInt("##Value", &number); value = number;
                } else {
                    float number = static_cast<float>(value);
                    edited = EditorTheme::SliderFloat("##Value", &number, static_cast<float>(d.minimum), static_cast<float>(d.maximum), d.minimum < .01 ? "%.3f" : "%.2f"); value = number;
                }
                if (edited) { preferences.Set(d.key, value); changed = true; }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (count == 0) ImGui::TextDisabled("No matching preferences.");
        ImGui::EndChild();
        ImGui::Separator();
        if (ImGui::Button("Reset this category")) { preferences.ResetCategory(m_Category.c_str()); changed = true; }
        ImGui::SameLine();
        ImGui::TextDisabled("Changes save automatically.");
    }
    ImGui::End();
    return changed;
}
} // namespace MipsyncEngine
