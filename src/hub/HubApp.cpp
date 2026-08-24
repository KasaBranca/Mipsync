#include "HubApp.h"
#include "../core/Log.h"
#include "../core/Window.h"
#include "../core/Win32AppIcon.h"
#include "../editor/EditorTheme.h"
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>

namespace MipsyncEngine {

namespace fs = std::filesystem;

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

constexpr float kSidebarWidth = 220.0f;
constexpr float kLogoSize = 36.0f;

std::filesystem::path ExeDirectory() {
#ifdef _WIN32
    wchar_t modulePath[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) > 0)
        return fs::path(modulePath).parent_path();
#endif
    return fs::current_path();
}

fs::path ResolveAppIconPng() {
    for (const fs::path& root : { ExeDirectory(), fs::current_path() }) {
        const fs::path candidate = root / "resources" / "icons" / "app_icon.png";
        if (fs::exists(candidate))
            return candidate;
    }
    return {};
}

void DrawImageAspectFit(ImDrawList* dl, GLuint texId, int texW, int texH, ImVec2 cellMin,
                        ImVec2 cellMax) {
    if (!dl || texId == 0 || texW <= 0 || texH <= 0)
        return;

    const float cellW = cellMax.x - cellMin.x;
    const float cellH = cellMax.y - cellMin.y;
    const float texAspect = static_cast<float>(texW) / static_cast<float>(texH);
    const float cellAspect = cellW / cellH;
    float displayW = cellW;
    float displayH = cellH;
    if (texAspect > cellAspect)
        displayH = cellW / texAspect;
    else
        displayW = cellH * texAspect;

    const float offsetX = (cellW - displayW) * 0.5f;
    const float offsetY = (cellH - displayH) * 0.5f;
    const ImVec2 imageMin(cellMin.x + offsetX, cellMin.y + offsetY);
    const ImVec2 imageMax(imageMin.x + displayW, imageMin.y + displayH);
    dl->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(texId)), imageMin, imageMax,
                 ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
}

std::string FormatRelativeTime(std::time_t t) {
    if (t == 0)
        return "—";

    const auto now = std::chrono::system_clock::now();
    const auto then = std::chrono::system_clock::from_time_t(t);
    const long long hours =
        std::chrono::duration_cast<std::chrono::hours>(now - then).count();
    if (hours < 24)
        return "今日";
    const long long days = hours / 24;
    if (days < 30)
        return std::to_string(days) + "日前";
    if (days < 365)
        return std::to_string(days / 30) + "ヶ月前";
    return std::to_string(days / 365) + "年前";
}

bool SidebarNavItem(const char* label, bool selected, bool enabled = true) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, EditorTheme::ShapeCornerMedium);

    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Header, UiTokens::BgSelected);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, UiTokens::BgSelected);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, UiTokens::BgSelected);
    }

    if (!enabled)
        ImGui::BeginDisabled();

    const bool clicked =
        ImGui::Selectable(label, selected, ImGuiSelectableFlags_None, ImVec2(-1.0f, 36.0f));

    if (!enabled)
        ImGui::EndDisabled();

    if (selected)
        ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
    return enabled && clicked;
}

} // namespace

HubApp::HubApp() {
    m_Projects = HubRegistry::Load();
    const std::string defaultRoot = HubRegistry::DefaultProjectsRoot();
    std::strncpy(m_NewLocation, defaultRoot.c_str(), sizeof(m_NewLocation) - 1);
}

HubApp::~HubApp() {}

void HubApp::EnsureLogoTexture() {
    if (m_LogoTexture && m_LogoTexture->GetID() != 0)
        return;

    const fs::path pngPath = ResolveAppIconPng();
    if (pngPath.empty())
        return;

    TextureParams params;
    params.nearestFilter = false;
    params.maxSize = 256;
    m_LogoTexture = std::make_shared<Texture>(pngPath.string(), params);
    if (m_LogoTexture->GetID() == 0)
        m_LogoTexture.reset();
}

HubResult HubApp::Run() {
    WindowProps props;
    props.title = "Mipsync Hub";
    props.width = 1120;
    props.height = 680;
    props.maximized = false;
#ifdef _WIN32
    props.appUserModelId = L"Mipsync.MipsyncEngine.Hub";
#endif

    Window window(props);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    EditorTheme::Apply();
    EditorTheme::LoadFonts();

    ImGui_ImplGlfw_InitForOpenGL(window.GetNativeWindow(), true);
    ImGui_ImplOpenGL3_Init("#version 330");

#ifdef _WIN32
    Win32AppIcon::ApplyToGlfwWindow(window.GetNativeWindow());
#endif

    while (!window.ShouldClose() && !m_Done) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        EnsureLogoTexture();
        DrawUI();

        ImGui::Render();
        glViewport(0, 0, window.GetWidth(), window.GetHeight());
        const ImVec4 clear = EditorTheme::GetClearColor();
        glClearColor(clear.x, clear.y, clear.z, clear.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        window.OnUpdate();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    return m_Result;
}

bool HubApp::ProjectMatchesFilter(const ProjectInfo& project) const {
    if (!m_SearchFilter[0])
        return true;

    std::string needle = m_SearchFilter;
    std::string hayName = project.name;
    std::string hayPath = project.path;
    auto lower = [](std::string& s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    };
    lower(needle);
    lower(hayName);
    lower(hayPath);
    return hayName.find(needle) != std::string::npos || hayPath.find(needle) != std::string::npos;
}

void HubApp::DrawUI() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 vpMin = viewport->WorkPos;
    const ImVec2 vpSize = viewport->WorkSize;
    const ImVec2 vpMax(vpMin.x + vpSize.x, vpMin.y + vpSize.y);

    ImDrawList* bgDraw = ImGui::GetBackgroundDrawList();
    bgDraw->AddRectFilled(vpMin, vpMax, ImGui::ColorConvertFloat4ToU32(UiTokens::Bg));

    ImGui::SetNextWindowPos(vpMin);
    ImGui::SetNextWindowSize(vpSize);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##MipsyncHub", nullptr, flags);
    ImGui::PopStyleVar(2);

    DrawSidebar(kSidebarWidth);

    ImGui::SameLine(0.0f, 0.0f);
    ImGui::BeginChild("##HubMain", ImVec2(0.0f, 0.0f), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    DrawProjectsPage();
    ImGui::EndChild();

    ImGui::End();

    if (m_OpenNewProjectDialog) {
        ImGui::OpenPopup("New Project");
        m_OpenNewProjectDialog = false;
    }
    if (m_OpenAddExistingDialog) {
        ImGui::OpenPopup("Add Existing Project");
        m_OpenAddExistingDialog = false;
    }

    DrawNewProjectModal();
    DrawAddExistingModal();
}

void HubApp::DrawSidebar(float width) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, UiTokens::Hex(0x181818));
    ImGui::BeginChild("##HubSidebar", ImVec2(width, 0.0f), false);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 20.0f));
    ImGui::BeginChild("##HubSidebarInner", ImVec2(0.0f, 0.0f), false);

    const ImVec2 logoPos = ImGui::GetCursorScreenPos();
    const ImVec2 logoMax(logoPos.x + kLogoSize, logoPos.y + kLogoSize);
    if (m_LogoTexture && m_LogoTexture->GetID() != 0) {
        DrawImageAspectFit(ImGui::GetWindowDrawList(), m_LogoTexture->GetID(),
                           m_LogoTexture->GetWidth(), m_LogoTexture->GetHeight(), logoPos, logoMax);
    } else {
        ImGui::GetWindowDrawList()->AddRectFilled(
            logoPos, logoMax, ImGui::ColorConvertFloat4ToU32(UiTokens::BrandTertiary),
            EditorTheme::ShapeCornerSmall);
    }

    ImGui::SetCursorScreenPos(ImVec2(logoMax.x + 10.0f, logoPos.y + 4.0f));
    ImGui::TextColored(UiTokens::Text, "Mipsync");
    ImGui::SetCursorScreenPos(ImVec2(logoMax.x + 10.0f, logoPos.y + 22.0f));
    ImGui::TextColored(UiTokens::TextSecondary, "Hub");

    ImGui::SetCursorScreenPos(ImVec2(logoPos.x, logoMax.y + 24.0f));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));

    ImGui::Spacing();
    SidebarNavItem("Projects", true, true);
    SidebarNavItem("Installs", false, false);
    SidebarNavItem("Learn", false, false);

    const float footerY = ImGui::GetWindowHeight() - 48.0f;
    if (footerY > ImGui::GetCursorPosY())
        ImGui::Dummy(ImVec2(0.0f, footerY - ImGui::GetCursorPosY()));

    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(UiTokens::TextTertiary, "Mipsync Engine");
    ImGui::TextColored(UiTokens::TextTertiary, "v0.1");

    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void HubApp::DrawProjectsPage() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f, 24.0f));
    ImGui::BeginChild("##HubProjectsInner", ImVec2(0.0f, 0.0f), false);

    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.empty() ? nullptr : ImGui::GetIO().Fonts->Fonts[0]);
    ImGui::SetWindowFontScale(1.35f);
    ImGui::TextColored(UiTokens::Text, "Projects");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopFont();

    ImGui::Spacing();

    const float searchW = 280.0f;
    const float buttonW = 132.0f;
    const float buttonH = EditorTheme::ButtonHeight;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float rowRight = ImGui::GetContentRegionMax().x;

    ImGui::SetNextItemWidth(searchW);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, UiTokens::BgSecondary);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, EditorTheme::ShapeCornerMedium);
    ImGui::InputTextWithHint("##search", "Search projects", m_SearchFilter, sizeof(m_SearchFilter));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::SameLine();
    float x = rowRight - buttonW * 2.0f - spacing;
    ImGui::SetCursorPosX(x);
    if (EditorTheme::AeroButton("Add existing", ImVec2(buttonW, buttonH), AeroButtonKind::Secondary)) {
        m_OpenAddExistingDialog = true;
        m_LastError.clear();
    }
    ImGui::SameLine();
    if (EditorTheme::AeroButton("+ New project", ImVec2(buttonW, buttonH), AeroButtonKind::Primary)) {
        m_OpenNewProjectDialog = true;
        m_LastError.clear();
    }

    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, UiTokens::BgSecondary);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, EditorTheme::ShapeCornerMedium);
    ImGui::BeginChild("##HubProjectList", ImVec2(0.0f, 0.0f), true);
    DrawProjectTable();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    if (!m_LastError.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(EditorTheme::Error, "%s", m_LastError.c_str());
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void HubApp::DrawProjectTable() {
    size_t visibleCount = 0;
    for (const auto& project : m_Projects) {
        if (ProjectMatchesFilter(project))
            ++visibleCount;
    }

    if (visibleCount == 0) {
        ImGui::Spacing();
        ImGui::Spacing();
        const char* msg = m_Projects.empty()
            ? "No projects yet. Create one with \"+ New project\"."
            : "No projects match your search.";
        ImGui::TextColored(EditorTheme::TextSecondary, "%s", msg);
        return;
    }

    const ImGuiTableFlags tableFlags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterH |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable;

    if (!ImGui::BeginTable("##Projects", 4, tableFlags)) {
        return;
    }

    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.42f);
    ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch, 0.38f);
    ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthFixed, 96.0f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 168.0f);
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();

    std::string toOpen;
    std::string toRemove;

    for (const auto& project : m_Projects) {
        if (!ProjectMatchesFilter(project))
            continue;

        ImGui::PushID(project.path.c_str());
        ImGui::TableNextRow(ImGuiTableRowFlags_None, 52.0f);

        const bool valid = Project::IsValidDir(project.path);

        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        if (!valid)
            ImGui::TextColored(UiTokens::Warning, "!");
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::TextColored(UiTokens::Text, "%s", project.name.c_str());

        ImGui::TableSetColumnIndex(1);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(valid ? UiTokens::TextTertiary : UiTokens::Danger, "%s%s",
                           project.path.c_str(), valid ? "" : "  (missing)");

        ImGui::TableSetColumnIndex(2);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(UiTokens::TextSecondary, "%s",
                           FormatRelativeTime(project.lastOpened).c_str());

        ImGui::TableSetColumnIndex(3);
        ImGui::AlignTextToFramePadding();
        ImGui::BeginDisabled(!valid);
        if (EditorTheme::AeroButton("Open", ImVec2(72.0f, EditorTheme::ButtonHeight),
                                    AeroButtonKind::Primary))
            toOpen = project.path;
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (EditorTheme::AeroButton("Remove", ImVec2(76.0f, EditorTheme::ButtonHeight),
                                    AeroButtonKind::Secondary))
            toRemove = project.path;

        ImGui::PopID();
    }

    ImGui::EndTable();

    if (!toOpen.empty()) {
        const auto it = std::find_if(m_Projects.begin(), m_Projects.end(),
                                     [&](const ProjectInfo& p) { return p.path == toOpen; });
        if (it != m_Projects.end())
            OpenProject(*it);
    }
    if (!toRemove.empty())
        RemoveProject(toRemove);
}

void HubApp::DrawNewProjectModal() {
    ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("New Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, EditorTheme::ShapeCornerMedium);
        ImGui::TextColored(EditorTheme::TextPrimary, "Create a new Mipsync Engine project");
        ImGui::Spacing();

        ImGui::PushItemWidth(-1.0f);
        ImGui::TextColored(EditorTheme::TextSecondary, "Project name");
        ImGui::InputText("##name", m_NewName, sizeof(m_NewName));
        ImGui::Spacing();

        ImGui::TextColored(EditorTheme::TextSecondary, "Location (parent directory)");
        ImGui::InputText("##loc", m_NewLocation, sizeof(m_NewLocation));
        ImGui::PopItemWidth();

        ImGui::Spacing();
        ImGui::TextColored(EditorTheme::TextSecondary, "Will create: %s%s%s", m_NewLocation,
                           (m_NewLocation[0] && m_NewLocation[std::strlen(m_NewLocation) - 1] != '/' &&
                            m_NewLocation[std::strlen(m_NewLocation) - 1] != '\\')
                               ? "/"
                               : "",
                           m_NewName);

        if (!m_LastError.empty())
            ImGui::TextColored(EditorTheme::Error, "%s", m_LastError.c_str());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const float buttonW = 110.0f;
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - buttonW * 2.0f -
                             ImGui::GetStyle().ItemSpacing.x);
        if (EditorTheme::AeroButton("Cancel", ImVec2(buttonW, EditorTheme::ButtonHeight),
                                    AeroButtonKind::Secondary)) {
            m_LastError.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();

        if (EditorTheme::AeroButton("Create", ImVec2(buttonW, EditorTheme::ButtonHeight),
                                    AeroButtonKind::Primary)) {
            ProjectInfo info;
            std::string err;
            if (Project::Create(m_NewLocation, m_NewName, info, err)) {
                info.lastOpened = std::time(nullptr);
                HubRegistry::AddOrUpdate(m_Projects, info);
                HubRegistry::Save(m_Projects);
                m_LastError.clear();
                ImGui::CloseCurrentPopup();
                OpenProject(info);
            } else {
                m_LastError = err;
            }
        }

        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }
}

void HubApp::DrawAddExistingModal() {
    ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Add Existing Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, EditorTheme::ShapeCornerMedium);
        ImGui::TextColored(EditorTheme::TextPrimary, "Add an existing Mipsync Engine project");
        ImGui::Spacing();

        ImGui::PushItemWidth(-1.0f);
        ImGui::TextColored(EditorTheme::TextSecondary,
                           "Project directory (must contain nostalty.project)");
        ImGui::InputText("##addpath", m_AddPath, sizeof(m_AddPath));
        ImGui::PopItemWidth();

        if (!m_LastError.empty())
            ImGui::TextColored(EditorTheme::Error, "%s", m_LastError.c_str());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const float buttonW = 110.0f;
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - buttonW * 2.0f -
                             ImGui::GetStyle().ItemSpacing.x);
        if (EditorTheme::AeroButton("Cancel", ImVec2(buttonW, EditorTheme::ButtonHeight),
                                    AeroButtonKind::Secondary)) {
            m_LastError.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();

        if (EditorTheme::AeroButton("Add", ImVec2(buttonW, EditorTheme::ButtonHeight),
                                    AeroButtonKind::Primary)) {
            ProjectInfo info;
            std::string err;
            if (Project::LoadFromDir(m_AddPath, info, err)) {
                HubRegistry::AddOrUpdate(m_Projects, info);
                HubRegistry::Save(m_Projects);
                m_LastError.clear();
                ImGui::CloseCurrentPopup();
            } else {
                m_LastError = err;
            }
        }

        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }
}

void HubApp::OpenProject(const ProjectInfo& info) {
    ProjectInfo updated = info;
    updated.lastOpened = std::time(nullptr);
    HubRegistry::AddOrUpdate(m_Projects, updated);
    HubRegistry::Save(m_Projects);

    std::string err;
    ProjectInfo diskInfo;
    if (Project::LoadFromDir(updated.path, diskInfo, err)) {
        diskInfo.lastOpened = updated.lastOpened;
        Project::SaveToDir(diskInfo, err);
    } else {
        MIPSYNC_WARN("Failed to refresh project metadata before opening: {}", err);
    }

    m_Result.action = HubAction::OpenProject;
    m_Result.projectPath = updated.path;
    m_Done = true;
}

void HubApp::RemoveProject(const std::string& path) {
    HubRegistry::Remove(m_Projects, path);
    HubRegistry::Save(m_Projects);
}

} // namespace MipsyncEngine
