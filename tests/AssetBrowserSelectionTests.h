#pragma once
#include "editor/AssetBrowserPanel.h"
#include "assets/AssetManager.h"
#include "core/NativePaths.h"
#include "core/AssetFiles.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace MipsyncEngine {
inline bool RunAssetBrowserSelectionTests(std::string& error) {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / ("mipsync-selection-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        fs::path path; std::string originalRoot;
        ~Cleanup() { AssetManager::Get().SetProjectRoot(originalRoot); try { NativePaths::RemoveTree(path); } catch (...) {} }
    } cleanup{root, AssetManager::Get().GetProjectRoot()};
    try {
        auto require = [](bool ok, const char* message) { if (!ok) throw std::runtime_error(message); };
        fs::create_directories(root / "assets/scripts");
        fs::create_directories(root / "assets/models");
        fs::create_directories(root / "assets/empty");
        fs::create_directories(root / "scenes");
        std::ofstream(root / "assets/scripts/Player.mips") << "class Player : MipsBehaviour {}";
        std::ofstream(root / "assets/models/Character.fbx") << "selection fixture";
        AssetManager::Get().SetProjectRoot(PathUtf8::ToString(root));
        AssetBrowserPanel browser(nullptr);
        require(browser.RevealAndSelectAsset("assets/scripts/Player.mips"), "could not select relative reference");
        require(browser.GetSelectedAssetKind() == AssetKind::Script && browser.GetCurrentFolder() == "assets/scripts", "reference not revealed in its folder");
        for (const auto* folder : {"assets", "assets/models", "assets/empty", "scenes", "assets/scripts"}) {
            browser.SetCurrentFolder(folder); browser.Refresh();
            require(browser.GetSelectedAssetCount() == 1 && browser.GetSelectedAssetPath() == "assets/scripts/Player.mips", "folder navigation cleared selection");
        }
        require(browser.TryRestoreFolder("assets/empty") && browser.GetSelectedAssetCount() == 1, "restoring folder cleared selection");
        require(!browser.RevealAndSelectAsset("assets/scripts/Missing.mips") && browser.GetSelectedAssetPath() == "assets/scripts/Player.mips", "missing reference cleared selection");
        require(!browser.RevealAndSelectAsset(__FILE__) && browser.GetSelectedAssetCount() == 1, "external file reference changed project selection");
        require(browser.RevealAndSelectAsset(PathUtf8::ToString(root / "assets/scripts/Player.mips")), "absolute reference failed");
        const auto clip = AssetBrowserPanel::MakeAnimationClipVirtualPath("assets/models/Character.fbx", "Walk", 2);
        require(browser.RevealAndSelectAsset(clip), "embedded clip reference failed");
        std::string model, name;
        require(browser.GetSelectedAssetKind() == AssetKind::AnimationClip && browser.GetSelectedAnimationClip(model, name) &&
            model == "assets/models/Character.fbx" && name == "Walk", "embedded clip selection metadata lost");
        browser.SetCurrentFolder("assets/scripts");
        require(browser.GetSelectedAssetPath() == clip, "embedded clip cleared by navigation");
        browser.ClearAssetSelection(); require(browser.GetSelectedAssetCount() == 0, "explicit deselection failed");
        return true;
    } catch (const std::exception& ex) { error = ex.what(); return false; }
}
} // namespace MipsyncEngine
