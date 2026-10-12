#include "mips/SkinnedSurfaceReduction.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
using namespace MipsyncEngine::Mips;
static void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
int main() {
    std::vector<SkinnedSurfaceVertex> vertices;
    std::vector<uint32_t> indices;
    constexpr unsigned width = 17, perSurface = width * width;
    // Nearby surfaces with separate UV islands and different joint motion
    // must never be fused by spatial proximity or averaged attributes.
    for (unsigned surface = 0; surface < 2; ++surface) {
        for (unsigned y = 0; y < width; ++y) for (unsigned x = 0; x < width; ++x) {
            SkinnedSurfaceVertex v;
            v.position = {x / 16.0f, y / 16.0f, surface * 0.001f};
            v.attributes[0] = x / 16.0f + surface * 2.0f;
            v.attributes[1] = y / 16.0f;
            for (unsigned pose = 0; pose < 8; ++pose)
                v.attributes[2 + pose * 3 + 2] = surface * y / 16.0f * std::sin(float(pose));
            vertices.push_back(v);
        }
        for (unsigned y = 0; y < 16; ++y) for (unsigned x = 0; x < 16; ++x) {
            const unsigned a = surface * perSurface + y * width + x;
            indices.insert(indices.end(), {a, a + 1, a + width, a + 1, a + width + 1, a + width});
        }
    }
    const auto reduced = ReduceSkinnedSurface(vertices, indices, 256);
    require(!reduced.indices.empty() && reduced.indices.size() < indices.size(), "Surface did not reduce");
    require(reduced.indices.size() <= 256 * 3, "Planar surface exceeded target");
    std::set<uint32_t> retained;
    for (uint32_t source : reduced.sourceVertices) {
        require(source < vertices.size(), "Generated an invented source vertex");
        require(retained.insert(source).second, "Output contains duplicate source vertices");
    }
    bool surfaces[2]{};
    for (size_t i = 0; i < reduced.indices.size(); i += 3) {
        const auto a = reduced.sourceVertices.at(reduced.indices[i]);
        const auto b = reduced.sourceVertices.at(reduced.indices[i + 1]);
        const auto c = reduced.sourceVertices.at(reduced.indices[i + 2]);
        require(a / perSurface == b / perSurface && a / perSurface == c / perSurface,
                "Joined disconnected UV/skinning surfaces");
        surfaces[a / perSurface] = true;
        const auto& p = vertices[a].position;
        const auto& q = vertices[b].position;
        const auto& r = vertices[c].position;
        require((q[0]-p[0])*(r[1]-p[1])-(q[1]-p[1])*(r[0]-p[0]) > 0,
                "Flipped or collapsed triangle winding");
    }
    require(surfaces[0] && surfaces[1], "Deleted a complete surface");
    // Exact coincident corners still differ in UV and deformation. A reducer
    // may retain more faces here, but must not average their attributes away.
    for (auto& vertex : vertices) vertex.position[2] = 0;
    const auto coincident = ReduceSkinnedSurface(vertices, indices, 256);
    require(!coincident.indices.empty(), "Deleted coincident surfaces");
    for (size_t i = 0; i < coincident.indices.size(); i += 3) {
        const auto a = coincident.sourceVertices.at(coincident.indices[i]) / perSurface;
        require(a == coincident.sourceVertices.at(coincident.indices[i + 1]) / perSurface &&
                a == coincident.sourceVertices.at(coincident.indices[i + 2]) / perSurface,
                "Welded coincident corners with different UVs/motion");
    }
    require(ReduceSkinnedSurface(vertices, {999999, 0, 1}, 1).indices.empty(), "Accepted invalid indices");
    require(ReduceSkinnedSurface({}, {}, 1).indices.empty(), "Accepted empty input");
    std::puts("Skinned surface reduction retains source endpoints, separate UVs, motion and winding.");
}
