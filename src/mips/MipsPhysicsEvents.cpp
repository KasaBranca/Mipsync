#include "MipsPhysicsEvents.h"

namespace MipsyncEngine {

void MipsPhysicsEventQueue::Clear() {
    m_Events.clear();
}

void MipsPhysicsEventQueue::Push(MipsPhysicsEvent event) {
    m_Events.push_back(event);
}

} // namespace MipsyncEngine
