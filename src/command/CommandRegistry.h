#pragma once

#include "CommandTypes.h"
#include <map>
#include <string>
#include <vector>

namespace MipsyncEngine::Command {

class CommandRegistry {
public:
    bool Register(CommandDescriptor descriptor, std::string* outError = nullptr);
    const CommandDescriptor* Find(const std::string& id) const;
    std::vector<const CommandDescriptor*> All() const;
    std::vector<const CommandDescriptor*> Under(const std::string& prefix) const;

private:
    std::map<std::string, CommandDescriptor> m_Commands;
};

} // namespace MipsyncEngine::Command
