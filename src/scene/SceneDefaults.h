#pragma once

#include "Scene.h"

namespace MipsyncEngine {

/// Sun light and global Post Process Volume every new scene starts with:
/// a Directional Light (same defaults as Create > Directional Light) and a
/// procedural gradient sky whose distance fog fades into the horizon colour.
inline void AddDefaultSceneEnvironment(Scene& scene) {
    Entity* light = scene.CreateEntity("Directional Light");
    if (auto* transform = light->GetComponent<TransformComponent>()) {
        transform->position = { 0.0f, 5.0f, 0.0f };
        transform->rotation = { -50.0f, 30.0f, 0.0f };
    }
    auto& sun = light->AddComponent<LightComponent>();
    sun.type = LightType::Directional;
    sun.color = { 1.0f, 0.96f, 0.9f };
    sun.range = 0.0f;

    Entity* volumeEntity = scene.CreateEntity("Post Process Volume");
    auto& volume = volumeEntity->AddComponent<PostProcessVolumeComponent>();
    volume.isGlobal = true;
    volume.skyboxEnabled = true;
    volume.fogEnabled = true;
    volume.fogColor = volume.skyHorizonColor;
    volume.fogStart = 15.0f;
    volume.fogEnd = 60.0f;
}

/// Contents of a scene created from the editor (New Scene / Create > Scene).
inline void PopulateNewScene(Scene& scene) {
    Entity* camera = scene.CreateEntity("Main Camera");
    auto& cameraComponent = camera->AddComponent<CameraComponent>();
    cameraComponent.camera.fov = 60.0f;
    cameraComponent.camera.nearClip = 0.1f;
    cameraComponent.camera.farClip = 100.0f;
    cameraComponent.primary = true;
    camera->GetComponent<TransformComponent>()->position = { 0.0f, 2.0f, 6.0f };
    AddDefaultSceneEnvironment(scene);
}

} // namespace MipsyncEngine
