#pragma once

#include "CommandRegistry.h"

namespace MipsyncEngine::Command {

class CommandExecutor {
public:
    explicit CommandExecutor(const CommandRegistry& registry) : m_Registry(registry) {}
    CommandResult Execute(const CommandRequest& request, CommandContext& context) const;

private:
    const CommandRegistry& m_Registry;
};

} // namespace MipsyncEngine::Command
