#pragma once
#include "mips/MipsRuntime.h"
#include "mips/PS1Export.h"
#include "scene/Scene.h"
#include "scene/SceneIO.h"
#include "core/Input.h"
#include "core/NativePaths.h"
#include "physics/PhysicsWorld.h"
#include <chrono>
#include <fstream>
#include <stdexcept>

namespace MipsyncEngine {
inline bool RunFPSControllerInputTests(std::string& error) {
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/("mipsync-fps-input-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        fs::path path; std::string original; bool enabled;
        ~Cleanup() { Input::EndGameFrame(); Input::SetPS1PadMapping({}); Input::SetGameInputEnabled(enabled);
            AssetManager::Get().SetProjectRoot(original); try { NativePaths::RemoveTree(path); } catch (...) {} }
    } cleanup{root,AssetManager::Get().GetProjectRoot(),Input::IsGameInputEnabled()};
    try {
        // Compile the actual shipped source, rather than a copy of its look calculation.
        std::ifstream source(fs::path(__FILE__).parent_path().parent_path()/"src/core/Engine.cpp");
        const std::string text{std::istreambuf_iterator<char>(source),std::istreambuf_iterator<char>()};
        const std::string marker="kFpsScript = R\"(";
        const auto at=text.find(marker);
        if (at==std::string::npos) throw std::runtime_error("shipped FPS source missing");
        const auto start=at+marker.size(), end=text.find(")\";",start);
        if (end==std::string::npos) throw std::runtime_error("shipped FPS source terminator missing");
        fs::create_directories(root);
        { std::ofstream file(root/"FPS.mips"); file<<text.substr(start,end-start); }
        std::vector<std::string> compileErrors;
        auto module=Mips::MipsRuntime::CompileScriptFile(PathUtf8::ToString(root/"FPS.mips"),compileErrors);
        std::string compatibilityError;
        if (!module || !Mips::ValidatePs1Target({*module},1,compatibilityError))
            throw std::runtime_error("shipped FPS is not PS1 compatible: "+compatibilityError);
        AssetManager::Get().SetProjectRoot(PathUtf8::ToString(root));
        Input::SetGameInputEnabled(true);
        for (bool childCamera : {false, true})
        for (int lookCase=0; lookCase<5; ++lookCase) for (int samplesPerTick : {1,4}) {
            Input::SetPS1PadMapping({});
            Scene scene; auto* entity=scene.CreateEntity("FPS input");
            Entity* view=entity;
            if (childCamera) {
                view=scene.CreateEntity("Camera"); scene.SetParent(view,entity);
                view->GetComponent<TransformComponent>()->position={0,1.7f,0};
            }
            view->AddComponent<CameraComponent>().primary=true;
            auto& script=entity->AddComponent<MipsScriptComponent>(); script.scriptPath="FPS.mips";
            std::vector<std::string> errors;
            if (!Mips::MipsRuntime::EnsureScriptReady(script,errors)) throw std::runtime_error("shipped FPS compile failed");
            if (childCamera) {
                const int field=script.module->FindFieldIndex("viewCamera");
                if (field<0) throw std::runtime_error("shipped FPS camera reference missing");
                script.fieldValues[static_cast<size_t>(field)]=view->GetID();
            }
            PhysicsWorld physics;
            Mips::MipsRuntime runtime; runtime.SetPhysicsWorld(&physics); runtime.OnPlayStarted(scene);
            physics.BeginPlay(scene);
            constexpr int sampleCount=24;
            for (int sampleIndex=0; sampleIndex<sampleCount; ++sampleIndex) {
                PS1PadSample sample; sample.seconds=1.f/60;
                if (lookCase==0) sample.mouse={1,-1};
                if (lookCase==1) sample.physicalAxes={0,0,.5f,.5f};
                if (lookCase==2) sample.mouse={1000,-500};
                if (lookCase==3) sample.physicalAxes={0,0,1,1};
                if (lookCase==4) sample.physicalButtons=(1u<<7)|(1u<<3); // R1 + Triangle
                Input::SamplePS1Pad(sample);
                if ((sampleIndex+1)%samplesPerTick) continue;
                const float dt=samplesPerTick/60.f;
                Input::BeginGameFrame(samplesPerTick>1);
                runtime.Update(scene,dt); physics.Simulate(scene,dt,dt); runtime.LateUpdate(scene,dt);
                Input::EndGameFrame();
            }
            const auto rotation=entity->GetComponent<TransformComponent>()->rotation;
            const auto viewRotation=view->GetComponent<TransformComponent>()->rotation;
            float expectedX=.18f, expectedY=.18f;
            if (lookCase==1) expectedX=expectedY=PS1PadInput::RemapAxis(.5f,.125f);
            if (lookCase==2) { expectedX=2/std::sqrt(5.f); expectedY=1/std::sqrt(5.f); }
            if (lookCase>=3) expectedX=expectedY=1/std::sqrt(2.f);
            const float degrees=144*sampleCount/60.f;
            if (std::abs(rotation.y+expectedX*degrees)>.01f || std::abs(viewRotation.x-expectedY*degrees)>.01f ||
                (childCamera && (std::abs(rotation.x)>.001f || std::abs(viewRotation.y)>.001f)))
                throw std::runtime_error("right-stick FPS look failed at "+std::to_string(60/samplesPerTick)+" FPS");
            PS1PadSample idle; idle.seconds=1.f/60; Input::SamplePS1Pad(idle);
            Input::BeginGameFrame(true); runtime.Update(scene,1.f/60); Input::EndGameFrame();
            if (glm::length(entity->GetComponent<TransformComponent>()->rotation-rotation)>.001f ||
                glm::length(view->GetComponent<TransformComponent>()->rotation-viewRotation)>.001f)
                throw std::runtime_error("FPS keeps looking after right stick stops");
            if (childCamera && glm::length(scene.GetWorldPosition(*view)-
                scene.GetWorldPosition(*entity)-glm::vec3(0,1.7f,0))>.001f)
                throw std::runtime_error("child FPS camera does not follow the body at eye height");
            physics.EndPlay(); runtime.OnPlayStopped(scene);
        }
        {
            Scene scene;
            auto* body=scene.CreateEntity("Body");
            auto* camera=scene.CreateEntity("Camera"); scene.SetParent(camera,body);
            camera->AddComponent<CameraComponent>().primary=true;
            camera->GetComponent<TransformComponent>()->position={0,1.7f,0};
            camera->GetComponent<TransformComponent>()->rotation.x=10;
            auto& script=body->AddComponent<MipsScriptComponent>(); script.scriptPath="FPS.mips";
            std::vector<std::string> errors;
            if (!Mips::MipsRuntime::EnsureScriptReady(script,errors)) throw std::runtime_error("FPS setup failed");
            const size_t viewField=static_cast<size_t>(script.module->FindFieldIndex("viewCamera"));
            script.fieldValues.at(viewField)=camera->GetID();
            auto* copy=scene.DuplicateEntity(*body);
            const auto copiedCamera=copy->GetChildIDs().at(0);
            if (copy->GetComponent<MipsScriptComponent>()->fieldValues.at(viewField)!=copiedCamera)
                throw std::runtime_error("duplicated FPS still references the original camera");
            const auto prefab=root/"controller.nprefab";
            std::string ioError;
            if (!SceneIO::SaveEntityToFile(*body,scene,PathUtf8::ToString(prefab),ioError))
                throw std::runtime_error("FPS prefab save failed: "+ioError);
            auto* instance=SceneIO::InstantiateFromFile(scene,PathUtf8::ToString(prefab),ioError);
            if (!instance || instance->GetComponent<MipsScriptComponent>()->fieldValues.at(viewField)!=instance->GetChildIDs().at(0))
                throw std::runtime_error("FPS prefab still references the original camera");
            copy->SetActive(false); instance->SetActive(false);
            Input::SetPS1PadMapping({});
            PhysicsWorld physics; Mips::MipsRuntime runtime; runtime.SetPhysicsWorld(&physics); runtime.OnPlayStarted(scene);
            PS1PadSample sample; sample.seconds=1.f/60; sample.physicalAxes={0,0,0,1};
            Input::SamplePS1Pad(sample); Input::BeginGameFrame(false); runtime.Update(scene,1.f/60); Input::EndGameFrame();
            if (std::abs(camera->GetComponent<TransformComponent>()->rotation.x-12.4f)>.01f ||
                body->GetComponent<TransformComponent>()->rotation.x!=0)
                throw std::runtime_error("FPS lost authored camera pitch or tilted the body");
            Input::SamplePS1Pad(sample); Input::BeginGameFrame(false); runtime.Update(scene,2); Input::EndGameFrame();
            if (camera->GetComponent<TransformComponent>()->rotation.x!=85)
                throw std::runtime_error("child camera pitch clamp failed");
            runtime.OnPlayStopped(scene);
        }
        return true;
    } catch (const std::exception& e) { error=e.what(); return false; }
}
}
