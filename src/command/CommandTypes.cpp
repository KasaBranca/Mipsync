#include "CommandTypes.h"

#include <stdexcept>

namespace MipsyncEngine::Command {

const char* ToString(ValueType value) {
    switch (value) {
        case ValueType::String: return "string";
        case ValueType::Integer: return "integer";
        case ValueType::Number: return "number";
        case ValueType::Boolean: return "boolean";
        case ValueType::Path: return "path";
        case ValueType::Json: return "json";
    }
    return "unknown";
}

const char* ToString(ExecutionMode value) {
    switch (value) {
        case ExecutionMode::Local: return "local";
        case ExecutionMode::Editor: return "editor";
        case ExecutionMode::Either: return "either";
    }
    return "unknown";
}

const char* ToString(SideEffect value) {
    switch (value) {
        case SideEffect::ReadOnly: return "read_only";
        case SideEffect::EditorMutation: return "editor_mutation";
        case SideEffect::ProjectMutation: return "project_mutation";
        case SideEffect::FilesystemWrite: return "filesystem_write";
        case SideEffect::Build: return "build";
        case SideEffect::RuntimeMutation: return "runtime_mutation";
        case SideEffect::ExternalProcess: return "external_process";
        case SideEffect::Dangerous: return "dangerous";
    }
    return "unknown";
}

Json Diagnostic::ToJson() const {
    Json result{{"code", code}, {"severity", severity}, {"message", message}};
    if (!location.is_null() && !location.empty()) result["location"] = location;
    if (!symbol.empty()) result["symbol"] = symbol;
    if (!expected.is_null() && !expected.empty()) result["expected"] = expected;
    if (!received.is_null() && !received.empty()) result["received"] = received;
    if (!suggestions.empty()) result["suggestions"] = suggestions;
    return result;
}

Json CommandRequest::ToJson() const {
    Json result{
        {"protocolVersion", protocolVersion},
        {"requestId", requestId},
        {"command", command},
        {"arguments", arguments},
    };
    if (!projectPath.empty()) result["projectPath"] = projectPath;
    return result;
}

CommandRequest CommandRequest::FromJson(const Json& value) {
    if (!value.is_object()) throw std::invalid_argument("request must be a JSON object");
    CommandRequest request;
    request.protocolVersion = value.value("protocolVersion", 0);
    request.requestId = value.value("requestId", std::string{});
    request.command = value.value("command", std::string{});
    request.arguments = value.value("arguments", Json::object());
    request.projectPath = value.value("projectPath", std::string{});
    if (!request.arguments.is_object()) throw std::invalid_argument("arguments must be an object");
    return request;
}

CommandResult CommandResult::Ok(Json resultValue) {
    CommandResult result;
    result.success = true;
    result.status = "completed";
    result.value = std::move(resultValue);
    return result;
}

CommandResult CommandResult::Fail(std::string code, std::string message) {
    CommandResult result;
    result.success = false;
    result.status = "failed";
    result.diagnostics.push_back({std::move(code), "error", std::move(message)});
    return result;
}

Json CommandResult::ToJson(const std::string& requestId) const {
    Json result{{"success", success}, {"status", status}};
    if (!requestId.empty()) result["requestId"] = requestId;
    if (!value.is_null()) result["result"] = value;
    result["diagnostics"] = Json::array();
    for (const auto& diagnostic : diagnostics)
        result["diagnostics"].push_back(diagnostic.ToJson());
    return result;
}

Json CommandDescriptor::ToJson(bool includeExamples) const {
    Json result{
        {"id", id},
        {"summary", summary},
        {"description", description},
        {"returns", returnType},
        {"executionMode", ToString(executionMode)},
        {"parameters", Json::array()},
        {"effects", Json::array()},
    };
    for (const auto& parameter : parameters) {
        Json item{
            {"name", parameter.name},
            {"type", ToString(parameter.type)},
            {"required", parameter.required},
            {"summary", parameter.summary},
        };
        if (!parameter.defaultValue.is_null()) item["default"] = parameter.defaultValue;
        result["parameters"].push_back(std::move(item));
    }
    for (const auto effect : effects) result["effects"].push_back(ToString(effect));
    if (includeExamples) result["examples"] = examples;
    return result;
}

} // namespace MipsyncEngine::Command
