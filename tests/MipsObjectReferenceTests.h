#pragma once
#include "mips/MipsRuntime.h"
#include "scene/Scene.h"
#include "assets/AssetManager.h"
#include "core/NativePaths.h"
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <cmath>
namespace MipsyncEngine {
inline bool RunMipsObjectReferenceTests(std::string& error) {
    namespace fs=std::filesystem;
    auto root=fs::temp_directory_path()/("mipsync-object-ref-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {fs::path p;std::string old;~Cleanup(){AssetManager::Get().SetProjectRoot(old);try{NativePaths::RemoveTree(p);}catch(...){}}} cleanup{root,AssetManager::Get().GetProjectRoot()};
    try {
        auto check=[](bool ok,const char* msg){if(!ok)throw std::runtime_error(msg);};
        fs::create_directories(root);AssetManager::Get().SetProjectRoot(PathUtf8::ToString(root));
        fs::copy_file(fs::path(__FILE__).parent_path()/"mips/ObjectReferences.mips",root/"ObjectReferences.mips");
        std::ofstream(root/"Health.mips")<<"class Health : MipsBehaviour { public float hp = 7; public float x = 3; }";
        Scene scene;auto* a=scene.CreateEntityWithId(1,"Owner");auto* b=scene.CreateEntityWithId(2,"Target");
        auto* p=scene.CreateEntityWithId(10,"Parent");p->GetComponent<TransformComponent>()->position.x=10;
        scene.SetParent(a,p);scene.SetParent(b,p);b->GetComponent<TransformComponent>()->position={3,4,0};
        scene.CreateEntityWithId(3,"Target")->SetActive(false); // duplicate names return the first match
        auto& script=a->AddComponent<MipsScriptComponent>();script.scriptPath="ObjectReferences.mips";
        b->AddComponent<MipsScriptComponent>().scriptPath="Health.mips";
        std::vector<std::string> errors;check(Mips::MipsRuntime::EnsureScriptReady(script,errors),"reference fixture compile failed");
        for(const char* field:{"target","targetTransform"})script.fieldValues[script.module->FindFieldIndex(field)]=2;
        Mips::MipsRuntime runtime;runtime.OnPlayStarted(scene);
        auto fields=runtime.InspectFields(script)["fields"];
        check((fields["referencesOK"]==true || fields["referencesOK"]==1) && fields["health"]==7,
            ("object lookup, null equality, name or targeted GetComponent failed: "+fields.dump()).c_str());
        check(std::abs(fields["before"].get<double>()-5)<0.0001 && std::abs(fields["after"].get<double>()-10)<0.0001,"world-space distance or other-object transform assignment failed");
        runtime.PhysicsEvents().Push({MipsPhysicsEvent::Kind::TriggerEnter,1,2});runtime.DispatchPhysicsEvents(scene);
        runtime.Update(scene,1.f/60);
        const auto cleared=runtime.InspectFields(script)["fields"]["cleared"];
        check(!scene.FindEntity(2) && (cleared==true || cleared==1),"destroyed Inspector/cached reference did not become null");
        runtime.OnPlayStopped(scene);return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
}
