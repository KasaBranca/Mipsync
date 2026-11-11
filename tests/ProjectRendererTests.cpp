#include "renderer/ProjectRenderer.h"
#include "core/AssetFiles.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <chrono>
namespace fs = std::filesystem;
using namespace MipsyncEngine;
void Check(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
void Write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}
int main() {
    const auto root = fs::temp_directory_path() / ("mipsync-renderer-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        const auto project = root / fs::path(L"game \u30ec\u30f3\u30c0\u30e9\u30fc");
        const auto starter = root / "starter";
        Write(project / "nostalty.project", "{}");
        Write(starter / "runtime/render.c", "standard render v1");
        Write(starter / "runtime/lighting.c", "standard lighting v1");
        Write(starter / "runtime/retired.c", "retired default");
        Write(starter / "runtime/old-custom.c", "old default");
        // Migrate a pre-existing project without touching its own source.
        Write(project / "renderer/pc/mesh.frag", "user shader before migration");
        std::string error;
        Check(ProjectRenderer::Ensure(project, starter, error), error);
        Check(AssetFiles::ReadText(project / "renderer/pc/mesh.frag") == "user shader before migration", "First migration replaced custom code");
        for (const auto& [name, source] : ProjectRenderer::DefaultShaders())
            Check(AssetFiles::Exists(project / "renderer/pc" / name), "Missing default shader");
        Check(AssetFiles::ReadText(project / "renderer/ps1/runtime/render.c") == "standard render v1", "Existing project was not migrated");
        Write(project / "renderer/ps1/runtime/render.c", "user renderer");
        Write(project / "renderer/ps1/runtime/old-custom.c", "user old helper");
        Write(project / "renderer/ps1/runtime/helpers/effect.c", "custom helper");
        Write(starter / "runtime/render.c", "standard render v2");
        Write(starter / "runtime/lighting.c", "standard lighting v2");
        Write(starter / "runtime/new.h", "new interface");
        fs::remove(starter / "runtime/retired.c");
        fs::remove(starter / "runtime/old-custom.c");
        Check(ProjectRenderer::Ensure(project, starter, error), error);
        Check(AssetFiles::ReadText(project / "renderer/ps1/runtime/render.c") == "user renderer", "Upgrade replaced custom renderer");
        Check(AssetFiles::ReadText(project / "renderer/ps1/runtime/lighting.c") == "standard lighting v2", "Untouched runtime did not upgrade");
        Check(AssetFiles::ReadText(project / ".nostalty/renderer-defaults/ps1/runtime/render.c") == "standard render v2", "Current standard unavailable for merging");
        Check(AssetFiles::ReadText(project / "renderer/ps1/runtime/new.h") == "new interface", "New default missing");
        Check(!fs::exists(project / "renderer/ps1/runtime/retired.c"), "Retired standard source retained");
        Check(AssetFiles::ReadText(project / "renderer/ps1/runtime/old-custom.c") == "user old helper", "Retirement removed custom helper");
        Check(ProjectRenderer::HasCustomPs1Runtime(project, starter), "Custom runtime not detected");
        for (const auto& scene : {root / "build", root / "build/scenes/1"}) {
            Check(ProjectRenderer::StagePs1Runtime(project, starter, scene, error), error);
            Check(AssetFiles::ReadText(scene / "runtime/render.c") == "user renderer", "Scene did not receive renderer");
            Check(AssetFiles::ReadText(scene / "runtime/helpers/effect.c") == "custom helper", "Additional helper missing");
        }
        fs::remove(project / "renderer/ps1/runtime/helpers/effect.c");
        Check(ProjectRenderer::StagePs1Runtime(project, starter, root / "build", error), error);
        Check(!fs::exists(root / "build/runtime/helpers/effect.c"), "Deleted helper remained in generated output");
        Write(project / "renderer/ps1/runtime/render.c", "standard render v2");
        fs::remove(project / "renderer/ps1/runtime/old-custom.c");
        Check(!ProjectRenderer::HasCustomPs1Runtime(project, starter), "Unchanged runtime incorrectly flagged custom");
        fs::remove(project / "renderer/pc/screen.frag");
        Check(ProjectRenderer::Ensure(project, starter, error), error);
        Check(AssetFiles::ReadText(project / "renderer/pc/screen.frag") == ProjectRenderer::DefaultShaders().at("screen.frag"), "Missing shader was not restored");
        Check(AssetFiles::ReadText(project / "renderer/pc/mesh.frag") == "user shader before migration", "Repeated open replaced custom shader");
        fs::remove_all(root);
        std::cout << "Project renderer migration, upgrades, preservation, per-scene staging and rebuild passed\n";
        return 0;
    } catch (const std::exception& ex) { std::cerr << ex.what() << '\n'; return 1; }
}
