#include "AgentIntegration.h"
#include "../project/Project.h"

#include <fstream>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace MipsyncEngine {
namespace fs = std::filesystem;
namespace {

constexpr const char* kManagedStart = "<!-- mipsync-agent-skill:start -->";
constexpr const char* kManagedEnd = "<!-- mipsync-agent-skill:end -->";

const char* kManagedInstructions = R"(<!-- mipsync-agent-skill:start -->
## Mipsync project automation

- For every task that inspects or changes this Mipsync project's scenes, entities, components, meshes, materials, Mips# scripts, or Editor runtime, you MUST invoke `$mipsync-cli` and follow its `SKILL.md` before acting.
- Use the Mipsync CLI for live project authoring and inspection. Do not edit `.nscene` files directly when the CLI can perform the operation.
- The Skill is project-scoped at `.agents/skills/mipsync-cli/SKILL.md` and is synchronized from the currently running Mipsync Editor version.
- Changes to the Mipsync engine C++ source itself are outside this project Skill.
<!-- mipsync-agent-skill:end -->)";

bool ReadText(const fs::path& path, std::string& out, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        error = "failed to read " + path.string();
        return false;
    }
    std::ostringstream stream;
    stream << input.rdbuf();
    out = stream.str();
    return true;
}

bool ReplaceFileAtomically(const fs::path& target, const std::string& contents,
                           std::string& error) {
    const fs::path temporary = target.string() + ".mipsync.tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            error = "failed to write " + temporary.string();
            return false;
        }
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (!output.good()) {
            error = "failed to finish writing " + temporary.string();
            output.close();
            std::error_code cleanupError;
            fs::remove(temporary, cleanupError);
            return false;
        }
    }

#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), target.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "failed to replace " + target.string() +
                " (Windows error " + std::to_string(GetLastError()) + ")";
        std::error_code cleanupError;
        fs::remove(temporary, cleanupError);
        return false;
    }
#else
    std::error_code ec;
    fs::rename(temporary, target, ec);
    if (ec) {
        error = "failed to replace " + target.string() + ": " + ec.message();
        fs::remove(temporary, ec);
        return false;
    }
#endif
    return true;
}

bool CopySkill(const fs::path& source, const fs::path& destination,
               bool& updated, std::string& error) {
    std::error_code ec;
    fs::create_directories(destination, ec);
    if (ec) {
        error = "failed to create Agent Skill directory: " + ec.message();
        return false;
    }

    for (fs::recursive_directory_iterator it(source, ec), end; it != end; it.increment(ec)) {
        if (ec) {
            error = "failed to enumerate bundled Agent Skill: " + ec.message();
            return false;
        }
        const fs::path relative = fs::relative(it->path(), source, ec);
        if (ec) {
            error = "failed to resolve bundled Agent Skill path: " + ec.message();
            return false;
        }
        const fs::path target = destination / relative;
        if (it->is_directory(ec)) {
            fs::create_directories(target, ec);
            if (ec) {
                error = "failed to create Agent Skill subdirectory: " + ec.message();
                return false;
            }
            continue;
        }
        if (!it->is_regular_file(ec))
            continue;

        bool differs = true;
        if (fs::is_regular_file(target, ec) && !ec) {
            const auto sourceSize = fs::file_size(it->path(), ec);
            const auto targetSize = ec ? 0 : fs::file_size(target, ec);
            differs = ec || sourceSize != targetSize;
            if (!differs) {
                std::string sourceText;
                std::string targetText;
                std::string readError;
                differs = !ReadText(it->path(), sourceText, readError) ||
                          !ReadText(target, targetText, readError) ||
                          sourceText != targetText;
            }
        }
        ec.clear();
        if (!differs)
            continue;

        fs::create_directories(target.parent_path(), ec);
        fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            error = "failed to synchronize Agent Skill file " + target.string() +
                    ": " + ec.message();
            return false;
        }
        updated = true;
    }
    return true;
}

bool EnsureInstructions(const fs::path& projectRoot, bool& updated,
                        std::string& error) {
    const fs::path path = projectRoot / "AGENTS.md";
    std::string existing;
    std::error_code ec;
    if (fs::exists(path, ec) && !ReadText(path, existing, error))
        return false;

    const size_t start = existing.find(kManagedStart);
    const size_t end = existing.find(kManagedEnd);
    if ((start == std::string::npos) != (end == std::string::npos) ||
        (start != std::string::npos && end < start)) {
        error = "AGENTS.md contains an incomplete Mipsync managed block; repair or remove it before retrying";
        return false;
    }

    const std::string newline = existing.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::string block = kManagedInstructions;
    if (newline == "\r\n") {
        size_t position = 0;
        while ((position = block.find('\n', position)) != std::string::npos) {
            block.replace(position, 1, "\r\n");
            position += 2;
        }
    }

    std::string desired;
    if (start == std::string::npos) {
        desired = existing;
        if (!desired.empty()) {
            while (desired.ends_with('\n') || desired.ends_with('\r'))
                desired.pop_back();
            desired += newline + newline;
        }
        desired += block + newline;
    } else {
        const size_t after = end + std::char_traits<char>::length(kManagedEnd);
        desired = existing.substr(0, start) + block + existing.substr(after);
    }

    if (desired == existing)
        return true;
    if (!ReplaceFileAtomically(path, desired, error))
        return false;
    updated = true;
    return true;
}

bool EnsureRuntimeBinding(const fs::path& skillDirectory,
                          const fs::path& executableDirectory,
                          bool& updated, std::string& error) {
#ifdef _WIN32
    const fs::path cliPath = executableDirectory / "mipsync.exe";
#else
    const fs::path cliPath = executableDirectory / "mipsync";
#endif
    std::error_code ec;
    if (!fs::is_regular_file(cliPath, ec))
        return true;

    const fs::path bindingPath = skillDirectory / "references" / "runtime.md";
    fs::create_directories(bindingPath.parent_path(), ec);
    if (ec) {
        error = "failed to create Agent Skill references directory: " + ec.message();
        return false;
    }

    const std::string desired =
        "# Mipsync runtime binding\n\n"
        "This file is generated by Mipsync. Use this exact CLI first because it "
        "matches the Editor version that synchronized the Skill.\n\n"
        "- CLI: `" + cliPath.string() + "`\n"
        "- Version directory: `" + executableDirectory.string() + "`\n\n"
        "Run the CLI with PowerShell's call operator (`&`) when using the absolute path. "
        "Do not select a same-named executable from a project's `Builds` output.\n";

    std::string existing;
    if (fs::is_regular_file(bindingPath, ec)) {
        if (!ReadText(bindingPath, existing, error))
            return false;
        if (existing == desired)
            return true;
    }
    if (!ReplaceFileAtomically(bindingPath, desired, error))
        return false;
    updated = true;
    return true;
}

} // namespace

AgentIntegrationResult EnsureAgentIntegration(const fs::path& projectRoot,
                                              const fs::path& executableDirectory) {
    AgentIntegrationResult result;
    std::error_code ec;
    if (projectRoot.empty() ||
        !fs::is_regular_file(projectRoot / Project::kProjectFile, ec)) {
        result.error = "not a Mipsync project: " + projectRoot.string();
        return result;
    }

    const fs::path source = executableDirectory / "skills" / "mipsync-cli";
    if (!fs::is_regular_file(source / "SKILL.md", ec)) {
        result.error = "bundled Mipsync Agent Skill was not found at " + source.string();
        return result;
    }

    result.skillPath = projectRoot / ".agents" / "skills" / "mipsync-cli";
    if (!CopySkill(source, result.skillPath, result.skillUpdated, result.error))
        return result;
    if (!EnsureRuntimeBinding(result.skillPath, executableDirectory,
                              result.skillUpdated, result.error))
        return result;
    if (!EnsureInstructions(projectRoot, result.instructionsUpdated, result.error))
        return result;

    result.success = true;
    return result;
}

} // namespace MipsyncEngine
