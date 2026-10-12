#pragma once
#include "Scene.h"
#include "../mips/Bytecode.h"
#include <cmath>

namespace MipsyncEngine {
// Redirect references inside a copied/prefab subtree to its newly created
// objects. References to objects outside that subtree remain unchanged.
inline void RemapScriptObjectReferences(Entity& entity,
                                       const std::unordered_map<uint32_t, uint32_t>& ids) {
    for (auto* script : entity.GetComponents<MipsScriptComponent>()) {
        if (!script->module) continue;
        for (size_t i = 0; i < script->module->fields.size() && i < script->fieldValues.size(); ++i) {
            if (script->module->fields[i].valueKind != Mips::FieldValueKind::EntityReference) continue;
            const double value = script->fieldValues[i];
            if (!std::isfinite(value) || value <= 0 || value > UINT32_MAX || std::floor(value) != value) continue;
            const auto found = ids.find(static_cast<uint32_t>(value));
            if (found != ids.end()) script->fieldValues[i] = found->second;
        }
    }
}
} // namespace MipsyncEngine
