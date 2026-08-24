#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace MipsyncEngine::Ps1 {

/// Resolve PSn00bSDK root: PSN00BSDK env → Hub settings → toolchain scan → dev cache.
std::optional<std::filesystem::path> ResolvePsn00bsdkRoot(
    const std::filesystem::path& engineDir);

} // namespace MipsyncEngine::Ps1
