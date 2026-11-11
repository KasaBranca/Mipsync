#pragma once

#include "CommandTypes.h"
#include <string>

namespace MipsyncEngine::Command {

std::string RenderHuman(const std::string& command, const CommandResult& result);
// Projection applies to result.value; protocol status/diagnostics stay intact.
Json ProjectOutput(const Json& value, const std::string& fields, int maxDepth = -1);

} // namespace MipsyncEngine::Command
