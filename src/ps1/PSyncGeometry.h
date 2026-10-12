#pragma once
#include "PSync.h"
#include "../../templates/ps1/starter/runtime/plane_tiles.h"
#include <algorithm>
#include <bit>
#include <unordered_map>
#include <vector>

namespace MipsyncEngine {
inline int PSyncPlaneAxis(float tiling, float offset, uint8_t subdivisionLevels,
                         ps1_plane_span spans[PS1_PLANE_AXIS_CAP]) {
    auto q8=[](float value) { return static_cast<int16_t>(std::lround(
        std::clamp(std::isfinite(value) ? double(value)*256 : 0.,-32768.,32767.))); };
    return ps1_plane_axis(q8(tiling),q8(offset),subdivisionLevels,spans);
}
inline int PSyncPlaneAxisTiles(float tiling, uint8_t subdivisionLevels) {
    ps1_plane_span spans[PS1_PLANE_AXIS_CAP];
    return PSyncPlaneAxis(tiling,0,subdivisionLevels,spans);
}
template<class VertexType, class Project>
void ProfilePSyncGeometry(PSyncWorkload& workload, const std::vector<VertexType>& vertices,
                          const std::vector<uint32_t>& indices, uint32_t offset, uint32_t count,
                          Project project, bool textured, bool doubleSided,
                          uint64_t lightSignature, std::unordered_map<uint64_t, PSyncGeometryCacheEntry>& previous,
                          std::unordered_map<uint64_t, PSyncGeometryCacheEntry>& current, uint64_t& cachedVertices,
                          uint64_t instanceSignature = 0, bool flatShading = false, bool cacheable = true, bool lit = true) {
    if (offset >= indices.size()) return;
    count = std::min<uint32_t>(count, static_cast<uint32_t>(indices.size() - offset));
    std::vector<glm::vec4> projected(vertices.size());
    std::vector<bool> visited(vertices.size());
    uint64_t hash = 1469598103934665603ULL;
    uint64_t used = 0;
    for (uint32_t i = offset; i < offset + count; ++i) {
        const uint32_t index = indices[i];
        if (index >= vertices.size() || visited[index]) continue;
        visited[index] = true;
        ++used;
        projected[index] = project(vertices[index]);
        for (int axis = 0; axis < 4; ++axis)
            hash = (hash ^ std::bit_cast<uint32_t>(projected[index][axis])) * 1099511628211ULL;
    }
    const uint64_t key = reinterpret_cast<uintptr_t>(vertices.data()) ^ (uint64_t(offset) << 32) ^ count ^ instanceSignature;
    // Match the export renderer's bounded static projection/lighting cache.
    // Never treat a moved camera/object/animated vertex as a cache hit.
    // A Lit/Unlit toggle invalidates the lighting cache independently of
    // projection; both modes still submit the runtime's Gouraud packets.
    if (!lit) lightSignature ^= 0x9e3779b97f4a7c15ULL;
    const auto old = previous.find(key);
    const bool eligible = cacheable && vertices.size() <= 4096 && current.size() < 64 && cachedVertices + used <= 16384;
    const bool projectionHit = eligible && old != previous.end() && old->second.projection == hash;
    const bool lightingHit = eligible && old != previous.end() && old->second.lighting == lightSignature;
    if (eligible) { current[key] = {hash, lightSignature}; cachedVertices += used; }
    if (!projectionHit) workload.projectedVertices += used;
    // Flat exported props light every face, including backfaces and faces
    // outside the drawing area. Lighting and projection have distinct caches.
    if (lit && !lightingHit) {
        if (flatShading) workload.litFaces += count / 3;
        else workload.litVertices += used;
    }
    workload.submittedTriangles += count / 3;
    for (uint32_t i = offset; i + 2 < offset + count; i += 3) {
        const auto a = indices[i], b = indices[i + 1], c = indices[i + 2];
        if (a >= vertices.size() || b >= vertices.size() || c >= vertices.size()) continue;
        workload.Triangle(projected[a], projected[b], projected[c], textured, true, false, doubleSided);
    }
}
} // namespace MipsyncEngine
