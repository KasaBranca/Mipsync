#include "PlayerBuild.h"
#include "BuildManifest.h"
#include "../assets/AssetManager.h"
#include "../core/AssetFiles.h"
#include "../core/Log.h"
#include "../core/NativePaths.h"
#include "../mips/MipsScriptLoader.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace MipsyncEngine {
namespace {
namespace fs=std::filesystem;
using Json=nlohmann::json;
std::string ProductName(std::string name) {
    for(char& c:name) if(static_cast<unsigned char>(c)<32 || std::string("/\\:*?\"<>|").find(c)!=std::string::npos)c='_';
    while(!name.empty() && (name.back()=='.' || name.back()==' '))name.pop_back();
    return name.empty()?"Game":name;
}
bool RuntimeAsset(const fs::path& path) {
    static const std::set<std::string> extensions={".fbx",".nmodel",".obj",".mtl",".gltf",".glb",".bin",".png",".jpg",".jpeg",".bmp",".tga",".hdr",".wav",".mp3",".ogg",".flac",".vag",".mid",".midi",".nmat",".nanim",".ncontroller",".nprefab",".prefab",".mips",".ttf",".otf"};
    auto ext=PathUtf8::ToString(path.extension());
    std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return std::tolower(c);});
    return extensions.count(ext)!=0;
}
fs::path ConfinedFile(const fs::path& root,const fs::path& relative) {
    if(relative.is_absolute() || relative.has_root_name())throw std::runtime_error("Player assets must be project-relative");
    for(const auto& part:relative)if(part=="..")throw std::runtime_error("Player asset path escapes project");
    auto actual=NativePaths::Canonical(root/relative),rel=actual.lexically_relative(root);
    if(rel.empty() || rel.is_absolute())throw std::runtime_error("Invalid player asset path");
    for(const auto& part:rel)if(part=="..")throw std::runtime_error("Linked player asset escapes project");
    return actual;
}
void CopyOptional(const fs::path& source,const fs::path& dest) {
    if(fs::exists(source))NativePaths::CopyTree(source,dest);
}
AssetFiles::Bytes TextBytes(const Json& j) {auto s=j.dump();return {s.begin(),s.end()};}
AssetFiles::Entries Collect(const fs::path& root,const PlayerSettings& settings) {
    AssetFiles::Entries files;
    auto add=[&](const fs::path& relative) {
        auto source=ConfinedFile(root,relative);
        auto name=PathUtf8::ToString(relative.lexically_normal());std::replace(name.begin(),name.end(),'\\','/');
        auto bytes=AssetFiles::Read(source);
        auto extension=PathUtf8::ToString(source.extension());
        std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return std::tolower(c);});
        if(extension==".mips") {
            std::vector<std::string> errors;auto module=Mips::MipsScriptLoader::CompileFile(root,name,errors);
            if(!module || !errors.empty())throw std::runtime_error("Player script compilation failed: "+name+(errors.empty()?"":"\n"+errors.front()));
            bytes=Mips::SerializePlayerModule(*module);
        }
        files.emplace("Project/"+name,std::move(bytes));
    };
    for(const auto& scene:settings.scenesInBuild) {
        auto relative=PathUtf8::FromString(scene);
        if(relative.extension()!=".nscene")throw std::runtime_error("Build scene must be an .nscene file");
        add(relative);
    }
    // Include runtime formats, including assets selected dynamically by scripts.
    // Exclude authoring files, caches, installers and unrelated project contents.
    for(const char* folder:{"assets","scripts"}) {
        if(!fs::is_directory(NativePaths::FilePath(root/folder)))continue;
        for(auto it=fs::recursive_directory_iterator(NativePaths::FilePath(root/folder));it!=fs::recursive_directory_iterator();++it) {
            auto filename=PathUtf8::ToString(it->path().filename());
            if(it->is_directory()) {if(!filename.empty() && filename.front()=='.')it.disable_recursion_pending();continue;}
            if(it->is_regular_file() && RuntimeAsset(it->path()))add(NativePaths::OrdinaryPath(it->path()).lexically_relative(root));
        }
    }
    Json boot={{"version",2},{"productName",settings.productName},{"companyName",settings.companyName},{"startupSceneIndex",settings.startupSceneIndex},{"scenesInBuild",settings.scenesInBuild}};
    files.emplace("boot.json",TextBytes(boot));
    Json project={{"name",settings.productName},{"defaultScene",BuildManifestIO::FromPlayerSettings(settings).StartupScenePath()},{"playerSettings",boot}};
    files.emplace("Project/nostalty.project",TextBytes(project));return files;
}
}
std::string PlayerBuild::DefaultOutputParent(const std::string& path) {return PathUtf8::ToString(PathUtf8::FromString(path)/"Builds"/"Windows");}
std::string PlayerBuild::GetRunningEngineDirectory() {
#ifdef _WIN32
    wchar_t path[32768]{};if(!GetModuleFileNameW(nullptr,path,32768))return {};
    return PathUtf8::ToString(fs::path(path).parent_path());
#else
    return PathUtf8::ToString(fs::current_path());
#endif
}
PlayerBuildResult PlayerBuild::BuildWindows(const PlayerBuildRequest& request) {
    PlayerBuildResult result;fs::path staging;
    auto progress=[&](float f,const char* stage){if(request.progress)request.progress(f,stage);};
    try {
        progress(.03f,"Validating protected PC build");auto settings=request.settings;settings.productName=ProductName(settings.productName);
        if(settings.scenesInBuild.empty())throw std::runtime_error("No scenes in build list. Add scenes in Build Settings.");
        if(settings.startupSceneIndex<0 || settings.startupSceneIndex>=static_cast<int>(settings.scenesInBuild.size()))throw std::runtime_error("Invalid startup scene index");
        auto root=NativePaths::Canonical(PathUtf8::FromString(request.projectPath));
        auto engineDir=PathUtf8::FromString(request.engineDirectory.empty()?GetRunningEngineDirectory():request.engineDirectory);
        auto parent=fs::absolute(PathUtf8::FromString(request.outputParent.empty()?DefaultOutputParent(request.projectPath):request.outputParent)).lexically_normal();
        auto output=parent/PathUtf8::FromString(settings.productName);
        if(output==root || fs::exists(NativePaths::FilePath(output/"nostalty.project")) || fs::exists(NativePaths::FilePath(output/"MipsyncEngine.exe")))throw std::runtime_error("Output contains a project or editor installation");
        if(fs::exists(NativePaths::FilePath(output)) && (!fs::is_directory(NativePaths::FilePath(output/PathUtf8::FromString(settings.productName+"_Data"))) || !fs::is_regular_file(NativePaths::FilePath(output/PathUtf8::FromString(settings.productName+".exe")))))
            throw std::runtime_error("Existing output is not a generated player build; choose another output folder");
        auto key=AssetFiles::RandomKey();std::string suffix;const char* hex="0123456789abcdef";
        for(size_t i=0;i<8;++i){suffix+=hex[key[i]>>4];suffix+=hex[key[i]&15];}
        staging=parent/PathUtf8::FromString(settings.productName+".building-"+suffix);fs::create_directories(NativePaths::FilePath(staging));
        auto data=staging/PathUtf8::FromString(settings.productName+"_Data");fs::create_directories(NativePaths::FilePath(data));
        progress(.2f,"Compiling scripts and collecting runtime assets");auto files=Collect(root,settings);
        progress(.5f,"Encrypting player data");std::string error;
        if(!AssetFiles::WritePack(data/"game.mpak",files,key,error))throw std::runtime_error(error);files.clear();
        auto exe=staging/PathUtf8::FromString(settings.productName+".exe");NativePaths::CopyRegularFile(engineDir/"MipsyncEngine.exe",exe);
        if(!AssetFiles::BindPlayerKey(exe,key,error))throw std::runtime_error(error);
        progress(.8f,"Packaging runtime libraries");
        for(const auto& entry:fs::directory_iterator(engineDir)) {
            auto ext=PathUtf8::ToString(entry.path().extension());
            if(entry.is_regular_file() && (ext==".dll" || ext==".DLL"))NativePaths::CopyRegularFile(entry.path(),staging/entry.path().filename());
        }
        CopyOptional(engineDir/"resources",data/"Resources");
        std::ofstream launcher(NativePaths::FilePath(staging/"Play.bat"),std::ios::binary);
        launcher<<"@echo off\r\ncd /d \"%~dp0\"\r\n\""<<settings.productName<<".exe\" --player --data \""<<settings.productName<<"_Data\"\r\n";
        launcher.close();if(!launcher)throw std::runtime_error("Cannot write player launcher");
        // Replace complete generated output, including old loose Project files.
        auto backup=parent/PathUtf8::FromString(settings.productName+".previous-"+suffix);
        if(fs::exists(NativePaths::FilePath(output)))fs::rename(NativePaths::FilePath(output),NativePaths::FilePath(backup));
        try{fs::rename(NativePaths::FilePath(staging),NativePaths::FilePath(output));}catch(...){if(fs::exists(NativePaths::FilePath(backup)))fs::rename(NativePaths::FilePath(backup),NativePaths::FilePath(output));throw;}
        staging.clear();if(fs::exists(NativePaths::FilePath(backup)))NativePaths::RemoveTree(backup);
        result.success=true;result.outputDirectory=PathUtf8::ToString(output);
        result.executablePath=PathUtf8::ToString(output/PathUtf8::FromString(settings.productName+".exe"));
        result.message="Protected PC build succeeded: "+result.outputDirectory;
        progress(1.f,"Protected PC build complete");MIPSYNC_INFO("Player build: {}",result.message);
    }catch(const std::exception& e){result.message=e.what();if(!staging.empty()){try{NativePaths::RemoveTree(staging);}catch(...){}}}
    return result;
}
}
