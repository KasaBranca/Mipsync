#include "MipsEditorIntegration.h"

#include "../assets/AssetManager.h"
#include "../core/Log.h"
#include "../core/RuntimePaths.h"
#include "../mips/MipsRuntime.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace MipsyncEngine {

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

bool LooksAbsolute(const std::string& path) {
    if (path.empty())
        return false;
    std::error_code ec;
    return std::filesystem::path(PathUtf8::FromString(path)).is_absolute();
}

std::string QuoteArg(const std::string& arg) {
    std::string out = "\"";
    for (char ch : arg) {
        if (ch == '"')
            out += '\\';
        out += ch;
    }
    out += '"';
    return out;
}

#ifdef _WIN32
bool ShellOpen(const std::string& target, const std::string& args = {}) {
    const std::wstring wTarget = PathUtf8::FromString(target).wstring();
    const std::wstring wArgs = PathUtf8::FromString(args).wstring();
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", wTarget.c_str(),
                                           args.empty() ? nullptr : wArgs.c_str(), nullptr,
                                           SW_SHOWNORMAL);
    return reinterpret_cast<intptr_t>(result) > 32;
}

bool TryOpenWithCommand(const wchar_t* command, const std::string& args) {
    const std::wstring wArgs = PathUtf8::FromString(args).wstring();
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", command, wArgs.c_str(), nullptr,
                                           SW_SHOWNORMAL);
    return reinterpret_cast<intptr_t>(result) > 32;
}
#endif

MipsEditorDiagnostic ParseDiagnosticLine(const std::string& line) {
    static const std::regex kLocated(R"(^(.+)\((\d+),(\d+)\):\s*(.+)$)");
    std::smatch match;
    MipsEditorDiagnostic diag;
    diag.message = line;
    if (std::regex_match(line, match, kLocated) && match.size() == 5) {
        diag.line = std::max(0, std::stoi(match[2].str()));
        diag.column = std::max(0, std::stoi(match[3].str()));
        diag.message = match[4].str();
        diag.hasLocation = true;
    }
    return diag;
}

bool CopyDirectoryContents(const fs::path& source, const fs::path& dest) {
    std::error_code ec;
    if (!fs::is_directory(source, ec))
        return false;
    fs::create_directories(dest, ec);
    if (ec)
        return false;

    bool ok = true;
    for (auto it = fs::recursive_directory_iterator(
             source, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) {
            ok = false;
            ec.clear();
            continue;
        }

        const fs::path rel = fs::relative(it->path(), source, ec);
        if (ec || rel.empty()) {
            ok = false;
            ec.clear();
            continue;
        }
        const std::string filename = rel.filename().string();
        if (it->is_directory(ec) &&
            (filename == "node_modules" || filename == ".git" || filename == ".vsix")) {
            it.disable_recursion_pending();
            continue;
        }
        if (filename == "package-lock.json" || filename == "npm-debug.log" ||
            rel.extension() == ".vsix") {
            continue;
        }

        const fs::path target = dest / rel;
        if (it->is_directory(ec)) {
            fs::create_directories(target, ec);
            if (ec) {
                ok = false;
                ec.clear();
            }
            continue;
        }
        if (!it->is_regular_file(ec))
            continue;

        fs::create_directories(target.parent_path(), ec);
        fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            ok = false;
            ec.clear();
        }
    }
    return ok;
}

fs::path UserProfileDirectory() {
    if (const char* profile = std::getenv("USERPROFILE")) {
        if (*profile)
            return PathUtf8::FromString(profile);
    }
    return {};
}

void SyncBundledMipsIdeExtension() {
    const fs::path exeDir = GetExeDirectory();
    const fs::path extensionSource = exeDir / "tools" / "vscode-mips";
    const fs::path serverSource = exeDir / "tools" / "mips-language-server";
    const fs::path packageJson = extensionSource / "package.json";
    std::error_code ec;
    if (!fs::is_regular_file(packageJson, ec))
        return;

    const fs::path profile = UserProfileDirectory();
    if (profile.empty())
        return;

    std::string publisher = "mipsync";
    std::string name = "mipsync-mipssharp-vscode";
    std::string version = "0.0.0";
    try {
        std::ifstream in(packageJson, std::ios::binary);
        json packageRoot;
        in >> packageRoot;
        if (packageRoot.contains("publisher") && packageRoot["publisher"].is_string())
            publisher = packageRoot["publisher"].get<std::string>();
        if (packageRoot.contains("name") && packageRoot["name"].is_string())
            name = packageRoot["name"].get<std::string>();
        if (packageRoot.contains("version") && packageRoot["version"].is_string())
            version = packageRoot["version"].get<std::string>();
    } catch (...) {
    }

    const fs::path installName = publisher + "." + name + "-" + version;
    const fs::path legacyInstallName = publisher + "." + name;
    const std::array<fs::path, 2> extensionRoots = {
        profile / ".vscode" / "extensions",
        profile / ".cursor" / "extensions",
    };

    for (const fs::path& root : extensionRoots) {
        const fs::path installDir = root / installName;
        const fs::path backupRoot = root.parent_path() / "mipsync-extension-backups";
        fs::create_directories(backupRoot, ec);
        if (fs::is_directory(root, ec)) {
            for (auto it = fs::directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
                 it != fs::directory_iterator(); it.increment(ec)) {
                if (ec) {
                    ec.clear();
                    continue;
                }
                if (!it->is_directory(ec))
                    continue;
                const fs::path candidate = it->path();
                if (candidate == installDir)
                    continue;
                const std::string dirName = candidate.filename().string();
                const bool isOldMipsExtension =
                    dirName.rfind("mipsync.mipsync-mips-vscode", 0) == 0;
                const bool isOldUnversionedNewExtension =
                    dirName == (publisher + "." + name);
                if (!isOldMipsExtension && !isOldUnversionedNewExtension)
                    continue;
                fs::path backupDir = backupRoot / dirName;
                if (fs::exists(backupDir, ec))
                    backupDir = backupRoot / (dirName + ".old");
                fs::rename(candidate, backupDir, ec);
                ec.clear();
            }
        }

        const fs::path legacyDir = root / legacyInstallName;
        if (legacyDir != installDir && fs::is_directory(legacyDir, ec)) {
            const fs::path backupDir = backupRoot / legacyInstallName;
            fs::rename(legacyDir, backupDir, ec);
            ec.clear();
        }

        const bool extensionOk = CopyDirectoryContents(extensionSource, installDir);
        bool serverOk = true;
        if (fs::is_directory(serverSource, ec))
            serverOk = CopyDirectoryContents(serverSource, installDir / "mips-language-server");
        if (extensionOk && serverOk)
            EDITOR_INFO("[Mips# IDE] Synced extension: {}", PathUtf8::ToString(installDir));
        else
            EDITOR_WARN("[Mips# IDE] Failed to fully sync extension: {}",
                        PathUtf8::ToString(installDir));
    }
}

fs::path ResolveCurrentProjectRoot(const std::string& resolvedScriptPath) {
    const std::string& root = AssetManager::Get().GetProjectRoot();
    if (!root.empty())
        return PathUtf8::FromString(root);

    fs::path p = PathUtf8::FromString(resolvedScriptPath);
    std::error_code ec;
    p = fs::absolute(p, ec);
    if (ec)
        return {};
    for (fs::path cur = p.parent_path(); !cur.empty(); cur = cur.parent_path()) {
        if (fs::is_regular_file(cur / "nostalty.project", ec))
            return cur;
        if (cur == cur.root_path())
            break;
    }
    return p.parent_path();
}

json LoadJsonObject(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
        return json::object();
    try {
        json root;
        in >> root;
        return root.is_object() ? root : json::object();
    } catch (...) {
        return json::object();
    }
}

bool SaveJsonObject(const fs::path& path, const json& root) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec)
        return false;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        return false;
    out << root.dump(2);
    out << '\n';
    return true;
}

void EnsureMipsSharpWorkspaceSettings(const fs::path& projectRoot) {
    if (projectRoot.empty())
        return;

    const fs::path vscodeDir = projectRoot / ".vscode";
    const fs::path settingsPath = vscodeDir / "settings.json";
    json settings = LoadJsonObject(settingsPath);
    if (!settings.contains("files.associations") || !settings["files.associations"].is_object())
        settings["files.associations"] = json::object();
    settings["files.associations"]["*.mips"] = "mipssharp";
    settings["editor.semanticHighlighting.enabled"] = true;

    if (SaveJsonObject(settingsPath, settings)) {
        EDITOR_INFO("[Mips# IDE] Wrote workspace language association: {}",
                    PathUtf8::ToString(settingsPath));
    } else {
        EDITOR_WARN("[Mips# IDE] Failed to write workspace settings: {}",
                    PathUtf8::ToString(settingsPath));
    }

    const fs::path extensionsPath = vscodeDir / "extensions.json";
    json extensions = LoadJsonObject(extensionsPath);
    if (!extensions.contains("recommendations") || !extensions["recommendations"].is_array())
        extensions["recommendations"] = json::array();
    constexpr const char* kExtensionId = "mipsync.mipsync-mipssharp-vscode";
    bool found = false;
    for (const auto& entry : extensions["recommendations"]) {
        if (entry.is_string() && entry.get<std::string>() == kExtensionId) {
            found = true;
            break;
        }
    }
    if (!found)
        extensions["recommendations"].push_back(kExtensionId);
    SaveJsonObject(extensionsPath, extensions);
}

} // namespace

std::string MipsEditorIntegration::ResolveScriptPath(const std::string& projectOrAbsolutePath) {
    if (projectOrAbsolutePath.empty())
        return {};
    if (LooksAbsolute(projectOrAbsolutePath))
        return projectOrAbsolutePath;
    return AssetManager::Get().ToAbsolute(projectOrAbsolutePath);
}

MipsEditorValidationResult MipsEditorIntegration::ValidateScript(
    const std::string& projectOrAbsolutePath) {
    MipsEditorValidationResult result;
    const std::string resolved = ResolveScriptPath(projectOrAbsolutePath);
    if (resolved.empty()) {
        result.diagnostics.push_back({ "No Mips# script assigned.", 0, 0, false });
        return result;
    }

    std::vector<std::string> errors;
    result.module = Mips::MipsRuntime::CompileScriptFile(resolved, errors);
    result.success = result.module && errors.empty();
    for (const std::string& error : errors) {
        result.diagnostics.push_back(ParseDiagnosticLine(error));
    }
    return result;
}

void MipsEditorIntegration::LogValidationResult(const std::string& projectOrAbsolutePath,
                                                const MipsEditorValidationResult& result) {
    const std::string display = projectOrAbsolutePath.empty() ? "<none>" : projectOrAbsolutePath;
    if (result.success) {
        const std::string cls = result.module ? result.module->className : std::string{"<unknown>"};
        EDITOR_INFO("[Mips# IDE] {} OK ({})", display, cls);
        return;
    }
    if (result.diagnostics.empty()) {
        EDITOR_WARN("[Mips# IDE] {} failed with no compiler diagnostics", display);
        return;
    }
    for (const auto& diag : result.diagnostics) {
        if (diag.hasLocation)
            EDITOR_ERROR("[Mips# IDE] {}({},{}) {}", display, diag.line, diag.column, diag.message);
        else
            EDITOR_ERROR("[Mips# IDE] {} {}", display, diag.message);
    }
}

bool MipsEditorIntegration::OpenScriptInIde(const std::string& projectOrAbsolutePath, int line,
                                            int column) {
    const std::string resolved = ResolveScriptPath(projectOrAbsolutePath);
    if (resolved.empty())
        return false;

#ifdef _WIN32
    SyncBundledMipsIdeExtension();
    const fs::path projectRoot = ResolveCurrentProjectRoot(resolved);
    EnsureMipsSharpWorkspaceSettings(projectRoot);

    const int safeLine = std::max(1, line);
    const int safeColumn = std::max(1, column);
    const std::string target = resolved + ":" + std::to_string(safeLine) +
                               ":" + std::to_string(safeColumn);
    const std::string vscodeArgs = "--reuse-window --goto " + QuoteArg(target);
    if (TryOpenWithCommand(L"code.cmd", vscodeArgs) || TryOpenWithCommand(L"code", vscodeArgs))
        return true;
    if (TryOpenWithCommand(L"cursor.cmd", vscodeArgs) || TryOpenWithCommand(L"cursor", vscodeArgs))
        return true;

    return ShellOpen(resolved);
#else
    (void)line;
    (void)column;
    return false;
#endif
}

std::string MipsEditorIntegration::BuildVsCodeExtensionPathHint() {
    const std::filesystem::path extensionDir = GetExeDirectory() / "tools" / "vscode-mips";
    return PathUtf8::ToString(extensionDir) +
           " (automatically synced to VS Code/Cursor when opening a Mips# script)";
}

} // namespace MipsyncEngine
