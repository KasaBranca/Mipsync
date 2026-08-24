#pragma once
// ─────────────────────────────────────────────────
// Mipsync Engine — Project Metadata & Hub Registry
// ─────────────────────────────────────────────────

#include <string>
#include <vector>
#include <ctime>

namespace MipsyncEngine {

/// PS1 build settings (stored as playerSettings in nostalty.project for compatibility).
struct PlayerSettings {
    std::string productName;
    std::string companyName;
    /// Project-relative scene paths, in build order (index 0 = first scene).
    std::vector<std::string> scenesInBuild;
    int startupSceneIndex = 0;
};

struct ProjectInfo {
    std::string name;
    std::string path;          // absolute project directory
    std::string engineVersion = "0.1.0";
    std::string defaultScene  = "scenes/default.nscene";
    std::string editorLastScene = "scenes/default.nscene";
    std::time_t lastOpened    = 0;
    PlayerSettings player;
};

namespace Project {

constexpr const char* kProjectFile = "nostalty.project";

bool LoadFromDir(const std::string& projectDir, ProjectInfo& outInfo, std::string& outError);
bool SaveToDir(const ProjectInfo& info, std::string& outError);
/// Creates <parentDir>/<name>/ with project file, scenes/default.nscene, scripts/Rotator.mips
bool Create(const std::string& parentDir, const std::string& name, ProjectInfo& outInfo, std::string& outError);
bool IsValidDir(const std::string& dir);

} // namespace Project

namespace HubRegistry {

std::string GetRegistryPath();
std::vector<ProjectInfo> Load();
bool Save(const std::vector<ProjectInfo>& projects);
void AddOrUpdate(std::vector<ProjectInfo>& list, const ProjectInfo& info);
void Remove(std::vector<ProjectInfo>& list, const std::string& path);
std::string DefaultProjectsRoot();

} // namespace HubRegistry

} // namespace MipsyncEngine
