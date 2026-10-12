#pragma once
#include "editor/EditorPlaySession.h"
#include "editor/GameViewSettings.h"
#include "ui/UILayout.h"
#include "mips/PS1SceneExport.h"
#include "mips/MipsRuntime.h"
#include "assets/AssetManager.h"
#include "ps1/Ps1Output.h"
#include <fstream>
#include <stdexcept>

namespace MipsyncEngine {
inline bool RunEditorSceneSessionTests(std::string& error) {
    namespace fs = std::filesystem;
    const auto temp = fs::temp_directory_path() / ("mipsync-session-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        fs::path directory;
        std::string assetRoot;
        ~Cleanup() {
            AssetManager::Get().SetProjectRoot(assetRoot);
            try { NativePaths::RemoveTree(directory); } catch (...) {}
        }
    } cleanup{temp, AssetManager::Get().GetProjectRoot()};
    try {
        auto require = [](bool condition, const char* message) {
            if (!condition) throw std::runtime_error(message);
        };
        Scene origin;
        auto* canvas = origin.CreateEntity("Canvas");
        const uint32_t canvasId = canvas->GetID();
        canvas->AddComponent<CanvasComponent>();
        auto* group = origin.CreateEntity("Menu");
        const uint32_t groupId = group->GetID();
        group->AddComponent<UIButtonGroupComponent>(); // Also tests groups without a RectTransform.
        origin.SetParent(group, canvas);
        auto* button = origin.CreateEntity("Start");
        const uint32_t buttonId = button->GetID();
        button->AddComponent<RectTransformComponent>();
        button->AddComponent<UIButtonComponent>();
        origin.SetParent(button, group);
        auto* overlay = origin.CreateEntity("Loading Overlay"); // Created before logo.
        const uint32_t overlayId = overlay->GetID();
        overlay->AddComponent<RectTransformComponent>();
        overlay->AddComponent<UIImageComponent>().enabled = false;
        origin.SetParent(overlay, canvas);
        auto* logo = origin.CreateEntity("Logo");
        const uint32_t logoId = logo->GetID();
        logo->AddComponent<RectTransformComponent>();
        logo->AddComponent<UIImageComponent>();
        origin.SetParent(logo, canvas);
        require(origin.ReorderEntity(overlay, canvas, nullptr, false), "overlay reorder failed");
        std::vector<uint32_t> draws;
        VisitCanvasUI(origin, *canvas, *canvas->GetComponent<CanvasComponent>(), {},
            [&](Entity& e, const UIRect&) { draws.push_back(e.GetID()); },
            [&](Entity& e, const UIRect&) { if (e.GetID() == groupId) draws.push_back(groupId); });
        require(draws == std::vector<uint32_t>{buttonId, groupId, logoId, overlayId},
                "cursor/overlay traversal order incorrect");
        origin.FindEntity(logoId)->GetComponent<TransformComponent>()->position.x = 37.5f;
        std::string expected;
        require(SceneIO::SerializeSceneFingerprint(origin, expected, error), "origin serialize failed");
        EditorPlaySession session;
        require(session.Begin(origin, "scenes/Unsaved.nscene", error), "session capture failed");
        Scene runtime;
        runtime.CreateEntity("Destination one");
        session.MarkTransition();
        require(session.Begin(runtime, "scenes/One.nscene", error), "transition recapture failed");
        runtime.Clear(); runtime.CreateEntity("Destination two"); session.MarkTransition();
        require(session.OriginalPath() == "scenes/Unsaved.nscene", "transition overwrote original path");
        require(session.Restore(runtime, error), "session restore failed");
        std::string restored;
        require(SceneIO::SerializeSceneFingerprint(runtime, restored, error) && restored == expected,
                "multiple transitions lost unsaved values or sibling order");
        session.Reset(); require(!session.Active(), "session reset failed");
        require(session.Begin(runtime, "", error), "untitled scene capture failed");
        runtime.DestroyEntity(runtime.FindEntity(logoId));
        runtime.FindEntity(buttonId)->GetComponent<UIButtonComponent>()->label = "Play edit";
        runtime.FindEntity(buttonId)->AddComponent<ColliderComponent>().radius = 3.25f;
        runtime.FindEntity(overlayId)->GetComponent<UIImageComponent>()->enabled = true;
        runtime.CreateEntity("Temporary runtime object");
        require(session.Restore(runtime, error), "non-transition stop failed");
        require(SceneIO::SerializeSceneFingerprint(runtime, restored, error) && restored == expected,
                "stop without a scene transition retained runtime edits, additions or deletions");
        require(session.OriginalPath().empty(), "untitled path changed");
        GameViewSettings gameView;
        gameView.playMode = GameViewPlayMode::Maximized;
        require(!gameView.IsMaximized(false) && gameView.IsMaximized(true) &&
                !gameView.IsMaximized(true, true) && gameView.IsMaximized(true, false),
                "maximized Game View did not restore/resume around Pause");
        gameView.playMode = GameViewPlayMode::Focused;
        require(!gameView.IsMaximized(true) && !gameView.IsMaximized(true, true),
                "focused Game View became maximized");

        fs::create_directories(temp / "assets/scripts");
        std::ofstream(temp / "assets/scripts/Live.mips") << R"(
            class Live : MipsBehaviour {
                public float speed = 2;
                void Start() { transform.position.x = 10; }
                void Update() { transform.position.x = transform.position.x + speed * Time.deltaTime; }
            })";
        AssetManager::Get().SetProjectRoot(temp.string());
        Scene liveScene;
        auto* liveEntity = liveScene.CreateEntity("Live script");
        auto& liveScript = liveEntity->AddComponent<MipsScriptComponent>();
        liveScript.scriptPath = "assets/scripts/Live.mips";
        Mips::MipsRuntime live;
        live.OnPlayStarted(liveScene);
        require(liveEntity->GetComponent<TransformComponent>()->position.x == 10,
                "initial Start did not execute");
        std::vector<std::string> scriptErrors;
        require(Mips::MipsRuntime::EnsureScriptReady(liveScript, scriptErrors), "live script failed to compile");
        liveScript.fieldValues[0] = 5;
        live.ApplyFieldOverrides(liveEntity, liveScript);
        live.Update(liveScene, 1);
        require(liveEntity->GetComponent<TransformComponent>()->position.x == 15,
                "Play field override did not affect running script");
        liveScript.enabled = false; live.Update(liveScene, 1);
        require(liveEntity->GetComponent<TransformComponent>()->position.x == 15,
                "disabled Play script continued updating");
        liveScript.enabled = true;
        liveEntity->RemoveComponent(&liveScript); live.Update(liveScene, 1);
        auto& addedScript = liveEntity->AddComponent<MipsScriptComponent>();
        addedScript.scriptPath = "assets/scripts/Live.mips";
        live.Update(liveScene, 1);
        require(liveEntity->GetComponent<TransformComponent>()->position.x == 12,
                "script added during Play did not initialize and update");
        liveScene.DestroyEntity(liveEntity); live.Update(liveScene, 1);
        live.OnPlayStopped(liveScene);

        fs::create_directories(temp / "generated");
        require(SceneIO::SaveToFile(origin, (temp / "Title.nscene").string(), error), "fixture save failed");
        BuildManifest manifest; manifest.scenesInBuild = {"Title.nscene"};
        Mips::Ps1SceneExportResult exported;
        require(Mips::ExportPs1SceneAndScripts(temp.string(), manifest, (temp / "generated").string(), exported, error),
                "UI PS1 export failed");
        require(exported.uiElements.size() == 3 && exported.uiElements[0].entityId == buttonId &&
                exported.uiElements[1].entityId == logoId && exported.uiElements[2].entityId == overlayId &&
                !exported.uiElements[2].visible, "PS1 ignored saved sibling order or hidden overlay");
        require(exported.uiButtonGroups.size() == 1 && exported.uiButtonGroups[0].cursorUiOffset == 1,
                "PS1 cursor drawn after overlay");
        require(Ps1::SelectArtifactDirectory(temp) == temp / "out", "empty output was redirected");
#ifdef _WIN32
        fs::create_directories(temp / "out");
        std::ofstream(temp / "out" / "game.bin") << "mounted disc";
        HANDLE mounted = CreateFileW(NativePaths::FilePath(temp / "out" / "game.bin").c_str(),
            GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        require(mounted != INVALID_HANDLE_VALUE, "could not mount test image");
        const auto alternate = Ps1::SelectArtifactDirectory(temp);
        CloseHandle(mounted);
        require(alternate != temp / "out" && alternate.parent_path() == temp &&
                !fs::exists(alternate), "mounted output was overwritten or redirected outside build root");
        require(Ps1::SelectArtifactDirectory(temp) == temp / "out", "unmounted output still redirected");
#endif
        return true;
    } catch (const std::exception& ex) {
        error = std::string("editor scene/session regression: ") + ex.what();
        return false;
    }
}
} // namespace MipsyncEngine
