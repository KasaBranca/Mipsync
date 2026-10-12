#include "editor/EditorComponentClipboard.h"
#include <cstdio>
#include <stdexcept>
using namespace MipsyncEngine;
static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class T>
static void CheckType(T source) {
    Entity destination(20, "Destination");
    source.enabled = false;
    EditorComponentClipboard clipboard;
    Require(clipboard.Copy(source), "component type was not copied");
    Component* existing = nullptr;
    for (Component* attached : destination.GetComponentsInOrder())
        if (clipboard.Matches(*attached)) existing = attached;
    if (existing) Require(!clipboard.CanPaste(destination), "existing mandatory component permitted background paste");
    Component* pasted = clipboard.Paste(destination, existing);
    Require(pasted && typeid(*pasted) == typeid(source) && !pasted->enabled &&
            pasted->entity == &destination, "type, enabled state or owner lost");
    const size_t count = destination.GetComponentsInOrder().size();
    pasted->enabled = true;
    Require(clipboard.Paste(destination, pasted) == pasted && !pasted->enabled &&
            destination.GetComponentsInOrder().size() == count, "same-type paste added instead of overwriting");
    Require(!clipboard.CanPaste(destination) && !clipboard.Paste(destination) &&
            destination.GetComponentsInOrder().size() == count,
            "background paste duplicated an existing component");
    Component* other = nullptr;
    if constexpr (std::is_same_v<T, AudioSourceComponent>) other = &destination.AddComponent<LightComponent>();
    else other = &destination.AddComponent<AudioSourceComponent>();
    Require(!clipboard.CanPaste(destination, other) && !clipboard.Paste(destination, other),
            "different-component paste duplicated an existing component");
}
int main() try {
    CheckType(TagComponent("Source"));
#define CHECK_COMPONENT(Type) CheckType(Type{})
    CHECK_COMPONENT(TransformComponent); CHECK_COMPONENT(SkinnedMeshRendererComponent);
    CHECK_COMPONENT(BoneComponent); CHECK_COMPONENT(AnimatorComponent);
    CHECK_COMPONENT(MeshRendererComponent); CHECK_COMPONENT(MeshSubdividerComponent);
    CHECK_COMPONENT(TerrainComponent); CHECK_COMPONENT(ProModelerComponent);
    CHECK_COMPONENT(CameraComponent); CHECK_COMPONENT(DistanceCullComponent);
    CHECK_COMPONENT(FrustumCullComponent); CHECK_COMPONENT(MipsScriptComponent);
    CHECK_COMPONENT(ColliderComponent); CHECK_COMPONENT(RigidbodyComponent);
    CHECK_COMPONENT(LightComponent); CHECK_COMPONENT(PostProcessVolumeComponent);
    CHECK_COMPONENT(AudioSourceComponent); CHECK_COMPONENT(CanvasComponent);
    CHECK_COMPONENT(RectTransformComponent); CHECK_COMPONENT(UIImageComponent);
    CHECK_COMPONENT(UITextComponent); CHECK_COMPONENT(UIButtonGroupComponent);
    CHECK_COMPONENT(UIButtonComponent); CHECK_COMPONENT(UIAudioSpectrumComponent);
#undef CHECK_COMPONENT
    EditorComponentClipboard clipboard;
    Entity source(1, "Source"), target(2, "Target");
    Require(!clipboard.Paste(target), "empty clipboard mutated target");
    auto& collider = source.AddComponent<ColliderComponent>();
    collider.shape = ColliderShape::Capsule; collider.radius = 1.75f;
    collider.center = {1, 2, 3}; collider.cameraTargetEntityId = 77;
    clipboard.Copy(collider); collider.radius = 5; source.RemoveComponent(&collider);
    auto& audio = target.AddComponent<AudioSourceComponent>(); audio.clipPath = "unchanged.wav";
    auto* copied = dynamic_cast<ColliderComponent*>(clipboard.Paste(target, &audio));
    Require(copied && copied->radius == 1.75f && copied->shape == ColliderShape::Capsule &&
            copied->center == glm::vec3(1,2,3) && copied->cameraTargetEntityId == 77 &&
            audio.clipPath == "unchanged.wav", "different-type paste or detached snapshot failed");
    Require(!clipboard.Paste(target, &audio), "existing collider duplicated through an unrelated header");
    target.RemoveComponent(copied);
    Require(clipboard.CanPaste(target, &audio) && clipboard.Paste(target, &audio),
            "removing the existing component did not re-enable new-component paste");
    auto& script = source.AddComponent<MipsScriptComponent>();
    script.scriptPath = "assets/scripts/FPS.mips"; script.fieldValues = {1, 2};
    script.fieldAssetPaths = {"audio/test.wav", ""}; clipboard.Copy(script);
    script.fieldValues[0] = 100; script.fieldAssetPaths[0].clear();
    auto& other = target.AddComponent<MipsScriptComponent>();
    other.scriptPath = "assets/scripts/Menu.mips"; other.fieldValues = {9};
    auto* newScript = dynamic_cast<MipsScriptComponent*>(clipboard.Paste(target, &other));
    Require(newScript && newScript != &other && newScript->fieldValues[0] == 1 &&
            newScript->fieldAssetPaths[0] == "audio/test.wav" && other.fieldValues[0] == 9,
            "different script should be a new component with frozen fields and references");
    newScript->scriptPath = "assets\\scripts\\FPS.mips";
    Require(clipboard.Matches(*newScript), "script path separator mismatch");
    clipboard.Paste(target, newScript); newScript->fieldValues[0] = 7;
    Require(!clipboard.CanPaste(target, &other) && !clipboard.Paste(target),
            "existing script permitted duplicate paste");
    Entity fresh(3, "Fresh");
    Require(!clipboard.CanPaste(fresh, newScript), "foreign-entity destination was accepted");
    auto* repeated = dynamic_cast<MipsScriptComponent*>(clipboard.Paste(fresh));
    Require(repeated && repeated->fieldValues[0] == 1, "paste mutated the clipboard");
    UIButtonComponent button; button.onClick.push_back({true, 42, "FPS.mips", "Jump"});
    clipboard.Copy(button); button.onClick[0].targetEntityId = 99;
    auto* pastedButton = dynamic_cast<UIButtonComponent*>(clipboard.Paste(target));
    Require(pastedButton && pastedButton->onClick[0].targetEntityId == 42,
            "event list was not independently copied");
    std::puts("All component types, detached snapshots, same/different type, script identity, references and repeat paste passed.");
    return 0;
} catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
