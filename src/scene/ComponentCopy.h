#pragma once
#include "Scene.h"
#include <type_traits>

namespace MipsyncEngine {

// Keep type dispatch shared between value snapshots, assignment and insertion.
// Standard value copies detach vectors/maps/strings while retaining asset refs.
template<class Visitor>
bool VisitComponentValue(const Component& source, Visitor&& visitor) {
#define MIPSYNC_VISIT_COMPONENT(Type) \
    if (const auto* typed = dynamic_cast<const Type*>(&source)) { visitor(*typed); return true; }
    MIPSYNC_VISIT_COMPONENT(TagComponent)
    MIPSYNC_VISIT_COMPONENT(TransformComponent)
    MIPSYNC_VISIT_COMPONENT(SkinnedMeshRendererComponent)
    MIPSYNC_VISIT_COMPONENT(BoneComponent)
    MIPSYNC_VISIT_COMPONENT(AnimatorComponent)
    MIPSYNC_VISIT_COMPONENT(MeshRendererComponent)
    MIPSYNC_VISIT_COMPONENT(MeshSubdividerComponent)
    MIPSYNC_VISIT_COMPONENT(TerrainComponent)
    MIPSYNC_VISIT_COMPONENT(ProModelerComponent)
    MIPSYNC_VISIT_COMPONENT(CameraComponent)
    MIPSYNC_VISIT_COMPONENT(DistanceCullComponent)
    MIPSYNC_VISIT_COMPONENT(FrustumCullComponent)
    MIPSYNC_VISIT_COMPONENT(MipsScriptComponent)
    MIPSYNC_VISIT_COMPONENT(ColliderComponent)
    MIPSYNC_VISIT_COMPONENT(RigidbodyComponent)
    MIPSYNC_VISIT_COMPONENT(LightComponent)
    MIPSYNC_VISIT_COMPONENT(PostProcessVolumeComponent)
    MIPSYNC_VISIT_COMPONENT(AudioSourceComponent)
    MIPSYNC_VISIT_COMPONENT(CanvasComponent)
    MIPSYNC_VISIT_COMPONENT(RectTransformComponent)
    MIPSYNC_VISIT_COMPONENT(UIImageComponent)
    MIPSYNC_VISIT_COMPONENT(UITextComponent)
    MIPSYNC_VISIT_COMPONENT(UIButtonGroupComponent)
    MIPSYNC_VISIT_COMPONENT(UIButtonComponent)
    MIPSYNC_VISIT_COMPONENT(UIAudioSpectrumComponent)
#undef MIPSYNC_VISIT_COMPONENT
    return false;
}

inline std::unique_ptr<Component> CloneComponentValue(const Component& source) {
    std::unique_ptr<Component> result;
    VisitComponentValue(source, [&](const auto& typed) {
        using T = std::remove_cvref_t<decltype(typed)>;
        result = std::make_unique<T>(typed);
    });
    if (result) result->entity = nullptr;
    return result;
}

inline std::unique_ptr<Component> CreateDefaultComponentValue(const Component& source) {
    std::unique_ptr<Component> result;
    VisitComponentValue(source, [&](const auto& typed) {
        using T = std::remove_cvref_t<decltype(typed)>;
        if constexpr (std::is_default_constructible_v<T>) {
            auto value = std::make_unique<T>();
            if constexpr (std::is_same_v<T, MipsScriptComponent>) {
                // A script asset defines the component's identity. Empty field
                // overrides are filled from its declarations by EnsureScriptReady.
                value->scriptPath = typed.scriptPath;
                value->module = typed.module;
            }
            result = std::move(value);
        }
    });
    return result;
}

inline bool AssignComponentValue(Component& destination, const Component& source) {
    if (typeid(destination) != typeid(source)) return false;
    Entity* owner = destination.entity;
    const bool copied = VisitComponentValue(source, [&](const auto& typed) {
        using T = std::remove_cvref_t<decltype(typed)>;
        static_cast<T&>(destination) = typed;
    });
    destination.entity = owner;
    return copied;
}

} // namespace MipsyncEngine
