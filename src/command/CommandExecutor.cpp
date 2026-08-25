#include "CommandExecutor.h"

#include <exception>
#include <unordered_set>

namespace MipsyncEngine::Command {
namespace {

bool MatchesType(const Json& value, ValueType type) {
    switch (type) {
        case ValueType::String:
        case ValueType::Path: return value.is_string();
        case ValueType::Integer: return value.is_number_integer() || value.is_number_unsigned();
        case ValueType::Number: return value.is_number();
        case ValueType::Boolean: return value.is_boolean();
        case ValueType::Json: return true;
    }
    return false;
}

} // namespace

CommandResult CommandExecutor::Execute(const CommandRequest& request, CommandContext& context) const {
    if (request.protocolVersion != kProtocolVersion) {
        return CommandResult::Fail("MIPSYNC_PROTOCOL_VERSION",
            "Unsupported protocol version " + std::to_string(request.protocolVersion) +
            "; this executable supports version " + std::to_string(kProtocolVersion) + ".");
    }
    const CommandDescriptor* descriptor = m_Registry.Find(request.command);
    if (!descriptor)
        return CommandResult::Fail("MIPSYNC_COMMAND_NOT_FOUND", "Unknown command: " + request.command);
    if (descriptor->executionMode == ExecutionMode::Editor && !context.editor)
        return CommandResult::Fail("MIPSYNC_EDITOR_REQUIRED", request.command + " requires a running Mipsync Editor.");
    if (!request.arguments.is_object())
        return CommandResult::Fail("MIPSYNC_INVALID_ARGUMENTS", "Command arguments must be a JSON object.");

    std::unordered_set<std::string> known;
    for (const auto& parameter : descriptor->parameters) {
        known.insert(parameter.name);
        const auto it = request.arguments.find(parameter.name);
        if (it == request.arguments.end()) {
            if (parameter.required)
                return CommandResult::Fail("MIPSYNC_MISSING_ARGUMENT", "Missing argument: " + parameter.name);
            continue;
        }
        if (!MatchesType(*it, parameter.type)) {
            return CommandResult::Fail("MIPSYNC_ARGUMENT_TYPE",
                "Argument '" + parameter.name + "' must be " + ToString(parameter.type) + ".");
        }
    }
    for (auto it = request.arguments.begin(); it != request.arguments.end(); ++it) {
        if (!known.contains(it.key()))
            return CommandResult::Fail("MIPSYNC_UNKNOWN_ARGUMENT", "Unknown argument: " + it.key());
    }

    try {
        return descriptor->handler(request, context);
    } catch (const std::exception& ex) {
        return CommandResult::Fail("MIPSYNC_COMMAND_EXCEPTION", ex.what());
    } catch (...) {
        return CommandResult::Fail("MIPSYNC_COMMAND_EXCEPTION", "Command failed with an unknown exception.");
    }
}

} // namespace MipsyncEngine::Command
