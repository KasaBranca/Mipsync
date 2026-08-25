#include "CommandRegistry.h"

#include <cctype>

namespace MipsyncEngine::Command {
namespace {

bool IsValidId(const std::string& id) {
    if (id.empty() || id.front() == '.' || id.back() == '.') return false;
    bool previousDot = false;
    for (const unsigned char c : id) {
        if (c == '.') {
            if (previousDot) return false;
            previousDot = true;
            continue;
        }
        previousDot = false;
        if (!std::islower(c) && !std::isdigit(c) && c != '_' && c != '-') return false;
    }
    return true;
}

} // namespace

bool CommandRegistry::Register(CommandDescriptor descriptor, std::string* outError) {
    if (!IsValidId(descriptor.id)) {
        if (outError) *outError = "invalid command id: " + descriptor.id;
        return false;
    }
    if (!descriptor.handler) {
        if (outError) *outError = "command has no handler: " + descriptor.id;
        return false;
    }
    const std::string id = descriptor.id;
    if (!m_Commands.emplace(id, std::move(descriptor)).second) {
        if (outError) *outError = "duplicate command id: " + id;
        return false;
    }
    return true;
}

const CommandDescriptor* CommandRegistry::Find(const std::string& id) const {
    const auto it = m_Commands.find(id);
    return it == m_Commands.end() ? nullptr : &it->second;
}

std::vector<const CommandDescriptor*> CommandRegistry::All() const {
    std::vector<const CommandDescriptor*> result;
    result.reserve(m_Commands.size());
    for (const auto& [id, descriptor] : m_Commands) result.push_back(&descriptor);
    return result;
}

std::vector<const CommandDescriptor*> CommandRegistry::Under(const std::string& prefix) const {
    if (prefix.empty()) return All();
    std::vector<const CommandDescriptor*> result;
    const std::string dotted = prefix + ".";
    for (const auto& [id, descriptor] : m_Commands) {
        if (id == prefix || id.rfind(dotted, 0) == 0) result.push_back(&descriptor);
    }
    return result;
}

} // namespace MipsyncEngine::Command
