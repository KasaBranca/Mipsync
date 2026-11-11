#include "ProjectRenderer.h"
#include "DefaultShaders.h"
#include "../core/AssetFiles.h"
#include "../core/NativePaths.h"
#include <fstream>
#include <stdexcept>
#include <set>
#include <vector>

namespace MipsyncEngine::ProjectRenderer {
namespace fs = std::filesystem;
namespace {
void Write(const fs::path& path, const std::string& contents) {
    fs::create_directories(NativePaths::FilePath(path.parent_path()));
    const auto temporary = fs::path(path).concat(".mipsync.tmp");
    {
        std::ofstream out(NativePaths::FilePath(temporary), std::ios::binary | std::ios::trunc);
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        out.close();
        if (!out) throw std::runtime_error("Cannot write renderer source: " + path.string());
    }
#ifdef _WIN32
    if (!MoveFileExW(NativePaths::FilePath(temporary).c_str(), NativePaths::FilePath(path).c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw fs::filesystem_error("Cannot replace renderer source", path,
                                  std::error_code(GetLastError(), std::system_category()));
#else
    fs::rename(temporary, path);
#endif
}
void Sync(const fs::path& root, const fs::path& relative, const std::string& desired) {
    const auto target = root / "renderer" / relative;
    const auto baseline = root / ".nostalty" / "renderer-defaults" / relative;
    const bool exists = fs::exists(NativePaths::FilePath(target));
    const auto existing = exists ? AssetFiles::ReadText(target) : std::string{};
    const bool unchanged = fs::is_regular_file(NativePaths::FilePath(baseline)) &&
                           existing == AssetFiles::ReadText(baseline);
    if (!exists || (unchanged && existing != desired)) Write(target, desired);
    // A pre-existing renderer without a baseline belongs to the user, even on
    // the first upgrade. Updating the baseline never overwrites that source.
    if (!fs::is_regular_file(NativePaths::FilePath(baseline)) ||
        AssetFiles::ReadText(baseline) != desired) Write(baseline, desired);
}
template<class Visitor>
void VisitSources(const fs::path& root, Visitor visitor) {
    if (!fs::is_directory(NativePaths::FilePath(root))) return;
    for (const auto& entry : fs::recursive_directory_iterator(NativePaths::FilePath(root))) {
        if (!entry.is_regular_file()) continue;
        const auto extension = entry.path().extension();
        if (extension != ".c" && extension != ".h" && extension != ".s" && extension != ".S") continue;
        visitor(entry.path(), NativePaths::OrdinaryPath(entry.path()).lexically_relative(fs::absolute(root)));
    }
}
}

const std::map<std::string, std::string>& DefaultShaders() {
    using namespace RendererDefaults;
    static const std::map<std::string, std::string> shaders = {
        {"mesh.vert", PS1_VERTEX_SHADER}, {"skinned.vert", PS1_SKINNED_VERTEX_SHADER},
        {"mesh.frag", PS1_FRAGMENT_SHADER}, {"screen.vert", SCREEN_VERTEX_SHADER},
        {"screen.frag", SCREEN_FRAGMENT_SHADER}, {"skybox.frag", SKYBOX_FRAGMENT_SHADER},
        {"outline-mask.vert", OUTLINE_MASK_VERTEX_SHADER}, {"outline-mask.frag", OUTLINE_MASK_FRAGMENT_SHADER},
        {"outline-compose.vert", OUTLINE_COMPOSE_VERTEX_SHADER}, {"outline-compose.frag", OUTLINE_COMPOSE_FRAGMENT_SHADER}
    };
    return shaders;
}

bool Ensure(const fs::path& projectRoot, const fs::path& starterRoot, std::string& error) {
    try {
        if (!fs::is_directory(NativePaths::FilePath(starterRoot / "runtime")))
            throw std::runtime_error("Bundled PS1 runtime is missing: " + starterRoot.string());
        std::set<fs::path> current;
        auto sync = [&](const fs::path& relative, const std::string& contents) {
            current.insert(relative);
            Sync(projectRoot, relative, contents);
        };
        for (const auto& [name, source] : DefaultShaders()) sync(fs::path("pc") / name, source);
        VisitSources(starterRoot / "runtime", [&](const fs::path& path, const fs::path& relative) {
            sync(fs::path("ps1/runtime") / relative, AssetFiles::ReadText(path));
        });
        sync("README.md", R"(# Game renderer

Edit pc/*.vert and pc/*.frag for the Editor and PC player. The Editor reloads
saved shaders about once per second. Errors keep the last valid shader set.
mesh.vert / skinned.vert share mesh.frag. screen.frag is a full-screen pass;
uScreen is the scene texture, uViewportSize its dimensions, uTime elapsed seconds.
skybox.frag and outline-* control the sky and selection outline. Keep the
standard input locations/uniform names when using data supplied by the engine.

Edit ps1/runtime/render.c and its helpers for PS1 rendering. Rebuild PS1 or
start PS1 Play to compile changes; export alone only stages sources. Added
C/assembly helpers are compiled, except the legacy interpreter vm.c. Keep
the interfaces used by main.c and generated scene data. All scenes receive
the same project runtime. Builds/.../ps1_src is generated and overwritten.

On project open, missing and unchanged defaults are updated automatically.
Customized files are preserved. Current defaults for comparison are stored
in .nostalty/renderer-defaults. Keep that directory with the project for
automatic upgrades of unchanged files. Custom interfaces may need merging
after an engine upgrade. PC GLSL and PS1 C are separate rendering backends.
)");
        const auto baselineRoot = projectRoot / ".nostalty/renderer-defaults";
        std::vector<fs::path> retired;
        for (const auto& entry : fs::recursive_directory_iterator(NativePaths::FilePath(baselineRoot))) {
            if (!entry.is_regular_file()) continue;
            const auto relative = NativePaths::OrdinaryPath(entry.path()).lexically_relative(fs::absolute(baselineRoot));
            if (current.contains(relative)) continue;
            const auto target = projectRoot / "renderer" / relative;
            // A retired standard helper must not become an unintended extra
            // compilation unit. Retain customized old helpers for manual merge.
            if (fs::is_regular_file(NativePaths::FilePath(target)) &&
                AssetFiles::ReadText(target) == AssetFiles::ReadText(entry.path()))
                fs::remove(NativePaths::FilePath(target));
            retired.push_back(entry.path());
        }
        for (const auto& path : retired) fs::remove(path);
        return true;
    } catch (const std::exception& ex) { error = ex.what(); return false; }
}

bool StagePs1Runtime(const fs::path& projectRoot, const fs::path& starterRoot,
                     const fs::path& outputRoot, std::string& error) {
    try {
        // This directory is generated. Recreating it also removes custom helper
        // sources that the user deleted since the preceding build.
        const auto runtime = outputRoot / "runtime";
        NativePaths::RemoveTree(runtime);
        NativePaths::CopyTree(starterRoot / "runtime", runtime);
        VisitSources(projectRoot / "renderer/ps1/runtime", [&](const fs::path& path, const fs::path& relative) {
            NativePaths::CopyRegularFile(path, runtime / relative);
        });
        return true;
    } catch (const std::exception& ex) { error = ex.what(); return false; }
}

bool HasCustomPs1Runtime(const fs::path& projectRoot, const fs::path& starterRoot) {
    bool custom = false;
    VisitSources(projectRoot / "renderer/ps1/runtime", [&](const fs::path& path, const fs::path& relative) {
        const auto original = starterRoot / "runtime" / relative;
        if (!fs::is_regular_file(NativePaths::FilePath(original)) ||
            AssetFiles::ReadText(path) != AssetFiles::ReadText(original)) custom = true;
    });
    return custom;
}
}
