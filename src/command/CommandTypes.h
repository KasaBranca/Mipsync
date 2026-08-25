#pragma once

#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace MipsyncEngine::Command {

using Json = nlohmann::json;

inline constexpr int kProtocolVersion = 1;

enum class ValueType { String, Integer, Number, Boolean, Path, Json };
enum class ExecutionMode { Local, Editor, Either };
enum class SideEffect {
    ReadOnly,
    EditorMutation,
    ProjectMutation,
    FilesystemWrite,
    Build,
    RuntimeMutation,
    ExternalProcess,
    Dangerous,
};

const char* ToString(ValueType value);
const char* ToString(ExecutionMode value);
const char* ToString(SideEffect value);

struct ParameterDescriptor {
    std::string name;
    ValueType type = ValueType::String;
    bool required = true;
    std::string summary;
    Json defaultValue;
};

struct Diagnostic {
    std::string code;
    std::string severity = "error";
    std::string message;
    Json location;
    std::string symbol;
    Json expected;
    Json received;
    Json suggestions = Json::array();

    Json ToJson() const;
};

struct CommandRequest {
    int protocolVersion = kProtocolVersion;
    std::string requestId;
    std::string command;
    Json arguments = Json::object();
    std::string projectPath;

    Json ToJson() const;
    static CommandRequest FromJson(const Json& value);
};

struct CommandResult {
    bool success = false;
    std::string status = "failed";
    Json value;
    std::vector<Diagnostic> diagnostics;

    static CommandResult Ok(Json value = Json::object());
    static CommandResult Fail(std::string code, std::string message);
    Json ToJson(const std::string& requestId = {}) const;
};

class CommandRegistry;
class SymbolRegistry;

class IEditorCommandService {
public:
    virtual ~IEditorCommandService() = default;
    virtual CommandResult ExecuteEditorCommand(const CommandRequest& request) = 0;
};

struct CommandContext {
    std::string projectPath;
    std::string engineVersion;
    CommandRegistry* commands = nullptr;
    SymbolRegistry* symbols = nullptr;
    IEditorCommandService* editor = nullptr;
};

using CommandHandler = std::function<CommandResult(const CommandRequest&, CommandContext&)>;

struct CommandDescriptor {
    std::string id;
    std::string summary;
    std::string description;
    std::vector<ParameterDescriptor> parameters;
    std::string returnType = "object";
    ExecutionMode executionMode = ExecutionMode::Local;
    std::vector<SideEffect> effects{ SideEffect::ReadOnly };
    std::vector<std::string> examples;
    CommandHandler handler;

    Json ToJson(bool includeExamples = true) const;
};

} // namespace MipsyncEngine::Command
