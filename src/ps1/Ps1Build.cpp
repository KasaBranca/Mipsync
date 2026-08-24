#include "Ps1Build.h"
#include "Ps1Toolchain.h"
#include "../build/BuildManifest.h"
#include "../core/Log.h"
#include "../assets/AssetManager.h"
#include "../mips/PS1SceneExport.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <unordered_set>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace MipsyncEngine::Ps1 {
namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

#ifdef _WIN32
fs::path ExtendedLengthPath(const fs::path& path) {
    const fs::path absolute = fs::absolute(path);
    const std::wstring native = absolute.native();
    if (native.rfind(LR"(\\?\)", 0) == 0)
        return absolute;
    if (native.rfind(LR"(\\)", 0) == 0)
        return fs::path(LR"(\\?\UNC\)" + native.substr(2));
    return fs::path(LR"(\\?\)" + native);
}
#else
const fs::path& ExtendedLengthPath(const fs::path& path) {
    return path;
}
#endif

bool ShouldSkipProjectEntry(const fs::path& relative) {
    const std::string rel = PathUtf8::ToString(relative);
    if (rel.empty())
        return false;
    if (rel == "Builds" || rel.rfind("Builds/", 0) == 0 || rel.rfind("Builds\\", 0) == 0)
        return true;
    if (rel == ".git" || rel.rfind(".git/", 0) == 0 || rel.rfind(".git\\", 0) == 0)
        return true;
    if (rel == ".nostalty" || rel.rfind(".nostalty/", 0) == 0 ||
        rel.rfind(".nostalty\\", 0) == 0)
        return true;
    if (rel == "editor_layout.ini")
        return true;
    return false;
}

void CopyProjectTree(const fs::path& sourceRoot, const fs::path& destRoot, std::string& outError) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(
             sourceRoot, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        const fs::path rel = fs::relative(it->path(), sourceRoot, ec);
        if (ec || rel.empty())
            continue;
        if (ShouldSkipProjectEntry(rel))
            it.disable_recursion_pending();

        const fs::path destPath = destRoot / rel;
        ec.clear();
        if (it->is_directory(ec)) {
            ec.clear();
            fs::create_directories(ExtendedLengthPath(destPath), ec);
            if (ec) {
                outError = "Create directory failed: " + PathUtf8::ToString(destPath) +
                           " — " + ec.message();
                return;
            }
            continue;
        }
        ec.clear();
        if (!it->is_regular_file(ec))
            continue;

        ec.clear();
        fs::create_directories(ExtendedLengthPath(destPath.parent_path()), ec);
        if (ec) {
            outError = "Create directory failed: " +
                       PathUtf8::ToString(destPath.parent_path()) + " — " + ec.message();
            return;
        }
        ec.clear();
        fs::copy_file(ExtendedLengthPath(it->path()), ExtendedLengthPath(destPath),
                      fs::copy_options::overwrite_existing, ec);
        if (ec) {
            outError = "Copy failed: " + PathUtf8::ToString(it->path()) + " -> " +
                       PathUtf8::ToString(destPath) + " — " + ec.message();
            return;
        }
    }
}

void CopyDirIfExists(const fs::path& src, const fs::path& dst, std::string& outError) {
    std::error_code ec;
    if (!fs::exists(src, ec))
        return;
    fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec)
        outError = "Copy failed: " + PathUtf8::ToString(src) + " — " + ec.message();
}

void CopyFileIfExists(const fs::path& src, const fs::path& dst, std::string& outError) {
    std::error_code ec;
    if (!fs::is_regular_file(src, ec))
        return;
    if (dst.has_parent_path())
        fs::create_directories(dst.parent_path(), ec);
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec)
        outError = "Copy failed: " + PathUtf8::ToString(src) + " — " + ec.message();
}

/// Refresh PS1 runtime sources from templates without overwriting exported `generated/`.
void CopyPs1StarterSources(const fs::path& starterRoot, const fs::path& ps1Src,
                           std::string& outError) {
    CopyFileIfExists(starterRoot / "main.c", ps1Src / "main.c", outError);
    if (!outError.empty())
        return;
    CopyFileIfExists(starterRoot / "CMakeLists.txt", ps1Src / "CMakeLists.txt", outError);
    if (!outError.empty())
        return;
    CopyFileIfExists(starterRoot / "iso.xml", ps1Src / "iso.xml", outError);
    if (!outError.empty())
        return;
    CopyFileIfExists(starterRoot / "system.cnf", ps1Src / "system.cnf", outError);
    if (!outError.empty())
        return;
    CopyDirIfExists(starterRoot / "cmake", ps1Src / "cmake", outError);
    if (!outError.empty())
        return;
    CopyDirIfExists(starterRoot / "runtime", ps1Src / "runtime", outError);
}

/// Search for a PS1 license file (licensea.dat, licensee.dat, or licensej.dat)
/// in common locations and, if found, copy it into ps1Src and patch iso.xml
/// so mkpsxiso injects it into the disc image's first 16 sectors.
/// Without this data, PS2's POPS emulator (POPStarter) refuses to boot the
/// disc and returns to the PS2 browser screen.
bool InjectLicenseFile(const fs::path& projectRoot, const fs::path& engineDir,
                       const fs::path& templates, const fs::path& ps1Src) {
    const char* licenseNames[] = {"licensea.dat", "licensee.dat", "licensej.dat"};
    fs::path foundLicense;
    std::string foundName;
    std::error_code ec;

    for (const char* name : licenseNames) {
        const fs::path candidates[] = {
            projectRoot / name,
            engineDir / name,
            engineDir / "ps1_runtime" / name,
            templates / name,
        };
        for (const auto& c : candidates) {
            if (fs::is_regular_file(c, ec)) {
                foundLicense = c;
                foundName = name;
                break;
            }
        }
        if (!foundLicense.empty()) break;
    }

    if (foundLicense.empty()) {
        MIPSYNC_WARN("PS1 license file not found — disc image will NOT boot on PS2 "
                     "(POPStarter / POPS). Place licensea.dat in your project folder "
                     "or next to the engine executable.");
        return false;
    }

    // Copy license file into ps1_src/ so mkpsxiso can find it.
    fs::copy_file(foundLicense, ps1Src / foundName,
                  fs::copy_options::overwrite_existing, ec);
    if (ec) {
        MIPSYNC_WARN("Failed to copy license file: {}", ec.message());
        return false;
    }

    // Patch iso.xml: insert <license file="..."/> before <directory_tree>
    // if not already present.
    const fs::path isoXml = ps1Src / "iso.xml";
    std::ifstream in(isoXml);
    if (!in.is_open()) return false;
    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    in.close();

    if (content.find("<license") != std::string::npos) {
        // Already has a license tag — skip patching.
        MIPSYNC_INFO("iso.xml already contains <license> tag.");
        return true;
    }

    const std::string marker = "<directory_tree>";
    const size_t pos = content.find(marker);
    if (pos == std::string::npos) return false;

    const std::string licenseTag =
        "        <license file=\"" + foundName + "\"/>\r\n";
    content.insert(pos, licenseTag);

    std::ofstream out(isoXml, std::ios::binary);
    if (!out.is_open()) return false;
    out << content;

    MIPSYNC_INFO("PS1 license injected from: {}", PathUtf8::ToString(foundLicense));
    return true;
}

bool WriteText(const fs::path& path, const std::string& text, std::string& outError) {
    try {
        if (path.has_parent_path())
            fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        if (!out.is_open()) {
            outError = "cannot write: " + PathUtf8::ToString(path);
            return false;
        }
        out << text;
        return true;
    } catch (const std::exception& ex) {
        outError = ex.what();
        return false;
    }
}

std::string SanitizeProductName(std::string name) {
    if (name.empty())
        name = "Game";
    for (char& c : name) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
            c == '>' || c == '|')
            c = '_';
    }
    return name;
}

fs::path ResolveTemplatesRoot(const std::string& engineRoot) {
    const fs::path engine = PathUtf8::FromString(engineRoot);
    const fs::path candidates[] = {
        engine / "templates" / "ps1",
        engine.parent_path() / "templates" / "ps1",
        engine.parent_path().parent_path() / "templates" / "ps1",
        fs::current_path() / "templates" / "ps1",
    };
    for (const fs::path& c : candidates) {
        std::error_code ec;
        if (fs::is_directory(c / "starter", ec))
            return c;
    }
    return {};
}

#ifdef _WIN32
bool RunPowerShellBuild(const fs::path& script, const fs::path& workingDir,
                        const fs::path& engineDir, const std::string& sdkRoot,
                        std::string& outLog) {
    auto quote = [](const std::wstring& s) { return L"\"" + s + L"\""; };
    std::wstring cmd =
        L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File " + quote(script.wstring()) +
        L" -ProductRoot " + quote(workingDir.wstring()) +
        L" -EngineRoot " + quote(engineDir.wstring());
    if (!sdkRoot.empty())
        cmd += L" -SdkRoot " + quote(PathUtf8::FromString(sdkRoot).wstring());
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmdLine(cmd.begin(), cmd.end());
    cmdLine.push_back(L'\0');

    if (!CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, workingDir.wstring().c_str(), &si, &pi)) {
        outLog = "Failed to start build script (error " + std::to_string(GetLastError()) + ")";
        return false;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    const fs::path logPath = workingDir / "build.log";
    if (fs::is_regular_file(logPath)) {
        std::ifstream in(logPath);
        outLog.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    return exitCode == 0;
}

std::string SummarizeBuildLog(const std::string& log) {
    if (log.empty()) return {};
    std::string last;
    size_t lineStart = 0;
    for (size_t i = 0; i <= log.size(); ++i) {
        if (i == log.size() || log[i] == '\n' || log[i] == '\r') {
            if (i > lineStart) {
                std::string line = log.substr(lineStart, i - lineStart);
                const size_t tsEnd = line.find(" ");
                if (tsEnd != std::string::npos && tsEnd + 1 < line.size())
                    line = line.substr(tsEnd + 1);
                if (!line.empty())
                    last = line;
            }
            while (i < log.size() && (log[i] == '\n' || log[i] == '\r')) ++i;
            lineStart = i;
            if (i < log.size()) --i;
        }
    }
    return last;
}
#endif

} // namespace

std::string DefaultOutputParent(const std::string& projectPath) {
    return PathUtf8::ToString(PathUtf8::FromString(projectPath) / "Builds" / "PS1");
}

Ps1BuildResult Build(const Ps1BuildRequest& request) {
    Ps1BuildResult result;
    PlayerSettings settings = request.settings;
    settings.productName = SanitizeProductName(settings.productName);

    if (settings.scenesInBuild.empty()) {
        result.message = "No scenes in build list. Add scenes in Build Settings.";
        return result;
    }

    const fs::path projectRoot = PathUtf8::FromString(request.projectPath);
    std::error_code ec;
    if (!fs::is_directory(projectRoot, ec)) {
        result.message = "Invalid project path.";
        return result;
    }

    const fs::path engineDir = request.engineRoot.empty()
        ? fs::current_path()
        : PathUtf8::FromString(request.engineRoot);
    const fs::path outputParent = PathUtf8::FromString(DefaultOutputParent(request.projectPath));
    const fs::path productRoot = outputParent / settings.productName;
    const fs::path mipsyncExport = productRoot / "mipsync_export";
    const fs::path projectCopy = mipsyncExport / "Project";
    const fs::path ps1Src = productRoot / "ps1_src";
    const fs::path outDir = productRoot / "out";

    std::string error;
    try {
        fs::create_directories(productRoot, ec);
        fs::create_directories(mipsyncExport, ec);
        fs::create_directories(projectCopy, ec);
        fs::create_directories(ps1Src, ec);

        // Clean previous build artifacts in out/. Without this, a stale
        // PSX.EXE/game.cue/game.bin left by an older engine release (or a
        // failed PSn00bSDK run) makes the SDK-success check below report a
        // false positive, so the prebuilt fallback never gets a chance to
        // refresh those files. Symptom: emulator boots the old PS-EXE
        // (PC0 mismatch) and stays on a black screen because the engine
        // updates to a new starter never reach the disc image.
        std::error_code rmEc;
        fs::remove(outDir / "PSX.EXE", rmEc);
        fs::remove(outDir / "game.cue", rmEc);
        fs::remove(outDir / "game.bin", rmEc);
        if (fs::is_directory(outDir, rmEc)) {
            for (fs::directory_iterator it(outDir, rmEc), end; it != end && !rmEc; it.increment(rmEc)) {
                if (!it->is_regular_file(rmEc))
                    continue;
                const std::string name = it->path().filename().string();
                if (name.rfind("game_", 0) == 0 && it->path().extension() == ".bin") {
                    std::error_code oldTrackEc;
                    fs::remove(it->path(), oldTrackEc);
                }
            }
        }
        fs::remove(outDir / "SYSTEM.CNF", rmEc);
        fs::create_directories(outDir, ec);

        CopyProjectTree(projectRoot, projectCopy, error);
        if (!error.empty()) {
            result.message = error;
            return result;
        }

        BuildManifest manifest = BuildManifestIO::FromPlayerSettings(settings);
        const fs::path bootPath = mipsyncExport / "boot.json";
        if (!BuildManifestIO::SaveToFile(PathUtf8::ToString(bootPath), manifest, error)) {
            result.message = error;
            return result;
        }

        json exportMeta;
        exportMeta["schemaVersion"] = 1;
        exportMeta["target"] = "ps1";
        exportMeta["productName"] = settings.productName;
        exportMeta["companyName"] = settings.companyName;
        exportMeta["startupScene"] = manifest.StartupScenePath();
        exportMeta["scenesInBuild"] = settings.scenesInBuild;
        exportMeta["note"] =
            "Editor content export for future MIPS/GPU pipeline. Runtime uses ps1_src starter until "
            "converter lands.";
        WriteText(mipsyncExport / "mipsync_export.json", exportMeta.dump(2), error);
        if (!error.empty()) {
            result.message = error;
            return result;
        }

        const fs::path templates = ResolveTemplatesRoot(PathUtf8::ToString(engineDir));
        if (templates.empty()) {
            result.success = true;
            result.outputDirectory = PathUtf8::ToString(productRoot);
            result.message =
                "Staged PS1 export at " + result.outputDirectory +
                "\n(templates/ps1 not found — add starter from repo to build PSX.EXE)";
            return result;
        }

        fs::create_directories(ps1Src / "generated", ec);
        CopyPs1StarterSources(templates / "starter", ps1Src, error);
        if (!error.empty()) {
            result.message = error;
            return result;
        }

        // Inject PS1 license data so the disc boots on PS2 (POPStarter/POPS).
        const bool hasLicense = InjectLicenseFile(
            projectRoot, engineDir, templates, ps1Src);

        // Export startup scene + scene-referenced Mipsync scripts into generated/
        // C tables consumed by the PS1 runtime (scene_data.c + scripts_data.c).
        Mips::Ps1SceneExportResult sceneExport{};
        std::vector<std::string> exportDiagnostics;
        std::string exportError;
        const bool sceneExported = Mips::ExportPs1SceneAndScripts(
            PathUtf8::ToString(projectRoot), manifest,
            PathUtf8::ToString(ps1Src / "generated"), sceneExport, exportError);
        if (!sceneExported) {
            result.message = "PS1 scene export failed: " + exportError;
            MIPSYNC_ERROR("{}", result.message);
            return result;
        } else {
            result.scriptCount = sceneExport.bindingCount;
            result.entityCount = sceneExport.entities.size();
        }
        if (!exportDiagnostics.empty()) {
            for (const auto& d : exportDiagnostics)
                MIPSYNC_WARN("Mipsync PS1 export: {}", d);
        }

        fs::copy_file(templates / "build_mipsync.ps1", productRoot / "build.ps1",
                      fs::copy_options::overwrite_existing, ec);

        const std::string sysCnf = "BOOT = cdrom:\\PSX.EXE;1\r\nTCB = 4\r\nEVENT = 10\r\nSTACK = "
                                   "801FFF00\r\n";
        WriteText(outDir / "SYSTEM.CNF", sysCnf, error);

#ifdef _WIN32
        const fs::path script = productRoot / "build.ps1";
        std::string log;
        bool sdkBuilt = false;
        const auto sdkRoot = ResolvePsn00bsdkRoot(engineDir);
        const std::string sdkRootStr =
            sdkRoot ? PathUtf8::ToString(*sdkRoot) : std::string{};

        if (fs::is_regular_file(script, ec)) {
            if (sdkRootStr.empty() && result.scriptCount > 0) {
                log = "PSn00bSDK not found. Set PSN00BSDK to an SDK root, then rebuild.";
            } else {
                const bool ran = RunPowerShellBuild(script, productRoot, engineDir, sdkRootStr, log);
                const fs::path psx = outDir / "PSX.EXE";
                const bool hasPsx = fs::is_regular_file(psx, ec);
                sdkBuilt = ran && hasPsx;
                if (hasPsx) {
                    result.psxExePath = PathUtf8::ToString(psx);
                    const fs::path cue = outDir / "game.cue";
                    if (fs::is_regular_file(cue, ec))
                        result.discCuePath = PathUtf8::ToString(cue);
                }
            }
        }

        if (result.psxExePath.empty()) {
            result.success = false;
            result.outputDirectory = PathUtf8::ToString(productRoot);
            result.message =
                "Scene exported (" + std::to_string(result.scriptCount) + " script" +
                (result.scriptCount == 1 ? "" : "s") + ", " +
                std::to_string(result.entityCount) + " entit" +
                (result.entityCount == 1 ? "y" : "ies") +
                ") but PSX.EXE was not compiled.\n" +
                (sdkRootStr.empty()
                     ? "Install PSn00bSDK, set PSN00BSDK to its root, then Build PS1 again."
                     : "PSn00bSDK build failed — open Builds/PS1/<Product>/build.log for details.") +
                (log.empty() ? "" : "\n" + SummarizeBuildLog(log));
            return result;
        }

        result.success = !result.psxExePath.empty();
        result.outputDirectory = PathUtf8::ToString(productRoot);
        const std::string scriptSummary =
            "  (" + std::to_string(result.scriptCount) + " script" +
            (result.scriptCount == 1 ? "" : "s") + ", " +
            std::to_string(result.entityCount) + " entit" +
            (result.entityCount == 1 ? "y" : "ies") + " from startup scene)";
        const std::string licenseNote = hasLicense
            ? ""
            : "\n⚠ No PS1 license file found — disc will NOT boot on PS2 (POPStarter)."
              "\n  Place licensea.dat in your project folder and rebuild.";
        if (sdkBuilt) {
            result.message = "PS1 build OK: " + result.psxExePath + scriptSummary + licenseNote;
            MIPSYNC_INFO("{}", result.message);
        } else if (result.success) {
            result.message = "PS1 build OK: " + result.psxExePath + scriptSummary + licenseNote;
            MIPSYNC_INFO("{}", result.message);
        } else {
            result.message =
                "Staged PS1 project at " + result.outputDirectory +
                "\nSDK build did not produce PSX.EXE.\n" +
                (log.empty() ? "" : log.substr(0, std::min<size_t>(log.size(), 800)));
        }
        return result;
#else
        result.success = true;
        result.outputDirectory = PathUtf8::ToString(productRoot);
        result.message = "Staged PS1 project at " + result.outputDirectory;
#endif
    } catch (const std::exception& ex) {
        result.message = ex.what();
    }
    return result;
}

Ps1BuildResult ExportOnly(const Ps1BuildRequest& request) {
    Ps1BuildResult result;
    PlayerSettings settings = request.settings;
    settings.productName = SanitizeProductName(settings.productName);

    if (settings.scenesInBuild.empty()) {
        result.message = "No scenes in build list.";
        return result;
    }

    const fs::path projectRoot = PathUtf8::FromString(request.projectPath);
    std::error_code ec;
    if (!fs::is_directory(projectRoot, ec)) {
        result.message = "Invalid project path.";
        return result;
    }

    const fs::path engineDir = request.engineRoot.empty()
        ? fs::current_path()
        : PathUtf8::FromString(request.engineRoot);
    const fs::path outputParent = PathUtf8::FromString(DefaultOutputParent(request.projectPath));
    const fs::path productRoot = outputParent / settings.productName;
    const fs::path ps1Src = productRoot / "ps1_src";

    std::string error;
    try {
        fs::create_directories(ps1Src, ec);
        fs::create_directories(ps1Src / "generated", ec);

        const fs::path templates = ResolveTemplatesRoot(PathUtf8::ToString(engineDir));
        if (templates.empty()) {
            result.message = "templates/ps1 not found next to the engine.";
            return result;
        }

        CopyPs1StarterSources(templates / "starter", ps1Src, error);
        if (!error.empty()) {
            result.message = error;
            return result;
        }

        BuildManifest manifest = BuildManifestIO::FromPlayerSettings(settings);
        Mips::Ps1SceneExportResult sceneExport{};
        std::string exportError;
        if (!Mips::ExportPs1SceneAndScripts(PathUtf8::ToString(projectRoot), manifest,
                                            PathUtf8::ToString(ps1Src / "generated"), sceneExport,
                                            exportError)) {
            result.message = "PS1 export failed: " + exportError;
            return result;
        }

        result.success = true;
        result.outputDirectory = PathUtf8::ToString(productRoot);
        result.entityCount = sceneExport.entities.size();
        result.scriptCount = sceneExport.bindingCount;
        result.message = "PS1 export OK: " + result.outputDirectory + " (" +
                         std::to_string(sceneExport.texturePaths.size()) + " texture" +
                         (sceneExport.texturePaths.size() == 1 ? "" : "s") + ", " +
                         std::to_string(result.entityCount) + " entities)";
        MIPSYNC_INFO("{}", result.message);
    } catch (const std::exception& ex) {
        result.message = ex.what();
    }
    return result;
}

Ps1BuildResult BuildDiscFolder(const Ps1BuildRequest& request) {
    // 1. Run the normal build first to ensure we have the compiled PSX.EXE.
    Ps1BuildResult result = Build(request);
    if (!result.success) {
        return result;
    }

    const fs::path productRoot = PathUtf8::FromString(result.outputDirectory);
    const fs::path outDir = productRoot / "out";
    const fs::path discDir = productRoot / "disc";
    const fs::path ps1Src = productRoot / "ps1_src";

    std::error_code ec;
    // Clean and recreate the disc directory to make sure it's clean.
    fs::remove_all(discDir, ec);
    if (!fs::create_directories(discDir, ec)) {
        result.success = false;
        result.message = "Failed to create disc directory: " + ec.message();
        return result;
    }

    // Copy PSX.EXE
    const fs::path srcExe = outDir / "PSX.EXE";
    const fs::path destExe = discDir / "PSX.EXE";
    if (!fs::is_regular_file(srcExe, ec)) {
        result.success = false;
        result.message = "Compiled PSX.EXE not found in out directory.";
        return result;
    }
    fs::copy_file(srcExe, destExe, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        result.success = false;
        result.message = "Failed to copy PSX.EXE to disc directory: " + ec.message();
        return result;
    }

    // Generate SYSTEM.CNF
    const std::string sysCnf = "BOOT = cdrom:\\PSX.EXE;1\r\nTCB = 4\r\nEVENT = 10\r\nSTACK = 801FFF00\r\n";
    std::string error;
    if (!WriteText(discDir / "SYSTEM.CNF", sysCnf, error)) {
        result.success = false;
        result.message = "Failed to write SYSTEM.CNF to disc directory: " + error;
        return result;
    }

    // Copy license file if one was injected/found in ps1_src.
    const char* licenseNames[] = {"licensea.dat", "licensee.dat", "licensej.dat"};
    bool hasLicense = false;
    for (const char* name : licenseNames) {
        if (fs::is_regular_file(ps1Src / name, ec)) {
            fs::copy_file(ps1Src / name, discDir / name, fs::copy_options::overwrite_existing, ec);
            if (!ec) {
                hasLicense = true;
                break;
            }
        }
    }

    // Write IMGBURN_README.txt
    const std::string readmeContent = 
        "=== How to Burn PS1 Game for MechaPwn fore-unlocked PS2 ===\n\n"
        "This folder contains the filesystem structure required to burn your PS1 game.\n"
        "To burn it correctly using ImgBurn, follow these steps:\n\n"
        "1. Open ImgBurn.\n"
        "2. Select \"Write files/folders to disc\" (Build Mode).\n"
        "3. Add all files inside this 'disc' folder (SYSTEM.CNF, PSX.EXE, license file if any) to the Source list.\n"
        "   Do NOT add the 'disc' folder itself; add its contents to the root of the disc.\n"
        "4. Go to the \"Options\" tab on the right side:\n"
        "   - Data Type: MODE2/XA\n"
        "   - File System: ISO9660\n"
        "5. Go to the \"Labels\" tab and specify a Volume Label (e.g. \"MIPSYNC_GAME\").\n"
        "6. Insert a blank CD-R (PS1 hardware does not support DVD-R for PS1 games).\n"
        "7. Click the Build/Burn button.\n"
        "8. If ImgBurn prompts you to auto-adjust settings or add folders, confirm.\n\n"
        "Note on License/Region:\n"
        "MechaPwn fore-unlocked consoles bypass region checking, but burning to CD-R requires\n"
        "high quality media and a good burner. If you experience issues, try burning at a lower speed.\n";

    if (!WriteText(discDir / "IMGBURN_README.txt", readmeContent, error)) {
        result.success = false;
        result.message = "Failed to write IMGBURN_README.txt to disc directory: " + error;
        return result;
    }

    result.discFolderPath = PathUtf8::ToString(discDir);
    result.message = "PS2 Disc Folder generated successfully at:\n" + result.discFolderPath +
                     (hasLicense ? "\n(License file included)" : "\n(Warning: No license file included)");
    return result;
}

} // namespace MipsyncEngine::Ps1
