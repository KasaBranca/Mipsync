#pragma once

#include <filesystem>
#include <string>

namespace MipsyncEngine {

struct AgentIntegrationResult {
    bool success = false;
    bool skillUpdated = false;
    bool instructionsUpdated = false;
    std::filesystem::path skillPath;
    std::string error;
};

/// Installs the Agent Skill bundled with this exact Editor/CLI build into the
/// project-scoped discovery directory and maintains the Mipsync section of
/// AGENTS.md. Existing instructions outside the managed section are preserved.
AgentIntegrationResult EnsureAgentIntegration(
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& executableDirectory);

} // namespace MipsyncEngine
