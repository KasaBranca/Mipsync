#pragma once
// ─────────────────────────────────────────────────
// Mipsync Hub — Project Launcher (Unity Hub-like)
// ─────────────────────────────────────────────────

#include "../project/Project.h"
#include "../renderer/Texture.h"
#include <memory>
#include <string>
#include <vector>

namespace MipsyncEngine {

enum class HubAction {
    Cancel,
    OpenProject,
};

struct HubResult {
    HubAction action = HubAction::Cancel;
    std::string projectPath;
};

class HubApp {
public:
    HubApp();
    ~HubApp();

    HubResult Run();

private:
    void DrawUI();
    void DrawSidebar(float width);
    void DrawProjectsPage();
    void DrawProjectTable();
    void DrawNewProjectModal();
    void DrawAddExistingModal();

    void EnsureLogoTexture();
    void OpenProject(const ProjectInfo& info);
    void RemoveProject(const std::string& path);
    bool ProjectMatchesFilter(const ProjectInfo& project) const;

    std::vector<ProjectInfo> m_Projects;
    HubResult m_Result;
    bool m_Done = false;

    std::shared_ptr<Texture> m_LogoTexture;

    bool m_OpenNewProjectDialog = false;
    bool m_OpenAddExistingDialog = false;
    char m_NewName[128] = "MyGame";
    char m_NewLocation[512] = {};
    char m_AddPath[512] = {};
    char m_SearchFilter[128] = {};
    std::string m_LastError;
};

} // namespace MipsyncEngine
