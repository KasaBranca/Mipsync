#pragma once

#include <memory>
#include <string>
#include <vector>

namespace MipsyncEngine {

namespace Mips {
struct CompiledModule;
}

struct MipsEditorDiagnostic {
    std::string message;
    int line = 0;
    int column = 0;
    bool hasLocation = false;
};

struct MipsEditorValidationResult {
    bool success = false;
    std::shared_ptr<Mips::CompiledModule> module;
    std::vector<MipsEditorDiagnostic> diagnostics;
};

class MipsEditorIntegration {
public:
    static MipsEditorValidationResult ValidateScript(const std::string& projectOrAbsolutePath);
    static void LogValidationResult(const std::string& projectOrAbsolutePath,
                                    const MipsEditorValidationResult& result);
    static bool OpenScriptInIde(const std::string& projectOrAbsolutePath, int line = 0,
                                int column = 0);
    static std::string BuildVsCodeExtensionPathHint();

private:
    static std::string ResolveScriptPath(const std::string& projectOrAbsolutePath);
};

} // namespace MipsyncEngine
