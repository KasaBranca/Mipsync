#pragma once
#include "../scene/ComponentCopy.h"

namespace MipsyncEngine {

class EditorComponentClipboard {
public:
    bool Copy(const Component& source) {
        auto copy = CloneComponentValue(source);
        if (!copy) return false;
        // Runtime state and geometry previews are not Inspector properties.
        if (auto* animator = dynamic_cast<AnimatorComponent*>(copy.get())) {
            animator->currentState.clear(); animator->nextState.clear();
            animator->stateTime = animator->transitionTime = animator->transitionDuration = 0;
            animator->inTransition = false; animator->heldTriggers.clear();
            for (auto& matrix : animator->boneMatrices) matrix = glm::mat4(1.0f);
        }
        if (auto* mesh = dynamic_cast<MeshRendererComponent*>(copy.get())) {
            mesh->ps1PreviewMesh.reset(); mesh->ps1PreviewMeshVersion = 0;
            mesh->ps1PreviewTexture.reset(); mesh->ps1PreviewTexturePath.clear();
        }
        if (auto* skin = dynamic_cast<SkinnedMeshRendererComponent*>(copy.get())) {
            skin->ps1RigidPreviewMesh.reset(); skin->ps1RigidPreviewVersion = 0;
        }
        if (auto* subdivider = dynamic_cast<MeshSubdividerComponent*>(copy.get()))
            subdivider->InvalidatePreview();
        if (auto* group = dynamic_cast<UIButtonGroupComponent*>(copy.get())) group->pressedThisFrame = false;
        if (auto* spectrum = dynamic_cast<UIAudioSpectrumComponent*>(copy.get())) spectrum->displayBands.clear();
        m_Value = std::move(copy);
        return true;
    }
    bool HasValue() const { return bool(m_Value); }
    bool Matches(const Component& destination) const {
        if (!m_Value || typeid(destination) != typeid(*m_Value)) return false;
        // Different Mips# scripts are different component types to the user.
        if (const auto* script = dynamic_cast<const MipsScriptComponent*>(m_Value.get())) {
            auto path = [](std::string text) {
                std::replace(text.begin(), text.end(), '\\', '/');
                return text;
            };
            return path(script->scriptPath) == path(static_cast<const MipsScriptComponent&>(destination).scriptPath);
        }
        return true;
    }
    bool CanPaste(const Entity& entity, const Component* destination = nullptr) const {
        if (!m_Value || (destination && destination->entity != &entity)) return false;
        if (destination && Matches(*destination)) return true;
        for (const Component* attached : entity.GetComponentsInOrder())
            if (Matches(*attached)) return false;
        return true;
    }
    Component* Paste(Entity& entity, Component* destination = nullptr) const {
        if (!CanPaste(entity, destination)) return nullptr;
        if (destination && destination->entity == &entity && Matches(*destination)) {
            AssignComponentValue(*destination, *m_Value);
            return destination;
        }
        return entity.AdoptComponent(CloneComponentValue(*m_Value));
    }
private:
    std::unique_ptr<Component> m_Value;
};

} // namespace MipsyncEngine
