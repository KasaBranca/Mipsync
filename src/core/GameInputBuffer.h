#pragma once
#include <array>
#include <glm/glm.hpp>

namespace MipsyncEngine {
template<size_t KeyCount> struct GameInputBuffer {
    std::array<bool, KeyCount> keyDown{};
    glm::vec2 mouseDelta{};
    float scrollDelta = 0.0f;
    void Accumulate(const bool* keys, glm::vec2 mouse, float scroll) {
        for (size_t i = 0; i < KeyCount; ++i) keyDown[i] = keyDown[i] || keys[i];
        mouseDelta += mouse;
        scrollDelta += scroll;
    }
    void Clear() { keyDown.fill(false); mouseDelta = {}; scrollDelta = 0.0f; }
};
}
