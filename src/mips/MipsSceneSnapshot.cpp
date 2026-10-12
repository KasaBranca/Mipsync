#include "MipsSceneSnapshot.h"
#include "../scene/Scene.h"

namespace MipsyncEngine::Mips {

void MipsSceneSnapshot::Capture(Scene& scene) {
    m_Transforms.clear();
    m_UITexts.clear();
    m_UIImages.clear();
    m_Transforms.reserve(scene.GetEntities().size());
    for (const auto& entityPtr : scene.GetEntities()) {
        Entity* entity = entityPtr.get();
        if (!entity)
            continue;
        if (const auto* text = entity->GetComponent<UITextComponent>())
            m_UITexts.push_back({entity->GetID(), text->enabled});
        if (const auto* image = entity->GetComponent<UIImageComponent>())
            m_UIImages.push_back({entity->GetID(), image->enabled});
        if (!entity->IsActive()) continue;
        const auto* transform = entity->GetComponent<TransformComponent>();
        if (!transform)
            continue;
        m_Transforms.push_back({ entity->GetID(), transform->position,
                                transform->rotation, transform->scale });
    }
}

void MipsSceneSnapshot::Restore(Scene& scene) const {
    for (const UITextEntry& entry : m_UIImages) {
        Entity* entity = scene.FindEntity(entry.entityId);
        if (entity) {
            if (auto* image = entity->GetComponent<UIImageComponent>())
                image->enabled = entry.enabled;
        }
    }
    for (const UITextEntry& entry : m_UITexts) {
        Entity* entity = scene.FindEntity(entry.entityId);
        if (entity) {
            if (auto* text = entity->GetComponent<UITextComponent>())
                text->enabled = entry.enabled;
        }
    }
    for (const TransformEntry& entry : m_Transforms) {
        Entity* entity = scene.FindEntity(entry.entityId);
        if (!entity)
            continue;
        if (auto* transform = entity->GetComponent<TransformComponent>()) {
            transform->position = entry.position;
            transform->rotation = entry.rotation;
            transform->scale = entry.scale;
        }
    }
}

} // namespace MipsyncEngine::Mips
