#include "ColliderUtils.h"
#include "../scene/Scene.h"
#include "../renderer/Mesh.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace MipsyncEngine {
namespace ColliderUtils {

namespace {

constexpr int kMaxHullPoints = 256;

glm::vec3 SafeLossyScale(const glm::mat4& worldMatrix) {
    return glm::vec3(
        glm::length(glm::vec3(worldMatrix[0])),
        glm::length(glm::vec3(worldMatrix[1])),
        glm::length(glm::vec3(worldMatrix[2])));
}

} // namespace

glm::vec3 GetLossyScale(const glm::mat4& worldMatrix) {
    return SafeLossyScale(worldMatrix);
}

ColliderWorldPose ComputeWorldPose(const Scene& scene, const Entity& entity,
                                   const ColliderComponent& col) {
    ColliderWorldPose pose;
    const glm::mat4 world = scene.GetWorldMatrix(entity);
    glm::vec3 skew;
    glm::vec4 perspective;
    glm::decompose(world, pose.lossyScale, pose.rotation, pose.center, skew, perspective);
    pose.lossyScale = glm::abs(pose.lossyScale);
    pose.worldEulerDegrees = glm::degrees(glm::eulerAngles(pose.rotation));

    if (glm::length(col.center) > 0.0001f)
        pose.center += pose.rotation * (col.center * pose.lossyScale);

    return pose;
}

void FitColliderToMesh(ColliderComponent& col, const Mesh& mesh) {
    const glm::vec3 bmin = mesh.GetBoundsMin();
    const glm::vec3 bmax = mesh.GetBoundsMax();
    const glm::vec3 size = bmax - bmin;
    const glm::vec3 center = (bmin + bmax) * 0.5f;

    col.center = center;

    switch (col.shape) {
    case ColliderShape::Sphere:
        col.radius = std::max(size.x, std::max(size.y, size.z)) * 0.5f;
        break;
    case ColliderShape::Capsule:
        col.radius = std::max(size.x, size.z) * 0.5f;
        col.capsuleHeight = std::max(0.01f, size.y - 2.0f * col.radius);
        break;
    case ColliderShape::Mesh:
        col.halfExtents = size * 0.5f;
        break;
    case ColliderShape::Box:
    default:
        col.shape = ColliderShape::Box;
        col.halfExtents = size * 0.5f;
        break;
    }
}

static JPH::RefConst<JPH::Shape> CreateBoxShape(const ColliderComponent& col, const glm::vec3& lossyScale) {
    const glm::vec3 he = col.halfExtents * glm::abs(lossyScale);
    JPH::BoxShapeSettings settings(JPH::Vec3(
        std::max(he.x, 0.01f), std::max(he.y, 0.01f), std::max(he.z, 0.01f)), 0.0f);
    return settings.Create().Get();
}

static JPH::RefConst<JPH::Shape> CreateSphereShape(const ColliderComponent& col, const glm::vec3& lossyScale) {
    const float scale = std::max(lossyScale.x, std::max(lossyScale.y, lossyScale.z));
    const float r = std::max(0.01f, col.radius * scale);
    JPH::SphereShapeSettings settings(r);
    return settings.Create().Get();
}

static JPH::RefConst<JPH::Shape> CreateCapsuleShape(const ColliderComponent& col, const glm::vec3& lossyScale) {
    const float r = std::max(0.01f, col.radius * std::max(lossyScale.x, lossyScale.z));
    const float halfH = std::max(0.01f, (col.capsuleHeight * 0.5f) * lossyScale.y);
    JPH::CapsuleShapeSettings settings(halfH, r);
    return settings.Create().Get();
}

static JPH::RefConst<JPH::Shape> CreateBoundsBoxShape(const glm::vec3& boundsMin,
                                                      const glm::vec3& boundsMax,
                                                      const glm::vec3& lossyScale) {
    const glm::vec3 he = (boundsMax - boundsMin) * 0.5f * glm::abs(lossyScale);
    JPH::BoxShapeSettings box(JPH::Vec3(
        std::max(he.x, 0.01f), std::max(he.y, 0.01f), std::max(he.z, 0.01f)), 0.0f);
    return box.Create().Get();
}

static JPH::RefConst<JPH::Shape> CreateConvexMeshShape(const ColliderComponent& col,
                                                       const Mesh& mesh,
                                                       const glm::vec3& lossyScale) {
    const auto& verts = mesh.GetVertices();
    if (verts.empty())
        return nullptr;

    const glm::vec3 boundsMin = mesh.GetBoundsMin();
    const glm::vec3 boundsMax = mesh.GetBoundsMax();
    const glm::vec3 scaledSize = (boundsMax - boundsMin) * glm::abs(lossyScale);
    if (scaledSize.x < 0.02f || scaledSize.y < 0.02f || scaledSize.z < 0.02f)
        return CreateBoundsBoxShape(boundsMin, boundsMax, lossyScale);

    std::vector<JPH::Vec3> points;
    points.reserve(std::min(static_cast<size_t>(kMaxHullPoints), verts.size()));

    const size_t stride = std::max<size_t>(1, verts.size() / kMaxHullPoints);
    for (size_t i = 0; i < verts.size() && points.size() < static_cast<size_t>(kMaxHullPoints); i += stride) {
        const glm::vec3 p = (verts[i].position - col.center) * lossyScale;
        points.emplace_back(p.x, p.y, p.z);
    }

    if (points.size() < 4) {
        return CreateBoundsBoxShape(boundsMin, boundsMax, lossyScale);
    }

    JPH::ConvexHullShapeSettings settings(points.data(), static_cast<int>(points.size()));
    auto result = settings.Create();
    if (result.HasError())
        return CreateBoundsBoxShape(boundsMin, boundsMax, lossyScale);
    return result.Get();
}

static JPH::RefConst<JPH::Shape> CreateTriangleMeshShape(const ColliderComponent& col,
                                                         const Mesh& mesh,
                                                         const glm::vec3& lossyScale) {
    const auto& sourceVertices = mesh.GetVertices();
    const auto& sourceIndices = mesh.GetIndices();
    if (sourceVertices.empty())
        return nullptr;

    constexpr JPH::uint32 kInvalidIndex = std::numeric_limits<JPH::uint32>::max();
    JPH::VertexList vertices;
    vertices.reserve(sourceVertices.size());
    std::vector<JPH::uint32> remap(sourceVertices.size(), kInvalidIndex);
    std::vector<glm::vec3> scaledPositions;
    scaledPositions.reserve(sourceVertices.size());

    for (size_t i = 0; i < sourceVertices.size(); ++i) {
        const glm::vec3 p = (sourceVertices[i].position - col.center) * lossyScale;
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
            continue;
        remap[i] = static_cast<JPH::uint32>(vertices.size());
        vertices.emplace_back(p.x, p.y, p.z);
        scaledPositions.push_back(p);
    }

    JPH::IndexedTriangleList triangles;
    const size_t indexCount = sourceIndices.empty() ? sourceVertices.size() : sourceIndices.size();
    triangles.reserve(indexCount / 3u);
    for (size_t i = 0; i + 2u < indexCount; i += 3u) {
        const uint32_t source[3] = {
            sourceIndices.empty() ? static_cast<uint32_t>(i) : sourceIndices[i],
            sourceIndices.empty() ? static_cast<uint32_t>(i + 1u) : sourceIndices[i + 1u],
            sourceIndices.empty() ? static_cast<uint32_t>(i + 2u) : sourceIndices[i + 2u],
        };
        if (source[0] >= remap.size() || source[1] >= remap.size() || source[2] >= remap.size())
            continue;
        const JPH::uint32 a = remap[source[0]];
        const JPH::uint32 b = remap[source[1]];
        const JPH::uint32 c = remap[source[2]];
        if (a == kInvalidIndex || b == kInvalidIndex || c == kInvalidIndex ||
            a == b || b == c || c == a)
            continue;
        const glm::vec3 cross = glm::cross(scaledPositions[b] - scaledPositions[a],
                                           scaledPositions[c] - scaledPositions[a]);
        if (glm::dot(cross, cross) <= 1e-12f)
            continue;
        triangles.emplace_back(a, b, c);
    }

    if (triangles.empty())
        return CreateBoundsBoxShape(mesh.GetBoundsMin(), mesh.GetBoundsMax(), lossyScale);

    JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
    auto result = settings.Create();
    if (result.HasError())
        return CreateBoundsBoxShape(mesh.GetBoundsMin(), mesh.GetBoundsMax(), lossyScale);
    return result.Get();
}

JPH::RefConst<JPH::Shape> CreateShape(const ColliderComponent& col, const Mesh* mesh,
                                      const glm::vec3& lossyScale) {
    switch (col.shape) {
    case ColliderShape::Sphere:
        return CreateSphereShape(col, lossyScale);
    case ColliderShape::Capsule:
        return CreateCapsuleShape(col, lossyScale);
    case ColliderShape::Mesh:
        if (mesh) {
            if (col.convex)
                return CreateConvexMeshShape(col, *mesh, lossyScale);
            return CreateTriangleMeshShape(col, *mesh, lossyScale);
        }
        return CreateBoxShape(col, lossyScale);
    case ColliderShape::Box:
    default:
        return CreateBoxShape(col, lossyScale);
    }
}

glm::quat PhysicsWorldRotation(const RigidbodyComponent* rb, const glm::quat& visualWorldRotation) {
    if (!rb || !rb->enabled || !rb->freezeRotation)
        return visualWorldRotation;

    glm::vec3 euler = glm::degrees(glm::eulerAngles(visualWorldRotation));
    euler.x = 0.0f;
    euler.z = 0.0f;
    return glm::quat(glm::radians(euler));
}

ColliderWorldPose ComputePhysicsWorldPose(const Scene& scene, const Entity& entity,
                                          const ColliderComponent& col,
                                          const RigidbodyComponent* rb) {
    ColliderWorldPose pose;
    const glm::mat4 world = scene.GetWorldMatrix(entity);
    glm::vec3 skew;
    glm::vec4 perspective;
    glm::decompose(world, pose.lossyScale, pose.rotation, pose.center, skew, perspective);
    pose.lossyScale = glm::abs(pose.lossyScale);
    pose.rotation = PhysicsWorldRotation(rb, pose.rotation);
    pose.worldEulerDegrees = glm::degrees(glm::eulerAngles(pose.rotation));

    if (glm::length(col.center) > 0.0001f)
        pose.center += pose.rotation * (col.center * pose.lossyScale);

    return pose;
}

bool IsCharacterController(const RigidbodyComponent* rb) {
    return rb && rb->enabled && rb->characterController;
}

void EnsureFirstPersonPhysics(Entity& entity) {
    if (!entity.HasComponent<ColliderComponent>()) {
        auto& col = entity.AddComponent<ColliderComponent>();
        col.shape = ColliderShape::Capsule;
        col.radius = 0.35f;
        col.capsuleHeight = 1.0f;
        // Entity pivot is at the camera; offset capsule down to the torso/feet.
        col.center = { 0.0f, -0.85f, 0.0f };
    }
    auto& rb = entity.HasComponent<RigidbodyComponent>()
        ? *entity.GetComponent<RigidbodyComponent>()
        : entity.AddComponent<RigidbodyComponent>();
    rb.bodyType = RigidbodyType::Kinematic;
    rb.useGravity = false;
    rb.freezeRotation = true;
    rb.characterController = true;
}

} // namespace ColliderUtils
} // namespace MipsyncEngine
