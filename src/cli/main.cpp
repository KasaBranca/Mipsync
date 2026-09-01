#include "command/CommandExecutor.h"
#include "command/CommandLine.h"
#include "command/CoreCommands.h"
#include "command/IpcTransport.h"
#include "command/ResultRenderer.h"
#include "command/SymbolRegistry.h"
#include "bootstrap/AgentIntegration.h"
#include "core/RuntimePaths.h"
#include "project/Project.h"
#include "EngineVersion.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>

namespace fs = std::filesystem;
using namespace MipsyncEngine::Command;

namespace {

std::string DetectProject() {
    std::error_code ec;
    fs::path directory = fs::current_path(ec);
    while (!ec && !directory.empty()) {
        if (fs::is_regular_file(directory / MipsyncEngine::Project::kProjectFile, ec))
            return directory.string();
        const fs::path parent = directory.parent_path();
        if (parent == directory) break;
        directory = parent;
    }
    return {};
}

std::optional<EditorInstanceInfo> SelectInstance(const ParsedCommandLine& parsed,
                                                 std::string& outError) {
    auto instances = InstanceRegistry::List();
    instances.erase(std::remove_if(instances.begin(), instances.end(), [](const auto& instance) {
        return !IpcClient::IsAvailable(instance);
    }), instances.end());
    if (!parsed.instanceId.empty()) {
        for (const auto& instance : instances)
            if (instance.instanceId == parsed.instanceId) return instance;
        outError = "No running Editor instance has ID " + parsed.instanceId + ".";
        return std::nullopt;
    }
    if (!parsed.projectPath.empty()) {
        auto matching = InstanceRegistry::FindForProject(parsed.projectPath);
        matching.erase(std::remove_if(matching.begin(), matching.end(), [](const auto& instance) {
            return !IpcClient::IsAvailable(instance);
        }), matching.end());
        if (matching.size() == 1) return matching.front();
        if (matching.empty()) {
            outError = "No running Editor was found for project: " + parsed.projectPath;
            return std::nullopt;
        }
        outError = "Multiple Editors are open for this project; use --instance <id>.";
        return std::nullopt;
    }
    if (instances.size() == 1) return instances.front();
    if (instances.empty()) outError = "No running Mipsync Editor was found.";
    else outError = "Multiple Editors are running; use --project <path> or --instance <id>.";
    return std::nullopt;
}

int Print(const ParsedCommandLine& parsed, const CommandResult& result) {
    if (parsed.json) std::cout << result.ToJson(parsed.request.requestId).dump(2) << '\n';
    else {
        std::ostream& stream = result.success ? std::cout : std::cerr;
        stream << RenderHuman(parsed.request.command, result);
    }
    return result.success ? 0 : 1;
}

void EnsureProjectSkill(const std::string& projectPath,
                        const fs::path& executableDirectory,
                        bool jsonOutput) {
    if (projectPath.empty()) return;
    const auto setup = MipsyncEngine::EnsureAgentIntegration(projectPath, executableDirectory);
    if (!setup.success && !jsonOutput)
        std::cerr << "mipsync: warning: Agent Skill setup failed: " << setup.error << '\n';
}

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--version" || argument == "-V") {
            std::cout << "MipsyncCLI " << MIPSYNC_ENGINE_VERSION
                      << " (protocol " << kProtocolVersion << ")\n";
            return 0;
        }
    }

    CommandRegistry commands;
    SymbolRegistry symbols;
    RegisterCoreCommands(commands, symbols);

    std::vector<std::string> arguments;
    arguments.reserve(argc > 1 ? static_cast<size_t>(argc - 1) : 0);
    for (int i = 1; i < argc; ++i) arguments.emplace_back(argv[i]);
    ParsedCommandLine parsed = ParseCommandLine(arguments, commands);
    if (!parsed.valid) {
        std::cerr << "mipsync: " << parsed.error << "\nRun 'mipsync help' to list commands.\n";
        return 2;
    }
    if (parsed.projectPath.empty()) parsed.projectPath = DetectProject();
    parsed.request.projectPath = parsed.projectPath;

    const CommandDescriptor* descriptor = commands.Find(parsed.request.command);
    if (!descriptor) return Print(parsed, CommandResult::Fail("MIPSYNC_COMMAND_NOT_FOUND", parsed.request.command));
    if (descriptor->executionMode == ExecutionMode::Editor) {
        std::string error;
        const auto instance = SelectInstance(parsed, error);
        if (!instance) return Print(parsed, CommandResult::Fail("MIPSYNC_EDITOR_NOT_FOUND", error));
        if (parsed.projectPath.empty()) {
            parsed.projectPath = instance->projectPath;
            parsed.request.projectPath = instance->projectPath;
        }
        fs::path editorDirectory = MipsyncEngine::GetExeDirectory();
        if (!instance->executablePath.empty())
            editorDirectory = fs::path(instance->executablePath).parent_path();
        EnsureProjectSkill(instance->projectPath, editorDirectory, parsed.json);
        return Print(parsed, IpcClient::Execute(*instance, parsed.request, error));
    }

    EnsureProjectSkill(parsed.projectPath, MipsyncEngine::GetExeDirectory(), parsed.json);

    CommandContext context;
    context.projectPath = parsed.projectPath;
    context.engineVersion = MIPSYNC_ENGINE_VERSION;
    context.commands = &commands;
    context.symbols = &symbols;
    CommandExecutor executor(commands);
    return Print(parsed, executor.Execute(parsed.request, context));
}
