#include "Ps1EditorPrefs.h"
#include "../assets/AssetManager.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace MipsyncEngine::Ps1 {
namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

fs::path EditorPrefsPath() {
#ifdef _WIN32
    if (const char* appdata = std::getenv("APPDATA")) {
        return fs::path(appdata) / "MipsyncEngine" / "editor.json";
    }
#endif
    return fs::current_path() / ".mipsync" / "editor.json";
}

} // namespace

bool LoadEditorPrefs(EditorPrefs& out, std::string& outError) {
    out = {};
    try {
        const fs::path path = EditorPrefsPath();
        if (!fs::is_regular_file(path))
            return true;
        std::ifstream in(path);
        if (!in.is_open()) {
            outError = "cannot open editor prefs: " + PathUtf8::ToString(path);
            return false;
        }
        json j;
        in >> j;
        if (j.contains("ps1") && j["ps1"].is_object()) {
            const json& p = j["ps1"];
            out.emulatorPath = p.value("emulatorPath", std::string{});
            out.biosPath = p.value("biosPath", std::string{});
            out.extraArgs = p.value("extraArgs", std::string{});
        }
        return true;
    } catch (const std::exception& ex) {
        outError = ex.what();
        return false;
    }
}

bool SaveEditorPrefs(const EditorPrefs& prefs, std::string& outError) {
    try {
        const fs::path path = EditorPrefsPath();
        if (path.has_parent_path())
            fs::create_directories(path.parent_path());

        json j;
        j["ps1"]["emulatorPath"] = prefs.emulatorPath;
        j["ps1"]["biosPath"] = prefs.biosPath;
        j["ps1"]["extraArgs"] = prefs.extraArgs;

        std::ofstream out(path);
        if (!out.is_open()) {
            outError = "cannot write editor prefs: " + PathUtf8::ToString(path);
            return false;
        }
        out << j.dump(2);
        return true;
    } catch (const std::exception& ex) {
        outError = ex.what();
        return false;
    }
}

std::string ResolveEmulatorPath(const EditorPrefs& prefs) {
    auto exists = [](const std::string& p) {
        std::error_code ec;
        return !p.empty() && fs::is_regular_file(PathUtf8::FromString(p), ec);
    };

    if (exists(prefs.emulatorPath))
        return prefs.emulatorPath;

    if (const char* env = std::getenv("MIPSYNC_PS1_EMULATOR")) {
        if (exists(env))
            return env;
    }

#ifdef _WIN32
    const wchar_t* candidates[] = {
        L"DuckStation.exe",
        L"duckstation-qt-x64-Release.exe",
        L"duckstation-qt-x64-ReleaseLTCG.exe",
    };
    const wchar_t* roots[] = {
        L"C:\\Program Files\\DuckStation",
        L"C:\\Program Files (x86)\\DuckStation",
    };
    for (const wchar_t* root : roots) {
        for (const wchar_t* name : candidates) {
            const fs::path p = fs::path(root) / name;
            std::error_code ec;
            if (fs::is_regular_file(p, ec))
                return PathUtf8::ToString(p);
        }
    }
#endif

    return {};
}

} // namespace MipsyncEngine::Ps1
