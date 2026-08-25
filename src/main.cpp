#include "core/BootSplash.h"
#include "core/Engine.h"
#include "core/Log.h"
#include "project/Project.h"
#include "ps1/Ps1Build.h"
#include "assets/AssetManager.h"
#include "mips/MipsRuntime.h"
#include "mips/MipsTest.h"
#include <filesystem>
#include <iostream>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

struct CliArgs {
    std::string projectPath;
    std::string playerDataDir;
    bool skipHub = false;
    bool noSplash = false;
    bool playerMode = false;
    bool exportPs1Only = false;
    bool buildDiscOnly = false;
    std::string validateMipsPath;
    std::string testMipsRuntimePath;
};

CliArgs ParseArgs(int argc, char** argv) {
    CliArgs args;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if ((a == "--project" || a == "-p") && i + 1 < argc) {
            args.projectPath = argv[++i];
            args.skipHub = true;
        } else if (a == "--no-splash") {
            args.noSplash = true;
        } else if (a == "--export-ps1") {
            args.exportPs1Only = true;
            args.skipHub = true;
            if (i + 1 < argc && argv[i + 1][0] != '-')
                args.projectPath = argv[++i];
        } else if (a == "--build-disc") {
            args.buildDiscOnly = true;
            args.skipHub = true;
            if (i + 1 < argc && argv[i + 1][0] != '-')
                args.projectPath = argv[++i];
        } else if (a == "--validate-mips" && i + 1 < argc) {
            args.validateMipsPath = argv[++i];
            args.skipHub = true;
        } else if (a == "--test-mips-runtime" && i + 1 < argc) {
            args.testMipsRuntimePath = argv[++i];
            args.skipHub = true;
        } else if (a == "--play" || a == "--player" || a == "--data-dir" || a == "--data") {
            args.skipHub = true;
            args.playerMode = true;
            if ((a == "--data-dir" || a == "--data") && i + 1 < argc)
                args.playerDataDir = argv[++i];
            else if (i + 1 < argc && argv[i + 1][0] != '-')
                args.playerDataDir = argv[++i];
        } else if (!args.projectPath.empty()) {
            // ignore extras
        } else if (a.rfind("-", 0) != 0) {
            args.projectPath = a;
            args.skipHub = true;
        }
    }
    return args;
}

std::filesystem::path ModulePathFromArgv0(const char* argv0) {
#ifdef _WIN32
    wchar_t modulePath[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) != 0)
        return std::filesystem::path(modulePath);
#endif
    if (argv0 && *argv0)
        return std::filesystem::absolute(std::filesystem::path(argv0));
    return {};
}

std::string DetectAdjacentPlayerDataDirectory(const char* argv0) {
    const std::filesystem::path modulePath = ModulePathFromArgv0(argv0);
    if (modulePath.empty())
        return {};

    const std::filesystem::path dir = modulePath.parent_path();
    const std::filesystem::path stemData = dir / (modulePath.stem().wstring() + L"_Data");
    std::error_code ec;
    if (std::filesystem::is_regular_file(stemData / "boot.json", ec))
        return stemData.string();

    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec)
            break;
        if (!entry.is_directory(ec))
            continue;
        const std::filesystem::path candidate = entry.path();
        const std::wstring name = candidate.filename().wstring();
        if (name.size() < 5 || name.substr(name.size() - 5) != L"_Data")
            continue;
        if (std::filesystem::is_regular_file(candidate / "boot.json", ec))
            return candidate.string();
    }
    return {};
}

bool TryLaunchMipsyncHub() {
#ifdef _WIN32
    wchar_t modulePath[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) == 0)
        return false;

    const std::filesystem::path hubExe =
        std::filesystem::path(modulePath).parent_path() / "MipsyncHub.exe";

    if (!std::filesystem::exists(hubExe)) {
        const std::filesystem::path legacy =
            std::filesystem::path(modulePath).parent_path() / "mipsync-hub.exe";
        if (!std::filesystem::exists(legacy))
            return false;
        const HINSTANCE legacyResult = ShellExecuteW(
            nullptr, L"open", legacy.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<intptr_t>(legacyResult) > 32;
    }

    const HINSTANCE result = ShellExecuteW(
        nullptr, L"open", hubExe.c_str(), nullptr, nullptr, SW_SHOWNORMAL);

    return reinterpret_cast<intptr_t>(result) > 32;
#else
    (void)0;
    return false;
#endif
}

} // namespace

int main(int argc, char** argv) {
    try {
        CliArgs args = ParseArgs(argc, argv);

        MipsyncEngine::Log::Init();
        if (!args.playerMode && args.projectPath.empty()) {
            args.playerDataDir = DetectAdjacentPlayerDataDirectory(argc > 0 ? argv[0] : nullptr);
            if (!args.playerDataDir.empty()) {
                args.playerMode = true;
                args.skipHub = true;
                args.noSplash = true;
            }
        }

        if (!args.skipHub) {
            if (TryLaunchMipsyncHub())
                return 0;
            std::cerr << "MipsyncHub.exe not found next to the editor." << std::endl;
            return 1;
        }

        if (!args.validateMipsPath.empty()) {
            std::vector<std::string> errors;
            const std::string validationPath = MipsyncEngine::PathUtf8::ToString(
                std::filesystem::absolute(
                    MipsyncEngine::PathUtf8::FromString(args.validateMipsPath)));
            const auto module = MipsyncEngine::Mips::MipsRuntime::CompileScriptFile(
                validationPath, errors);
            for (const auto& error : errors)
                std::cerr << error << std::endl;
            if (!module || !errors.empty())
                return 1;
            std::cout << "Mips# validation OK: " << args.validateMipsPath << std::endl;
            return 0;
        }

        if (!args.testMipsRuntimePath.empty()) {
            std::vector<std::string> errors;
            const bool ok = MipsyncEngine::Mips::RunMipsRuntimeRegressionTests(
                args.testMipsRuntimePath.c_str(), errors);
            for (const auto& error : errors)
                std::cerr << error << std::endl;
            if (!ok)
                return 1;
            std::cout << "Mips# runtime tests OK: " << args.testMipsRuntimePath << std::endl;
            return 0;
        }

        if (args.exportPs1Only) {
            if (args.projectPath.empty()) {
                std::cerr << "Usage: MipsyncEngine --export-ps1 <projectDir>" << std::endl;
                return 1;
            }
            MipsyncEngine::ProjectInfo info;
            std::string err;
            if (!MipsyncEngine::Project::LoadFromDir(args.projectPath, info, err)) {
                std::cerr << "Project load failed: " << err << std::endl;
                return 1;
            }
            std::string engineRoot = std::filesystem::current_path().string();
#ifdef _WIN32
            wchar_t modulePath[MAX_PATH]{};
            if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) != 0)
                engineRoot = std::filesystem::path(modulePath).parent_path().string();
#endif
            MipsyncEngine::Ps1::Ps1BuildRequest req;
            req.projectPath = args.projectPath;
            req.settings = info.player;
            req.engineRoot = engineRoot;
            const auto result = MipsyncEngine::Ps1::ExportOnly(req);
            std::cout << result.message << std::endl;
            return result.success ? 0 : 1;
        }

        if (args.buildDiscOnly) {
            if (args.projectPath.empty()) {
                std::cerr << "Usage: MipsyncEngine --build-disc <projectDir>" << std::endl;
                return 1;
            }
            MipsyncEngine::ProjectInfo info;
            std::string err;
            if (!MipsyncEngine::Project::LoadFromDir(args.projectPath, info, err)) {
                std::cerr << "Project load failed: " << err << std::endl;
                return 1;
            }
            std::string engineRoot = std::filesystem::current_path().string();
#ifdef _WIN32
            wchar_t modulePath[MAX_PATH]{};
            if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) != 0)
                engineRoot = std::filesystem::path(modulePath).parent_path().string();
#endif
            MipsyncEngine::Ps1::Ps1BuildRequest req;
            req.projectPath = args.projectPath;
            req.settings = info.player;
            req.engineRoot = engineRoot;
            const auto result = MipsyncEngine::Ps1::BuildDiscFolder(req);
            std::cout << result.message << std::endl;
            return result.success ? 0 : 1;
        }

        if (!args.noSplash)
            MipsyncEngine::BootSplash::Play();

        const auto launchMode = args.playerMode
            ? MipsyncEngine::EngineLaunchMode::Player
            : MipsyncEngine::EngineLaunchMode::Editor;
        MipsyncEngine::Engine engine(args.projectPath, launchMode, args.playerDataDir);
        engine.Run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal Exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
