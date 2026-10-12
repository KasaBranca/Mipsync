#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace MipsyncEngine::Mips {
// Bind position, original UV, and eight normalized deformation offsets. Exact
// record welding keeps UV seams and differently deforming coincident corners apart.
struct SkinnedSurfaceVertex {
    std::array<float, 3> position{};
    std::array<float, 26> attributes{};
};
struct SkinnedSurfaceReduction {
    std::vector<uint32_t> sourceVertices;
    std::vector<uint32_t> indices;
    float error = 0.0f;
};
SkinnedSurfaceReduction ReduceSkinnedSurface(
    const std::vector<SkinnedSurfaceVertex>& vertices,
    const std::vector<uint32_t>& indices, size_t targetTriangles);
}
