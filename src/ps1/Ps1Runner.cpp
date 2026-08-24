#include "Ps1Runner.h"
#include "Ps1EditorPrefs.h"
#include "../assets/AssetManager.h"
#include "../core/Log.h"
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace MipsyncEngine::Ps1 {
namespace fs = std::filesystem;

namespace {

enum class EmulatorFamily {
    PcsxRedux,    // -iso / -loadexe / -run / -portable / -bios (bundled OpenBIOS)
    DuckStation,  // -cdimage / -exec / -bios / -fullscreen
};

EmulatorFamily DetectFamily(const fs::path& emuPath) {
    auto stem = emuPath.stem().string();
    for (auto& c : stem)
        c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    if (stem.find("pcsx") != std::string::npos)
        return EmulatorFamily::PcsxRedux;
    return EmulatorFamily::DuckStation;
}

std::string ResolveBiosOverride(const LaunchRequest& request, std::error_code& ec) {
    if (!request.prefs.biosPath.empty() &&
        fs::is_regular_file(PathUtf8::FromString(request.prefs.biosPath), ec)) {
        return request.prefs.biosPath;
    }
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 2] = {};
    const DWORD n = GetEnvironmentVariableW(L"MIPSYNC_OPENBIOS_PATH", buf,
                                            static_cast<DWORD>(std::size(buf)));
    if (n > 0 && n < std::size(buf)) {
        const int needed = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
        if (needed > 0) {
            std::string out(static_cast<size_t>(needed - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, buf, -1, out.data(), needed, nullptr, nullptr);
            if (fs::is_regular_file(PathUtf8::FromString(out), ec))
                return out;
        }
    }
#else
    if (const char* env = std::getenv("MIPSYNC_OPENBIOS_PATH")) {
        std::string out(env);
        if (!out.empty() && fs::is_regular_file(PathUtf8::FromString(out), ec))
            return out;
    }
#endif
    return {};
}

} // namespace

LaunchResult LaunchInEmulator(const LaunchRequest& request) {
    LaunchResult result;

    const std::string emulator = ResolveEmulatorPath(request.prefs);
    if (emulator.empty()) {
        result.message =
            "PS1 emulator not found. Set MIPSYNC_PS1_EMULATOR or configure "
            "Build Settings → PS1 Emulator manually.";
        return result;
    }

    std::error_code ec;
    const fs::path emuPath = PathUtf8::FromString(emulator);
    if (!fs::is_regular_file(emuPath, ec)) {
        result.message = "Emulator not found: " + emulator;
        return result;
    }

    const fs::path cue =
        request.discCuePath.empty() ? fs::path{} : PathUtf8::FromString(request.discCuePath);
    const fs::path exe =
        request.psxExePath.empty() ? fs::path{} : PathUtf8::FromString(request.psxExePath);

    const bool hasCue = !cue.empty() && fs::is_regular_file(cue, ec);
    const bool hasExe = !exe.empty() && fs::is_regular_file(exe, ec);
    if (!hasCue && !hasExe) {
        result.message = "No PSX.EXE or .cue to run. Build PS1 target first.";
        return result;
    }

    const EmulatorFamily family = DetectFamily(emuPath);

    // BIOS resolution order:
    //   1. editor override (request.prefs.biosPath, set in Build Settings),
    //   2. MIPSYNC_OPENBIOS_PATH environment variable.
    std::string biosArg = ResolveBiosOverride(request, ec);

    std::ostringstream args;
    if (family == EmulatorFamily::PcsxRedux) {
        const auto hasToken = [](const std::string& haystack, const char* needle) -> bool {
            return haystack.find(needle) != std::string::npos;
        };
        const bool userForcesDynarec = hasToken(request.prefs.extraArgs, "-dynarec");
        const bool userForcesInterpreter = hasToken(request.prefs.extraArgs, "-interpreter");
        const bool userWantsStdout = hasToken(request.prefs.extraArgs, "-stdout");

        // -portable keeps per-user state next to the bundled exe.
        args << "-portable -run ";
        // Don't default to dumping emulator logs into the editor stdout.
        // Some BIOS/runtime probes can spam "unknown address" reads/writes forever.
        if (userWantsStdout) {
            args << "-stdout ";
        }
        // Default to interpreter unless the user explicitly requests dynarec.
        // This avoids dynarec-related crashes on some bundled builds.
        if (!userForcesDynarec && !userForcesInterpreter) {
            args << "-interpreter ";
        }
        if (userForcesDynarec) {
            MIPSYNC_WARN("PS1 emulator extra args include -dynarec; this may crash with OpenBIOS. Consider removing it.");
        }
        if (!biosArg.empty()) {
            args << "-bios \"" << biosArg << "\" ";
        }
        if (hasCue) {
            args << "-iso \"" << PathUtf8::ToString(cue) << "\"";
        } else {
            args << "-loadexe \"" << PathUtf8::ToString(exe) << "\"";
        }
    } else {
        args << "-fullscreen ";
        if (!biosArg.empty()) {
            args << "-bios \"" << biosArg << "\" ";
        }
        if (hasCue) {
            args << "-cdimage \"" << PathUtf8::ToString(cue) << "\"";
        } else {
            args << "-exec \"" << PathUtf8::ToString(exe) << "\"";
        }
    }
    if (!request.prefs.extraArgs.empty())
        args << " " << request.prefs.extraArgs;

#ifdef _WIN32
    auto utf8ToWide = [](const std::string& s) {
        if (s.empty())
            return std::wstring{};
        const int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        std::wstring out(static_cast<size_t>(len), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), len);
        if (!out.empty() && out.back() == L'\0')
            out.pop_back();
        return out;
    };

    std::wstring cmdLine = L"\"" + emuPath.wstring() + L"\" " + utf8ToWide(args.str());

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    std::vector<wchar_t> mutableCmd(cmdLine.begin(), cmdLine.end());
    mutableCmd.push_back(L'\0');

    // PCSX-Redux uses -portable, which stores per-user settings/saves next to its exe.
    // Pinning the working directory there keeps state out of the user's project folder.
    const fs::path workDir =
        family == EmulatorFamily::PcsxRedux
            ? emuPath.parent_path()
            : (hasExe ? exe.parent_path() : emuPath.parent_path());
    if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, 0, nullptr,
                      workDir.wstring().c_str(), &si, &pi)) {
        result.message = "CreateProcess failed: " + std::to_string(GetLastError());
        return result;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    result.success = true;
    result.message = "Launched emulator: " + PathUtf8::ToString(emuPath.filename());
    MIPSYNC_INFO("{}", result.message);
    MIPSYNC_INFO("PS1 emulator cmd: \"{}\" {}", PathUtf8::ToString(emuPath), args.str());
    return result;
#else
    (void)args;
    result.message = "PS1 emulator launch is only implemented on Windows.";
    return result;
#endif
}

} // namespace MipsyncEngine::Ps1
