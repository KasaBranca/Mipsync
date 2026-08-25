#include "command/CommandExecutor.h"
#include "command/CommandLine.h"
#include "command/CoreCommands.h"
#include "command/SymbolRegistry.h"
#include "EngineVersion.h"
#include "mips/Compiler.h"
#include "mips/Lexer.h"
#include "mips/Parser.h"
#include "mips/MipsScriptLoader.h"

#include <iostream>
#include <filesystem>
#include <fstream>

using namespace MipsyncEngine::Command;

namespace {

bool Check(bool condition, const char* message) {
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

} // namespace

int main() {
    CommandRegistry commands;
    SymbolRegistry symbols;
    RegisterCoreCommands(commands, symbols);
    CommandContext context;
    context.engineVersion = MIPSYNC_ENGINE_VERSION;
    context.commands = &commands;
    context.symbols = &symbols;
    CommandExecutor executor(commands);

    bool ok = true;
    ok &= Check(commands.Find("entity.inspect") != nullptr, "editor command is discoverable");
    ok &= Check(commands.Find("entity.create") != nullptr, "entity creation command is discoverable");
    ok &= Check(commands.Find("entity.select") != nullptr, "entity selection command is discoverable");
    ok &= Check(commands.Find("entity.transform") != nullptr, "entity transform command is discoverable");
    ok &= Check(commands.Find("entity.delete") != nullptr, "entity deletion command is discoverable");
    ok &= Check(commands.Find("entity.duplicate") != nullptr, "entity duplication command is discoverable");
    ok &= Check(commands.Find("component.add") != nullptr, "component authoring command is discoverable");
    ok &= Check(commands.Find("material.create") != nullptr, "material creation command is discoverable");
    ok &= Check(commands.Find("material.apply") != nullptr, "material assignment command is discoverable");
    ok &= Check(commands.Find("scene.get-json") != nullptr, "scene serialization command is discoverable");
    ok &= Check(commands.Find("scene.patch") != nullptr, "generic scene patch command is discoverable");
    ok &= Check(commands.Find("ide.open") != nullptr, "IDE open command is discoverable");
    ok &= Check(symbols.Find("Transform.position") != nullptr, "Mips# symbol is discoverable");

    CommandRequest search;
    search.command = "search";
    search.requestId = "test-search";
    search.arguments = {{"query", "move every frame"}, {"limit", 10}};
    const auto searchResult = executor.Execute(search, context);
    ok &= Check(searchResult.success, "search executes");
    ok &= Check(searchResult.value["matches"].is_array(), "search returns typed matches");

    CommandRequest invalid;
    invalid.command = "entity.inspect";
    invalid.arguments = {{"entity", "Player"}};
    const auto invalidResult = executor.Execute(invalid, context);
    ok &= Check(!invalidResult.success, "editor-only command fails without editor");
    ok &= Check(!invalidResult.diagnostics.empty() &&
                invalidResult.diagnostics.front().code == "MIPSYNC_EDITOR_REQUIRED",
                "editor-only failure is structured");

    const auto parsed = ParseCommandLine({"entity", "inspect", "Player", "--json"}, commands);
    ok &= Check(parsed.valid && parsed.request.command == "entity.inspect", "hierarchical CLI command parses");
    ok &= Check(parsed.request.arguments.value("entity", "") == "Player", "positional argument maps from metadata");
    ok &= Check(parsed.json, "global JSON option parses");

    const auto createParsed = ParseCommandLine(
        {"entity", "create", "CLI Cube", "--primitive", "cube", "--x", "2", "--y", "1.5"},
        commands);
    ok &= Check(createParsed.valid && createParsed.request.command == "entity.create",
                "entity create command parses from descriptor metadata");
    ok &= Check(createParsed.request.arguments.value("name", "") == "CLI Cube" &&
                createParsed.request.arguments.value("primitive", "") == "cube" &&
                createParsed.request.arguments.value("x", 0.0) == 2.0,
                "entity create typed arguments are populated");

    const auto selectParsed = ParseCommandLine({"entity", "select", "CLI Cube"}, commands);
    ok &= Check(selectParsed.valid && selectParsed.request.command == "entity.select" &&
                selectParsed.request.arguments.value("entity", "") == "CLI Cube",
                "entity select command parses from descriptor metadata");

    const auto materialParsed = ParseCommandLine(
        {"material", "create", "assets/materials/Brick.nmat", "0.7", "0.2", "0.1"}, commands);
    ok &= Check(materialParsed.valid && materialParsed.request.command == "material.create" &&
                materialParsed.request.arguments.value("r", 0.0) == 0.7 &&
                materialParsed.request.arguments.value("a", 0.0) == 1.0,
                "material create parses typed color channels and default alpha");

    const auto patchParsed = ParseCommandLine(
        {"scene", "patch", "[{\"op\":\"replace\",\"path\":\"/version\",\"value\":1}]", "--confirm", "true"}, commands);
    ok &= Check(patchParsed.valid && patchParsed.request.command == "scene.patch" &&
                patchParsed.request.arguments.at("patch").is_array() &&
                patchParsed.request.arguments.value("confirm", false),
                "scene patch parses structured JSON and confirmation");

    const auto deleteParsed = ParseCommandLine(
        {"entity", "delete", "CLI Cube", "--confirm", "true"}, commands);
    ok &= Check(deleteParsed.valid && deleteParsed.request.arguments.value("confirm", false),
                "dangerous deletion requires a typed confirmation argument");

    CommandRequest describe;
    describe.command = "describe";
    describe.arguments = {{"symbol", "Transform.position"}};
    const auto describeResult = executor.Execute(describe, context);
    ok &= Check(describeResult.success, "describe executes");
    ok &= Check(describeResult.value["descriptor"]["id"] == "Transform.position", "describe returns exact symbol");

    CommandRequest rootDescribe;
    rootDescribe.command = "describe";
    const auto rootDescribeResult = executor.Execute(rootDescribe, context);
    bool foundEntityNamespace = false;
    if (rootDescribeResult.success) {
        for (const auto& child : rootDescribeResult.value["children"])
            foundEntityNamespace |= child.value("id", "") == "entity" && child.value("kind", "") == "namespace";
    }
    ok &= Check(foundEntityNamespace, "root discovery collapses commands into namespaces");

    CommandRequest compile;
    compile.command = "language.compile";
    compile.arguments = {{"file", (std::filesystem::path(__FILE__).parent_path() /
                                    "mips" / "InvalidSyntax.mips").string()}};
    const auto compileResult = executor.Execute(compile, context);
    ok &= Check(!compileResult.success && !compileResult.diagnostics.empty(),
                "compiler errors are returned as structured diagnostics");
    ok &= Check(!compileResult.diagnostics.empty() &&
                compileResult.diagnostics.front().location.value("line", 0) > 0,
                "compiler diagnostic contains source location");

    {
        const std::string source =
            "class ButtonHandlers : MipsBehaviour { "
            "public void HandleClick() {} "
            "void HiddenHelper() {} "
            "public void WithArgument(float value) {} "
            "}";
        MipsyncEngine::Mips::Lexer lexer(source, "ButtonHandlers.mips");
        const auto tokens = lexer.Tokenize();
        MipsyncEngine::Mips::Parser parser(tokens, "ButtonHandlers.mips");
        auto program = parser.ParseProgram();
        std::vector<std::string> compilerErrors;
        auto module = program && !program->classes.empty()
            ? MipsyncEngine::Mips::CompileClass(*program->classes.front(), program.get(),
                                                "ButtonHandlers.mips", compilerErrors)
            : nullptr;
        const auto* click = module ? module->FindMethod("HandleClick") : nullptr;
        const auto* hidden = module ? module->FindMethod("HiddenHelper") : nullptr;
        const auto* withArgument = module ? module->FindMethod("WithArgument") : nullptr;
        ok &= Check(module && compilerErrors.empty(), "button handler fixture compiles");
        ok &= Check(click && click->isPublic && click->parameterCount == 0 &&
                    click->returnType == "void", "public no-argument button method metadata is preserved");
        ok &= Check(hidden && !hidden->isPublic, "non-public method metadata is preserved");
        ok &= Check(withArgument && withArgument->parameterCount == 1,
                    "button dropdown can reject methods with arguments");
    }

    {
        const std::filesystem::path root =
            std::filesystem::temp_directory_path() / "mipsync_script_resolver_test";
        const std::filesystem::path scripts = root / "assets" / "scripts";
        std::error_code ec;
        std::filesystem::create_directories(scripts, ec);
        {
            std::ofstream script(scripts / "Safe.mips");
            script << "class Safe : MipsBehaviour { void Start() {} }";
        }
        std::string resolveError;
        const auto legacyResolved = MipsyncEngine::Mips::MipsScriptLoader::ResolvePath(
            root, "Safe.mips", resolveError);
        ok &= Check(!legacyResolved.empty() && resolveError.empty(),
                    "legacy script filename resolves under project assets/scripts");
        const auto escaped = MipsyncEngine::Mips::MipsScriptLoader::ResolvePath(
            root, "../outside.mips", resolveError);
        ok &= Check(escaped.empty() && !resolveError.empty(),
                    "script resolver rejects project-root traversal");
        std::filesystem::remove_all(root, ec);
    }

    CommandRequest protocolRequest;
    protocolRequest.requestId = "round-trip";
    protocolRequest.command = "entity.inspect";
    protocolRequest.arguments = {{"entity", "Player"}};
    protocolRequest.projectPath = "project";
    const auto roundTrip = CommandRequest::FromJson(protocolRequest.ToJson());
    ok &= Check(roundTrip.requestId == protocolRequest.requestId &&
                roundTrip.command == protocolRequest.command &&
                roundTrip.arguments == protocolRequest.arguments,
                "versioned IPC request round-trips through JSON");

    if (ok) std::cout << "Command Platform tests passed\n";
    return ok ? 0 : 1;
}
