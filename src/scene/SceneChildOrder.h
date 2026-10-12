#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <limits>
#include <unordered_set>
#include <vector>

namespace MipsyncEngine {
// Preserve legacy order for omitted children; malformed metadata never loses,
// duplicates or reparents an entity. Shared by scene loading and PS1 export.
inline void ApplySceneChildOrder(std::vector<uint32_t>& children, const nlohmann::json& order) {
    if (!order.is_array()) return;
    std::unordered_set<uint32_t> remaining(children.begin(), children.end());
    std::vector<uint32_t> ordered;
    ordered.reserve(children.size());
    for (const auto& value : order) {
        if (!value.is_number_integer() ||
            (!value.is_number_unsigned() && value.get<int64_t>() < 0)) continue;
        const auto id = value.get<uint64_t>();
        if (id <= std::numeric_limits<uint32_t>::max() && remaining.erase(static_cast<uint32_t>(id)))
            ordered.push_back(static_cast<uint32_t>(id));
    }
    for (uint32_t id : children)
        if (remaining.erase(id)) ordered.push_back(id);
    children = std::move(ordered);
}
} // namespace MipsyncEngine
