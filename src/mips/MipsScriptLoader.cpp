#include "MipsScriptLoader.h"
#include "Compiler.h"
#include "Lexer.h"
#include "Parser.h"
#include "../assets/AssetManager.h"
#include <fstream>
#include <sstream>

namespace MipsyncEngine::Mips {
namespace fs = std::filesystem;

namespace {

bool IsWithinRoot(const fs::path& candidate, const fs::path& root) {
    const fs::path relative = candidate.lexically_relative(root);
    if (relative.empty())
        return candidate == root;
    const auto first = relative.begin();
    return first != relative.end() && *first != "..";
}

fs::path NormalizeExisting(const fs::path& path) {
    std::error_code ec;
    fs::path normalized = fs::weakly_canonical(path, ec);
    if (ec)
        normalized = fs::absolute(path, ec).lexically_normal();
    return normalized;
}

} // namespace

fs::path MipsScriptLoader::ResolvePath(const fs::path& projectRoot,
                                       const std::string& scriptPath,
                                       std::string& outError) {
    outError.clear();
    if (scriptPath.empty()) {
        outError = "script path is empty";
        return {};
    }

    const fs::path requested = PathUtf8::FromString(scriptPath);
    std::error_code ec;
    if (requested.is_absolute()) {
        const fs::path resolved = NormalizeExisting(requested);
        if (!projectRoot.empty()) {
            const fs::path root = NormalizeExisting(projectRoot);
            if (!IsWithinRoot(resolved, root)) {
                outError = "absolute script path is outside the project root: " + scriptPath;
                return {};
            }
        }
        if (fs::is_regular_file(resolved, ec))
            return resolved;
        outError = "script file does not exist: " + PathUtf8::ToString(resolved);
        return {};
    }

    if (projectRoot.empty()) {
        outError = "cannot resolve relative script path without a project root: " + scriptPath;
        return {};
    }

    const fs::path root = NormalizeExisting(projectRoot);
    const fs::path direct = NormalizeExisting(root / requested);
    if (!IsWithinRoot(direct, root)) {
        outError = "script path escapes the project root: " + scriptPath;
        return {};
    }
    if (fs::is_regular_file(direct, ec))
        return direct;

    // Compatibility for legacy components that stored only "Player.mips".
    const fs::path scriptsCandidate = NormalizeExisting(
        root / "assets" / "scripts" / requested.filename());
    if (IsWithinRoot(scriptsCandidate, root) && fs::is_regular_file(scriptsCandidate, ec))
        return scriptsCandidate;

    outError = "script file not found under project root: " + scriptPath;
    return {};
}

std::shared_ptr<CompiledModule> MipsScriptLoader::CompileFile(
    const fs::path& projectRoot, const std::string& scriptPath,
    std::vector<std::string>& errors) {
    std::string resolveError;
    const fs::path resolved = ResolvePath(projectRoot, scriptPath, resolveError);
    if (resolved.empty()) {
        errors.push_back(resolveError);
        return nullptr;
    }

    std::ifstream file(resolved, std::ios::binary);
    if (!file.is_open()) {
        errors.push_back("failed to open script: " + PathUtf8::ToString(resolved));
        return nullptr;
    }
    std::ostringstream source;
    source << file.rdbuf();
    const std::string fileName = PathUtf8::ToString(resolved);

    Lexer lexer(source.str(), fileName);
    const std::vector<Token>& tokens = lexer.Tokenize();
    errors.insert(errors.end(), lexer.GetErrors().begin(), lexer.GetErrors().end());
    if (!lexer.GetErrors().empty())
        return nullptr;

    Parser parser(tokens, fileName);
    auto program = parser.ParseProgram();
    errors.insert(errors.end(), parser.GetErrors().begin(), parser.GetErrors().end());
    if (!program || program->classes.empty() || !parser.GetErrors().empty())
        return nullptr;

    return CompileClass(*program->classes[0], program.get(), fileName, errors);
}

} // namespace MipsyncEngine::Mips
