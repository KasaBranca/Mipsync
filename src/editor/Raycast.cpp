#include "Raycast.h"
#include "../renderer/Mesh.h"
#include "../scene/Scene.h"
#include "../ui/UICanvasLayout.h"
#include "../ui/UILayout.h"
#include "../ui/RectTransform.h"
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include "../assets/AssetManager.h"
#include "../animation/SkinnedMesh.h"
#include "../animation/SkeletalModel.h"

namespace MipsyncEngine {

static glm::vec3 TransformPoint(const glm::mat4& m, const glm::vec3& p) {
    glm::vec4 result = m * glm::vec4(p, 1.0f);
    return glm::vec3(result) / result.w;
}

static void GetWorldAABB(const glm::mat4& transform, const glm::vec3& localMin, const glm::vec3& localMax, glm::vec3& outMin, glm::vec3& outMax) {
    glm::vec3 corners[8] = {
        { localMin.x, localMin.y, localMin.z },
        { localMax.x, localMin.y, localMin.z },
        { localMin.x, localMax.y, localMin.z },
        { localMax.x, localMax.y, localMin.z },
        { localMin.x, localMin.y, localMax.z },
        { localMax.x, localMin.y, localMax.z },
        { localMin.x, localMax.y, localMax.z },
        { localMax.x, localMax.y, localMax.z },
    };

    outMin = TransformPoint(transform, corners[0]);
    outMax = outMin;

    for (int i = 1; i < 8; ++i) {
        glm::vec3 world = TransformPoint(transform, corners[i]);
        outMin = glm::min(outMin, world);
        outMax = glm::max(outMax, world);
    }

    const float minThickness = 0.05f;
    glm::vec3 extent = outMax - outMin;
    glm::vec3 center = (outMin + outMax) * 0.5f;
    if (extent.x < minThickness) { outMin.x = center.x - minThickness * 0.5f; outMax.x = center.x + minThickness * 0.5f; }
    if (extent.y < minThickness) { outMin.y = center.y - minThickness * 0.5f; outMax.y = center.y + minThickness * 0.5f; }
    if (extent.z < minThickness) { outMin.z = center.z - minThickness * 0.5f; outMax.z = center.z + minThickness * 0.5f; }
}

static void GetWorldAABB(const glm::mat4& transform, const Mesh& mesh, glm::vec3& outMin, glm::vec3& outMax) {
    GetWorldAABB(transform, mesh.GetBoundsMin(), mesh.GetBoundsMax(), outMin, outMax);
}

static Ray ScreenToWorldRay(const Camera& camera, float ndcX, float ndcY) {
    glm::mat4 invView = glm::inverse(camera.GetViewMatrix());
    glm::mat4 invProj = glm::inverse(camera.GetProjectionMatrix());

    glm::vec4 viewNear = invProj * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 viewFar  = invProj * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
    viewNear /= viewNear.w;
    viewFar  /= viewFar.w;

    glm::vec4 worldNear = invView * glm::vec4(viewNear.x, viewNear.y, viewNear.z, 1.0f);
    glm::vec4 worldFar  = invView * glm::vec4(viewFar.x,  viewFar.y,  viewFar.z,  1.0f);

    Ray ray;
    ray.origin = glm::vec3(worldNear);
    ray.direction = glm::normalize(glm::vec3(worldFar - worldNear));
    return ray;
}

static bool RayIntersectsAABB(const Ray& ray, const glm::vec3& boxMin, const glm::vec3& boxMax, float& outDistance) {
    glm::vec3 invDir;
    invDir.x = ray.direction.x != 0.0f ? 1.0f / ray.direction.x : std::numeric_limits<float>::max();
    invDir.y = ray.direction.y != 0.0f ? 1.0f / ray.direction.y : std::numeric_limits<float>::max();
    invDir.z = ray.direction.z != 0.0f ? 1.0f / ray.direction.z : std::numeric_limits<float>::max();

    glm::vec3 t0 = (boxMin - ray.origin) * invDir;
    glm::vec3 t1 = (boxMax - ray.origin) * invDir;

    glm::vec3 tMin = glm::min(t0, t1);
    glm::vec3 tMax = glm::max(t0, t1);

    float tNear = glm::max(glm::max(tMin.x, tMin.y), tMin.z);
    float tFar  = glm::min(glm::min(tMax.x, tMax.y), tMax.z);

    if (tNear > tFar || tFar < 0.0f)
        return false;

    outDistance = tNear >= 0.0f ? tNear : tFar;
    return true;
}

static bool RayIntersectPlane(const Ray& ray, const glm::vec3& planeOrigin, const glm::vec3& planeNormal,
                              float& outDistance, glm::vec3& outHit) {
    const float denom = glm::dot(planeNormal, ray.direction);
    if (std::abs(denom) < 1e-6f)
        return false;

    outDistance = glm::dot(planeOrigin - ray.origin, planeNormal) / denom;
    if (outDistance < 0.0f)
        return false;

    outHit = ray.origin + ray.direction * outDistance;
    return true;
}

static Entity* PickUIEntityOnCanvasPlane(Scene& scene, const Camera& camera, const Ray& ray,
                                         int layoutWidth, int layoutHeight, float viewportAspect,
                                         float& inOutClosestDist) {
    const int layoutW = std::max(layoutWidth, 1);
    const int layoutH = std::max(layoutHeight, 1);

    Entity* closest = nullptr;

    for (const auto& entityPtr : scene.GetEntities()) {
        auto* canvas = entityPtr->GetComponent<CanvasComponent>();
        if (!canvas || !canvas->enabled)
            continue;

        const float scale = ComputeCanvasScaleFactor(*canvas, layoutW, layoutH);
        const float unitsPerPixel = ComputeSceneViewCanvasUnitsPerPixel(
            camera, canvas->planeDistance, layoutW, layoutH, viewportAspect);
        const glm::mat4 planeWorld = BuildCanvasWorldMatrix(scene, *entityPtr, *canvas, camera, unitsPerPixel, true);

        const glm::vec3 planeOrigin = glm::vec3(planeWorld[3]);
        const glm::vec3 planeNormal = glm::normalize(glm::vec3(planeWorld[2]));

        float planeDist = 0.0f;
        glm::vec3 worldHit;
        if (!RayIntersectPlane(ray, planeOrigin, planeNormal, planeDist, worldHit))
            continue;
        if (planeDist >= inOutClosestDist)
            continue;

        const glm::vec4 localHit = glm::inverse(planeWorld) * glm::vec4(worldHit, 1.0f);
        const float hitX = localHit.x;
        const float hitY = localHit.y;

        UILayoutContext ctx;
        ctx.viewportWidth = static_cast<float>(layoutW);
        ctx.viewportHeight = static_cast<float>(layoutH);
        ctx.scaleFactor = scale;
        ctx.pixelSpace = false;

        VisitCanvasUI(scene, *entityPtr, *canvas, ctx, [&](Entity& entity, const UIRect& rect) {
            if (entity.GetID() == entityPtr->GetID())
                return;
            if (hitX < rect.minX || hitX > rect.maxX || hitY < rect.minY || hitY > rect.maxY)
                return;
            if (planeDist < inOutClosestDist) {
                inOutClosestDist = planeDist;
                closest = &entity;
            }
        });
    }

    return closest;
}

Entity* PickEntityAtPoint(
    Scene& scene,
    const Camera& camera,
    float mouseX, float mouseY,
    float viewportMinX, float viewportMinY,
    float viewportWidth, float viewportHeight,
    int uiLayoutWidth, int uiLayoutHeight)
{
    if (viewportWidth <= 0.0f || viewportHeight <= 0.0f)
        return nullptr;

    float ndcX = ((mouseX - viewportMinX) / viewportWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - ((mouseY - viewportMinY) / viewportHeight) * 2.0f;
    Ray ray = ScreenToWorldRay(camera, ndcX, ndcY);

    Entity* closest = nullptr;
    float closestDist = std::numeric_limits<float>::max();

    for (const auto& entity : scene.GetEntities()) {
        auto* transform = entity->GetComponent<TransformComponent>();
        if (!transform)
            continue;

        auto* meshRenderer = entity->GetComponent<MeshRendererComponent>();
        if (meshRenderer && meshRenderer->mesh) {
            if (meshRenderer->prerenderOccluder)
                continue;
            // Editor-only meshes (e.g. PrerenderPreview quads) are not selectable.
            if (meshRenderer->editorOnly)
                continue;

            glm::vec3 boxMin, boxMax;
            GetWorldAABB(scene.GetWorldMatrix(*entity), *meshRenderer->mesh, boxMin, boxMax);

            float dist = 0.0f;
            if (RayIntersectsAABB(ray, boxMin, boxMax, dist) && dist < closestDist) {
                closestDist = dist;
                closest = entity.get();
            }

        }

        auto* skinned = entity->GetComponent<SkinnedMeshRendererComponent>();
        if (skinned && skinned->mesh) {
            std::shared_ptr<SkeletalModelAsset> skel;
            if (!skinned->modelPath.empty())
                skel = AssetManager::Get().GetSkeletalModel(skinned->modelPath);
            
            if (skel) {
                glm::vec3 boxMin, boxMax;
                GetWorldAABB(scene.GetWorldMatrix(*entity), skel->boundsMin, skel->boundsMax, boxMin, boxMax);

                float dist = 0.0f;
                if (RayIntersectsAABB(ray, boxMin, boxMax, dist) && dist < closestDist) {
                    closestDist = dist;
                    closest = entity.get();
                }
            }
        }
    }

    if (uiLayoutWidth > 0 && uiLayoutHeight > 0) {
        const float aspect = viewportHeight > 0.0f ? (viewportWidth / viewportHeight) : 1.0f;
        if (Entity* uiHit = PickUIEntityOnCanvasPlane(scene, camera, ray, uiLayoutWidth, uiLayoutHeight,
                                                      aspect, closestDist))
            closest = uiHit;
    }

    if (closest) {
        Entity* current = closest;
        while (current) {
            uint32_t parentId = current->GetParentID();
            if (parentId == 0)
                break;
            Entity* parent = const_cast<Entity*>(scene.FindEntity(parentId));
            if (!parent)
                break;
            // Do not select camera entities – stop before reaching them.
            if (parent->GetComponent<CameraComponent>())
                break;
            current = parent;
        }
        closest = current;
    }

    return closest;
}

glm::vec3 PickPointOnPlane(
    const Camera& camera,
    float mouseX, float mouseY,
    float viewportMinX, float viewportMinY,
    float viewportWidth, float viewportHeight,
    float planeY)
{
    if (viewportWidth <= 0.0f || viewportHeight <= 0.0f)
        return { 0.0f, planeY, 0.0f };

    const float ndcX = ((mouseX - viewportMinX) / viewportWidth) * 2.0f - 1.0f;
    const float ndcY = 1.0f - ((mouseY - viewportMinY) / viewportHeight) * 2.0f;
    const Ray ray = ScreenToWorldRay(camera, ndcX, ndcY);

    if (std::abs(ray.direction.y) < 1e-5f)
        return { ray.origin.x, planeY, ray.origin.z };

    float t = (planeY - ray.origin.y) / ray.direction.y;
    if (t < 0.0f)
        t = 0.0f;
    return ray.origin + ray.direction * t;
}

RaycastHit RaycastWorld(Scene& scene, const glm::vec3& origin, const glm::vec3& direction,
                        float maxDistance) {
    RaycastHit result;
    if (glm::length(direction) < 1e-6f)
        return result;

    Ray ray;
    ray.origin = origin;
    ray.direction = glm::normalize(direction);

    Entity* closest = nullptr;
    float closestDist = maxDistance;

    for (const auto& entityPtr : scene.GetEntities()) {
        auto* transform = entityPtr->GetComponent<TransformComponent>();
        if (!transform)
            continue;

        auto* meshRenderer = entityPtr->GetComponent<MeshRendererComponent>();
        if (meshRenderer && meshRenderer->mesh) {
            glm::vec3 boxMin, boxMax;
            GetWorldAABB(scene.GetWorldMatrix(*entityPtr), *meshRenderer->mesh, boxMin, boxMax);

            float dist = 0.0f;
            if (RayIntersectsAABB(ray, boxMin, boxMax, dist) && dist >= 0.0f && dist < closestDist) {
                closestDist = dist;
                closest = entityPtr.get();
            }
        }

        auto* skinned = entityPtr->GetComponent<SkinnedMeshRendererComponent>();
        if (skinned && skinned->mesh) {
            std::shared_ptr<SkeletalModelAsset> skel;
            if (!skinned->modelPath.empty())
                skel = AssetManager::Get().GetSkeletalModel(skinned->modelPath);
            
            if (skel) {
                glm::vec3 boxMin, boxMax;
                GetWorldAABB(scene.GetWorldMatrix(*entityPtr), skel->boundsMin, skel->boundsMax, boxMin, boxMax);

                float dist = 0.0f;
                if (RayIntersectsAABB(ray, boxMin, boxMax, dist) && dist >= 0.0f && dist < closestDist) {
                    closestDist = dist;
                    closest = entityPtr.get();
                }
            }
        }
    }

    if (closest) {
        result.hit = true;
        result.distance = closestDist;
        result.entity = closest;
    }
    return result;
}

} // namespace MipsyncEngine
