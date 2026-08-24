#include "Project.h"
#include "../core/Log.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace MipsyncEngine {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr const char* kDefaultSceneTemplate = R"({
  "version": 1,
  "entities": [
    {
      "id": 1,
      "name": "PS1 Cube",
      "transform": { "position": [0.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0] },
      "meshRenderer": { "primitive": "Cube", "size": 2.0, "color": [1.0, 1.0, 1.0, 1.0] },
      "mipsScript": { "path": "Rotator.mips" }
    },
    {
      "id": 2,
      "name": "Floor",
      "transform": { "position": [0.0, -1.5, 0.0], "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0] },
      "meshRenderer": { "primitive": "Plane", "size": 20.0, "color": [1.0, 1.0, 1.0, 1.0] },
      "collider": { "shape": 0, "center": [0.0, 0.0, 0.0], "halfExtents": [10.0, 0.05, 10.0], "radius": 0.5, "capsuleHeight": 1.0, "isTrigger": false },
      "rigidbody": { "bodyType": 0, "mass": 1.0, "useGravity": true, "linearDrag": 0.05, "bounciness": 0.2, "freezeRotation": false }
    },
    {
      "id": 3,
      "name": "Main Camera",
      "transform": { "position": [0.0, 2.0, 6.0], "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0] },
      "camera": { "primary": true, "fov": 60.0, "nearClip": 0.1, "farClip": 100.0 }
    }
  ]
}
)";

constexpr const char* kRotatorScriptTemplate = R"(class Rotator : MipsBehaviour
{
    public float speed = 90.0;

    void Awake()
    {
        Log.Info("Rotator awake");
    }

    void Start()
    {
        Log.Info("Rotator ready");
    }

    void Update()
    {
        transform.rotation.y = transform.rotation.y + speed * Time.deltaTime;
    }

    void OnDestroy()
    {
        Log.Info("Rotator destroyed");
    }
}
)";

bool WriteText(const fs::path& path, const std::string& contents, std::string& outError) {
    try {
        if (path.has_parent_path())
            fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        if (!out.is_open()) {
            outError = "failed to open file for writing: " + path.string();
            return false;
        }
        out << contents;
        return true;
    } catch (const std::exception& ex) {
        outError = ex.what();
        return false;
    }
}

std::string NormalizeProjectPath(const std::string& path) {
    std::error_code ec;
    fs::path input(path);
    fs::path resolved = fs::weakly_canonical(input, ec);
    if (ec)
        resolved = fs::absolute(input, ec);
    if (ec)
        resolved = input;

    std::string s = resolved.string();
#ifdef _WIN32
    const std::string extended = R"(\\?\)";
    if (s.rfind(extended, 0) == 0) {
        s = s.substr(extended.size());
        const std::string unc = R"(UNC\)";
        if (s.rfind(unc, 0) == 0)
            s = R"(\\)" + s.substr(unc.size());
    }
#endif
    return s;
}

bool PathsEqual(const std::string& a, const std::string& b) {
    const std::string na = NormalizeProjectPath(a);
    const std::string nb = NormalizeProjectPath(b);
#ifdef _WIN32
    if (na.size() != nb.size())
        return false;
    return std::equal(na.begin(), na.end(), nb.begin(), nb.end(),
        [](unsigned char x, unsigned char y) {
            return std::tolower(x) == std::tolower(y);
        });
#else
    return na == nb;
#endif
}

std::vector<ProjectInfo> DedupeProjects(std::vector<ProjectInfo> projects) {
    std::vector<ProjectInfo> result;
    for (auto& project : projects) {
        project.path = NormalizeProjectPath(project.path);
        auto it = std::find_if(result.begin(), result.end(),
            [&](const ProjectInfo& existing) { return PathsEqual(existing.path, project.path); });
        if (it != result.end()) {
            if (project.lastOpened >= it->lastOpened)
                *it = project;
        } else {
            result.push_back(project);
        }
    }
    std::sort(result.begin(), result.end(),
        [](const ProjectInfo& a, const ProjectInfo& b) { return a.lastOpened > b.lastOpened; });
    return result;
}

} // namespace

namespace Project {

bool IsValidDir(const std::string& dir) {
    std::error_code ec;
    return fs::is_regular_file(fs::path(dir) / kProjectFile, ec);
}

bool LoadFromDir(const std::string& projectDir, ProjectInfo& outInfo, std::string& outError) {
    try {
        const fs::path filePath = fs::path(projectDir) / kProjectFile;
        std::ifstream file(filePath);
        if (!file.is_open()) {
            outError = "missing nostalty.project in: " + projectDir;
            return false;
        }
        json j;
        file >> j;

        outInfo.path          = NormalizeProjectPath(projectDir);
        outInfo.name          = j.value("name", fs::path(projectDir).filename().string());
        outInfo.engineVersion = j.value("engineVersion", "0.1.0");
        outInfo.defaultScene  = j.value("defaultScene", "scenes/default.nscene");
        outInfo.editorLastScene = j.value("editorLastScene", outInfo.defaultScene);
        outInfo.lastOpened    = static_cast<std::time_t>(j.value("lastOpened", int64_t{0}));

        if (j.contains("playerSettings") && j["playerSettings"].is_object()) {
            const json& ps = j["playerSettings"];
            outInfo.player.productName = ps.value("productName", std::string{});
            outInfo.player.companyName = ps.value("companyName", std::string{});
            outInfo.player.startupSceneIndex = ps.value("startupSceneIndex", 0);
            outInfo.player.scenesInBuild.clear();
            if (ps.contains("scenesInBuild") && ps["scenesInBuild"].is_array()) {
                for (const json& entry : ps["scenesInBuild"]) {
                    if (entry.is_string())
                        outInfo.player.scenesInBuild.push_back(entry.get<std::string>());
                }
            }
        }

        if (outInfo.player.productName.empty())
            outInfo.player.productName = outInfo.name;
        if (outInfo.player.scenesInBuild.empty())
            outInfo.player.scenesInBuild.push_back(outInfo.defaultScene);
        return true;
    } catch (const std::exception& ex) {
        outError = ex.what();
        return false;
    }
}

bool SaveToDir(const ProjectInfo& info, std::string& outError) {
    try {
        const fs::path filePath = fs::path(info.path) / kProjectFile;
        fs::create_directories(filePath.parent_path());

        json j;
        j["name"]          = info.name;
        j["engineVersion"] = info.engineVersion;
        j["defaultScene"]  = info.defaultScene;
        j["editorLastScene"] = info.editorLastScene.empty() ? info.defaultScene : info.editorLastScene;
        j["lastOpened"]    = static_cast<int64_t>(info.lastOpened);

        json ps;
        ps["productName"] = info.player.productName;
        ps["companyName"] = info.player.companyName;
        ps["startupSceneIndex"] = info.player.startupSceneIndex;
        ps["scenesInBuild"] = json::array();
        for (const std::string& scene : info.player.scenesInBuild)
            ps["scenesInBuild"].push_back(scene);
        j["playerSettings"] = ps;

        std::ofstream out(filePath);
        if (!out.is_open()) {
            outError = "failed to open: " + filePath.string();
            return false;
        }
        out << j.dump(2);
        return true;
    } catch (const std::exception& ex) {
        outError = ex.what();
        return false;
    }
}

bool Create(const std::string& parentDir, const std::string& name, ProjectInfo& outInfo, std::string& outError) {
    try {
        if (name.empty()) {
            outError = "project name is empty";
            return false;
        }

        const fs::path projectPath = fs::path(parentDir) / name;
        std::error_code ec;
        if (fs::exists(projectPath, ec) && !fs::is_empty(projectPath, ec)) {
            outError = "directory already exists and is not empty: " + projectPath.string();
            return false;
        }

        fs::create_directories(projectPath, ec);
        if (ec) {
            outError = "failed to create directory: " + ec.message();
            return false;
        }

        outInfo.name         = name;
        outInfo.path         = NormalizeProjectPath(projectPath.string());
        outInfo.lastOpened   = std::time(nullptr);
        outInfo.defaultScene = "scenes/default.nscene";
        outInfo.editorLastScene = outInfo.defaultScene;
        outInfo.player.productName = name;
        outInfo.player.scenesInBuild = { outInfo.defaultScene };

        if (!SaveToDir(outInfo, outError))
            return false;

        if (!WriteText(projectPath / "scenes" / "default.nscene", kDefaultSceneTemplate, outError))
            return false;

        if (!WriteText(projectPath / "scripts" / "Rotator.mips", kRotatorScriptTemplate, outError))
            return false;

        for (const char* sub : { "assets", "assets/textures", "assets/materials", "assets/prefabs", "assets/models" }) {
            std::error_code subEc;
            fs::create_directories(projectPath / sub, subEc);
        }

        return true;
    } catch (const std::exception& ex) {
        outError = ex.what();
        return false;
    }
}

} // namespace Project

namespace HubRegistry {

namespace {

fs::path MipsyncConfigDir() {
#ifdef _WIN32
    if (const char* appdata = std::getenv("APPDATA"))
        return fs::path(appdata) / "MipsyncEngine";
#else
    if (const char* home = std::getenv("HOME"))
        return fs::path(home) / ".config" / "nostalty";
#endif
    return fs::current_path() / ".nostalty";
}

} // namespace

std::string GetRegistryPath() {
    return (MipsyncConfigDir() / "hub.json").string();
}

std::string DefaultProjectsRoot() {
#ifdef _WIN32
    if (const char* userProfile = std::getenv("USERPROFILE"))
        return (fs::path(userProfile) / "MipsyncProjects").string();
#else
    if (const char* home = std::getenv("HOME"))
        return (fs::path(home) / "MipsyncProjects").string();
#endif
    return (fs::current_path() / "MipsyncProjects").string();
}

std::vector<ProjectInfo> Load() {
    std::vector<ProjectInfo> result;
    try {
        const fs::path path = GetRegistryPath();
        std::ifstream file(path);
        if (!file.is_open())
            return result;

        json j;
        file >> j;
        if (!j.contains("projects") || !j["projects"].is_array())
            return result;

        for (const json& entry : j["projects"]) {
            ProjectInfo info;
            info.name          = entry.value("name", "");
            info.path          = entry.value("path", "");
            info.engineVersion = entry.value("engineVersion", "0.1.0");
            info.defaultScene  = entry.value("defaultScene", "scenes/default.nscene");
            info.lastOpened    = static_cast<std::time_t>(entry.value("lastOpened", int64_t{0}));
            if (!info.path.empty())
                result.push_back(info);
        }
    } catch (const std::exception&) {
        // ignore — return empty list on parse failure
    }
    const size_t before = result.size();
    result = DedupeProjects(std::move(result));
    if (result.size() != before)
        Save(result);
    return result;
}

bool Save(const std::vector<ProjectInfo>& projects) {
    try {
        const fs::path path = GetRegistryPath();
        fs::create_directories(path.parent_path());

        json j;
        j["version"] = 1;
        j["projects"] = json::array();
        for (const auto& p : projects) {
            json entry;
            entry["name"]          = p.name;
            entry["path"]          = p.path;
            entry["engineVersion"] = p.engineVersion;
            entry["defaultScene"]  = p.defaultScene;
            entry["lastOpened"]    = static_cast<int64_t>(p.lastOpened);
            j["projects"].push_back(entry);
        }

        std::ofstream out(path);
        if (!out.is_open())
            return false;
        out << j.dump(2);
        return true;
    } catch (const std::exception& ex) {
        MIPSYNC_WARN("HubRegistry save failed: {}", ex.what());
        return false;
    }
}

void AddOrUpdate(std::vector<ProjectInfo>& list, const ProjectInfo& info) {
    ProjectInfo updated = info;
    updated.path = NormalizeProjectPath(updated.path);

    auto it = std::find_if(list.begin(), list.end(),
        [&](const ProjectInfo& p) { return PathsEqual(p.path, updated.path); });
    if (it != list.end())
        *it = updated;
    else
        list.push_back(updated);

    std::sort(list.begin(), list.end(),
        [](const ProjectInfo& a, const ProjectInfo& b) { return a.lastOpened > b.lastOpened; });
}

void Remove(std::vector<ProjectInfo>& list, const std::string& path) {
    list.erase(std::remove_if(list.begin(), list.end(),
        [&](const ProjectInfo& p) { return PathsEqual(p.path, path); }), list.end());
}

} // namespace HubRegistry
} // namespace MipsyncEngine
