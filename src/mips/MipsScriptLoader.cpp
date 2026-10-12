#include "MipsScriptLoader.h"
#include "Compiler.h"
#include "Lexer.h"
#include "Parser.h"
#include "../core/AssetFiles.h"
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
        if (AssetFiles::Exists(resolved))
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
    if (AssetFiles::Exists(direct))
        return direct;

    // Compatibility for legacy components that stored only "Player.mips".
    const fs::path scriptsCandidate = NormalizeExisting(
        root / "assets" / "scripts" / requested.filename());
    if (IsWithinRoot(scriptsCandidate, root) && AssetFiles::Exists(scriptsCandidate))
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

    if(AssetFiles::IsPackedPath(resolved)) {
        try{return DeserializePlayerModule(AssetFiles::Read(resolved));}
        catch(const std::exception& e){errors.push_back(e.what());return nullptr;}
    }
    std::string source;
    try { source = AssetFiles::ReadText(resolved); }
    catch (const std::exception& ex) { errors.push_back(ex.what()); return nullptr; }
    const std::string fileName = PathUtf8::ToString(resolved);

    Lexer lexer(source, fileName);
    const std::vector<Token>& tokens = lexer.Tokenize();
    errors.insert(errors.end(), lexer.GetErrors().begin(), lexer.GetErrors().end());
    if (!lexer.GetErrors().empty())
        return nullptr;

    Parser parser(tokens, fileName);
    auto program = parser.ParseProgram();
    errors.insert(errors.end(), parser.GetErrors().begin(), parser.GetErrors().end());
    if (!program || program->classes.empty() || !parser.GetErrors().empty())
        return nullptr;

    // The compiler may produce partial bytecode together with diagnostics.
    // Never expose that module as runnable/exportable source.
    std::vector<std::string> compileErrors;
    auto module = CompileClass(*program->classes[0], program.get(), fileName, compileErrors);
    errors.insert(errors.end(), compileErrors.begin(), compileErrors.end());
    if (!compileErrors.empty()) return nullptr;
    return module;
}

} // namespace MipsyncEngine::Mips
