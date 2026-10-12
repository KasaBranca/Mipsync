#include "SkinnedSurfaceReduction.h"
#include <meshoptimizer.h>
#include <algorithm>
#include <cstddef>
#include <limits>

namespace MipsyncEngine::Mips {
SkinnedSurfaceReduction ReduceSkinnedSurface(
    const std::vector<SkinnedSurfaceVertex>& vertices,
    const std::vector<uint32_t>& indices, size_t targetTriangles) {
    SkinnedSurfaceReduction result;
    if (vertices.empty() || indices.empty() || indices.size() % 3 != 0)
        return result;
    for (uint32_t index : indices)
        if (index >= vertices.size()) return result;
    static_assert(sizeof(SkinnedSurfaceVertex) == 29 * sizeof(float));
    std::vector<unsigned int> remap(vertices.size());
    const size_t count = meshopt_generateVertexRemap(remap.data(), indices.data(), indices.size(),
        vertices.data(), vertices.size(), sizeof(SkinnedSurfaceVertex));
    std::vector<SkinnedSurfaceVertex> welded(count);
    meshopt_remapVertexBuffer(welded.data(), vertices.data(), vertices.size(),
        sizeof(SkinnedSurfaceVertex), remap.data());
    std::vector<unsigned int> weldedIndices(indices.size());
    meshopt_remapIndexBuffer(weldedIndices.data(), indices.data(), indices.size(), remap.data());
    std::vector<uint32_t> original(count, UINT32_MAX);
    for (size_t i = 0; i < remap.size(); ++i)
        if (remap[i] != UINT32_MAX && original[remap[i]] == UINT32_MAX)
            original[remap[i]] = static_cast<uint32_t>(i);
    std::array<float, 26> weights;
    weights.fill(1.0f);
    weights[0] = weights[1] = 0.5f;
    std::vector<unsigned int> reduced(indices.size());
    const size_t reducedCount = meshopt_simplifyWithAttributes(reduced.data(),
        weldedIndices.data(), weldedIndices.size(), welded[0].position.data(), count,
        sizeof(SkinnedSurfaceVertex), welded[0].attributes.data(), sizeof(SkinnedSurfaceVertex),
        weights.data(), weights.size(), nullptr, std::min(indices.size(), targetTriangles * 3),
        0.05f, meshopt_SimplifyRegularize, &result.error);
    // Never use permissive welding, spatial averaging, or sloppy fallback to
    // force a budget: retaining original endpoints preserves UVs and skinning.
    std::vector<uint32_t> compact(count, UINT32_MAX);
    result.indices.reserve(reducedCount);
    for (size_t i = 0; i < reducedCount; ++i) {
        const unsigned int vertex = reduced[i];
        if (compact[vertex] == UINT32_MAX) {
            compact[vertex] = static_cast<uint32_t>(result.sourceVertices.size());
            result.sourceVertices.push_back(original[vertex]);
        }
        result.indices.push_back(compact[vertex]);
    }
    return result;
}
}
