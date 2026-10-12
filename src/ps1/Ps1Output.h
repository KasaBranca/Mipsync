#pragma once
#include "../core/NativePaths.h"
#include <chrono>

namespace MipsyncEngine::Ps1 {
// Keep a mounted disc intact. A read-only emulator handle commonly disallows
// deleting or overwriting game.bin even after compilation has succeeded.
inline std::filesystem::path SelectArtifactDirectory(const std::filesystem::path& productRoot) {
    namespace fs = std::filesystem;
    const auto output = productRoot / "out";
#ifdef _WIN32
    std::error_code ec;
    bool mounted = false;
    if (fs::is_directory(NativePaths::FilePath(output), ec)) {
        NativePaths::VisitDirectory(output, [&](const WIN32_FIND_DATAW& entry) {
            if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return;
            const auto file = output / entry.cFileName;
            HANDLE probe = CreateFileW(NativePaths::FilePath(file).c_str(), GENERIC_WRITE | DELETE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (probe != INVALID_HANDLE_VALUE) CloseHandle(probe);
            else if (GetLastError() == ERROR_SHARING_VIOLATION) mounted = true;
        });
    }
    if (mounted) {
        const auto unique = std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
        auto alternate = productRoot / ("out-" + unique);
        unsigned suffix = 0;
        while (fs::exists(NativePaths::FilePath(alternate)))
            alternate = productRoot / ("out-" + unique + "-" + std::to_string(++suffix));
        return alternate;
    }
#endif
    return output;
}
} // namespace MipsyncEngine::Ps1
