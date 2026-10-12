#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace MipsyncEngine {
class Scene;
}

namespace MipsyncEngine::Mips {

/// Edit-mode transform and UI text visibility baselines restored after Play mode. This deliberately
/// lives outside the VM/runtime instance manager.
class MipsSceneSnapshot {
public:
    void Capture(MipsyncEngine::Scene& scene);
    void Restore(MipsyncEngine::Scene& scene) const;
    bool Empty() const { return m_Transforms.empty() && m_UITexts.empty() && m_UIImages.empty(); }

private:
    struct TransformEntry {
        uint32_t entityId = 0;
        glm::vec3 position{};
        glm::vec3 rotation{};
        glm::vec3 scale{ 1.0f };
    };
    std::vector<TransformEntry> m_Transforms;
    struct UITextEntry {
        uint32_t entityId = 0;
        bool enabled = true;
    };
    std::vector<UITextEntry> m_UITexts;
    std::vector<UITextEntry> m_UIImages;
};

} // namespace MipsyncEngine::Mips
