#pragma once

#include "Bytecode.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace MipsyncEngine::Mips {

/// Project-rooted script loading. Relative paths are never resolved through
/// the process working directory or parent-directory probing.
class MipsScriptLoader {
public:
    static std::filesystem::path ResolvePath(const std::filesystem::path& projectRoot,
                                             const std::string& scriptPath,
                                             std::string& outError);
    static std::shared_ptr<CompiledModule> CompileFile(const std::filesystem::path& projectRoot,
                                                       const std::string& scriptPath,
                                                       std::vector<std::string>& errors);
};

} // namespace MipsyncEngine::Mips
