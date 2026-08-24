#pragma once

#include <string>
#include <vector>

namespace MipsyncEngine::Mips {

// Phase 1: lexer/parser smoke tests (logs to engine console).
void RunMipsPhase1Tests();
bool RunMipsRuntimeRegressionTests(const char* scriptPath,
                                   std::vector<std::string>& errors);

} // namespace MipsyncEngine::Mips
