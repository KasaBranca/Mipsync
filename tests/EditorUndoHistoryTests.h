#pragma once
#include "editor/EditorUndoStack.h"
#include "scene/Scene.h"
#include "assets/Material.h"
#include "assets/AssetManager.h"
#include "core/NativePaths.h"
#include "core/AssetFiles.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace MipsyncEngine {
inline bool RunEditorUndoHistoryTests(std::string& error) {
    namespace fs = std::filesystem;
    const auto root=fs::temp_directory_path()/ ("mipsync-history-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        fs::path path; std::string originalRoot;
        ~Cleanup() { AssetManager::Get().SetProjectRoot(originalRoot); try { NativePaths::RemoveTree(path); } catch (...) {} }
    } cleanup{root,AssetManager::Get().GetProjectRoot()};
    try {
        const auto require=[](bool ok,const char* message) { if (!ok) throw std::runtime_error(message); };
        fs::create_directories(root/"assets");
        AssetManager::Get().SetProjectRoot(PathUtf8::ToString(root));
        const auto write=[](const fs::path& path,const std::string& bytes) {
            fs::create_directories(path.parent_path()); std::ofstream file(path,std::ios::binary); file.write(bytes.data(),bytes.size());
        };
        const auto read=[](const fs::path& path) { std::ifstream file(path,std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()); };
        Scene scene;
        const auto id=scene.CreateEntity("Undo fixture")->GetID();
        const auto position=[&]() -> float& { return scene.FindEntity(id)->GetComponent<TransformComponent>()->position.x; };
        EditorUndoStack history;
        const auto begin=[&]() { require(history.BeginFrame(scene,error),"frame baseline failed"); };
        const auto end=[&](uint64_t gesture=0) { require(history.EndFrame(scene,gesture,error),"history commit failed"); };
        const auto track=[&](const fs::path& path,bool recursive=false) { require(history.TrackPath(PathUtf8::ToString(path),recursive,error),"asset capture failed"); };
        const auto undo=[&]() { require(history.Undo(scene,error),"Undo failed"); };
        const auto redo=[&]() { require(history.Redo(scene,error),"Redo failed"); };
        begin(); end(); require(!history.CanUndo(),"navigation created history");
        begin(); position()=1; history.PushState(scene); end();
        undo(); require(position()==0,"post-change callback lost scene baseline");
        begin(); end(); require(history.CanRedo(),"no-op frame cleared Redo");
        redo(); require(position()==1,"scene Redo failed");
        history.Clear();
        for (int i=2;i<=4;++i) { begin(); position()=float(i); end(42); }
        begin(); position()=5; end(); // release frame belongs to the same drag
        undo(); require(position()==1 && !history.CanUndo(),"drag split into several steps");
        redo(); require(position()==5,"drag Redo lost final value");
        begin(); position()=6; end(42); begin(); end();
        undo(); require(position()==5,"separate gesture with same ID merged");
        begin(); position()=7; end(); require(!history.CanRedo(),"new edit did not discard Redo branch");
        history.Clear();
        const auto material=root/"assets/paint.nmat";
        Material mat; mat.texturePath="assets/first.png";
        require(Material::Save(PathUtf8::ToString(material),mat,error),"fixture material save");
        const auto initial=read(material);
        begin(); track(material); mat.texturePath="assets/second.png"; mat.color={.1,.2,.3,1};
        require(Material::Save(PathUtf8::ToString(material),mat,error),"material update"); end();
        const auto modified=read(material);
        begin(); position()=8; end(); undo(); require(position()==7 && read(material)==modified,"mixed history order");
        undo(); require(read(material)==initial,"material texture and color not restored");
        redo(); require(read(material)==modified,"material Redo not restored"); redo(); require(position()==8,"mixed scene Redo");
        history.Clear();
        const auto folder=root/PathUtf8::FromString("assets/音源");
        const auto moved=root/"assets/renamed";
        const std::string binary("a\0b\xff",4);
        write(folder/"sound.bin",binary); fs::create_directories(folder/"empty");
        begin(); track(folder,true); track(moved,true); fs::rename(folder,moved); end();
        undo(); require(read(folder/"sound.bin")==binary && fs::is_directory(folder/"empty") && !fs::exists(moved),"folder rename Undo lost binary/empty directory");
        redo(); require(read(moved/"sound.bin")==binary && !fs::exists(folder),"folder move Redo failed");
        begin(); track(moved,true); NativePaths::RemoveTree(moved); end();
        undo(); require(read(moved/"sound.bin")==binary && fs::is_directory(moved/"empty"),"deleted folder not restored");
        redo(); require(!fs::exists(moved),"folder delete Redo failed");
        history.Clear();
        const auto created=root/"assets/new/deep/file.nanim";
        begin(); track(created); write(created,"keys"); end(); undo();
        require(!fs::exists(root/"assets/new"),"new parent directories survived Undo");
        redo(); require(read(created)=="keys","new asset Redo failed");
        write(created.parent_path()/"unrelated","outside edit");
        require(!history.Undo(scene,error) && history.CanUndo(),"unsafe restore succeeded or discarded history");
        require(read(created)=="keys" && read(created.parent_path()/"unrelated")=="outside edit","failed Undo did not roll back file changes");
        history.Clear();
        begin(); history.IgnorePreviewTransform(id); position()=42; end();
        require(!history.CanUndo(),"animation preview polluted history");
        // Play changes restore the edit scene, while project files and their
        // Undo/Redo steps persist without resurrecting runtime objects.
        EditorUndoStack playHistory;
        require(playHistory.BeginFrame(scene,error),"Play transaction begin");
        require(playHistory.TrackPath(PathUtf8::ToString(material),false,error),"Play asset capture");
        write(material,"play asset"); position()=99;
        scene.CreateEntity("Temporary Play object");
        require(playHistory.EndFrame(scene,0,error),"Play transaction end");
        scene.Clear(); auto* restored=scene.CreateEntity("Restored edit object");
        const auto restoredId=restored->GetID();
        restored->GetComponent<TransformComponent>()->position.x=12;
        require(history.AppendAssetHistory(playHistory,scene,error),"Play asset history transfer");
        require(read(material)=="play asset","transfer rewrote project file");
        require(history.Undo(scene,error) && read(material)==modified &&
                scene.GetEntities().size()==1 &&
                scene.FindEntity(restoredId)->GetComponent<TransformComponent>()->position.x==12,
                "asset Undo resurrected temporary Play scene");
        require(history.Redo(scene,error) && read(material)=="play asset" &&
                scene.GetEntities().size()==1,"asset Redo lost persisted Play edit");
        return true;
    } catch (const std::exception& ex) { error=ex.what(); return false; }
}
} // namespace MipsyncEngine
