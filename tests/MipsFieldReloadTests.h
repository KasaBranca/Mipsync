#pragma once
#include "mips/MipsRuntime.h"
#include "scene/Scene.h"
#include "assets/AssetManager.h"
#include "core/NativePaths.h"
#include <chrono>
#include <fstream>
#include <stdexcept>

namespace MipsyncEngine {
inline bool RunMipsFieldReloadTests(std::string& error) {
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/("mipsync-field-reload-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        fs::path path; std::string original;
        ~Cleanup() { AssetManager::Get().SetProjectRoot(original); try { NativePaths::RemoveTree(path); } catch (...) {} }
    } cleanup{root,AssetManager::Get().GetProjectRoot()};
    try {
        fs::create_directories(root);
        AssetManager::Get().SetProjectRoot(PathUtf8::ToString(root));
        const auto path=root/"Reload.mips";
        { std::ofstream file(path); file<<"class Reload : MipsBehaviour { public float moveSpeed=5; public float mouseSensitivity=0.15; public AudioClip clip; }"; }
        Scene scene; auto* entity=scene.CreateEntity("FPS reload");
        auto& script=entity->AddComponent<MipsScriptComponent>(); script.scriptPath="Reload.mips";
        std::vector<std::string> errors;
        if (!Mips::MipsRuntime::EnsureScriptReady(script,errors)) throw std::runtime_error("reload fixture did not compile");
        script.fieldValues[0]=9; script.fieldValues[1]=.03;
        script.fieldAssetPaths[2]="assets/sound.wav";
        Mips::MipsRuntime runtime; runtime.ReloadChangedScripts(scene);
        const auto before=fs::last_write_time(path);
        { std::ofstream file(path); file<<"class Reload : MipsBehaviour { public AudioClip clip; public float lookSpeed=144; public float moveSpeed=5; public float added=7; }"; }
        fs::last_write_time(path,before+std::chrono::seconds(2));
        runtime.ReloadChangedScripts(scene);
        if (script.fieldValues.size()!=4 || script.fieldValues[1]!=144 || script.fieldValues[2]!=9 ||
            script.fieldValues[3]!=7 || script.fieldAssetPaths[0]!="assets/sound.wav" ||
                !script.fieldAssetPaths[1].empty()) throw std::runtime_error("renamed/reordered script fields inherited unrelated values");
        script.fieldValues[1]=28.8;
        runtime.ResetScriptsByFileName(scene,"Reload.mips");
        if (script.fieldValues[1]!=28.8 || script.fieldValues[2]!=9 || script.fieldAssetPaths[0]!="assets/sound.wav")
            throw std::runtime_error("builtin script upgrade discarded inspector tuning");
        return true;
    } catch (const std::exception& e) { error=e.what(); return false; }
}
}
