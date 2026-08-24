#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace MipsyncEngine {

/// Queued physics callback for Mips# (OnCollisionEnter / OnTriggerEnter).
struct MipsPhysicsEvent {
    enum class Kind : uint8_t { CollisionEnter, CollisionExit, TriggerEnter, TriggerExit } kind =
        Kind::CollisionEnter;

    uint32_t selfEntityId = 0;
    uint32_t otherEntityId = 0;
};

class MipsPhysicsEventQueue {
public:
    void Clear();
    void Push(MipsPhysicsEvent event);
    const std::vector<MipsPhysicsEvent>& Events() const { return m_Events; }

private:
    std::vector<MipsPhysicsEvent> m_Events;
};

} // namespace MipsyncEngine
