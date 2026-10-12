#pragma once
#include <algorithm>
#include <cmath>
#include <string>

namespace MipsyncEngine::Mips {
// Editor-only metadata. Field storage, VM access and PS1 bytecode are unchanged.
struct InspectorAttributes {
    std::string header, tooltip, displayName;
    double space = 0.0, rangeMin = 0.0, rangeMax = 0.0, minimum = 0.0;
    int order = 0;
    bool hasRange = false, hasMin = false, readOnly = false, hidden = false;

    double Constrain(double value, bool integer = false) const {
        if (hasRange) value = std::clamp(value, integer ? std::ceil(rangeMin) : rangeMin,
                                              integer ? std::floor(rangeMax) : rangeMax);
        if (hasMin) value = std::max(value, integer ? std::ceil(minimum) : minimum);
        return value;
    }
};
} // namespace MipsyncEngine::Mips
