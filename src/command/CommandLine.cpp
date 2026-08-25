#include "CommandLine.h"

#include <charconv>
#include <chrono>
#include <cctype>
#include <sstream>

namespace MipsyncEngine::Command {
namespace {

bool ParseBool(const std::string& value, bool& out) {
    if (value == "true" || value == "1" || value == "yes") { out = true; return true; }
    if (value == "false" || value == "0" || value == "no") { out = false; return true; }
    return false;
}

bool Convert(const std::string& text, ValueType type, Json& out) {
    try {
        switch (type) {
            case ValueType::String:
            case ValueType::Path: out = text; return true;
            case ValueType::Integer: {
                int64_t value = 0;
                const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
                if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return false;
                out = value;
                return true;
            }
            case ValueType::Number: {
                size_t used = 0;
                const double value = std::stod(text, &used);
                if (used != text.size()) return false;
                out = value;
                return true;
            }
            case ValueType::Boolean: {
                bool value = false;
                if (!ParseBool(text, value)) return false;
                out = value;
                return true;
            }
            case ValueType::Json: out = Json::parse(text); return true;
        }
    } catch (...) {}
    return false;
}

std::string MakeRequestId() {
    return std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

} // namespace

std::vector<std::string> TokenizeCommandLine(const std::string& line, std::string& outError) {
    std::vector<std::string> result;
    std::string token;
    char quote = 0;
    bool escape = false;
    for (char c : line) {
        if (escape) { token.push_back(c); escape = false; continue; }
        if (c == '\\') { escape = true; continue; }
        if (quote) {
            if (c == quote) quote = 0;
            else token.push_back(c);
            continue;
        }
        if (c == '\'' || c == '"') { quote = c; continue; }
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!token.empty()) { result.push_back(std::move(token)); token.clear(); }
            continue;
        }
        token.push_back(c);
    }
    if (escape) token.push_back('\\');
    if (quote) { outError = "unterminated quoted string"; return {}; }
    if (!token.empty()) result.push_back(std::move(token));
    return result;
}

ParsedCommandLine ParseCommandLine(const std::vector<std::string>& args,
                                   const CommandRegistry& registry) {
    ParsedCommandLine parsed;
    std::vector<std::string> commandTokens;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--json") { parsed.json = true; continue; }
        if ((args[i] == "--project" || args[i] == "-p") && i + 1 < args.size()) {
            parsed.projectPath = args[++i];
            continue;
        }
        if (args[i] == "--instance" && i + 1 < args.size()) {
            parsed.instanceId = args[++i];
            continue;
        }
        if (args[i] == "--help" || args[i] == "-h") {
            commandTokens.push_back("help");
            continue;
        }
        commandTokens.push_back(args[i]);
    }
    if (commandTokens.empty()) commandTokens.push_back("help");

    const CommandDescriptor* descriptor = nullptr;
    size_t consumed = 0;
    for (size_t count = std::min<size_t>(4, commandTokens.size()); count > 0; --count) {
        std::string candidate;
        for (size_t i = 0; i < count; ++i) {
            if (!candidate.empty()) candidate += '.';
            candidate += commandTokens[i];
        }
        if (const auto* found = registry.Find(candidate)) {
            descriptor = found;
            consumed = count;
            break;
        }
        if (count == 1 && commandTokens[0].find('.') != std::string::npos) {
            if (const auto* found = registry.Find(commandTokens[0])) {
                descriptor = found;
                consumed = 1;
                break;
            }
        }
    }
    if (!descriptor) {
        parsed.error = "unknown command: " + commandTokens[0];
        return parsed;
    }

    parsed.request.command = descriptor->id;
    parsed.request.requestId = MakeRequestId();
    parsed.request.projectPath = parsed.projectPath;
    size_t positional = 0;
    for (size_t i = consumed; i < commandTokens.size(); ++i) {
        std::string name;
        std::string text;
        if (commandTokens[i].rfind("--", 0) == 0) {
            name = commandTokens[i].substr(2);
            if (i + 1 >= commandTokens.size()) {
                parsed.error = "missing value for --" + name;
                return parsed;
            }
            text = commandTokens[++i];
        } else {
            if (positional >= descriptor->parameters.size()) {
                parsed.error = "too many positional arguments for " + descriptor->id;
                return parsed;
            }
            name = descriptor->parameters[positional++].name;
            text = commandTokens[i];
        }
        const auto parameter = std::find_if(descriptor->parameters.begin(), descriptor->parameters.end(),
            [&](const ParameterDescriptor& item) { return item.name == name; });
        if (parameter == descriptor->parameters.end()) {
            parsed.error = "unknown argument --" + name;
            return parsed;
        }
        Json value;
        if (!Convert(text, parameter->type, value)) {
            parsed.error = "invalid " + std::string(ToString(parameter->type)) + " value for " + name;
            return parsed;
        }
        parsed.request.arguments[name] = std::move(value);
    }
    for (const auto& parameter : descriptor->parameters) {
        if (!parsed.request.arguments.contains(parameter.name) && !parameter.defaultValue.is_null())
            parsed.request.arguments[parameter.name] = parameter.defaultValue;
    }
    parsed.valid = true;
    return parsed;
}

} // namespace MipsyncEngine::Command
