#pragma once

#include "CommandRegistry.h"
#include <string>
#include <vector>

namespace MipsyncEngine::Command {

struct ParsedCommandLine {
    bool valid = false;
    bool json = false;
    std::string projectPath;
    std::string instanceId;
    CommandRequest request;
    std::string error;
};

std::vector<std::string> TokenizeCommandLine(const std::string& line, std::string& outError);
ParsedCommandLine ParseCommandLine(const std::vector<std::string>& args,
                                   const CommandRegistry& registry);

} // namespace MipsyncEngine::Command
