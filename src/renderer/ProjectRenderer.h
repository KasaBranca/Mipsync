#pragma once
#include <filesystem>
#include <map>
#include <string>

namespace MipsyncEngine::ProjectRenderer {

// Paths are relative to <project>/renderer/pc. Also used as the built-in
// fallback when an older player package has no project shaders.
const std::map<std::string, std::string>& DefaultShaders();

// Run on every Editor project open, including existing projects. Only missing
// files and files still equal to the previous engine default are replaced.
// The latest defaults are retained in .nostalty/renderer-defaults for comparison.
bool Ensure(const std::filesystem::path& projectRoot,
            const std::filesystem::path& starterRoot, std::string& error);

// Recreate generated runtime sources from this engine, then apply the game's
// overrides. Used for every scene, both export-only and compiled PS1 builds.
bool StagePs1Runtime(const std::filesystem::path& projectRoot,
                     const std::filesystem::path& starterRoot,
                     const std::filesystem::path& outputRoot, std::string& error);
bool HasCustomPs1Runtime(const std::filesystem::path& projectRoot,
                        const std::filesystem::path& starterRoot);
}
