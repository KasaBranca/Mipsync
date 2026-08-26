#include "EditorApp.h"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include "AssetBrowserPanel.h"
#include "EditorAnimatorControllerInspector.h"
#include "EditorAnimatorDebug.h"
#include "EditorTheme.h"
#include "EditorIcons.h"
#include "EditorAssetIcons.h"
#include "Raycast.h"
#include "EditorColliderGizmo.h"
#include "EditorLightGizmo.h"
#include "EditorCanvasGizmo.h"
#include "EditorRectTransformGizmo.h"
#include "EditorUISnap.h"
#include "EditorSceneFraming.h"
#include "MipsEditorIntegration.h"
#include "GameViewSettings.h"
#include "../ui/UILayout.h"
#include "../ui/UICanvasLayout.h"
#include "../core/Engine.h"
#include "../core/Win32AppIcon.h"
#include "../core/Log.h"
#include "../core/Time.h"
#include "../core/Input.h"
#include "../scene/Scene.h"
#include "../animation/AnimationTypes.h"
#include "../animation/AnimatorRuntime.h"
#include "../animation/SkeletalModel.h"
#include "../renderer/Renderer.h"
#include "../renderer/Texture.h"
#include "../mips/MipsRuntime.h"
#include "../physics/PhysicsWorld.h"
#include "../audio/AudioSystem.h"
#include "../physics/ColliderUtils.h"
#include "../scene/SceneIO.h"
#include "../project/Project.h"
#include "../assets/AssetManager.h"
#include "../assets/AssetThumbnail.h"
#include "../assets/Material.h"
#include "../core/OsFileDrop.h"
#include "../core/RuntimePaths.h"
#include "../command/EditorCommandHost.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_glfw.h>
#include <GLFW/glfw3.h>
#include <backends/imgui_impl_opengl3.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <string>

namespace MipsyncEngine {

namespace {

EditorApp* s_EditorForWindowClose = nullptr;

bool IsUIButtonCallableMethod(const Mips::CompiledMethod& method) {
    if (!method.isPublic || method.parameterCount != 0 || method.returnType != "void")
        return false;
    static constexpr const char* kLifecycleMethods[] = {
        "Awake", "Start", "Update", "LateUpdate", "OnDestroy",
        "OnCollisionEnter", "OnCollisionExit", "OnTriggerEnter", "OnTriggerExit",
    };
    return std::none_of(std::begin(kLifecycleMethods), std::end(kLifecycleMethods),
                        [&](const char* name) { return method.name == name; });
}

class ScopedDrawListClipRect {
public:
    ScopedDrawListClipRect(ImDrawList* drawList, const ImVec2& min, const ImVec2& max)
        : m_DrawList(drawList) {
        m_DrawList->PushClipRect(min, max, true);
    }

    ~ScopedDrawListClipRect() {
        m_DrawList->PopClipRect();
    }

    ScopedDrawListClipRect(const ScopedDrawListClipRect&) = delete;
    ScopedDrawListClipRect& operator=(const ScopedDrawListClipRect&) = delete;

private:
    ImDrawList* m_DrawList;
};

void GLFWWindowCloseCallback(GLFWwindow* window) {
    if (s_EditorForWindowClose && s_EditorForWindowClose->OnWindowCloseRequested())
        glfwSetWindowShouldClose(window, GLFW_FALSE);
}

std::vector<std::string> PathsFromDragPayload(const ImGuiPayload* payload) {
    if (!payload || !payload->Data)
        return {};

    if (payload->IsDataType(DragDrop::kAssetMove))
        return DragDrop::ParseAssetMovePayload(payload->Data, payload->DataSize);

    if (payload->IsDataType(DragDrop::kAssetTexture) || payload->IsDataType(DragDrop::kAssetMaterial) ||
        payload->IsDataType(DragDrop::kAssetAnimatorController) ||
        payload->IsDataType(DragDrop::kAssetPrefab) || payload->IsDataType(DragDrop::kAssetModel) ||
        payload->IsDataType(DragDrop::kAssetScript) || payload->IsDataType(DragDrop::kAssetAudio)) {
        const std::string path(static_cast<const char*>(payload->Data),
                               payload->DataSize > 0 ? payload->DataSize - 1 : 0);
        if (!path.empty())
            return { path };
    }
    return {};
}

void AddDefaultCollider(Entity& entity, const std::string& primitive, float meshSize) {
    if (entity.HasComponent<ColliderComponent>())
        return;

    auto& col = entity.AddComponent<ColliderComponent>();

    if (auto* mr = entity.GetComponent<MeshRendererComponent>(); mr && mr->mesh) {
        ColliderUtils::FitColliderToMesh(col, *mr->mesh);
        return;
    }

    if (primitive == "Sphere") {
        col.shape = ColliderShape::Sphere;
        col.radius = meshSize * 0.5f;
    } else if (primitive == "Plane") {
        col.shape = ColliderShape::Box;
        col.halfExtents = { meshSize * 0.5f, 0.05f, meshSize * 0.5f };
    } else {
        col.shape = ColliderShape::Box;
        const float h = meshSize * 0.5f;
        col.halfExtents = { h, h, h };
    }
}

Entity* FindButtonGroupAncestor(Scene& scene, Entity* entity) {
    Entity* walk = entity;
    while (walk) {
        if (walk->HasComponent<UIButtonGroupComponent>())
            return walk;
        walk = scene.FindEntity(walk->GetParentID());
    }
    return nullptr;
}

void FitRectTransformToTextureAspect(Entity* entity, const Texture* texture) {
    if (!entity || !texture || texture->GetWidth() <= 0 || texture->GetHeight() <= 0)
        return;
    auto* rect = entity->GetComponent<RectTransformComponent>();
    if (!rect)
        return;

    const float aspect = static_cast<float>(texture->GetWidth()) /
                         static_cast<float>(texture->GetHeight());
    if (aspect <= 0.0001f)
        return;

    if (std::abs(rect->sizeDelta.x) > 0.001f) {
        rect->sizeDelta.y = rect->sizeDelta.x / aspect;
    } else if (std::abs(rect->sizeDelta.y) > 0.001f) {
        rect->sizeDelta.x = rect->sizeDelta.y * aspect;
    } else {
        rect->sizeDelta.x = static_cast<float>(texture->GetWidth());
        rect->sizeDelta.y = static_cast<float>(texture->GetHeight());
    }
}

void DrawInspectorDivider() {
    ImGui::Spacing();
    const ImVec2 windowPos = ImGui::GetWindowPos();
    const ImVec2 windowSize = ImGui::GetWindowSize();
    const float dividerY = ImGui::GetCursorScreenPos().y;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(windowPos,
                           ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y),
                           false);
    drawList->AddRectFilled(ImVec2(windowPos.x, dividerY),
                            ImVec2(windowPos.x + windowSize.x, dividerY + 1.0f),
                            ImGui::GetColorU32(EditorTheme::BorderLight));
    drawList->PopClipRect();
    ImGui::Dummy(ImVec2(0.0f, 1.0f));
    ImGui::Spacing();
}

bool DrawComponentEnabledToggle(bool& enabled) {
    const bool pressed = EditorTheme::Checkbox("##enabled", &enabled);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(enabled ? "Disable component" : "Enable component");
    return pressed;
}

struct EditorRay {
    glm::vec3 origin{ 0.0f };
    glm::vec3 direction{ 0.0f, 0.0f, -1.0f };
};

EditorRay BuildEditorRay(const Camera& camera, float mouseX, float mouseY,
                         const ImVec2& imageMin, const ImVec2& imageSize) {
    const float ndcX = ((mouseX - imageMin.x) / imageSize.x) * 2.0f - 1.0f;
    const float ndcY = 1.0f - ((mouseY - imageMin.y) / imageSize.y) * 2.0f;
    const glm::mat4 invView = glm::inverse(camera.GetViewMatrix());
    const glm::mat4 invProj = glm::inverse(camera.GetProjectionMatrix());
    glm::vec4 nearP = invProj * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farP = invProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    nearP /= nearP.w;
    farP /= farP.w;
    nearP = invView * nearP;
    farP = invView * farP;
    EditorRay ray;
    ray.origin = glm::vec3(nearP);
    ray.direction = glm::normalize(glm::vec3(farP - nearP));
    return ray;
}

bool RayTriangleHit(const EditorRay& ray, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                    float& outT) {
    const glm::vec3 edge1 = b - a;
    const glm::vec3 edge2 = c - a;
    const glm::vec3 h = glm::cross(ray.direction, edge2);
    const float det = glm::dot(edge1, h);
    if (std::abs(det) < 1e-7f)
        return false;
    const float invDet = 1.0f / det;
    const glm::vec3 s = ray.origin - a;
    const float u = invDet * glm::dot(s, h);
    if (u < 0.0f || u > 1.0f)
        return false;
    const glm::vec3 q = glm::cross(s, edge1);
    const float v = invDet * glm::dot(ray.direction, q);
    if (v < 0.0f || u + v > 1.0f)
        return false;
    const float t = invDet * glm::dot(edge2, q);
    if (t < 0.0f)
        return false;
    outT = t;
    return true;
}

float DistanceToSegment2D(const ImVec2& p, const ImVec2& a, const ImVec2& b) {
    const ImVec2 ab{ b.x - a.x, b.y - a.y };
    const ImVec2 ap{ p.x - a.x, p.y - a.y };
    const float len2 = ab.x * ab.x + ab.y * ab.y;
    const float t = len2 > 1e-6f ? std::clamp((ap.x * ab.x + ap.y * ab.y) / len2, 0.0f, 1.0f) : 0.0f;
    const ImVec2 closest{ a.x + ab.x * t, a.y + ab.y * t };
    const float dx = p.x - closest.x;
    const float dy = p.y - closest.y;
    return std::sqrt(dx * dx + dy * dy);
}

void AddUniqueVertex(std::vector<uint32_t>& vertices, uint32_t index) {
    if (std::find(vertices.begin(), vertices.end(), index) == vertices.end())
        vertices.push_back(index);
}

void AddUniqueFaceTriangle(std::vector<size_t>& triangles, size_t triangleStart) {
    triangleStart = (triangleStart / 3u) * 3u;
    if (std::find(triangles.begin(), triangles.end(), triangleStart) == triangles.end())
        triangles.push_back(triangleStart);
}

glm::vec3 ProModelerTriangleNormal(const ProModelerComponent& pb, size_t triangleStart) {
    if (triangleStart + 2 >= pb.indices.size())
        return { 0.0f, 1.0f, 0.0f };
    const uint32_t i0 = pb.indices[triangleStart + 0];
    const uint32_t i1 = pb.indices[triangleStart + 1];
    const uint32_t i2 = pb.indices[triangleStart + 2];
    if (i0 >= pb.vertices.size() || i1 >= pb.vertices.size() || i2 >= pb.vertices.size())
        return { 0.0f, 1.0f, 0.0f };
    glm::vec3 normal = glm::normalize(glm::cross(
        pb.vertices[i1].position - pb.vertices[i0].position,
        pb.vertices[i2].position - pb.vertices[i0].position));
    if (!std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z))
        normal = { 0.0f, 1.0f, 0.0f };
    return normal;
}

bool ProModelerTrianglesShareEdge(const ProModelerComponent& pb, size_t a, size_t b) {
    if (a + 2 >= pb.indices.size() || b + 2 >= pb.indices.size())
        return false;
    int shared = 0;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            if (pb.indices[a + static_cast<size_t>(i)] == pb.indices[b + static_cast<size_t>(j)])
                ++shared;
        }
    }
    return shared >= 2;
}

std::vector<size_t> CollectProModelerFaceTriangles(const ProModelerComponent& pb, size_t seedTriangleStart) {
    std::vector<size_t> result;
    if (pb.indices.size() < 3 || seedTriangleStart + 2 >= pb.indices.size())
        return result;

    seedTriangleStart = (seedTriangleStart / 3u) * 3u;
    const size_t seedTriangleIndex = seedTriangleStart / 3u;
    if (pb.triangleFaceIds.size() == pb.indices.size() / 3u &&
        seedTriangleIndex < pb.triangleFaceIds.size()) {
        const uint32_t faceId = pb.triangleFaceIds[seedTriangleIndex];
        for (size_t triangleIndex = 0; triangleIndex < pb.triangleFaceIds.size(); ++triangleIndex) {
            if (pb.triangleFaceIds[triangleIndex] == faceId)
                result.push_back(triangleIndex * 3u);
        }
        return result;
    }

    const glm::vec3 seedNormal = ProModelerTriangleNormal(pb, seedTriangleStart);
    const uint32_t seedIndex = pb.indices[seedTriangleStart];
    if (seedIndex >= pb.vertices.size())
        return result;
    const float seedPlane = glm::dot(seedNormal, pb.vertices[seedIndex].position);
    const float epsilon = std::max(0.0005f, glm::length(pb.size) * 0.001f);

    std::vector<size_t> stack{ seedTriangleStart };
    AddUniqueFaceTriangle(result, seedTriangleStart);
    while (!stack.empty()) {
        const size_t current = stack.back();
        stack.pop_back();
        for (size_t candidate = 0; candidate + 2 < pb.indices.size(); candidate += 3) {
            if (std::find(result.begin(), result.end(), candidate) != result.end())
                continue;
            if (!ProModelerTrianglesShareEdge(pb, current, candidate))
                continue;
            const glm::vec3 normal = ProModelerTriangleNormal(pb, candidate);
            if (glm::dot(seedNormal, normal) < 0.995f)
                continue;
            bool samePlane = true;
            for (int i = 0; i < 3; ++i) {
                const uint32_t vi = pb.indices[candidate + static_cast<size_t>(i)];
                if (vi >= pb.vertices.size() ||
                    std::abs(glm::dot(seedNormal, pb.vertices[vi].position) - seedPlane) > epsilon) {
                    samePlane = false;
                    break;
                }
            }
            if (!samePlane)
                continue;
            AddUniqueFaceTriangle(result, candidate);
            stack.push_back(candidate);
        }
    }
    return result;
}

bool PositionsNearlyEqual(const glm::vec3& a, const glm::vec3& b, float epsilon) {
    return glm::length(a - b) <= epsilon;
}

bool ProModelerEdgeIsCoplanarInterior(const ProModelerComponent& pb, size_t triangleStart, uint32_t edgeA, uint32_t edgeB) {
    const glm::vec3 normal = ProModelerTriangleNormal(pb, triangleStart);
    const size_t triangleIndex = triangleStart / 3u;
    for (size_t other = 0; other + 2 < pb.indices.size(); other += 3) {
        if (other == triangleStart)
            continue;

        bool hasA = false;
        bool hasB = false;
        for (int i = 0; i < 3; ++i) {
            const uint32_t index = pb.indices[other + static_cast<size_t>(i)];
            hasA |= index == edgeA;
            hasB |= index == edgeB;
        }
        if (!hasA || !hasB)
            continue;

        // Hide triangulation only inside one semantic polygon. An edge shared
        // by different face IDs is a real loop-cut boundary.
        const size_t otherIndex = other / 3u;
        if (pb.triangleFaceIds.size() == pb.indices.size() / 3u &&
            triangleIndex < pb.triangleFaceIds.size() &&
            otherIndex < pb.triangleFaceIds.size() &&
            pb.triangleFaceIds[triangleIndex] != pb.triangleFaceIds[otherIndex])
            continue;

        const glm::vec3 otherNormal = ProModelerTriangleNormal(pb, other);
        if (glm::dot(normal, otherNormal) > 0.995f)
            return true;
    }
    return false;
}

bool EntityWorldAabb(Scene& scene, Entity& entity, glm::vec3& outMin, glm::vec3& outMax) {
    const glm::mat4 world = scene.GetWorldMatrix(entity);
    const glm::vec3 worldPos(world[3]);
    auto* collider = entity.GetComponent<ColliderComponent>();
    if (!collider || collider->shape == ColliderShape::Mesh) {
        outMin = outMax = worldPos;
        return collider != nullptr;
    }

    glm::vec3 center = glm::vec3(world * glm::vec4(collider->center, 1.0f));
    glm::vec3 half = collider->halfExtents;
    if (collider->shape == ColliderShape::Sphere) {
        half = glm::vec3(collider->radius);
    } else if (collider->shape == ColliderShape::Capsule) {
        half = glm::vec3(collider->radius, collider->capsuleHeight * 0.5f + collider->radius,
                         collider->radius);
    }

    const glm::vec3 worldHalf =
        glm::abs(glm::vec3(world[0])) * half.x +
        glm::abs(glm::vec3(world[1])) * half.y +
        glm::abs(glm::vec3(world[2])) * half.z;
    outMin = center - worldHalf;
    outMax = center + worldHalf;
    return true;
}

bool AabbOverlap(const glm::vec3& aMin, const glm::vec3& aMax,
                 const glm::vec3& bMin, const glm::vec3& bMax) {
    return aMin.x < bMax.x && aMax.x > bMin.x &&
           aMin.y < bMax.y && aMax.y > bMin.y &&
           aMin.z < bMax.z && aMax.z > bMin.z;
}

bool IsCameraShotTarget(Entity& entity) {
    for (auto* script : entity.GetComponents<MipsScriptComponent>())
        if (!script->scriptPath.empty() || script->module) return true;
    if (auto* rb = entity.GetComponent<RigidbodyComponent>();
        rb && rb->bodyType != RigidbodyType::Static)
        return true;
    if (auto* tag = entity.GetComponent<TagComponent>()) {
        std::string lower = tag->tag;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lower.find("player") != std::string::npos)
            return true;
    }
    return false;
}

bool IsCameraShotTrigger(Entity& entity) {
    auto* collider = entity.GetComponent<ColliderComponent>();
    return collider && collider->enabled && collider->cameraShotTrigger;
}

float CameraTriggerDistanceSq(Scene& scene, Entity& cameraEntity, const glm::vec3& triggerCenter) {
    const glm::vec3 cameraPos = glm::vec3(scene.GetWorldMatrix(cameraEntity)[3]);
    return glm::dot(cameraPos - triggerCenter, cameraPos - triggerCenter);
}

bool CameraCandidateBeats(Entity* camera, float distanceSq, Entity* other, float otherDistanceSq) {
    if (!other)
        return true;
    if (distanceSq < otherDistanceSq)
        return true;
    if (distanceSq > otherDistanceSq)
        return false;

    auto* cameraComp = camera->GetComponent<CameraComponent>();
    auto* otherComp = other->GetComponent<CameraComponent>();
    const int cameraPriority = cameraComp ? cameraComp->shotPriority : 0;
    const int otherPriority = otherComp ? otherComp->shotPriority : 0;
    return cameraPriority > otherPriority;
}

Entity* SelectAdjacentShotCamera(Scene& scene, const glm::vec3& triggerCenter,
                                 int movementAxis, float movementOnAxis,
                                 uint32_t activeCameraEntityId) {
    Entity* nearest = nullptr;
    Entity* secondNearest = nullptr;
    float nearestDistSq = std::numeric_limits<float>::max();
    float secondNearestDistSq = std::numeric_limits<float>::max();

    for (const auto& candidatePtr : scene.GetEntities()) {
        Entity* candidate = candidatePtr.get();
        auto* cameraComp = candidate->GetComponent<CameraComponent>();
        if (!cameraComp || !cameraComp->enabled)
            continue;

        const float distanceSq = CameraTriggerDistanceSq(scene, *candidate, triggerCenter);
        if (CameraCandidateBeats(candidate, distanceSq, nearest, nearestDistSq)) {
            secondNearest = nearest;
            secondNearestDistSq = nearestDistSq;
            nearest = candidate;
            nearestDistSq = distanceSq;
        } else if (CameraCandidateBeats(candidate, distanceSq, secondNearest, secondNearestDistSq)) {
            secondNearest = candidate;
            secondNearestDistSq = distanceSq;
        }
    }

    if (!nearest)
        return nullptr;
    if (!secondNearest)
        return nearest;

    if (nearest->GetID() == activeCameraEntityId)
        return secondNearest;
    if (secondNearest->GetID() == activeCameraEntityId)
        return nearest;

    Entity* pair[2] = { nearest, secondNearest };
    Entity* bestOnSide = nullptr;
    float bestSideDistSq = std::numeric_limits<float>::max();
    for (Entity* camera : pair) {
        const glm::vec3 cameraPos = glm::vec3(scene.GetWorldMatrix(*camera)[3]);
        const float side = cameraPos[movementAxis] - triggerCenter[movementAxis];
        if (movementOnAxis * side <= 0.0f)
            continue;
        const float distanceSq = glm::dot(cameraPos - triggerCenter, cameraPos - triggerCenter);
        if (CameraCandidateBeats(camera, distanceSq, bestOnSide, bestSideDistSq)) {
            bestOnSide = camera;
            bestSideDistSq = distanceSq;
        }
    }

    return bestOnSide ? bestOnSide : nearest;
}

/// Inspector header: enable checkbox, foldout, right-click remove.
bool BeginInspectableComponent(const char* title, Component& comp, bool& removeRequested,
                               EditorApp* editorForUndo) {
    ImGui::PushID(&comp);
    DrawComponentEnabledToggle(comp.enabled);
    ImGui::SameLine();
    const bool open = ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen);
    const bool headerContext = ImGui::BeginPopupContextItem("##component_context");
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 42.0f);
    ImGui::TextDisabled("?");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s component", title);
    ImGui::SameLine(0.0f, 8.0f);
    if (ImGui::SmallButton("..."))
        ImGui::OpenPopup("##component_menu");
    if (headerContext || ImGui::BeginPopup("##component_menu")) {
        if (ImGui::MenuItem("Remove Component")) {
            if (editorForUndo)
                editorForUndo->RecordUndoSnapshot();
            removeRequested = true;
        }
        ImGui::EndPopup();
    }
    if (!comp.enabled)
        ImGui::BeginDisabled();
    return open;
}

void EndInspectableComponent(const Component& comp) {
    if (!comp.enabled)
        ImGui::EndDisabled();
    ImGui::PopID();
    DrawInspectorDivider();
}

} // namespace

EditorApp::EditorApp(Engine* engine) : m_Engine(engine) {}

EditorApp::~EditorApp() { Shutdown(); }

void EditorApp::ResetShotCameraState() {
    m_ActiveShotCameraEntityID = 0;
    m_ShotCameraStatePrimed = false;
    m_ShotTriggerOverlap.clear();
    m_ShotTargetPreviousCenters.clear();
}

Entity* EditorApp::ResolveActiveShotCameraEntity(bool allowShotSelection) {
    Scene& scene = m_Engine->GetScene();
    if (!allowShotSelection) {
        ResetShotCameraState();
        return scene.GetPrimaryCameraEntity();
    }

    Entity* best = nullptr;
    int bestPriority = std::numeric_limits<int>::min();
    float bestSideDistance = std::numeric_limits<float>::max();
    std::unordered_map<uint32_t, glm::vec3> currentTargetCenters;
    std::vector<uint32_t> processedTriggers;

    for (const auto& targetPtr : scene.GetEntities()) {
        Entity* target = targetPtr.get();
        if (!IsCameraShotTarget(*target))
            continue;

        glm::vec3 targetMin, targetMax;
        if (!EntityWorldAabb(scene, *target, targetMin, targetMax))
            continue;
        currentTargetCenters[target->GetID()] = (targetMin + targetMax) * 0.5f;
    }

    if (!m_ShotCameraStatePrimed) {
        for (const auto& triggerPtr : scene.GetEntities()) {
            Entity* trigger = triggerPtr.get();
            bool isShotTrigger = IsCameraShotTrigger(*trigger);
            const uint32_t triggerId = trigger->GetID();
            if (!isShotTrigger) {
                for (const auto& cameraPtr : scene.GetEntities()) {
                    Entity* legacyCamera = cameraPtr.get();
                    auto* legacyCameraComp = legacyCamera->GetComponent<CameraComponent>();
                    if (legacyCameraComp && legacyCameraComp->enabled &&
                        legacyCameraComp->shotTriggerEntityId == triggerId) {
                        isShotTrigger = true;
                        break;
                    }
                }
            }
            if (!isShotTrigger)
                continue;

            glm::vec3 triggerMin, triggerMax;
            if (!EntityWorldAabb(scene, *trigger, triggerMin, triggerMax))
                continue;

            bool nowOverlap = false;
            for (const auto& targetPtr : scene.GetEntities()) {
                Entity* target = targetPtr.get();
                if (target == trigger || !IsCameraShotTarget(*target))
                    continue;
                glm::vec3 targetMin, targetMax;
                if (!EntityWorldAabb(scene, *target, targetMin, targetMax))
                    continue;
                if (AabbOverlap(targetMin, targetMax, triggerMin, triggerMax)) {
                    nowOverlap = true;
                    break;
                }
            }
            m_ShotTriggerOverlap[triggerId] = nowOverlap;
        }
        m_ShotTargetPreviousCenters = std::move(currentTargetCenters);
        m_ShotCameraStatePrimed = true;
        Entity* primary = scene.GetPrimaryCameraEntity();
        m_ActiveShotCameraEntityID = primary ? primary->GetID() : 0;
        return primary;
    }

    for (const auto& triggerPtr : scene.GetEntities()) {
        Entity* trigger = triggerPtr.get();
        bool nowOverlap = false;
        Entity* enteredTarget = nullptr;
        bool isShotTrigger = IsCameraShotTrigger(*trigger);

        const uint32_t triggerId = trigger->GetID();
        if (std::find(processedTriggers.begin(), processedTriggers.end(), triggerId) != processedTriggers.end())
            continue;

        if (!isShotTrigger) {
            for (const auto& cameraPtr : scene.GetEntities()) {
                Entity* legacyCamera = cameraPtr.get();
                auto* legacyCameraComp = legacyCamera->GetComponent<CameraComponent>();
                if (legacyCameraComp && legacyCameraComp->enabled &&
                    legacyCameraComp->shotTriggerEntityId == triggerId) {
                    isShotTrigger = true;
                    break;
                }
            }
        }
        if (!isShotTrigger)
            continue;
        processedTriggers.push_back(triggerId);

        glm::vec3 triggerMin, triggerMax;
        if (!EntityWorldAabb(scene, *trigger, triggerMin, triggerMax))
            continue;

        for (const auto& targetPtr : scene.GetEntities()) {
            Entity* target = targetPtr.get();
            if (target == trigger || !IsCameraShotTarget(*target))
                continue;

            glm::vec3 targetMin, targetMax;
            if (!EntityWorldAabb(scene, *target, targetMin, targetMax))
                continue;
            if (!AabbOverlap(targetMin, targetMax, triggerMin, triggerMax))
                continue;

            nowOverlap = true;
            enteredTarget = target;
            break;
        }

        const bool wasOverlap = m_ShotTriggerOverlap[triggerId];
        if (nowOverlap && !wasOverlap && enteredTarget) {
            const uint32_t targetId = enteredTarget->GetID();
            auto currentIt = currentTargetCenters.find(targetId);
            auto prevIt = m_ShotTargetPreviousCenters.find(targetId);
            if (currentIt != currentTargetCenters.end() && prevIt != m_ShotTargetPreviousCenters.end()) {
                const glm::vec3 triggerCenter = (triggerMin + triggerMax) * 0.5f;
                const glm::vec3 delta = currentIt->second - prevIt->second;
                int axis = 0;
                if (std::abs(delta.y) > std::abs(delta[axis])) axis = 1;
                if (std::abs(delta.z) > std::abs(delta[axis])) axis = 2;
                bool hasMotion = false;
                if (std::abs(delta[axis]) > 0.0001f) {
                    hasMotion = true;
                    for (int pass = 0; pass < 2 && !best; ++pass) {
                        float bestDistanceSq = std::numeric_limits<float>::max();
                        for (const auto& candidatePtr : scene.GetEntities()) {
                            Entity* candidateCamera = candidatePtr.get();
                            auto* candidateCameraComp = candidateCamera->GetComponent<CameraComponent>();
                            if (!candidateCameraComp || !candidateCameraComp->enabled)
                                continue;
                            if (m_ActiveShotCameraEntityID != 0 &&
                                candidateCamera->GetID() == m_ActiveShotCameraEntityID)
                                continue;

                            const glm::vec3 cameraPos = glm::vec3(scene.GetWorldMatrix(*candidateCamera)[3]);
                            const float side = cameraPos[axis] - triggerCenter[axis];
                            if (pass == 0 && delta[axis] * side <= 0.0f)
                                continue;

                            const float distanceSq = glm::dot(cameraPos - triggerCenter, cameraPos - triggerCenter);
                            if (!best ||
                                distanceSq < bestDistanceSq ||
                                (distanceSq == bestDistanceSq &&
                                 candidateCameraComp->shotPriority > bestPriority)) {
                                best = candidateCamera;
                                bestPriority = candidateCameraComp->shotPriority;
                                bestDistanceSq = distanceSq;
                                bestSideDistance = std::abs(side);
                            }
                        }
                    }
                }

                if (hasMotion && !best) {
                    for (const auto& candidatePtr : scene.GetEntities()) {
                        Entity* candidateCamera = candidatePtr.get();
                        auto* candidateCameraComp = candidateCamera->GetComponent<CameraComponent>();
                        if (!candidateCameraComp || !candidateCameraComp->enabled ||
                            candidateCameraComp->shotTriggerEntityId != triggerId)
                            continue;
                        if (!best || candidateCameraComp->shotPriority > bestPriority) {
                            best = candidateCamera;
                            bestPriority = candidateCameraComp->shotPriority;
                        }
                    }
                }
            }
        }
        m_ShotTriggerOverlap[triggerId] = nowOverlap;
    }

    m_ShotTargetPreviousCenters = std::move(currentTargetCenters);

    if (best)
        m_ActiveShotCameraEntityID = best->GetID();

    if (m_ActiveShotCameraEntityID != 0) {
        Entity* active = scene.FindEntity(m_ActiveShotCameraEntityID);
        if (active) {
            auto* cameraComp = active->GetComponent<CameraComponent>();
            if (cameraComp && cameraComp->enabled)
                return active;
        }
        m_ActiveShotCameraEntityID = 0;
    }

    return scene.GetPrimaryCameraEntity();
}

void EditorApp::Init() {
    MIPSYNC_INFO("Initializing Editor (Dear ImGui)...");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    namespace fs = std::filesystem;
    const fs::path projectRoot = m_Engine->GetProjectPath().empty()
        ? fs::current_path()
        : PathUtf8::FromString(m_Engine->GetProjectPath());

    m_LayoutIniPath  = PathUtf8::ToString(projectRoot / "editor_layout.ini");
    m_SceneFilePath  = PathUtf8::ToString(projectRoot / "scenes" / "default.nscene");
    ProjectInfo projectInfo;
    std::string projectInfoError;
    if (Project::LoadFromDir(PathUtf8::ToString(projectRoot), projectInfo, projectInfoError)) {
        std::string sceneRel = projectInfo.editorLastScene.empty()
            ? projectInfo.defaultScene
            : projectInfo.editorLastScene;
        if (sceneRel.empty())
            sceneRel = "scenes/default.nscene";
        if (!std::filesystem::exists(projectRoot / PathUtf8::FromString(sceneRel)) &&
            !projectInfo.defaultScene.empty()) {
            sceneRel = projectInfo.defaultScene;
        }
        if (!sceneRel.empty())
            m_SceneFilePath = PathUtf8::ToString(projectRoot / sceneRel);

        std::string migrateProjectError;
        if (!Project::SaveToDir(projectInfo, migrateProjectError))
            MIPSYNC_WARN("Project metadata migration failed: {}", migrateProjectError);
    }

    if (m_Engine->IsPlayerMode())
        io.IniFilename = nullptr;
    else
        io.IniFilename = m_LayoutIniPath.c_str();
    if (!m_Engine->IsPlayerMode()) {
        RegisterSettingsHandler();
        ImGui::LoadIniSettingsFromDisk(m_LayoutIniPath.c_str());
    }

    MIPSYNC_INFO("Editor paths: layout={}, scene={}", m_LayoutIniPath, m_SceneFilePath);
    
    // Gameplay UI reads keys through Input directly. Dear ImGui keyboard
    // navigation would otherwise make arrow keys move focus around editor UI.
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    EditorTheme::Apply();
    LoadEditorSettings();
    EditorTheme::LoadFonts(m_FontSize);

    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    GLFWwindow* window = m_Engine->GetWindow().GetNativeWindow();
    OsFileDrop::Init(window);
    s_EditorForWindowClose = this;
    glfwSetWindowCloseCallback(window, GLFWWindowCloseCallback);
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    if (m_PendingSceneCamPivot) {
        m_SceneCamera.SetOrbitState(m_PendingSceneCamPivotPos, m_PendingSceneCamDistance,
                                    m_PendingSceneCamYaw, m_PendingSceneCamPitch);
        m_SceneCameraRestoredFromSettings = true;
    }
    if (!m_SceneCameraRestoredFromSettings)
        m_SceneCamera.FocusOn({ 0.0f, 0.0f, 0.0f }, 8.0f);
    m_AssetBrowser = std::make_unique<AssetBrowserPanel>(this);
    m_BuildSettings = std::make_unique<EditorBuildSettings>(m_Engine);
    if (!m_PendingAssetBrowserTreeExpanded.empty())
        m_AssetBrowser->SetExpandedTreeFolders(m_PendingAssetBrowserTreeExpanded);
    m_PendingAssetBrowserTreeExpanded.clear();
    if (!m_PendingAssetBrowserFolder.empty())
        m_AssetBrowser->TryRestoreFolder(m_PendingAssetBrowserFolder);
    m_PendingAssetBrowserFolder.clear();

    m_FallbackGameCamera.SetPosition({ 0.0f, 2.0f, 6.0f });
    m_FallbackGameCamera.LookAt({ 0.0f, 0.0f, 0.0f });

    m_SceneViewPsxBaseline = m_Engine->GetRenderer().GetPS1Settings();
    if (!m_Engine->IsPlayerMode() && std::filesystem::exists(PathUtf8::FromString(m_SceneFilePath))) {
        LoadScene(false);
    }
    RefreshSavedSceneState();
    m_UndoStack.Clear();
    m_UndoStack.PushState(m_Engine->GetScene());
    m_HadActiveEdit = false;

    if (!m_Engine->IsPlayerMode()) {
        m_CommandHost = std::make_unique<Command::EditorCommandHost>(*m_Engine);
        std::string commandError;
        if (!m_CommandHost->Start(commandError))
            MIPSYNC_WARN("Command Platform IPC unavailable: {}", commandError);
        else
            MIPSYNC_INFO("Command Platform ready for project: {}", m_Engine->GetProjectPath());
    }
}

void EditorApp::Shutdown() {
    if (m_CommandHost) {
        m_CommandHost->Stop();
        m_CommandHost.reset();
    }
    if (s_EditorForWindowClose == this)
        s_EditorForWindowClose = nullptr;

    if (ImGui::GetCurrentContext() != nullptr) {
        if (const char* iniPath = ImGui::GetIO().IniFilename)
            ImGui::SaveIniSettingsToDisk(iniPath);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
}

void EditorApp::TickPendingPlaySetup() {
    if (!m_PendingPhysicsBeginPlay)
        return;
    m_PendingPhysicsBeginPlay = false;
    MIPSYNC_INFO("Editor: deferred Physics BeginPlay");
    m_Engine->GetPhysicsWorld().BeginPlay(m_Engine->GetScene());
    MIPSYNC_INFO("Editor: deferred Physics BeginPlay done");
}

void EditorApp::StartPlayMode() {
    if (m_IsPlaying)
        return;
    Scene& scene = m_Engine->GetScene();
    auto& mips = m_Engine->GetMipsRuntime();
    ResetShotCameraState();
    ReloadAllSceneAnimators(scene);
    mips.OnPlayStarted(scene);
    m_Engine->GetAudioSystem().BeginPlay(scene, m_Engine->GetProjectPath());
    m_PendingPhysicsBeginPlay = true;
    m_IsPlaying = true;
    m_IsPaused = false;
    m_StepOneFrame = false;
}

void AddUniqueEdge(std::vector<std::pair<uint32_t, uint32_t>>& edges,
                   uint32_t a, uint32_t b) {
    if (a > b) std::swap(a, b);
    const std::pair<uint32_t, uint32_t> edge{a, b};
    if (std::find(edges.begin(), edges.end(), edge) == edges.end())
        edges.push_back(edge);
}

bool DrawAssetReferenceField(EditorApp* editor, const char* label, AssetKind expectedKind,
                             std::string& path, const char* emptyText) {
    bool changed = false;
    ImGui::PushID(label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(112.0f);

    const float clearWidth = ImGui::GetFrameHeight();
    const float fieldWidth = std::max(70.0f, ImGui::GetContentRegionAvail().x - clearWidth - 4.0f);
    std::string display = emptyText;
    if (!path.empty()) {
        display = PathUtf8::ToString(PathUtf8::FromString(path).filename());
        if (display.empty()) display = path;
    }

    ImGui::PushStyleColor(ImGuiCol_Button, EditorTheme::InputBg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorTheme::BtnFaceLight);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, EditorTheme::BtnFace);
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.04f, 0.5f));
    if (ImGui::Button(display.c_str(), ImVec2(fieldWidth, 0.0f)) && !path.empty()) {
        if (editor && editor->GetAssetBrowser()) {
            editor->GetAssetBrowser()->RevealAndSelectAsset(path);
            editor->FocusDockedWindow("Project");
        }
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DragDrop::kAssetMove);
        if (!payload) payload = ImGui::AcceptDragDropPayload(DragDrop::kAssetScript);
        if (!payload) payload = ImGui::AcceptDragDropPayload(DragDrop::kAssetAnimatorController);
        if (!payload) payload = ImGui::AcceptDragDropPayload(DragDrop::kAssetModel);
        if (!payload) payload = ImGui::AcceptDragDropPayload(DragDrop::kAssetTexture);
        if (!payload) payload = ImGui::AcceptDragDropPayload(DragDrop::kAssetMaterial);
        if (!payload) payload = ImGui::AcceptDragDropPayload(DragDrop::kAssetAudio);
        if (payload) {
            const auto paths = PathsFromDragPayload(payload);
            if (paths.size() == 1 &&
                AssetBrowserPanel::ClassifyAssetByPath(paths.front()) == expectedKind) {
                path = paths.front();
                changed = true;
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::SameLine(0.0f, 4.0f);
    ImGui::BeginDisabled(path.empty());
    if (ImGui::Button("x", ImVec2(clearWidth, 0.0f))) {
        path.clear();
        changed = true;
    }
    ImGui::EndDisabled();
    ImGui::PopID();
    return changed;
}

void DrawInspectorCubeIcon(ImDrawList* drawList, ImVec2 center, float size, ImU32 color) {
    const float h = size * 0.5f;
    const ImVec2 a{center.x - h, center.y - h * 0.45f};
    const ImVec2 b{center.x, center.y - h};
    const ImVec2 c{center.x + h, center.y - h * 0.45f};
    const ImVec2 d{center.x, center.y};
    const ImVec2 e{center.x - h, center.y + h * 0.55f};
    const ImVec2 f{center.x, center.y + h};
    const ImVec2 g{center.x + h, center.y + h * 0.55f};
    drawList->AddLine(a, b, color, 1.5f); drawList->AddLine(b, c, color, 1.5f);
    drawList->AddLine(a, d, color, 1.5f); drawList->AddLine(c, d, color, 1.5f);
    drawList->AddLine(a, e, color, 1.5f); drawList->AddLine(c, g, color, 1.5f);
    drawList->AddLine(d, f, color, 1.5f); drawList->AddLine(e, f, color, 1.5f);
    drawList->AddLine(f, g, color, 1.5f);
}

void EditorApp::StopPlayMode() {
    if (!m_IsPlaying)
        return;
    m_PendingPhysicsBeginPlay = false;
    m_Engine->GetMipsRuntime().OnPlayStopped(m_Engine->GetScene());
    m_Engine->GetAudioSystem().EndPlay();
    m_Engine->GetPhysicsWorld().EndPlay();
    m_IsPlaying = false;
    m_IsPaused = false;
    m_StepOneFrame = false;
    m_PlayCursorLocked = false;
    ResetShotCameraState();
    Input::SetCursorLocked(false);
}

void EditorApp::TickAutoPlayOnStart() {
    if (!m_AutoPlayOnStart || m_AutoPlayTriggered)
        return;
    m_AutoPlayTriggered = true;
    StartPlayMode();
}

void EditorApp::BeginFrame() {

    if (m_CommandHost)
        m_CommandHost->Pump();

    const ImVec4 clear = EditorTheme::GetClearColor();
    glViewport(0, 0, m_Engine->GetWindow().GetWidth(), m_Engine->GetWindow().GetHeight());
    glClearColor(clear.x, clear.y, clear.z, clear.w);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
}

void EditorApp::EndFrame() {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)m_Engine->GetWindow().GetWidth(), (float)m_Engine->GetWindow().GetHeight());

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        GLFWwindow* backup_current_context = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
#ifdef _WIN32
        Win32AppIcon::ApplyToImGuiViewports();
#endif
        glfwMakeContextCurrent(backup_current_context);
    }
}

void EditorApp::OnImGuiRender() {
    if (m_RequestQuitAfterSaveDialog) {
        m_RequestQuitAfterSaveDialog = false;
        if (m_RequestRestartAfterSaveDialog) {
            m_RequestRestartAfterSaveDialog = false;
            TriggerRestart();
        } else {
            m_Engine->Quit();
        }
    }

    if (m_Engine->IsPlayerMode()) {
        DrawPlayerModeUI();
        UpdatePlayInputState();
        return;
    }

    if (m_PendingDockLayoutReset) {
        m_PendingDockLayoutReset = false;
        ResetDockLayout();
    }

    UpdateSceneDirtyState();
    DrawSaveOnExitModal();
    DrawFontRestartModal();

    SetupDockspace();

    ApplyLoadedTabIdsToNodes();

    DrawSceneViewConsolePanels();
    DrawGameViewPanel();
    DrawHierarchyPanel();
    DrawInspectorPanel();
    DrawProjectPanel();
    DrawProModelerWindow();

    if (m_BuildSettings)
        m_BuildSettings->Draw();

    DrawBuildToast();

    UpdatePlayInputState();

    if (!m_InitialTabSelectionRestored) {
        if (m_LoadedTabSelections.empty()) {
            m_InitialTabSelectionRestored = true;
        } else {
            ApplySavedTabSelections();

            bool allRestored = true;
            for (const auto& [nodeId, windowName] : m_LoadedTabSelections) {
                ImGuiDockNode* node = ImGui::DockBuilderGetNode(nodeId);
                if (!node || !node->TabBar)
                    continue;

                ImGuiWindow* window = ImGui::FindWindowByName(windowName.c_str());
                if (!window || node->TabBar->SelectedTabId != window->TabId) {
                    allRestored = false;
                    break;
                }
            }
            if (allRestored)
                m_InitialTabSelectionRestored = true;
        }
    }

    if (m_InitialTabSelectionRestored)
        CollectDockTabSelections();

    HandleEditorShortcuts();
    UpdateUndoRecording();

    EnsureAnimatorControllerDocked();

    ImGui::End();
}

void EditorApp::SetupDockspace() {
    static bool opt_fullscreen = true;
    static bool opt_padding = false;
    static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    if (opt_fullscreen) {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    }

    if (!opt_padding)
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("MipsyncDockspace", nullptr, window_flags);

    if (!opt_padding)
        ImGui::PopStyleVar();

    if (opt_fullscreen)
        ImGui::PopStyleVar(2);

    {
        const ImVec2 p0 = ImGui::GetWindowPos();
        const ImVec2 p1 = ImVec2(p0.x + ImGui::GetWindowWidth(), p0.y + ImGui::GetWindowHeight());
        EditorTheme::DrawCanvasBackground(ImGui::GetWindowDrawList(), p0, p1);
    }

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) SaveScene();
            if (ImGui::MenuItem("Load Scene", "Ctrl+O")) LoadScene();
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) m_Engine->Quit();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Build")) {
            if (ImGui::MenuItem("Build Settings...", "Ctrl+Shift+B")) OpenBuildSettings();
            if (ImGui::MenuItem("Build PS1")) {
                std::string msg;
                const bool ok = m_BuildSettings->RunBuild(msg);
                TriggerBuildToast(msg.empty() ? std::string{ok ? "Build complete." : "Build failed."}
                                              : msg,
                                  ok);
            }
            if (ImGui::MenuItem("Build PS1 && Run in Emulator")) {
                std::string msg;
                const bool ok = m_BuildSettings->RunBuildAndRun(msg);
                TriggerBuildToast(msg.empty() ? std::string{ok ? "Launched emulator." : "Build/Run failed."}
                                              : msg,
                                  ok);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Build PC Native")) {
                std::string msg;
                const bool ok = m_BuildSettings->RunPcNativeBuild(msg);
                TriggerBuildToast(msg.empty() ? std::string{ok ? "PC native build complete." : "PC native build failed."}
                                              : msg,
                                  ok);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            std::string err;
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, m_UndoStack.CanUndo())) {
                if (m_UndoStack.Undo(m_Engine->GetScene(), err))
                    AfterSceneRestoredFromHistory();
                else if (!err.empty())
                    MIPSYNC_WARN("Undo failed: {}", err);
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, m_UndoStack.CanRedo())) {
                if (m_UndoStack.Redo(m_Engine->GetScene(), err))
                    AfterSceneRestoredFromHistory();
                else if (!err.empty())
                    MIPSYNC_WARN("Redo failed: {}", err);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Preferences")) {
            DrawPreferencesMenu();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Window")) {
            if (ImGui::MenuItem("Animation")) {
                OpenAnimationWindow();
            }
            DrawLayoutMenu();
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }

    DrawMenuBarPlayButton();

    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
        ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);

        if (m_PendingDockLayoutApply) {
            ApplyDockLayout(m_DockLayoutPreset, dockspace_id);
            m_PendingDockLayoutApply = false;
            m_DockLayoutInitialized = true;
        } else if (!m_DockLayoutInitialized) {
            m_DockLayoutInitialized = true;
            if (!HasSavedLayout())
                ApplyDockLayout(m_DockLayoutPreset, dockspace_id);
        }
    }
}

void EditorApp::DrawLayoutMenu() {
    if (ImGui::BeginMenu("Layout")) {
        const bool isDefault = m_DockLayoutPreset == DockLayoutPreset::ClassicEditor;
        const bool isSceneTop = m_DockLayoutPreset == DockLayoutPreset::SceneTopGameBottom;

        if (ImGui::MenuItem("Scene Top / Game Bottom", nullptr, isSceneTop)) {
            if (!isSceneTop) {
                m_DockLayoutPreset = DockLayoutPreset::SceneTopGameBottom;
                RequestDockLayoutApply();
            }
        }
        if (ImGui::MenuItem("Classic Editor", nullptr, isDefault)) {
            if (!isDefault) {
                m_DockLayoutPreset = DockLayoutPreset::ClassicEditor;
                RequestDockLayoutApply();
            }
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Reset Layout")) {
            m_PendingDockLayoutReset = true;
        }

        ImGui::EndMenu();
    }
}

namespace {

/// Strips [Docking] and per-window DockId= lines so factory layout is not overridden.
void StripDockingSectionFromIni(const std::string& iniPath) {
    if (iniPath.empty())
        return;

    std::ifstream in(iniPath);
    if (!in.is_open())
        return;

    std::ostringstream out;
    bool skipDocking = false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("[Docking]", 0) == 0) {
            skipDocking = true;
            continue;
        }
        if (skipDocking) {
            if (!line.empty() && line.front() == '[')
                skipDocking = false;
            else
                continue;
        }
        if (line.rfind("DockId=", 0) == 0)
            continue;

        out << line << '\n';
    }

    std::ofstream file(iniPath, std::ios::trunc);
    if (file.is_open())
        file << out.str();
}

void SetDockNodeSelectedTab(ImGuiID nodeId, ImGuiID tabId) {
    ImGuiDockNode* node = ImGui::DockBuilderGetNode(nodeId);
    if (!node)
        return;

    node->SelectedTabId = tabId;
    if (node->TabBar) {
        node->TabBar->SelectedTabId = tabId;
        node->TabBar->NextSelectedTabId = tabId;
    }
}

} // namespace

void EditorApp::ResetDockLayout() {
    StripDockingSectionFromIni(m_LayoutIniPath);
    m_AnimatorControllerDockEnsured = false;
    RequestDockLayoutApply();
    m_DockLayoutInitialized = true;
}

void EditorApp::ApplyDockLayout(DockLayoutPreset preset, ImGuiID dockspaceId) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

    ImGuiID dockMainId = dockspaceId;
    m_SceneConsoleDockNodeId = 0;
    m_ProjectDockNodeId = 0;
    m_ForceSceneViewTab = false;
    m_ForceProjectTab = false;

    switch (preset) {
    case DockLayoutPreset::SceneTopGameBottom: {
        // ~43% Scene+Game (stacked) | ~57% Hierarchy | Project | Inspector (equal thirds).
        constexpr float kViewportsWidth = 0.43f;       // left column share
        constexpr float kRightStripWidth = 1.0f - kViewportsWidth;
        constexpr float kSceneGameSplit = 0.50f;       // Scene top, Game bottom
        constexpr float kSidePanelThird = 1.0f / 3.0f; // equal width per side column

        ImGuiID dockIdRightStrip = 0;
        ImGuiID dockIdLeftColumn = 0;
        ImGuiID dockIdGame = 0;
        ImGuiID dockIdScene = 0;
        ImGuiID dockIdHierarchy = 0;
        ImGuiID dockIdProject = 0;
        ImGuiID dockIdInspector = 0;
        ImGuiID dockIdRightMid = 0;

        dockMainId = dockspaceId;
        ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Right, kRightStripWidth, &dockIdRightStrip,
                                    &dockIdLeftColumn);
        ImGui::DockBuilderSplitNode(dockIdLeftColumn, ImGuiDir_Down, kSceneGameSplit, &dockIdGame, &dockIdScene);
        ImGui::DockBuilderSplitNode(dockIdRightStrip, ImGuiDir_Right, kSidePanelThird, &dockIdInspector,
                                    &dockIdRightMid);
        ImGui::DockBuilderSplitNode(dockIdRightMid, ImGuiDir_Right, 0.50f, &dockIdProject, &dockIdHierarchy);

        ImGui::DockBuilderDockWindow("Console", dockIdScene);
        ImGui::DockBuilderDockWindow("Scene View", dockIdScene);
        ImGui::DockBuilderDockWindow(kAnimatorControllerWindowTitle, dockIdScene);
        ImGui::DockBuilderDockWindow("Game View", dockIdGame);
        ImGui::DockBuilderDockWindow("Scene Hierarchy", dockIdHierarchy);
        ImGui::DockBuilderDockWindow("Project", dockIdProject);
        ImGui::DockBuilderDockWindow("Inspector", dockIdInspector);

        m_SceneConsoleDockNodeId = dockIdScene;
        m_ForceSceneViewTab = true;
        m_ProjectDockNodeId = dockIdProject;
        m_ForceProjectTab = true;
        break;
    }
    case DockLayoutPreset::ClassicEditor:
    default: {
        ImGuiID dockIdBottom = 0;
        ImGuiID dockIdLeft = 0;
        ImGuiID dockIdRight = 0;

        dockMainId = dockspaceId;
        ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Down, 0.30f, &dockIdBottom, &dockMainId);
        ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Left, 0.20f, &dockIdLeft, &dockMainId);
        ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Right, 0.25f, &dockIdRight, &dockMainId);

        ImGui::DockBuilderDockWindow("Scene View", dockMainId);
        ImGui::DockBuilderDockWindow("Game View", dockMainId);
        ImGui::DockBuilderDockWindow(kAnimatorControllerWindowTitle, dockMainId);
        ImGui::DockBuilderDockWindow("Scene Hierarchy", dockIdLeft);
        ImGui::DockBuilderDockWindow("Inspector", dockIdRight);
        ImGui::DockBuilderDockWindow("Console", dockIdBottom);
        ImGui::DockBuilderDockWindow("Project", dockIdBottom);

        m_SceneConsoleDockNodeId = dockMainId;
        m_ForceSceneViewTab = true;
        m_ProjectDockNodeId = dockIdBottom;
        m_ForceProjectTab = true;
        break;
    }
    }

    ImGui::DockBuilderFinish(dockspaceId);
    m_AnimatorControllerDockEnsured = true;
}

void EditorApp::RequestDockLayoutApply() {
    m_AnimatorControllerDockEnsured = false;
    m_PendingDockLayoutApply = true;
    m_InitialTabSelectionRestored = false;
    m_LoadedTabSelections.clear();
    m_SavedTabSelections.clear();
}

ImGuiID EditorApp::CalcWindowTabId(const char* windowName) {
    const ImGuiID windowId = ImHashStr(windowName);
    return ImHashStr("#TAB", 4, windowId);
}

std::string EditorApp::GetSceneConsolePreferredTab() const {
    if (m_ForceSceneViewTab)
        return "Scene View";

    const auto& selections = m_InitialTabSelectionRestored ? m_SavedTabSelections : m_LoadedTabSelections;
    for (const auto& [nodeId, name] : selections) {
        if (name == "Scene View" || name == "Console")
            return name;
    }
    return "Scene View";
}

void EditorApp::EnsureSceneViewTabSelected() {
    if (!m_ForceSceneViewTab || m_SceneConsoleDockNodeId == 0)
        return;

    SetDockNodeSelectedTab(m_SceneConsoleDockNodeId, CalcWindowTabId("Scene View"));
    m_ForceSceneViewTab = false;
}

void EditorApp::EnsureProjectTabSelected() {
    if (!m_ForceProjectTab || m_ProjectDockNodeId == 0)
        return;

    SetDockNodeSelectedTab(m_ProjectDockNodeId, CalcWindowTabId("Project"));
    m_ForceProjectTab = false;
}

void EditorApp::ApplyLoadedTabIdsToNodes() {
    if (m_ForceSceneViewTab) {
        EnsureSceneViewTabSelected();
    }
    if (m_ForceProjectTab) {
        EnsureProjectTabSelected();
    }
    if (m_ForceSceneViewTab || m_ForceProjectTab) {
        return;
    }

    if (m_InitialTabSelectionRestored || m_LoadedTabSelections.empty())
        return;

    for (const auto& [nodeId, windowName] : m_LoadedTabSelections) {
        ImGuiDockNode* node = ImGui::DockBuilderGetNode(nodeId);
        if (!node)
            continue;

        const ImGuiID tabId = CalcWindowTabId(windowName.c_str());
        node->SelectedTabId = tabId;
        if (node->TabBar)
            node->TabBar->SelectedTabId = node->TabBar->NextSelectedTabId = tabId;
    }
}

void EditorApp::EnsureAnimatorControllerDocked() {
    if (m_AnimatorControllerDockEnsured)
        return;
    if (!(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable))
        return;

    ImGuiWindow* sceneWin = ImGui::FindWindowByName("Scene View");
    if (!sceneWin || !sceneWin->DockNode)
        return;

    ImGuiWindow* animWin = ImGui::FindWindowByName(kAnimatorControllerWindowTitle);
    if (animWin && animWin->DockNode == sceneWin->DockNode) {
        m_SceneConsoleDockNodeId = sceneWin->DockNode->ID;
        m_AnimatorControllerDockEnsured = true;
        return;
    }

    const ImGuiID dockspaceId = ImGui::GetID("MyDockSpace");
    ImGui::DockBuilderDockWindow(kAnimatorControllerWindowTitle, sceneWin->DockNode->ID);
    ImGui::DockBuilderFinish(dockspaceId);

    animWin = ImGui::FindWindowByName(kAnimatorControllerWindowTitle);
    if (animWin && animWin->DockNode == sceneWin->DockNode) {
        m_SceneConsoleDockNodeId = sceneWin->DockNode->ID;
        m_AnimatorControllerDockEnsured = true;
    }
}

void EditorApp::DrawSceneViewConsolePanels() {
    // ImGui selects the last tab added on the first frame; draw the preferred tab last.
    if (GetSceneConsolePreferredTab() == "Console") {
        DrawSceneViewPanel();
        DrawAnimatorControllerPanel();
        if (m_ShowAnimationWindow) DrawAnimationPanel();
        DrawConsolePanel();
    } else {
        DrawConsolePanel();
        if (m_ShowAnimationWindow) DrawAnimationPanel();
        DrawAnimatorControllerPanel();
        DrawSceneViewPanel();
    }
    EnsureSceneViewTabSelected();
}

bool EditorApp::HasSavedLayout() const {
    std::ifstream file(m_LayoutIniPath);
    if (!file.is_open())
        return false;

    std::string line;
    while (std::getline(file, line)) {
        if (line.rfind("[Docking]", 0) == 0)
            return true;
    }
    return false;
}

void EditorApp::RegisterSettingsHandler() {
    ImGuiSettingsHandler handler{};
    handler.TypeName = "Editor";
    handler.TypeHash = ImHashStr("Editor");
    handler.UserData = this;
    handler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler* handlerPtr, const char*) -> void* {
        return handlerPtr->UserData;
    };
    handler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler* handlerPtr, void*, const char* line) {
        auto* app = static_cast<EditorApp*>(handlerPtr->UserData);
        int preset = 0;
        if (std::sscanf(line, "LayoutPreset=%d", &preset) == 1) {
            app->m_DockLayoutPreset = static_cast<DockLayoutPreset>(preset);
            return;
        }

        unsigned int selectedId = 0;
        if (std::sscanf(line, "SelectedEntity=%u", &selectedId) == 1) {
            app->m_PendingSelectedEntityID = selectedId;
            return;
        }
        if (std::sscanf(line, "SelectedEntitySet=%u", &selectedId) == 1) {
            app->m_PendingSelectedEntityIDs.insert(selectedId);
            return;
        }

        unsigned int nodeId = 0;
        char windowName[256] = {};
        if (std::sscanf(line, "TabSelection_%08X=%255[^\r\n]", &nodeId, windowName) == 2)
            app->m_LoadedTabSelections[nodeId] = windowName;

        char folderPath[512] = {};
        if (std::sscanf(line, "AssetBrowserFolder=%511[^\r\n]", folderPath) == 1)
            app->m_PendingAssetBrowserFolder = folderPath;

        char treePath[512] = {};
        if (std::sscanf(line, "TreeExpanded=%511[^\r\n]", treePath) == 1)
            app->m_PendingAssetBrowserTreeExpanded.push_back(treePath);

        int psxJit = 1, psxAff = 1, psx15b = 1, psxDith = 1, psxMesh = 1;
        if (std::sscanf(line, "SceneViewPsxJit=%d", &psxJit) == 1)
            app->m_SceneViewPsx.vertexJitter = (psxJit != 0);
        if (std::sscanf(line, "SceneViewPsxAff=%d", &psxAff) == 1)
            app->m_SceneViewPsx.affineMapping = (psxAff != 0);
        if (std::sscanf(line, "SceneViewPsx15b=%d", &psx15b) == 1)
            app->m_SceneViewPsx.colorDepthLimit = (psx15b != 0);
        if (std::sscanf(line, "SceneViewPsxDith=%d", &psxDith) == 1)
            app->m_SceneViewPsx.dithering = (psxDith != 0);
        if (std::sscanf(line, "SceneViewPsxMesh=%d", &psxMesh) == 1)
            app->m_SceneViewPsx.meshPreview = (psxMesh != 0);

        float px = 0, py = 0, pz = 0, dist = 0, yaw = 0, pitch = 0;
        if (std::sscanf(line, "SceneCamPivot=%f,%f,%f", &px, &py, &pz) == 3) {
            app->m_PendingSceneCamPivotPos = { px, py, pz };
            app->m_PendingSceneCamPivot = true;
        }
        if (std::sscanf(line, "SceneCamDistance=%f", &dist) == 1)
            app->m_PendingSceneCamDistance = dist;
        if (std::sscanf(line, "SceneCamYaw=%f", &yaw) == 1)
            app->m_PendingSceneCamYaw = yaw;
        if (std::sscanf(line, "SceneCamPitch=%f", &pitch) == 1)
            app->m_PendingSceneCamPitch = pitch;
    };
    handler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* handlerPtr, ImGuiTextBuffer* buf) {
        auto* app = static_cast<EditorApp*>(handlerPtr->UserData);
        app->CollectDockTabSelections();

        buf->appendf("[%s][Data]\n", handlerPtr->TypeName);
        buf->appendf("LayoutPreset=%d\n", static_cast<int>(app->m_DockLayoutPreset));
        buf->appendf("SelectedEntity=%u\n", app->m_SelectedEntityID);
        for (uint32_t id : app->m_SelectedEntityIDs)
            buf->appendf("SelectedEntitySet=%u\n", id);
        for (const auto& [nodeId, windowName] : app->m_SavedTabSelections)
            buf->appendf("TabSelection_%08X=%s\n", nodeId, windowName.c_str());
        if (app->m_AssetBrowser) {
            buf->appendf("AssetBrowserFolder=%s\n", app->m_AssetBrowser->GetCurrentFolder().c_str());
            for (const std::string& path : app->m_AssetBrowser->GetExpandedTreeFolderPaths())
                buf->appendf("TreeExpanded=%s\n", path.c_str());
        }

        const glm::vec3 pivot = app->m_SceneCamera.GetPivot();
        buf->appendf("SceneViewPsxJit=%d\n", app->m_SceneViewPsx.vertexJitter ? 1 : 0);
        buf->appendf("SceneViewPsxAff=%d\n", app->m_SceneViewPsx.affineMapping ? 1 : 0);
        buf->appendf("SceneViewPsx15b=%d\n", app->m_SceneViewPsx.colorDepthLimit ? 1 : 0);
        buf->appendf("SceneViewPsxDith=%d\n", app->m_SceneViewPsx.dithering ? 1 : 0);
        buf->appendf("SceneViewPsxMesh=%d\n", app->m_SceneViewPsx.meshPreview ? 1 : 0);
        buf->appendf("SceneCamPivot=%.6f,%.6f,%.6f\n", pivot.x, pivot.y, pivot.z);
        buf->appendf("SceneCamDistance=%.6f\n", app->m_SceneCamera.GetDistance());
        buf->appendf("SceneCamYaw=%.6f\n", app->m_SceneCamera.GetOrbitYaw());
        buf->appendf("SceneCamPitch=%.6f\n", app->m_SceneCamera.GetOrbitPitch());
    };
    ImGui::AddSettingsHandler(&handler);
}

void EditorApp::CollectDockTabSelections() {
    m_SavedTabSelections.clear();

    ImGuiContext& g = *ImGui::GetCurrentContext();
    for (int i = 0; i < g.DockContext.Nodes.Data.Size; ++i) {
        ImGuiDockNode* node = static_cast<ImGuiDockNode*>(g.DockContext.Nodes.Data[i].val_p);
        if (!node || !node->TabBar || node->TabBar->SelectedTabId == 0)
            continue;

        for (ImGuiWindow* window : g.Windows) {
            if (window->TabId == node->TabBar->SelectedTabId) {
                m_SavedTabSelections[node->ID] = window->Name;
                break;
            }
        }
    }
}

void EditorApp::ApplySavedTabSelections() {
    const auto& selections = m_LoadedTabSelections;
    if (selections.empty())
        return;

    for (const auto& [nodeId, windowName] : selections) {
        ImGuiDockNode* node = ImGui::DockBuilderGetNode(nodeId);
        if (!node || !node->TabBar)
            continue;

        ImGuiWindow* window = ImGui::FindWindowByName(windowName.c_str());
        if (!window)
            continue;

        if (ImGui::TabBarFindTabByID(node->TabBar, window->TabId) == nullptr)
            continue;

        node->SelectedTabId = window->TabId;
        node->TabBar->SelectedTabId = window->TabId;
        node->TabBar->NextSelectedTabId = window->TabId;
        ImGui::FocusWindow(window);
    }
}

void EditorApp::SaveScene() {
    std::string error;
    if (!CommandSaveScene(error))
        MIPSYNC_WARN("Scene save failed: {}", error);
}

bool EditorApp::CommandSaveScene(std::string& outError) {
    if (m_IsPlaying) {
        m_Engine->GetMipsRuntime().OnPlayStopped(m_Engine->GetScene());
        m_Engine->GetAudioSystem().EndPlay();
        m_Engine->GetPhysicsWorld().EndPlay();
        m_IsPlaying = false;
        m_IsPaused = false;
        m_PlayCursorLocked = false;
        ResetShotCameraState();
        Input::SetCursorLocked(false);
    }

    if (SceneIO::SaveToFile(m_Engine->GetScene(), m_SceneFilePath, outError)) {
        MIPSYNC_INFO("Scene saved: {}", m_SceneFilePath);
        PersistEditorLastScene();
        m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
        RefreshSavedSceneState();
        return true;
    }
    return false;
}

void EditorApp::PersistEditorLastScene() {
    if (!m_Engine || m_Engine->IsPlayerMode() || m_SceneFilePath.empty())
        return;

    ProjectInfo projectInfo;
    std::string err;
    const std::string projectPath = m_Engine->GetProjectPath();
    if (projectPath.empty() || !Project::LoadFromDir(projectPath, projectInfo, err))
        return;

    const std::string rel = AssetManager::Get().ToProjectRelative(m_SceneFilePath);
    if (rel.empty() || rel == projectInfo.editorLastScene)
        return;

    projectInfo.editorLastScene = rel;
    if (!Project::SaveToDir(projectInfo, err))
        MIPSYNC_WARN("Failed to save last open scene: {}", err);
}

void EditorApp::OpenBuildSettings() {
    if (!m_BuildSettings)
        return;
    m_BuildSettings->ReloadFromDisk();
    m_BuildSettings->SetOpen(true);
}

void EditorApp::LoadSceneFromPath(const std::string& absolutePath, bool restartPlayIfWasActive) {
    if (absolutePath.empty())
        return;
    m_SceneFilePath = absolutePath;
    LoadScene(restartPlayIfWasActive);
}

void EditorApp::LoadScene(bool restartPlayIfWasActive) {
    const bool wasPlaying = m_IsPlaying;
    if (m_IsPlaying) {
        m_Engine->GetMipsRuntime().OnPlayStopped(m_Engine->GetScene());
        m_Engine->GetAudioSystem().EndPlay();
        m_Engine->GetPhysicsWorld().EndPlay();
        m_IsPlaying = false;
        m_IsPaused = false;
        m_PlayCursorLocked = false;
        ResetShotCameraState();
        if (!restartPlayIfWasActive)
            Input::SetCursorLocked(false);
    }

    std::string error;
    if (SceneIO::LoadFromFile(m_Engine->GetScene(), m_SceneFilePath, error)) {
        ResetShotCameraState();
        ClearEntitySelection();
        if (m_PendingSelectedEntityID != 0 &&
            m_Engine->GetScene().FindEntity(m_PendingSelectedEntityID)) {
            m_SelectedEntityID = m_PendingSelectedEntityID;
            m_HierarchySelectionAnchor = m_SelectedEntityID;
        }
        for (uint32_t id : m_PendingSelectedEntityIDs) {
            if (m_Engine->GetScene().FindEntity(id))
                m_SelectedEntityIDs.insert(id);
        }
        if (m_SelectedEntityID != 0)
            m_SelectedEntityIDs.insert(m_SelectedEntityID);
        m_PendingSelectedEntityID = 0;
        m_PendingSelectedEntityIDs.clear();
        SyncAnimatorWindowToSelectedEntity();
        m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
        RefreshSavedSceneState();
        m_UndoStack.Clear();
        m_UndoStack.PushState(m_Engine->GetScene());
        m_HadActiveEdit = false;
        MIPSYNC_INFO("Scene loaded: {}", m_SceneFilePath);
        PersistEditorLastScene();

        if (restartPlayIfWasActive && wasPlaying) {
            StartPlayMode();
        }
    } else {
        MIPSYNC_WARN("Scene load failed: {}", error);
    }
}

void EditorApp::RefreshSavedSceneState() {
    std::string err;
    if (!SceneIO::SerializeSceneFingerprint(m_Engine->GetScene(), m_SceneSavedFingerprint, err))
        MIPSYNC_WARN("Scene fingerprint failed: {}", err);
    m_SceneDirty = false;
}

void EditorApp::UpdateSceneDirtyState() {
    if (m_IsPlaying)
        return;

    std::string current;
    std::string err;
    if (!SceneIO::SerializeSceneFingerprint(m_Engine->GetScene(), current, err))
        return;

    m_SceneDirty = (current != m_SceneSavedFingerprint);
}

bool EditorApp::OnWindowCloseRequested() {
    if (m_Engine->IsPlayerMode())
        return false;

    UpdateSceneDirtyState();
    if (!m_SceneDirty)
        return false;

    m_OpenSaveOnExitModal = true;
    return true;
}

void EditorApp::DrawSaveOnExitModal() {
    if (m_OpenSaveOnExitModal)
        ImGui::OpenPopup("SaveSceneBeforeExit");

    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("SaveSceneBeforeExit", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextColored(EditorTheme::TextPrimary,
        "The scene has unsaved changes.\nDo you want to save before closing?");
    ImGui::Spacing();

    const float bw = 110.0f;
    if (EditorTheme::AeroButton("Save", ImVec2(bw, EditorTheme::ButtonHeight), AeroButtonKind::Primary)) {
        SaveScene();
        ImGui::CloseCurrentPopup();
        m_OpenSaveOnExitModal = false;
        if (!m_SceneDirty)
            m_RequestQuitAfterSaveDialog = true;
    }
    ImGui::SameLine();
    if (EditorTheme::AeroButton("Don't Save", ImVec2(bw, EditorTheme::ButtonHeight), AeroButtonKind::Danger)) {
        ImGui::CloseCurrentPopup();
        m_OpenSaveOnExitModal = false;
        m_RequestQuitAfterSaveDialog = true;
    }
    ImGui::SameLine();
    if (EditorTheme::AeroButton("Cancel", ImVec2(bw, EditorTheme::ButtonHeight), AeroButtonKind::Secondary)) {
        ImGui::CloseCurrentPopup();
        m_OpenSaveOnExitModal = false;
    }

    ImGui::EndPopup();
}

void EditorApp::DrawFontRestartModal() {
    if (m_OpenFontRestartModal) {
        ImGui::OpenPopup("FontRestartRequired");
    }

    ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("FontRestartRequired", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextColored(EditorTheme::TextPrimary, "Font size settings have been changed.\nPlease restart the editor to apply changes.");
    ImGui::Spacing();

    const float bw = 80.0f;
    if (EditorTheme::AeroButton("OK", ImVec2(bw, EditorTheme::ButtonHeight), AeroButtonKind::Primary)) {
        ImGui::CloseCurrentPopup();
        m_OpenFontRestartModal = false;
        if (m_SceneDirty) {
            m_RequestRestartAfterSaveDialog = true;
            m_OpenSaveOnExitModal = true;
        } else {
            TriggerRestart();
        }
    }

    ImGui::EndPopup();
}

void EditorApp::TriggerRestart() {
#ifdef _WIN32
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    if (CreateProcessW(exePath, nullptr, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        m_Engine->Quit();
    } else {
        MIPSYNC_WARN("Failed to restart editor: error {}", GetLastError());
    }
#else
    m_Engine->Quit();
#endif
}

void EditorApp::UpdatePlayInputState() {
    if (!m_IsPlaying) {
        Input::SetGameInputEnabled(true);
        if (m_PlayCursorLocked) {
            Input::SetCursorLocked(false);
            m_PlayCursorLocked = false;
        }
        return;
    }

    if (Input::IsKeyPressed(GLFW_KEY_ESCAPE))
        m_PlayCursorLocked = false;

    if (m_IsPaused) {
        Input::SetGameInputEnabled(false);
        Input::SetCursorLocked(false);
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const bool gameMouse = m_PlayCursorLocked ||
                           (m_GameViewHovered && !m_SceneViewHovered && !io.WantCaptureMouse);
    Input::SetGameInputEnabled(gameMouse);
    Input::SetCursorLocked(m_PlayCursorLocked);
}

void EditorApp::RecordUndoSnapshot() {
    if (m_IsPlaying)
        return;
    m_UndoStack.PushState(m_Engine->GetScene());
}

void EditorApp::AfterSceneRestoredFromHistory() {
    Scene& scene = m_Engine->GetScene();
    ResetShotCameraState();
    if (!scene.FindEntity(m_SelectedEntityID))
        m_SelectedEntityID = 0;
    m_Engine->GetMipsRuntime().SyncEditSnapshot(scene);
    if (m_AssetBrowser)
        m_AssetBrowser->ClearAssetSelection();
    UpdateSceneDirtyState();
}

void EditorApp::UpdateUndoRecording() {
    if (m_IsPlaying)
        return;

    const bool active = ImGui::IsAnyItemActive() || ImGuizmo::IsUsing();
    if (active && !m_HadActiveEdit)
        RecordUndoSnapshot();
    m_HadActiveEdit = active;
}

void EditorApp::HandleEditorShortcuts() {
    if (m_IsPlaying)
        return;

    const bool ctrl = Input::IsKeyPressed(GLFW_KEY_LEFT_CONTROL) ||
                      Input::IsKeyPressed(GLFW_KEY_RIGHT_CONTROL);
    if (!ctrl || ImGui::GetIO().WantTextInput)
        return;

    if (ImGui::IsKeyPressed(ImGuiKey_S, false))
        SaveScene();
    if (ImGui::IsKeyPressed(ImGuiKey_O, false))
        LoadScene();

    const bool shift = Input::IsKeyPressed(GLFW_KEY_LEFT_SHIFT) ||
                       Input::IsKeyPressed(GLFW_KEY_RIGHT_SHIFT);
    if (shift && ImGui::IsKeyPressed(ImGuiKey_B, false))
        OpenBuildSettings();
    if (shift && ImGui::IsKeyPressed(ImGuiKey_P, false) && m_BuildSettings) {
        std::string msg;
        const bool ok = m_BuildSettings->RunBuild(msg);
        TriggerBuildToast(
            msg.empty() ? std::string{ok ? "Build complete." : "Build failed."} : msg, ok);
    }

    std::string err;
    if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        if (m_UndoStack.Undo(m_Engine->GetScene(), err))
            AfterSceneRestoredFromHistory();
        else if (!err.empty())
            MIPSYNC_WARN("Undo failed: {}", err);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
        if (m_UndoStack.Redo(m_Engine->GetScene(), err))
            AfterSceneRestoredFromHistory();
        else if (!err.empty())
            MIPSYNC_WARN("Redo failed: {}", err);
    }
}

void EditorApp::HandlePlayModeShortcuts() {
    const bool ctrl = Input::IsKeyPressed(GLFW_KEY_LEFT_CONTROL) ||
                      Input::IsKeyPressed(GLFW_KEY_RIGHT_CONTROL);
    const bool shift = Input::IsKeyPressed(GLFW_KEY_LEFT_SHIFT) ||
                       Input::IsKeyPressed(GLFW_KEY_RIGHT_SHIFT);
    const bool alt = Input::IsKeyPressed(GLFW_KEY_LEFT_ALT) ||
                     Input::IsKeyPressed(GLFW_KEY_RIGHT_ALT);

    if (!ImGui::GetIO().WantTextInput && ctrl &&
        ImGui::IsKeyPressed(ImGuiKey_P, false)) {
        if (alt) {
            if (m_IsPlaying) {
                m_IsPaused = true;
                m_Engine->GetMipsRuntime().SetPaused(true);
                m_StepOneFrame = true;
            }
        } else if (shift) {
            if (m_IsPlaying) {
                m_IsPaused = !m_IsPaused;
                m_Engine->GetMipsRuntime().SetPaused(m_IsPaused);
            }
        } else if (m_IsPlaying) {
            StopPlayMode();
        } else {
            StartPlayMode();
        }
        return;
    }

    if (m_IsPlaying && ctrl && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_S, false))
            SaveScene();
        if (ImGui::IsKeyPressed(ImGuiKey_O, false))
            LoadScene();
    }

}

void EditorApp::TriggerBuildToast(const std::string& message, bool success) {
    m_BuildToastMessage = message;
    m_BuildToastSuccess = success;
    m_BuildToastExpires = ImGui::GetTime() + 8.0;
    if (success)
        MIPSYNC_INFO("{}", message);
    else
        MIPSYNC_WARN("{}", message);
}

void EditorApp::DrawBuildToast() {
    if (m_BuildToastMessage.empty())
        return;
    if (ImGui::GetTime() > m_BuildToastExpires) {
        m_BuildToastMessage.clear();
        return;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 size{std::min(560.0f, vp->WorkSize.x * 0.6f), 0.0f};
    const ImVec2 pos{vp->WorkPos.x + vp->WorkSize.x - 24.0f,
                     vp->WorkPos.y + vp->WorkSize.y - 24.0f};
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("##build_toast", nullptr, flags)) {
        const ImVec4 color = m_BuildToastSuccess ? ImVec4(0.4f, 0.9f, 0.5f, 1.0f)
                                                 : ImVec4(1.0f, 0.45f, 0.4f, 1.0f);
        ImGui::TextColored(color, "%s", m_BuildToastSuccess ? "Build" : "Build failed");
        ImGui::Separator();
        ImGui::TextWrapped("%s", m_BuildToastMessage.c_str());
        ImGui::Separator();
        if (ImGui::SmallButton("Dismiss"))
            m_BuildToastMessage.clear();
        ImGui::SameLine();
        if (ImGui::SmallButton("Open Build Settings...")) {
            OpenBuildSettings();
            m_BuildToastMessage.clear();
        }
    }
    ImGui::End();
}

// 笏笏笏 Editor-wide persistent settings 笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏



static std::filesystem::path EditorSettingsPath() {
    return GetEngineSettingsDirectory() / "editor_settings.json";
}

void EditorApp::LoadEditorSettings() {
    using json = nlohmann::json;
    std::ifstream f(EditorSettingsPath());
    if (!f.is_open()) {
        m_FontSize = EditorTheme::GetDefaultFontSizeForDisplay();
        return;
    }
    try {
        json j = json::parse(f);
        m_FontSize = j.value("fontSize", EditorTheme::GetDefaultFontSizeForDisplay());
        m_FontSize = std::clamp(m_FontSize, 8.0f, 32.0f);
    } catch (...) {
        m_FontSize = EditorTheme::GetDefaultFontSizeForDisplay();
    }
}

void EditorApp::SaveEditorSettings() {
    using json = nlohmann::json;
    json j;
    j["fontSize"] = m_FontSize;
    std::ofstream f(EditorSettingsPath());
    if (f.is_open())
        f << j.dump(2);
}

void EditorApp::DrawPreferencesMenu() {
    ImGui::Text("Font Size");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);

    static float s_StartFontSize = 0.0f;
    float fs = m_FontSize;
    if (ImGui::SliderFloat("##fontsize", &fs, 8.0f, 32.0f, "%.0f px")) {
        float rounded = std::round(fs);
        if (rounded != m_FontSize) {
            m_FontSize = rounded;
            SaveEditorSettings();
        }
    }
    if (ImGui::IsItemActivated()) {
        s_StartFontSize = m_FontSize;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        if (m_FontSize != s_StartFontSize) {
            m_OpenFontRestartModal = true;
        }
    }

    ImGui::Separator();
    const float autoSize = EditorTheme::GetDefaultFontSizeForDisplay();
    char autoLabel[64];
    std::snprintf(autoLabel, sizeof(autoLabel), "Reset to Auto (%.0f px)", autoSize);
    if (ImGui::MenuItem(autoLabel)) {
        if (m_FontSize != autoSize) {
            m_FontSize = autoSize;
            SaveEditorSettings();
            m_OpenFontRestartModal = true;
        }
    }
}

void EditorApp::DrawMenuBarPlayButton() {
    constexpr float toolbarHeight = 32.0f;
    constexpr float buttonWidth = 42.0f;
    constexpr float buttonHeight = 22.0f;
    constexpr float buttonGap = 4.0f;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.145f, 0.145f, 0.145f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 5.0f));
    ImGui::BeginChild("##MainPlayToolbar", ImVec2(0.0f, toolbarHeight), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    std::string sceneLabel = PathUtf8::ToString(
        PathUtf8::FromString(m_SceneFilePath).filename());
    if (sceneLabel.empty())
        sceneLabel = "Untitled Scene";
    if (m_SceneDirty)
        sceneLabel += " *";
    ImGui::SetCursorPos(ImVec2(8.0f, 5.0f));
    ImGui::SetNextItemWidth(282.0f);
    std::string pendingScenePath;
    if (ImGui::BeginCombo("##ToolbarScene", sceneLabel.c_str())) {
        std::vector<std::filesystem::path> sceneFiles;
        const std::filesystem::path scenesDir =
            PathUtf8::FromString(m_Engine->GetProjectPath()) / "scenes";
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator it(scenesDir, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec) || it->path().extension() != ".nscene")
                continue;
            sceneFiles.push_back(it->path());
        }
        std::sort(sceneFiles.begin(), sceneFiles.end());
        const std::filesystem::path current = PathUtf8::FromString(m_SceneFilePath);
        for (const std::filesystem::path& scenePath : sceneFiles) {
            const std::string label = PathUtf8::ToString(
                scenePath.lexically_relative(scenesDir));
            const bool selected = scenePath == current;
            if (ImGui::Selectable(label.c_str(), selected))
                pendingScenePath = PathUtf8::ToString(scenePath);
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (!pendingScenePath.empty())
        LoadSceneFromPath(pendingScenePath, m_IsPlaying);

    enum class TransportIcon { PlayStop, Pause, Step };
    auto transportButton = [&](const char* id, TransportIcon icon, bool enabled,
                               bool active) {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(id, ImVec2(buttonWidth, buttonHeight));
        const bool hovered = enabled && ImGui::IsItemHovered();
        const bool held = enabled && ImGui::IsItemActive();
        const bool pressed = enabled && ImGui::IsItemClicked();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const float visualInset = 1.0f;
        const ImVec2 visualMin(pos.x + visualInset, pos.y + visualInset);
        const ImVec2 max(pos.x + buttonWidth - visualInset,
                         pos.y + buttonHeight - visualInset);
        ImVec4 face = active ? UiTokens::Success : UiTokens::BgTertiary;
        if (icon == TransportIcon::PlayStop && m_IsPlaying)
            face = UiTokens::Danger;
        if (!enabled)
            face = UiTokens::BgPressed;
        else if (held)
            face = UiTokens::BgPressed;
        else if (hovered && !active)
            face = UiTokens::BgHover;
        draw->AddRectFilled(visualMin, max, ImGui::ColorConvertFloat4ToU32(face), 2.0f);
        draw->AddRect(visualMin, max,
                      ImGui::ColorConvertFloat4ToU32(enabled ? UiTokens::BorderHighlight
                                                            : UiTokens::Border),
                      2.0f);
        const ImU32 iconColor = ImGui::ColorConvertFloat4ToU32(
            enabled ? UiTokens::Icon : UiTokens::IconSecondary);
        const ImVec2 center((visualMin.x + max.x) * 0.5f,
                            (visualMin.y + max.y) * 0.5f);
        if (icon == TransportIcon::PlayStop) {
            if (m_IsPlaying) {
                draw->AddRectFilled(ImVec2(center.x - 3.0f, center.y - 3.0f),
                                    ImVec2(center.x + 3.0f, center.y + 3.0f), iconColor);
            } else {
                draw->AddTriangleFilled(ImVec2(center.x - 3.5f, center.y - 5.0f),
                                        ImVec2(center.x - 3.5f, center.y + 5.0f),
                                        ImVec2(center.x + 4.5f, center.y), iconColor);
            }
        } else if (icon == TransportIcon::Pause) {
            draw->AddRectFilled(ImVec2(center.x - 4.0f, center.y - 5.0f),
                                ImVec2(center.x - 2.0f, center.y + 5.0f), iconColor);
            draw->AddRectFilled(ImVec2(center.x + 2.0f, center.y - 5.0f),
                                ImVec2(center.x + 4.0f, center.y + 5.0f), iconColor);
        } else {
            draw->AddTriangleFilled(ImVec2(center.x - 5.0f, center.y - 4.0f),
                                    ImVec2(center.x - 5.0f, center.y + 4.0f),
                                    ImVec2(center.x + 1.0f, center.y), iconColor);
            draw->AddRectFilled(ImVec2(center.x + 3.0f, center.y - 5.0f),
                                ImVec2(center.x + 5.0f, center.y + 5.0f), iconColor);
        }
        return pressed;
    };

    const float transportWidth = buttonWidth * 3.0f + buttonGap * 2.0f;
    const float transportX = (ImGui::GetWindowWidth() - transportWidth) * 0.5f;
    ImGui::SetCursorPos(ImVec2(transportX, 5.0f));
    if (transportButton("##PlayStop", TransportIcon::PlayStop, true, m_IsPlaying)) {
        if (m_IsPlaying)
            StopPlayMode();
        else
            StartPlayMode();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Play / Stop  (Ctrl+P)");
    ImGui::SetCursorPos(ImVec2(transportX + buttonWidth + buttonGap, 5.0f));
    if (transportButton("##Pause", TransportIcon::Pause, m_IsPlaying, m_IsPaused)) {
        m_IsPaused = !m_IsPaused;
        m_Engine->GetMipsRuntime().SetPaused(m_IsPaused);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Pause / Resume  (Ctrl+Shift+P)");
    ImGui::SetCursorPos(ImVec2(transportX + (buttonWidth + buttonGap) * 2.0f, 5.0f));
    if (transportButton("##Step", TransportIcon::Step, m_IsPlaying, false)) {
        m_IsPaused = true;
        m_Engine->GetMipsRuntime().SetPaused(true);
        m_StepOneFrame = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Step one frame  (Ctrl+Alt+P)");

    constexpr float layoutWidth = 218.0f;
    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - layoutWidth - 8.0f, 5.0f));
    ImGui::SetNextItemWidth(layoutWidth);
    const char* layoutLabel = m_DockLayoutPreset == DockLayoutPreset::SceneTopGameBottom
        ? "Layout: Scene Top / Game Bottom"
        : "Layout: Classic Editor";
    if (ImGui::BeginCombo("##ToolbarLayout", layoutLabel)) {
        const bool sceneTop = m_DockLayoutPreset == DockLayoutPreset::SceneTopGameBottom;
        if (ImGui::Selectable("Scene Top / Game Bottom", sceneTop) && !sceneTop) {
            m_DockLayoutPreset = DockLayoutPreset::SceneTopGameBottom;
            RequestDockLayoutApply();
        }
        const bool classic = m_DockLayoutPreset == DockLayoutPreset::ClassicEditor;
        if (ImGui::Selectable("Classic Editor", classic) && !classic) {
            m_DockLayoutPreset = DockLayoutPreset::ClassicEditor;
            RequestDockLayoutApply();
        }
        ImGui::EndCombo();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void EditorApp::RenderSceneToFramebuffer(Framebuffer& fbo, const Camera& camera, ImVec2 size,
                                         uint32_t highlightEntityId, uint32_t activeCameraEntityId,
                                         bool sceneView3D, int layoutWidth, int layoutHeight) {
    if (size.x <= 0.0f || size.y <= 0.0f)
        return;

    int width = (int)size.x;
    int height = (int)size.y;
    if (fbo.GetWidth() != width || fbo.GetHeight() != height)
        fbo.Resize(width, height);

    Scene& scene = m_Engine->GetScene();
    if (sceneView3D)
        m_Engine->GetUIRenderer().SetPointerState(false, 0.0f, 0.0f, false, false, false);
    PS1Settings& psx = m_Engine->GetRenderer().GetPS1Settings();
    const PS1Settings savedPsx = psx;
    ApplyPostProcessVolumeSettings(psx);

    const bool usePs1PreviewMeshes = sceneView3D ? m_SceneViewPsx.meshPreview : true;
    const Texture* prerenderBackground =
        sceneView3D ? nullptr : ResolvePrerenderedBackgroundTexture(activeCameraEntityId);
    scene.Render(m_Engine->GetRenderer(), camera, &fbo, highlightEntityId,
                 usePs1PreviewMeshes, sceneView3D, prerenderBackground);
    scene.RenderUI(m_Engine->GetUIRenderer(), camera, width, height, &fbo, activeCameraEntityId,
                   sceneView3D, layoutWidth, layoutHeight);
    psx = savedPsx;

    // Thumbnails / outline passes can leave depth off or the wrong FBO bound.
    Renderer::RestoreDefaultOpenGLState();
    if (GLFWwindow* window = m_Engine->GetWindow().GetNativeWindow()) {
        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        if (fbW > 0 && fbH > 0)
            glViewport(0, 0, fbW, fbH);
    }
}

void EditorApp::ApplyPostProcessVolumeSettings(PS1Settings& settings) {
    if (!m_Engine)
        return;

    m_Engine->GetRenderer().ClearSkyboxTexture();

    PostProcessVolumeComponent* best = nullptr;
    for (const auto& entityPtr : m_Engine->GetScene().GetEntities()) {
        if (!entityPtr || !entityPtr->IsActive())
            continue;
        auto* post = entityPtr->GetComponent<PostProcessVolumeComponent>();
        if (!post || !post->enabled)
            continue;
        if (!best || post->priority >= best->priority)
            best = post;
    }
    if (!best)
        return;

    settings.fogEnabled = best->fogEnabled;
    settings.fogColor = best->fogColor;
    settings.fogStart = best->fogStart;
    settings.fogEnd = best->fogEnd;
    settings.colorGradingEnabled = best->colorGradingEnabled;
    settings.exposure = best->exposure;
    settings.contrast = best->contrast;
    settings.saturation = best->saturation;
    settings.colorFilter = best->colorFilter;
    settings.vignetteEnabled = best->vignetteEnabled;
    settings.vignetteColor = best->vignetteColor;
    settings.vignetteIntensity = best->vignetteIntensity;
    settings.vignetteSmoothness = best->vignetteSmoothness;

    if (best->skyboxEnabled && !best->skyboxTexturePath.empty()) {
        if (!best->skyboxTexture)
            best->skyboxTexture = AssetManager::Get().GetSkyboxTexture(best->skyboxTexturePath);
        if (best->skyboxTexture && best->skyboxTexture->GetID() != 0) {
            m_Engine->GetRenderer().SetSkyboxTexture(best->skyboxTexture.get(),
                                                     best->skyboxTint,
                                                     best->skyboxExposure,
                                                     best->skyboxRotationDegrees);
        }
    }
}

const Texture* EditorApp::ResolvePrerenderedBackgroundTexture(uint32_t cameraEntityId) {
    if (cameraEntityId == 0)
        return nullptr;

    Scene& scene = m_Engine->GetScene();
    Entity* cameraEntity = scene.FindEntity(cameraEntityId);
    if (!cameraEntity)
        return nullptr;

    auto* cameraComp = cameraEntity->GetComponent<CameraComponent>();
    if (!cameraComp || cameraComp->prerenderedBackgroundPath.empty())
        return nullptr;

    const std::string& raw = cameraComp->prerenderedBackgroundPath;
    const std::filesystem::path rawPath = PathUtf8::FromString(raw);
    const std::filesystem::path projectRoot = PathUtf8::FromString(AssetManager::Get().GetProjectRoot());
    const std::filesystem::path scenePath = PathUtf8::FromString(m_SceneFilePath);

    std::vector<std::filesystem::path> candidates;
    if (rawPath.is_absolute()) {
        candidates.push_back(rawPath);
    } else {
        candidates.push_back(projectRoot / rawPath);
        candidates.push_back(projectRoot / "assets" / rawPath);
        if (!scenePath.empty())
            candidates.push_back(scenePath.parent_path() / rawPath);
    }

    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(candidate, ec))
            continue;

        std::string key = PathUtf8::ToString(candidate);
        auto cached = m_PrerenderedBackgroundTextures.find(key);
        if (cached != m_PrerenderedBackgroundTextures.end())
            return cached->second.get();

        TextureParams params;
        params.nearestFilter = true;
        params.clampToEdge = true;
        params.maxSize = 0;
        auto texture = std::make_shared<Texture>(key, params);
        if (!texture || texture->GetID() == 0)
            return nullptr;
        const Texture* out = texture.get();
        m_PrerenderedBackgroundTextures.emplace(std::move(key), std::move(texture));
        return out;
    }

    return nullptr;
}

Entity* EditorApp::GetSelectedEntity() {
    if (m_SelectedEntityID == 0)
        return nullptr;

    for (const auto& e : m_Engine->GetScene().GetEntities()) {
        if (e->GetID() == m_SelectedEntityID)
            return e.get();
    }
    return nullptr;
}

bool EditorApp::IsEntitySelected(uint32_t entityId) const {
    return entityId != 0 && (m_SelectedEntityID == entityId ||
                             m_SelectedEntityIDs.count(entityId) != 0);
}

void EditorApp::SelectSingleEntity(uint32_t entityId) {
    m_SelectedEntityID = entityId;
    m_SelectedEntityIDs.clear();
    if (entityId != 0)
        m_SelectedEntityIDs.insert(entityId);
    m_HierarchySelectionAnchor = entityId;
    if (m_AssetBrowser)
        m_AssetBrowser->ClearAssetSelection();
    SyncAnimatorWindowToSelectedEntity();
}

void EditorApp::CommandRevealEntity(uint32_t entityId, bool frameInSceneView) {
    Entity* entity = m_Engine->GetScene().FindEntity(entityId);
    if (!entity)
        return;

    SelectSingleEntity(entityId);
    m_ForceSceneViewTab = true;
    if (frameInSceneView)
        FrameEntityInSceneView(*entity);
}

bool EditorApp::CommandUndo(std::string& outError) {
    if (m_IsPlaying) {
        outError = "Undo is unavailable during Play Mode.";
        return false;
    }
    if (!m_UndoStack.CanUndo()) {
        outError = "Nothing to undo.";
        return false;
    }
    if (!m_UndoStack.Undo(m_Engine->GetScene(), outError))
        return false;
    AfterSceneRestoredFromHistory();
    return true;
}

bool EditorApp::CommandRedo(std::string& outError) {
    if (m_IsPlaying) {
        outError = "Redo is unavailable during Play Mode.";
        return false;
    }
    if (!m_UndoStack.CanRedo()) {
        outError = "Nothing to redo.";
        return false;
    }
    if (!m_UndoStack.Redo(m_Engine->GetScene(), outError))
        return false;
    AfterSceneRestoredFromHistory();
    return true;
}

void EditorApp::SyncAnimatorWindowToSelectedEntity() {
    Entity* selected = GetSelectedEntity();
    if (!selected)
        return;
    auto* animator = selected->GetComponent<AnimatorComponent>();
    if (animator && !animator->controllerPath.empty())
        m_AnimatorControllerWindowPath = animator->controllerPath;
}

void EditorApp::CollectHierarchyOrder(Entity& entity, std::vector<uint32_t>& out) const {
    out.push_back(entity.GetID());
    const Scene& scene = m_Engine->GetScene();
    for (uint32_t childId : entity.GetChildIDs()) {
        if (const Entity* child = scene.FindEntity(childId))
            CollectHierarchyOrder(*const_cast<Entity*>(child), out);
    }
}

void EditorApp::HandleHierarchySelection(uint32_t entityId) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyShift && m_HierarchySelectionAnchor != 0) {
        auto anchor = std::find(m_HierarchyOrder.begin(), m_HierarchyOrder.end(),
                                m_HierarchySelectionAnchor);
        auto clicked = std::find(m_HierarchyOrder.begin(), m_HierarchyOrder.end(), entityId);
        if (anchor != m_HierarchyOrder.end() && clicked != m_HierarchyOrder.end()) {
            if (anchor > clicked) std::swap(anchor, clicked);
            if (!io.KeyCtrl) m_SelectedEntityIDs.clear();
            for (auto it = anchor; it != clicked + 1; ++it)
                m_SelectedEntityIDs.insert(*it);
            m_SelectedEntityID = entityId;
        }
    } else if (io.KeyCtrl) {
        if (m_SelectedEntityIDs.count(entityId)) {
            m_SelectedEntityIDs.erase(entityId);
            if (m_SelectedEntityID == entityId)
                m_SelectedEntityID = m_SelectedEntityIDs.empty() ? 0 : *m_SelectedEntityIDs.begin();
        } else {
            m_SelectedEntityIDs.insert(entityId);
            m_SelectedEntityID = entityId;
        }
        m_HierarchySelectionAnchor = entityId;
    } else {
        SelectSingleEntity(entityId);
    }
    if (m_AssetBrowser) m_AssetBrowser->ClearAssetSelection();
    SyncAnimatorWindowToSelectedEntity();
}

std::vector<uint32_t> EditorApp::GetSelectedHierarchyRootIDs(uint32_t fallbackId) const {
    std::unordered_set<uint32_t> selected = m_SelectedEntityIDs;
    if (selected.empty() && fallbackId != 0)
        selected.insert(fallbackId);

    std::vector<uint32_t> roots;
    const Scene& scene = m_Engine->GetScene();
    for (uint32_t id : selected) {
        const Entity* entity = scene.FindEntity(id);
        if (!entity) continue;
        bool ancestorSelected = false;
        uint32_t parentId = entity->GetParentID();
        while (parentId != 0) {
            if (selected.count(parentId)) {
                ancestorSelected = true;
                break;
            }
            const Entity* parent = scene.FindEntity(parentId);
            parentId = parent ? parent->GetParentID() : 0;
        }
        if (!ancestorSelected)
            roots.push_back(id);
    }
    std::sort(roots.begin(), roots.end(), [&](uint32_t a, uint32_t b) {
        auto ia = std::find(m_HierarchyOrder.begin(), m_HierarchyOrder.end(), a);
        auto ib = std::find(m_HierarchyOrder.begin(), m_HierarchyOrder.end(), b);
        return ia < ib;
    });
    return roots;
}

namespace {

bool IsCameraUsable(const Camera& camera) {
    if (!std::isfinite(camera.fov) || camera.fov < 0.1f)
        return false;
    if (!std::isfinite(camera.nearClip) || camera.nearClip <= 0.0f)
        return false;
    if (!std::isfinite(camera.farClip) || camera.farClip <= camera.nearClip)
        return false;
    return std::isfinite(camera.GetViewMatrix()[0][0]);
}

} // namespace

void EditorApp::HandleSceneViewControls(bool viewportHovered) {
    if (!viewportHovered)
        return;

    if (ImGuizmo::IsUsing())
        return;

    const ImGuiIO& io = ImGui::GetIO();
    if (m_ProModelerEditMode > 0 && io.KeyCtrl && !io.WantTextInput &&
        ImGui::IsKeyPressed(ImGuiKey_A)) {
        if (Entity* selected = GetSelectedEntity()) {
            if (auto* pb = selected->GetComponent<ProModelerComponent>()) {
                ClearProModelerSelection();
                m_ProModelerSelectionEntityID = selected->GetID();
                for (uint32_t vi = 0; vi < pb->vertices.size(); ++vi)
                    AddUniqueVertex(m_ProModelerSelectedVertices, vi);
                if (m_ProModelerEditMode == 2) {
                    for (size_t ti = 0; ti + 2 < pb->indices.size(); ti += 3) {
                        const uint32_t edges[3][2] = {
                            {pb->indices[ti], pb->indices[ti + 1]},
                            {pb->indices[ti + 1], pb->indices[ti + 2]},
                            {pb->indices[ti + 2], pb->indices[ti]},
                        };
                        for (const auto& edge : edges) {
                            if (!ProModelerEdgeIsCoplanarInterior(*pb, ti, edge[0], edge[1]))
                                AddUniqueEdge(m_ProModelerSelectedEdges, edge[0], edge[1]);
                        }
                    }
                } else if (m_ProModelerEditMode == 3) {
                    for (size_t ti = 0; ti + 2 < pb->indices.size(); ti += 3)
                        AddUniqueFaceTriangle(m_ProModelerSelectedFaceTriangles, ti);
                }
            }
        }
    }

    glm::vec2 mouseDelta = Input::GetMouseDelta();
    bool alt = Input::IsKeyPressed(GLFW_KEY_LEFT_ALT) || Input::IsKeyPressed(GLFW_KEY_RIGHT_ALT);
    const bool allowSceneCamera =
        !m_IsPlaying || alt || (viewportHovered && !m_GameViewHovered);

    if (allowSceneCamera) {
        // Unity-style orbit: Alt + LMB
        if (alt && Input::IsMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT)) {
            m_SceneCamera.ProcessOrbit(mouseDelta.x, mouseDelta.y);
        }
        // Pan: MMB (or Alt+MMB)
        else if (Input::IsMouseButtonPressed(GLFW_MOUSE_BUTTON_MIDDLE)) {
            m_SceneCamera.ProcessPan(mouseDelta.x, mouseDelta.y);
        }
        // Zoom: Alt + RMB drag
        else if (alt && Input::IsMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT)) {
            m_SceneCamera.ProcessZoom(-mouseDelta.y * 0.02f);
        }
        // Fly: RMB + WASD/QE
        else if (Input::IsMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT)) {
            float speed = 5.0f;
            if (Input::IsKeyPressed(GLFW_KEY_LEFT_SHIFT) || Input::IsKeyPressed(GLFW_KEY_RIGHT_SHIFT))
                speed = 15.0f;

            glm::vec3 moveDir(0.0f);
            const Camera& cam = m_SceneCamera.GetCamera();
            if (Input::IsKeyPressed(GLFW_KEY_W)) moveDir += cam.GetForward();
            if (Input::IsKeyPressed(GLFW_KEY_S)) moveDir -= cam.GetForward();
            if (Input::IsKeyPressed(GLFW_KEY_A)) moveDir -= cam.GetRight();
            if (Input::IsKeyPressed(GLFW_KEY_D)) moveDir += cam.GetRight();
            if (Input::IsKeyPressed(GLFW_KEY_E)) moveDir += glm::vec3(0.0f, 1.0f, 0.0f);
            if (Input::IsKeyPressed(GLFW_KEY_Q)) moveDir -= glm::vec3(0.0f, 1.0f, 0.0f);

            if (glm::length(moveDir) > 0.0f)
                m_SceneCamera.ProcessFly(glm::normalize(moveDir), speed, Time::GetDeltaTime());

            if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f)
                m_SceneCamera.ProcessOrbit(mouseDelta.x, mouseDelta.y);
        }
    }

    float scroll = Input::GetScrollDelta();
    if (scroll != 0.0f)
        m_SceneCamera.ProcessZoom(scroll);

    // Hotkeys for gizmo mode (Unity-style, not while flying in edit mode)
    const bool blockGizmoHotkeys =
        Input::IsMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT) && !m_IsPlaying;
    if (m_SceneViewFocused && !ImGui::GetIO().WantTextInput && !blockGizmoHotkeys) {
        if (Input::IsKeyPressed(GLFW_KEY_W)) m_GizmoOperation = ImGuizmo::TRANSLATE;
        if (Input::IsKeyPressed(GLFW_KEY_E)) m_GizmoOperation = ImGuizmo::ROTATE;
        if (Input::IsKeyPressed(GLFW_KEY_R)) m_GizmoOperation = ImGuizmo::SCALE;

        if (Input::IsKeyPressed(GLFW_KEY_F)) {
            if (Entity* selected = GetSelectedEntity()) {
                if (auto* transform = selected->GetComponent<TransformComponent>())
                    m_SceneCamera.FocusOn(transform->position, m_SceneCamera.GetDistance());
            } else {
                m_SceneCamera.FocusOn({ 0.0f, 0.0f, 0.0f });
            }
        }
    }
}

bool EditorApp::DrawProModelerSubobjectGizmo(const ImVec2& imageMin, const ImVec2& imageSize, const Camera& camera) {
    if (m_ProModelerEditMode <= 0 || m_ProModelerSelectedVertices.empty())
        return false;
    Entity* selected = GetSelectedEntity();
    if (!selected || selected->GetID() != m_ProModelerSelectionEntityID)
        return false;
    auto* pb = selected->GetComponent<ProModelerComponent>();
    auto* meshRenderer = selected->GetComponent<MeshRendererComponent>();
    if (!pb || !meshRenderer)
        return false;

    glm::vec3 localCenter(0.0f);
    int count = 0;
    for (uint32_t vi : m_ProModelerSelectedVertices) {
        if (vi >= pb->vertices.size())
            continue;
        localCenter += pb->vertices[vi].position;
        ++count;
    }
    if (count == 0)
        return false;
    localCenter /= static_cast<float>(count);

    Scene& scene = m_Engine->GetScene();
    const glm::mat4 entityWorld = scene.GetWorldMatrix(*selected);
    const glm::vec3 worldCenter = glm::vec3(entityWorld * glm::vec4(localCenter, 1.0f));
    const glm::mat4 centerMatrix = glm::translate(glm::mat4(1.0f), worldCenter);

    static glm::mat4 activeSubGizmoMatrix(1.0f);
    static uint32_t activeSubGizmoEntity = 0;
    float matrix[16];
    static bool wasSubGizmoUsing = false;
    const bool continuingDrag = wasSubGizmoUsing && activeSubGizmoEntity == selected->GetID();
    const glm::mat4 inputGizmoMatrix = continuingDrag ? activeSubGizmoMatrix : centerMatrix;
    std::memcpy(matrix, glm::value_ptr(inputGizmoMatrix), sizeof(matrix));

    const bool gizmoUsingBefore = ImGuizmo::IsUsing();
    glm::mat4 view = camera.GetViewMatrix();
    glm::mat4 proj = camera.GetProjectionMatrix();
    const bool changed = ImGuizmo::Manipulate(
        glm::value_ptr(view),
        glm::value_ptr(proj),
        m_GizmoOperation,
        m_GizmoOperation == ImGuizmo::SCALE ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
        matrix);
    const bool gizmoUsing = ImGuizmo::IsUsing();
    const bool gizmoStarted = gizmoUsing && !wasSubGizmoUsing && !gizmoUsingBefore;
    if (gizmoStarted) {
        activeSubGizmoMatrix = inputGizmoMatrix;
        activeSubGizmoEntity = selected->GetID();
    }

    if (gizmoStarted)
        RecordUndoSnapshot();

    // Shift+Drag extrude: when drag begins in face mode with Shift held, extrude first.
    if (gizmoStarted && m_GizmoOperation == ImGuizmo::TRANSLATE &&
        m_ProModelerEditMode == 3 &&
        !m_ProModelerSelectedFaceTriangles.empty() &&
        glfwGetKey(m_Engine->GetWindow().GetNativeWindow(), GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
        pb->ExtrudeFaces(m_ProModelerSelectedFaceTriangles,
                         m_ProModelerSelectedVertices,
                         m_ProModelerSelectedFaceTriangles);
        m_ProModelerSelectionIsExtrudedCap = true;
        pb->shape = ProModelerComponent::Shape::Custom;
        pb->RebuildMesh(*meshRenderer);
        m_Engine->GetMipsRuntime().SyncEditSnapshot(scene);
        m_SceneDirty = true;
    }

    if (changed || gizmoUsing) {
        const glm::mat4 newMatrix = glm::make_mat4(matrix);
        const glm::mat4 worldDeltaMatrix = newMatrix * glm::inverse(inputGizmoMatrix);
        activeSubGizmoMatrix = newMatrix;
        activeSubGizmoEntity = selected->GetID();
        const bool hasDelta = glm::length(glm::vec3(worldDeltaMatrix[3])) > 1e-7f ||
                              glm::length(glm::vec3(worldDeltaMatrix[0]) - glm::vec3(1, 0, 0)) > 1e-7f ||
                              glm::length(glm::vec3(worldDeltaMatrix[1]) - glm::vec3(0, 1, 0)) > 1e-7f ||
                              glm::length(glm::vec3(worldDeltaMatrix[2]) - glm::vec3(0, 0, 1)) > 1e-7f;
        if (hasDelta) {
            const float epsilon = std::max(0.0005f, glm::length(pb->size) * 0.001f);
            std::vector<glm::vec3> selectedPositions;
            if (!m_ProModelerSelectionIsExtrudedCap) {
                selectedPositions.reserve(m_ProModelerSelectedVertices.size());
                for (uint32_t vi : m_ProModelerSelectedVertices) {
                    if (vi < pb->vertices.size())
                        selectedPositions.push_back(pb->vertices[vi].position);
                }
            }

            const glm::mat4 worldToLocal = glm::inverse(entityWorld);
            auto transformVertex = [&](ProModelerVertex& vertex) {
                const glm::vec3 worldPosition = glm::vec3(
                    entityWorld * glm::vec4(vertex.position, 1.0f));
                vertex.position = glm::vec3(
                    worldToLocal * worldDeltaMatrix * glm::vec4(worldPosition, 1.0f));
            };

            if (!selectedPositions.empty()) {
                for (auto& vertex : pb->vertices) {
                    for (const glm::vec3& selectedPosition : selectedPositions) {
                        if (PositionsNearlyEqual(vertex.position, selectedPosition, epsilon)) {
                            transformVertex(vertex);
                            break;
                        }
                    }
                }
            } else {
                for (uint32_t vi : m_ProModelerSelectedVertices) {
                    if (vi < pb->vertices.size())
                        transformVertex(pb->vertices[vi]);
                }
            }
            pb->shape = ProModelerComponent::Shape::Custom;
            pb->RebuildMesh(*meshRenderer);
            m_Engine->GetMipsRuntime().SyncEditSnapshot(scene);
            m_SceneDirty = true;
        }
    }

    wasSubGizmoUsing = gizmoUsing;
    if (!gizmoUsing) {
        activeSubGizmoEntity = 0;
        activeSubGizmoMatrix = glm::mat4(1.0f);
    }

    return true;
}

bool EditorApp::HandleTerrainBrush(const ImVec2& imageMin, const ImVec2& imageSize, bool viewportHovered) {
    Entity* selected = GetSelectedEntity();
    if (!selected || !viewportHovered)
        return false;

    auto* terrain = selected->GetComponent<TerrainComponent>();
    auto* meshRenderer = selected->GetComponent<MeshRendererComponent>();
    auto* transform = selected->GetComponent<TransformComponent>();
    if (!terrain || !meshRenderer || !transform || !terrain->brushEnabled)
        return false;

    ImGuiIO& io = ImGui::GetIO();
    if (Input::IsKeyPressed(GLFW_KEY_LEFT_ALT) || Input::IsKeyPressed(GLFW_KEY_RIGHT_ALT))
        return false;
    if (ImGuizmo::IsUsing())
        return true;

    Scene& scene = m_Engine->GetScene();
    const glm::mat4 world = scene.GetWorldMatrix(*selected);
    const glm::vec3 origin = glm::vec3(world * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    const glm::vec3 hitWorld = PickPointOnPlane(
        m_SceneCamera.GetCamera(),
        io.MousePos.x, io.MousePos.y,
        imageMin.x, imageMin.y,
        imageSize.x, imageSize.y,
        origin.y);
    const glm::vec3 local = glm::vec3(glm::inverse(world) * glm::vec4(hitWorld, 1.0f));
    const float half = terrain->size * 0.5f;
    const bool inTerrain =
        local.x >= -half - terrain->brushRadius && local.x <= half + terrain->brushRadius &&
        local.z >= -half - terrain->brushRadius && local.z <= half + terrain->brushRadius;
    if (!inTerrain)
        return false;

    const glm::vec3 centerWorld = glm::vec3(world * glm::vec4(local.x, local.y, local.z, 1.0f));
    const glm::vec3 edgeWorld = glm::vec3(world * glm::vec4(local.x + terrain->brushRadius, local.y, local.z, 1.0f));
    ImVec2 centerScreen{};
    ImVec2 edgeScreen{};
    if (EditorCameraGizmo::WorldToScreen(centerWorld, m_SceneCamera.GetCamera().GetViewMatrix(), m_SceneCamera.GetCamera().GetProjectionMatrix(),
                                         imageMin, imageSize, centerScreen) &&
        EditorCameraGizmo::WorldToScreen(edgeWorld, m_SceneCamera.GetCamera().GetViewMatrix(), m_SceneCamera.GetCamera().GetProjectionMatrix(),
                                         imageMin, imageSize, edgeScreen)) {
        const float radius = std::max(4.0f, std::abs(edgeScreen.x - centerScreen.x));
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddCircle(centerScreen, radius, IM_COL32(255, 210, 64, 220), 48, 2.0f);
        drawList->AddCircleFilled(centerScreen, 3.0f, IM_COL32(255, 210, 64, 220));
    }

    const bool painting = io.MouseDown[ImGuiMouseButton_Left] && !ImGui::IsAnyItemActive();
    if (!painting)
        return true;

    if (io.MouseClicked[ImGuiMouseButton_Left])
        RecordUndoSnapshot();

    const bool shiftDown = Input::IsKeyPressed(GLFW_KEY_LEFT_SHIFT) || Input::IsKeyPressed(GLFW_KEY_RIGHT_SHIFT);
    const TerrainComponent::BrushMode savedMode = terrain->brushMode;
    if (shiftDown && terrain->brushMode == TerrainComponent::BrushMode::Raise)
        terrain->brushMode = TerrainComponent::BrushMode::Lower;
    const bool applied = terrain->ApplyBrush(local);
    terrain->brushMode = savedMode;

    if (applied) {
        terrain->RebuildMesh(*meshRenderer);
        if (!meshRenderer->texture)
            meshRenderer->texture = std::make_shared<Texture>(Texture::CreateCheckerboard(256, 32));
        m_Engine->GetMipsRuntime().SyncEditSnapshot(scene);
        m_SceneDirty = true;
    }
    return true;
}

void EditorApp::ClearProModelerSelection() {
    m_ProModelerSelectedVertices.clear();
    m_ProModelerSelectedEdges.clear();
    m_ProModelerSelectedFaceTriangles.clear();
    m_ProModelerSelectionIsExtrudedCap = false;
    m_ProModelerSelectionEntityID = 0;
}

bool EditorApp::HandleProModelerSubobjectPick(const ImVec2& imageMin, const ImVec2& imageSize, bool viewportHovered) {
    if (m_ProModelerEditMode <= 0)
        return false;
    Entity* selected = GetSelectedEntity();
    if (!selected)
        return false;
    auto* pb = selected->GetComponent<ProModelerComponent>();
    if (!pb || pb->vertices.empty() || pb->indices.size() < 3)
        return false;
    if (!viewportHovered)
        return true;

    ImGuiIO& io = ImGui::GetIO();
    if (!io.MouseClicked[ImGuiMouseButton_Left])
        return false;
    if (Input::IsKeyPressed(GLFW_KEY_LEFT_ALT) || Input::IsKeyPressed(GLFW_KEY_RIGHT_ALT))
        return false;
    if (ImGuizmo::IsOver() || ImGuizmo::IsUsing())
        return true;

    Scene& scene = m_Engine->GetScene();
    const glm::mat4 world = scene.GetWorldMatrix(*selected);
    const EditorRay ray = BuildEditorRay(
        m_SceneCamera.GetCamera(), io.MousePos.x, io.MousePos.y, imageMin, imageSize);

    size_t bestTri = SIZE_MAX;
    float bestT = std::numeric_limits<float>::max();
    for (size_t ti = 0; ti + 2 < pb->indices.size(); ti += 3) {
        const uint32_t i0 = pb->indices[ti + 0];
        const uint32_t i1 = pb->indices[ti + 1];
        const uint32_t i2 = pb->indices[ti + 2];
        if (i0 >= pb->vertices.size() || i1 >= pb->vertices.size() || i2 >= pb->vertices.size())
            continue;
        const glm::vec3 a = glm::vec3(world * glm::vec4(pb->vertices[i0].position, 1.0f));
        const glm::vec3 b = glm::vec3(world * glm::vec4(pb->vertices[i1].position, 1.0f));
        const glm::vec3 c = glm::vec3(world * glm::vec4(pb->vertices[i2].position, 1.0f));
        float t = 0.0f;
        if (RayTriangleHit(ray, a, b, c, t) && t < bestT) {
            bestT = t;
            bestTri = ti;
        }
    }

    const bool additive = Input::IsKeyPressed(GLFW_KEY_LEFT_SHIFT) ||
                          Input::IsKeyPressed(GLFW_KEY_RIGHT_SHIFT) ||
                          Input::IsKeyPressed(GLFW_KEY_LEFT_CONTROL) ||
                          Input::IsKeyPressed(GLFW_KEY_RIGHT_CONTROL);
    if (!additive)
        ClearProModelerSelection();
    m_ProModelerSelectionEntityID = selected->GetID();
    m_ProModelerSelectionIsExtrudedCap = false;

    if (bestTri == SIZE_MAX)
        return true;

    const uint32_t tri[3] = {
        pb->indices[bestTri + 0],
        pb->indices[bestTri + 1],
        pb->indices[bestTri + 2],
    };

    if (m_ProModelerEditMode == 3) {
        const std::vector<size_t> faceTriangles = CollectProModelerFaceTriangles(*pb, bestTri);
        for (size_t faceTri : faceTriangles) {
            AddUniqueFaceTriangle(m_ProModelerSelectedFaceTriangles, faceTri);
            AddUniqueVertex(m_ProModelerSelectedVertices, pb->indices[faceTri + 0]);
            AddUniqueVertex(m_ProModelerSelectedVertices, pb->indices[faceTri + 1]);
            AddUniqueVertex(m_ProModelerSelectedVertices, pb->indices[faceTri + 2]);
        }
        return true;
    }

    ImVec2 screen[3]{};
    bool visible[3]{};
    for (int i = 0; i < 3; ++i) {
        const glm::vec3 wp = glm::vec3(world * glm::vec4(pb->vertices[tri[i]].position, 1.0f));
        visible[i] = EditorCameraGizmo::WorldToScreen(
            wp, m_SceneCamera.GetCamera().GetViewMatrix(), m_SceneCamera.GetCamera().GetProjectionMatrix(),
            imageMin, imageSize, screen[i]);
    }

    if (m_ProModelerEditMode == 1) {
        float bestDist = std::numeric_limits<float>::max();
        int bestVertex = 0;
        for (int i = 0; i < 3; ++i) {
            if (!visible[i])
                continue;
            const float dx = io.MousePos.x - screen[i].x;
            const float dy = io.MousePos.y - screen[i].y;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < bestDist) {
                bestDist = dist;
                bestVertex = i;
            }
        }
        AddUniqueVertex(m_ProModelerSelectedVertices, tri[bestVertex]);
        return true;
    }

    if (m_ProModelerEditMode == 2) {
        const int edges[3][2] = { { 0, 1 }, { 1, 2 }, { 2, 0 } };
        float bestDist = std::numeric_limits<float>::max();
        int bestEdge = -1;
        for (int i = 0; i < 3; ++i) {
            const int a = edges[i][0];
            const int b = edges[i][1];
            if (!visible[a] || !visible[b])
                continue;
            if (ProModelerEdgeIsCoplanarInterior(*pb, bestTri, tri[a], tri[b]))
                continue;
            const float dist = DistanceToSegment2D(io.MousePos, screen[a], screen[b]);
            if (dist < bestDist) {
                bestDist = dist;
                bestEdge = i;
            }
        }
        if (bestEdge < 0)
            return true;
        AddUniqueVertex(m_ProModelerSelectedVertices, tri[edges[bestEdge][0]]);
        AddUniqueVertex(m_ProModelerSelectedVertices, tri[edges[bestEdge][1]]);
        AddUniqueEdge(m_ProModelerSelectedEdges,
                      tri[edges[bestEdge][0]], tri[edges[bestEdge][1]]);
    }

    return true;
}

void EditorApp::DrawProModelerOverlay(const ImVec2& imageMin, const ImVec2& imageSize, const Camera& camera) {
    if (m_ProModelerEditMode <= 0)
        return;
    Entity* selected = GetSelectedEntity();
    if (!selected)
        return;
    auto* pb = selected->GetComponent<ProModelerComponent>();
    if (!pb)
        return;

    if (m_ProModelerSelectionEntityID != selected->GetID() && !m_ProModelerSelectedVertices.empty())
        ClearProModelerSelection();

    Scene& scene = m_Engine->GetScene();
    const glm::mat4 world = scene.GetWorldMatrix(*selected);
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    auto isSelectedVertex = [&](uint32_t index) {
        return std::find(m_ProModelerSelectedVertices.begin(), m_ProModelerSelectedVertices.end(), index) !=
               m_ProModelerSelectedVertices.end();
    };
    auto isSelectedFaceTriangle = [&](size_t triangleStart) {
        triangleStart = (triangleStart / 3u) * 3u;
        return std::find(m_ProModelerSelectedFaceTriangles.begin(), m_ProModelerSelectedFaceTriangles.end(), triangleStart) !=
               m_ProModelerSelectedFaceTriangles.end();
    };
    auto isSelectedEdge = [&](uint32_t a, uint32_t b) {
        if (a > b) std::swap(a, b);
        return std::find(m_ProModelerSelectedEdges.begin(), m_ProModelerSelectedEdges.end(),
                         std::pair<uint32_t, uint32_t>{a, b}) !=
               m_ProModelerSelectedEdges.end();
    };

    for (size_t ti = 0; ti + 2 < pb->indices.size(); ti += 3) {
        ImVec2 p[3]{};
        bool ok = true;
        for (int i = 0; i < 3; ++i) {
            const uint32_t vi = pb->indices[ti + static_cast<size_t>(i)];
            if (vi >= pb->vertices.size()) {
                ok = false;
                break;
            }
            const glm::vec3 wp = glm::vec3(world * glm::vec4(pb->vertices[vi].position, 1.0f));
            ok &= EditorCameraGizmo::WorldToScreen(
                wp, camera.GetViewMatrix(), camera.GetProjectionMatrix(), imageMin, imageSize, p[i]);
        }
        if (!ok)
            continue;
        const bool selectedFace =
            isSelectedFaceTriangle(ti) ||
            (isSelectedVertex(pb->indices[ti + 0]) &&
             isSelectedVertex(pb->indices[ti + 1]) &&
             isSelectedVertex(pb->indices[ti + 2]));
        if (selectedFace)
            drawList->AddTriangleFilled(p[0], p[1], p[2], IM_COL32(255, 210, 64, 58));
        const ImU32 edgeColor = selectedFace ? IM_COL32(255, 210, 64, 235) : IM_COL32(80, 190, 255, 120);
        const uint32_t edgeIndices[3][2] = {
            { pb->indices[ti + 0], pb->indices[ti + 1] },
            { pb->indices[ti + 1], pb->indices[ti + 2] },
            { pb->indices[ti + 2], pb->indices[ti + 0] },
        };
        const ImVec2 edgePoints[3][2] = {
            { p[0], p[1] },
            { p[1], p[2] },
            { p[2], p[0] },
        };
        for (int edge = 0; edge < 3; ++edge) {
            const bool selectedEdge = isSelectedEdge(edgeIndices[edge][0], edgeIndices[edge][1]);
            if (!selectedEdge &&
                ProModelerEdgeIsCoplanarInterior(*pb, ti, edgeIndices[edge][0], edgeIndices[edge][1]))
                continue;
            drawList->AddLine(edgePoints[edge][0], edgePoints[edge][1],
                              selectedEdge ? IM_COL32(255, 210, 64, 255) : edgeColor,
                              selectedEdge ? 3.0f : (selectedFace ? 2.0f : 1.0f));
        }
    }

    std::vector<glm::vec3> drawnVertexPositions;
    const float vertexEpsilon = std::max(0.0005f, glm::length(pb->size) * 0.001f);
    for (uint32_t vi = 0; vi < pb->vertices.size(); ++vi) {
        bool alreadyDrawn = false;
        for (const glm::vec3& drawnPosition : drawnVertexPositions) {
            if (PositionsNearlyEqual(drawnPosition, pb->vertices[vi].position, vertexEpsilon)) {
                alreadyDrawn = true;
                break;
            }
        }
        if (alreadyDrawn)
            continue;
        drawnVertexPositions.push_back(pb->vertices[vi].position);

        const glm::vec3 wp = glm::vec3(world * glm::vec4(pb->vertices[vi].position, 1.0f));
        ImVec2 p{};
        if (!EditorCameraGizmo::WorldToScreen(
                wp, camera.GetViewMatrix(), camera.GetProjectionMatrix(), imageMin, imageSize, p))
            continue;
        bool selectedVertex = isSelectedVertex(vi);
        if (!selectedVertex) {
            for (uint32_t selectedVi : m_ProModelerSelectedVertices) {
                if (selectedVi < pb->vertices.size() &&
                    PositionsNearlyEqual(pb->vertices[selectedVi].position, pb->vertices[vi].position, vertexEpsilon)) {
                    selectedVertex = true;
                    break;
                }
            }
        }
        drawList->AddCircleFilled(p, selectedVertex ? 4.5f : 3.0f,
                                  selectedVertex ? IM_COL32(255, 210, 64, 255)
                                                 : IM_COL32(70, 180, 255, 190));
        drawList->AddCircle(p, selectedVertex ? 4.5f : 3.0f, IM_COL32(0, 0, 0, 180), 12, 1.0f);
    }
}

void EditorApp::DrawProModelerWindow() {
    Entity* selectedEntity = GetSelectedEntity();
    if (!selectedEntity) {
        m_ProModelerEditMode = 0;
        ClearProModelerSelection();
        return;
    }

    auto* proModelerComp = selectedEntity->GetComponent<ProModelerComponent>();
    if (!proModelerComp) {
        m_ProModelerEditMode = 0;
        ClearProModelerSelection();
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(24.0f, 118.0f), ImGuiCond_FirstUseEver);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoCollapse;
    if (!ImGui::Begin("ProModeler", nullptr, flags)) {
        ImGui::End();
        return;
    }

    const auto* tagComp = selectedEntity->GetComponent<TagComponent>();
    ImGui::TextDisabled("Selected: %s", tagComp ? tagComp->tag.c_str() : "Entity");
    if (ImGui::SmallButton("Remove ProModeler Component")) {
        RecordUndoSnapshot();
        selectedEntity->RemoveComponent<ProModelerComponent>();
        m_ProModelerEditMode = 0;
        ClearProModelerSelection();
        ImGui::End();
        return;
    }

    bool changed = false;
    bool rebuild = false;

    // 笏笏 helper: 40ﾃ・0 icon button with active-highlight and tooltip 笏笏笏笏笏笏笏笏笏笏
    auto drawIconButton = [](const char* id, int iconIndex, int currentSel, int btnType, const char* tooltip) -> bool {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        const float sz = 40.0f;
        const bool active = (currentSel == iconIndex);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button,        EditorTheme::Accent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UiTokens::BrandHover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  UiTokens::BrandPressed);
        }
        bool clicked = ImGui::Button(id, ImVec2(sz, sz));
        if (active) ImGui::PopStyleColor(3);

        ImDrawList* dl  = ImGui::GetWindowDrawList();
        ImVec2 bMin     = ImGui::GetItemRectMin();
        ImVec2 bMax     = ImGui::GetItemRectMax();
        if      (btnType == 0) DrawEditModeIcon   (dl, bMin, bMax, iconIndex);
        else if (btnType == 1) DrawShapePresetIcon(dl, bMin, bMax, iconIndex);
        else if (btnType == 2) DrawExtrudeIcon    (dl, bMin, bMax, iconIndex);

        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered() && tooltip && tooltip[0])
            ImGui::SetTooltip("%s", tooltip);
        return clicked;
    };

    ImGui::SeparatorText("Edit Mode");
    const char* editTooltips[] = { "Object Mode", "Vertex Mode", "Edge Mode", "Face Mode" };
    for (int i = 0; i < 4; ++i) {
        if (i > 0) ImGui::SameLine();
        char id[32]; std::snprintf(id, sizeof(id), "##mode_%d", i);
        if (drawIconButton(id, i, m_ProModelerEditMode, 0, editTooltips[i])) {
            m_ProModelerEditMode = i;
            ClearProModelerSelection();
        }
    }
    if (m_ProModelerEditMode > 0) {
        ImGui::TextDisabled("Scene View: click parts. Ctrl/Shift adds. Ctrl+A selects all.");
        if (m_ProModelerEditMode == 3)
            ImGui::TextDisabled("Face: drop a Material onto selection. SHIFT+Drag extrudes.");
        if (ImGui::Button("Clear Selection", ImVec2(-1, 0)))
            ClearProModelerSelection();
    }

    ImGui::SeparatorText("Shape Preset");
    const char* shapeNames[] = { "Box", "Plane", "Ramp", "Stairs", "Custom" };
    int shape = static_cast<int>(proModelerComp->shape);
    if (ImGui::Combo("Preset##combo", &shape, shapeNames, IM_ARRAYSIZE(shapeNames))) {
        RecordUndoSnapshot();
        proModelerComp->shape = static_cast<ProModelerComponent::Shape>(std::clamp(shape, 0, 4));
        if      (proModelerComp->shape == ProModelerComponent::Shape::Plane)  proModelerComp->ResetPlane();
        else if (proModelerComp->shape == ProModelerComponent::Shape::Ramp)   proModelerComp->ResetRamp();
        else if (proModelerComp->shape == ProModelerComponent::Shape::Stairs) proModelerComp->ResetStairs();
        else if (proModelerComp->shape == ProModelerComponent::Shape::Box)    proModelerComp->ResetBox();
        rebuild = true;
    }

    if (ImGui::DragFloat3("Size", glm::value_ptr(proModelerComp->size), 0.05f, 0.01f, 256.0f))
        changed = true;
    if (ImGui::IsItemActivated()) RecordUndoSnapshot();
    if (proModelerComp->shape == ProModelerComponent::Shape::Stairs) {
        if (ImGui::SliderInt("Steps", &proModelerComp->steps, 1, 32)) changed = true;
        if (ImGui::IsItemActivated()) RecordUndoSnapshot();
    }

    // Shape preset icon buttons
    const char* shapeTooltips[] = { "Box", "Plane", "Ramp", "Stairs" };
    for (int i = 0; i < 4; ++i) {
        if (i > 0) ImGui::SameLine();
        char id[32]; std::snprintf(id, sizeof(id), "##shape_%d", i);
        if (drawIconButton(id, i, static_cast<int>(proModelerComp->shape), 1, shapeTooltips[i])) {
            RecordUndoSnapshot();
            proModelerComp->shape = static_cast<ProModelerComponent::Shape>(i);
            if      (i == 0) proModelerComp->ResetBox();
            else if (i == 1) proModelerComp->ResetPlane();
            else if (i == 2) proModelerComp->ResetRamp();
            else if (i == 3) proModelerComp->ResetStairs();
            rebuild = true;
        }
    }

    ImGui::SeparatorText("Face Tools");
    ImGui::DragFloat("Extrude Amount", &proModelerComp->extrudeAmount, 0.01f, 0.01f, 64.0f);

    auto extrudeSelected = [&](const glm::vec3& direction) {
        if (m_ProModelerSelectedFaceTriangles.empty())
            return false;
        proModelerComp->ExtrudeFaces(m_ProModelerSelectedFaceTriangles,
                                     m_ProModelerSelectedVertices,
                                     m_ProModelerSelectedFaceTriangles);
        m_ProModelerSelectionIsExtrudedCap = true;
        m_ProModelerSelectedEdges.clear();
        const glm::vec3 delta = glm::normalize(direction) * proModelerComp->extrudeAmount;
        for (uint32_t vi : m_ProModelerSelectedVertices)
            if (vi < proModelerComp->vertices.size())
                proModelerComp->vertices[vi].position += delta;
        return true;
    };

    ImGui::BeginDisabled(m_ProModelerSelectedFaceTriangles.empty());
    if (ImGui::Button("Extrude Selected", ImVec2(-1.0f, 0.0f))) {
        RecordUndoSnapshot();
        glm::vec3 normal(0.0f, 1.0f, 0.0f);
        if (!m_ProModelerSelectedFaceTriangles.empty()) {
            const size_t ti = m_ProModelerSelectedFaceTriangles.front();
            if (ti + 2 < proModelerComp->indices.size()) {
                const glm::vec3 a = proModelerComp->vertices[proModelerComp->indices[ti + 0]].position;
                const glm::vec3 b = proModelerComp->vertices[proModelerComp->indices[ti + 1]].position;
                const glm::vec3 c = proModelerComp->vertices[proModelerComp->indices[ti + 2]].position;
                normal = glm::normalize(glm::cross(b - a, c - a));
            }
        }
        if (extrudeSelected(normal)) rebuild = true;
    }
    if (ImGui::Button("Flip Face Normal", ImVec2(-1.0f, 0.0f))) {
        RecordUndoSnapshot();
        proModelerComp->FlipFaces(m_ProModelerSelectedFaceTriangles);
        rebuild = true;
    }
    ImGui::EndDisabled();

    const char* extrudeTooltips[] = { "Extrude Up (+Y)", "Extrude Right (+X)", "Extrude Forward (+Z)" };
    for (int i = 0; i < 3; ++i) {
        if (i > 0) ImGui::SameLine();
        char id[32]; std::snprintf(id, sizeof(id), "##extrude_%d", i);
        if (drawIconButton(id, i, -1, 2, extrudeTooltips[i])) {
            RecordUndoSnapshot();
            const glm::vec3 dirs[] = {{ 0,1,0 }, { 1,0,0 }, { 0,0,1 }};
            if (!extrudeSelected(dirs[i]))
                proModelerComp->MoveFaceVertices(dirs[i], proModelerComp->extrudeAmount);
            rebuild = true;
        }
    }

    ImGui::SeparatorText("Edge Tools");
    ImGui::TextDisabled("Select two opposite edges with Ctrl/Shift-click.");
    ImGui::BeginDisabled(m_ProModelerSelectedEdges.size() < 2);
    if (ImGui::Button("Connect Edges / Add Loop", ImVec2(-1.0f, 0.0f))) {
        RecordUndoSnapshot();
        const size_t firstEdge = m_ProModelerSelectedEdges.size() - 2u;
        const size_t secondEdge = m_ProModelerSelectedEdges.size() - 1u;
        if (proModelerComp->ConnectOppositeEdges(m_ProModelerSelectedEdges[firstEdge],
                                                  m_ProModelerSelectedEdges[secondEdge],
                                                  m_ProModelerSelectedVertices)) {
            m_ProModelerSelectedEdges.clear();
            if (m_ProModelerSelectedVertices.size() == 2)
                AddUniqueEdge(m_ProModelerSelectedEdges,
                              m_ProModelerSelectedVertices[0],
                              m_ProModelerSelectedVertices[1]);
            m_ProModelerSelectedFaceTriangles.clear();
            m_ProModelerSelectionIsExtrudedCap = false;
            rebuild = true;
        }
    }
    ImGui::EndDisabled();

    if (changed && proModelerComp->shape != ProModelerComponent::Shape::Custom) {
        if      (proModelerComp->shape == ProModelerComponent::Shape::Plane)  proModelerComp->ResetPlane();
        else if (proModelerComp->shape == ProModelerComponent::Shape::Ramp)   proModelerComp->ResetRamp();
        else if (proModelerComp->shape == ProModelerComponent::Shape::Stairs) proModelerComp->ResetStairs();
        else                                                                   proModelerComp->ResetBox();
        rebuild = true;
    }

    if (rebuild) {
        auto* mr = selectedEntity->GetComponent<MeshRendererComponent>();
        if (!mr)
            mr = &selectedEntity->AddComponent<MeshRendererComponent>();
        proModelerComp->steps = std::clamp(proModelerComp->steps, 1, 32);
        proModelerComp->RebuildMesh(*mr);
        if (!mr->texture)
            mr->texture = std::make_shared<Texture>(Texture::CreateCheckerboard(128, 16));
        m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
        m_SceneDirty = true;
    }

    ImGui::TextDisabled("%d vertices, %d tris",
        static_cast<int>(proModelerComp->vertices.size()),
        static_cast<int>(proModelerComp->indices.size() / 3));
    ImGui::End();
}

void EditorApp::DrawSceneViewPanel() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });
    ImGui::Begin("Scene View");

    m_SceneViewFocused = ImGui::IsWindowFocused();

    DrawGizmoToolbar();

    ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
    if (viewportPanelSize.x > 0.0f && viewportPanelSize.y > 0.0f) {
        m_LastSceneViewAspect = viewportPanelSize.x / viewportPanelSize.y;
        m_SceneCamera.SetViewportAspect(m_LastSceneViewAspect);
        m_Engine->GetRenderer().SetRenderMode(m_SceneRenderMode);

        PS1Settings& psx = m_Engine->GetRenderer().GetPS1Settings();
        const PS1Settings savedPsx = psx;
        ApplySceneViewPsxOverrides(psx);
        RenderSceneToFramebuffer(m_SceneViewFBO, m_SceneCamera.GetCamera(), viewportPanelSize,
                                 m_SelectedEntityID, 0, true, m_GameViewSettings.renderWidth,
                                 m_GameViewSettings.renderHeight);
        psx = savedPsx;

        uint32_t textureID = m_SceneViewFBO.GetColorAttachment();

        ImVec2 imageMin = ImGui::GetCursorScreenPos();
        ImVec2 imageMax = ImVec2(imageMin.x + viewportPanelSize.x, imageMin.y + viewportPanelSize.y);
        ImVec2 imageSize = viewportPanelSize;
        ImDrawList* sceneDrawList = ImGui::GetWindowDrawList();
        sceneDrawList->AddImage(
            (ImTextureID)(intptr_t)textureID,
            imageMin, imageMax,
            ImVec2{ 0, 1 }, ImVec2{ 1, 0 });

        // Scene overlays share the Scene View window's draw list with the
        // toolbar. Establish one viewport-level clip boundary so every current
        // and future gizmo is confined to the rendered image by default.
        ScopedDrawListClipRect viewportClip(sceneDrawList, imageMin, imageMax);

        DrawGizmoControls(imageMin, imageSize, m_SceneCamera.GetCamera());
        DrawColliderGizmos(imageMin, imageSize, m_SceneCamera.GetCamera());
        DrawLightGizmos(imageMin, imageSize, m_SceneCamera.GetCamera());
        DrawCameraFrustumGizmo(imageMin, imageSize, m_SceneCamera.GetCamera());
        DrawProModelerOverlay(imageMin, imageSize, m_SceneCamera.GetCamera());

        ImGuiIO& io = ImGui::GetIO();
        bool windowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        bool mouseInViewport =
            io.MousePos.x >= imageMin.x && io.MousePos.x < imageMax.x &&
            io.MousePos.y >= imageMin.y && io.MousePos.y < imageMax.y;
        bool viewportHovered = windowHovered && mouseInViewport;
        m_SceneViewHovered = viewportHovered;
        const bool terrainBrushConsumed = HandleTerrainBrush(imageMin, imageSize, viewportHovered);
        const bool proModelerConsumed = terrainBrushConsumed ? false : HandleProModelerSubobjectPick(imageMin, imageSize, viewportHovered);

        if (io.MouseClicked[ImGuiMouseButton_Left] && viewportHovered &&
            !terrainBrushConsumed &&
            !proModelerConsumed &&
            !Input::IsKeyPressed(GLFW_KEY_LEFT_ALT) && !Input::IsKeyPressed(GLFW_KEY_RIGHT_ALT)) {

            bool gizmoBlocks = m_SelectedEntityID != 0 && (ImGuizmo::IsOver() || ImGuizmo::IsUsing());

            if (!gizmoBlocks) {
                Entity* picked = PickEntityAtPoint(
                    m_Engine->GetScene(),
                    m_SceneCamera.GetCamera(),
                    io.MousePos.x, io.MousePos.y,
                    imageMin.x, imageMin.y,
                    imageSize.x, imageSize.y,
                    m_GameViewSettings.renderWidth,
                    m_GameViewSettings.renderHeight);
                if ((picked ? picked->GetID() : 0) != m_SelectedEntityID)
                    ClearProModelerSelection();
                SelectSingleEntity(picked ? picked->GetID() : 0);
            }
        }

        // 5) 郢晢ｽｬ郢ｧ・､郢ｧ・｢郢ｧ・ｦ郢晁ご逡醍ｹ敖郢晄ｺ倥・繝ｻ繝ｻD=0 邵ｺ・ｪ邵ｺ・ｮ邵ｺ・ｧ IsAnyItemHovered 邵ｺ・ｫ陟厄ｽｱ鬮ｻ・ｿ邵ｺ蜉ｱ竊醍ｸｺ繝ｻ・ｼ繝ｻ        ImGui::Dummy(viewportPanelSize);
        AcceptSceneViewDrops(imageMin, imageSize);

        // Canvas gizmo on top of dummy so toolbar row is not covered by hit-test / draw order
        DrawCanvasGizmo(imageMin, imageSize, m_SceneCamera.GetCamera());

        if (Entity* selected = GetSelectedEntity()) {
            const float aspect = imageSize.y > 0.0f ? (imageSize.x / imageSize.y) : 1.0f;
            EditorUISnap::DrawActiveGuides(
                m_Engine->GetScene(),
                *selected,
                m_SceneCamera.GetCamera(),
                m_GameViewSettings.renderWidth,
                m_GameViewSettings.renderHeight,
                aspect,
                imageMin,
                imageSize,
                sceneDrawList);
        }

        HandleSceneViewControls(viewportHovered && !terrainBrushConsumed && !proModelerConsumed);
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

void EditorApp::DrawGameViewToolbar() {
    Scene& scene = m_Engine->GetScene();
    const int presetCount = GetGameViewResolutionPresetCount();
    const int customIndex = presetCount - 1;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 3));
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::BeginCombo("##GameViewResolution", GetGameViewResolutionPresetLabel(m_GameViewSettings.resolutionPreset))) {
        for (int i = 0; i < presetCount; ++i) {
            const bool selected = m_GameViewSettings.resolutionPreset == i;
            if (ImGui::Selectable(GetGameViewResolutionPresetLabel(i), selected)) {
                ApplyGameViewResolutionPreset(m_GameViewSettings, i);
                SyncSceneCanvasesToGameView(scene, m_GameViewSettings);
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (m_GameViewSettings.resolutionPreset == customIndex) {
        int w = m_GameViewSettings.renderWidth;
        int h = m_GameViewSettings.renderHeight;
        ImGui::SetNextItemWidth(72.0f);
        if (ImGui::InputInt("W##GameView", &w, 0, 0)) {
            m_GameViewSettings.renderWidth = std::max(w, 1);
            SyncSceneCanvasesToGameView(scene, m_GameViewSettings);
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("x");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(72.0f);
        if (ImGui::InputInt("H##GameView", &h, 0, 0)) {
            m_GameViewSettings.renderHeight = std::max(h, 1);
            SyncSceneCanvasesToGameView(scene, m_GameViewSettings);
        }
    } else {
        ImGui::Text("%d x %d", m_GameViewSettings.renderWidth, m_GameViewSettings.renderHeight);
    }

    ImGui::SameLine();
    if (EditorTheme::Checkbox("Sync Canvas Resolution", &m_GameViewSettings.syncCanvasReferenceResolution)) {
        SyncSceneCanvasesToGameView(scene, m_GameViewSettings);
    }
    ImGui::PopStyleVar();
}

void EditorApp::DrawPlayerModeUI() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                                   ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##PlayerFullscreen", nullptr, flags);

    ImVec2 panelSize = ImGui::GetContentRegionAvail();
    if (panelSize.x > 1.0f && panelSize.y > 1.0f) {
        const ImVec2 panelMin = ImGui::GetCursorScreenPos();
        const ImVec2 panelMax{ panelMin.x + panelSize.x, panelMin.y + panelSize.y };
        ImGuiIO& io = ImGui::GetIO();
        const bool pointerActive = !Input::IsCursorLocked() &&
            io.MousePos.x >= panelMin.x && io.MousePos.x < panelMax.x &&
            io.MousePos.y >= panelMin.y && io.MousePos.y < panelMax.y;
        const float pointerX = (io.MousePos.x - panelMin.x) / std::max(panelSize.x, 1.0f) * panelSize.x;
        const float pointerY = (1.0f - (io.MousePos.y - panelMin.y) /
                                      std::max(panelSize.y, 1.0f)) * panelSize.y;
        m_Engine->GetUIRenderer().SetPointerState(
            pointerActive, pointerX, pointerY, io.MouseDown[ImGuiMouseButton_Left],
            pointerActive && ImGui::IsMouseClicked(ImGuiMouseButton_Left),
            ImGui::IsMouseReleased(ImGuiMouseButton_Left));

        Scene& scene = m_Engine->GetScene();
        Entity* cameraEntity = ResolveActiveShotCameraEntity(m_IsPlaying || m_Engine->IsPlayerMode());
        Camera activeCamera = m_FallbackGameCamera;

        if (cameraEntity) {
            auto* transformComp = cameraEntity->GetComponent<TransformComponent>();
            auto* cameraComp = cameraEntity->GetComponent<CameraComponent>();
            if (transformComp && cameraComp && cameraComp->enabled) {
                const glm::mat4 world = scene.GetWorldMatrix(*cameraEntity);
                cameraComp->camera.SyncFromWorldMatrix(glm::vec3(world[3]), world);
                if (IsCameraUsable(cameraComp->camera))
                    activeCamera = cameraComp->camera;
            }
        }

        const float aspect = panelSize.x / panelSize.y;
        activeCamera.SetPerspective(activeCamera.fov, aspect, activeCamera.nearClip,
                                    activeCamera.farClip);

        const uint32_t activeCamId = cameraEntity ? cameraEntity->GetID() : 0u;
        const int renderW = std::max(1, static_cast<int>(panelSize.x));
        const int renderH = std::max(1, static_cast<int>(panelSize.y));
        RenderSceneToFramebuffer(m_GameViewFBO, activeCamera, panelSize, 0, activeCamId, false,
                                 renderW, renderH);

        ImGui::GetWindowDrawList()->AddImage(
            (ImTextureID)(intptr_t)m_GameViewFBO.GetColorAttachment(), panelMin, panelMax,
            ImVec2{ 0, 1 }, ImVec2{ 1, 0 });

        m_GameViewHovered = true;
    }

    ImGui::End();
    ImGui::PopStyleVar(3);

    if (Input::IsKeyPressed(GLFW_KEY_ESCAPE))
        m_Engine->RequestQuit();
}

void EditorApp::DrawGameViewPanel() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 4, 4 });
    ImGui::Begin("Game View");

    DrawGameViewToolbar();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });
    ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
    if (viewportPanelSize.x > 0.0f && viewportPanelSize.y > 0.0f) {
        Scene& scene = m_Engine->GetScene();
        if (m_GameViewSettings.syncCanvasReferenceResolution)
            SyncSceneCanvasesToGameView(scene, m_GameViewSettings);

        Entity* cameraEntity = ResolveActiveShotCameraEntity(m_IsPlaying || m_Engine->IsPlayerMode());
        Camera activeCamera = m_FallbackGameCamera;

        if (cameraEntity) {
            auto* transformComp = cameraEntity->GetComponent<TransformComponent>();
            auto* cameraComp = cameraEntity->GetComponent<CameraComponent>();
            if (transformComp && cameraComp && cameraComp->enabled) {
                const glm::mat4 world = scene.GetWorldMatrix(*cameraEntity);
                cameraComp->camera.SyncFromWorldMatrix(glm::vec3(world[3]), world);
                if (IsCameraUsable(cameraComp->camera))
                    activeCamera = cameraComp->camera;
            }
        }

        const float renderAspect = static_cast<float>(std::max(m_GameViewSettings.renderWidth, 1)) /
                                   static_cast<float>(std::max(m_GameViewSettings.renderHeight, 1));
        activeCamera.SetPerspective(activeCamera.fov, renderAspect, activeCamera.nearClip,
                                    activeCamera.farClip);

        const ImVec2 renderSize{ static_cast<float>(m_GameViewSettings.renderWidth),
                                 static_cast<float>(m_GameViewSettings.renderHeight) };
        const uint32_t activeCamId = cameraEntity ? cameraEntity->GetID() : 0u;
        const ImVec2 panelMin = ImGui::GetCursorScreenPos();
        const GameViewLetterbox letterbox =
            ComputeGameViewLetterbox(panelMin, viewportPanelSize, m_GameViewSettings);
        const ImVec2 displayMax{ letterbox.displayMin.x + letterbox.displaySize.x,
                                 letterbox.displayMin.y + letterbox.displaySize.y };

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(panelMin,
                                ImVec2(panelMin.x + letterbox.panelSize.x, panelMin.y + letterbox.panelSize.y),
                                IM_COL32(0, 0, 0, 255));

        const uint32_t textureID = m_GameViewFBO.GetColorAttachment();
        drawList->AddImage((ImTextureID)(intptr_t)textureID, letterbox.displayMin, displayMax,
                           ImVec2{ 0, 1 }, ImVec2{ 1, 0 });

        ImGuiIO& io = ImGui::GetIO();
        const bool windowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        const bool mouseInDisplay =
            io.MousePos.x >= letterbox.displayMin.x && io.MousePos.x < displayMax.x &&
            io.MousePos.y >= letterbox.displayMin.y && io.MousePos.y < displayMax.y;
        m_GameViewHovered = windowHovered && mouseInDisplay;

        const float pointerX = (io.MousePos.x - letterbox.displayMin.x) /
            std::max(letterbox.displaySize.x, 1.0f) *
            static_cast<float>(m_GameViewSettings.renderWidth);
        const float pointerY = (1.0f - (io.MousePos.y - letterbox.displayMin.y) /
            std::max(letterbox.displaySize.y, 1.0f)) *
            static_cast<float>(m_GameViewSettings.renderHeight);
        const bool pointerActive = m_GameViewHovered && !Input::IsCursorLocked();
        m_Engine->GetUIRenderer().SetPointerState(
            pointerActive, pointerX, pointerY, io.MouseDown[ImGuiMouseButton_Left],
            pointerActive && ImGui::IsMouseClicked(ImGuiMouseButton_Left),
            ImGui::IsMouseReleased(ImGuiMouseButton_Left));

        RenderSceneToFramebuffer(m_GameViewFBO, activeCamera, renderSize, 0, activeCamId, false,
                                 m_GameViewSettings.renderWidth, m_GameViewSettings.renderHeight);

        if (m_IsPlaying && m_GameViewHovered) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                m_PlayCursorLocked = true;
        }

        ImGui::Dummy(viewportPanelSize);
    } else {
        m_GameViewHovered = false;
    }

    if (m_IsPlaying && m_GameViewHovered && !m_PlayCursorLocked) {
        ImGui::SetTooltip("Right-click to capture mouse (Esc to release)");
    }

    ImGui::PopStyleVar();
    ImGui::End();
    ImGui::PopStyleVar();
}

void EditorApp::DrawHierarchyContextMenu(Entity* parentForNew) {
    Scene& scene = m_Engine->GetScene();

    auto selectNew = [&](Entity* e) {
        if (parentForNew) scene.SetParent(e, parentForNew);
        m_SelectedEntityID = e->GetID();
        if (m_AssetBrowser) m_AssetBrowser->ClearAssetSelection();
    };

    if (ImGui::MenuItem("Create Empty Entity")) {
        RecordUndoSnapshot();
        selectNew(scene.CreateEntity("Empty Entity"));
    }

    if (ImGui::BeginMenu("3D Object")) {
        if (ImGui::MenuItem("Cube")) {
            RecordUndoSnapshot();
            auto* entity = scene.CreateEntity("Cube");
            auto& mr = entity->AddComponent<MeshRendererComponent>();
            mr.SetPrimitive("Cube", 1.0f);
            mr.texture = std::make_shared<Texture>(Texture::CreateCheckerboard(128, 16));
            AddDefaultCollider(*entity, "Cube", 1.0f);
            selectNew(entity);
        }
        if (ImGui::MenuItem("Sphere")) {
            RecordUndoSnapshot();
            auto* entity = scene.CreateEntity("Sphere");
            auto& mr = entity->AddComponent<MeshRendererComponent>();
            mr.SetPrimitive("Sphere", 1.0f);
            mr.texture = std::make_shared<Texture>(Texture::CreateCheckerboard(128, 16));
            AddDefaultCollider(*entity, "Sphere", 1.0f);
            selectNew(entity);
        }
        if (ImGui::MenuItem("Plane")) {
            RecordUndoSnapshot();
            auto* entity = scene.CreateEntity("Plane");
            auto& mr = entity->AddComponent<MeshRendererComponent>();
            mr.SetPrimitive("Plane", 10.0f);
            mr.texture = std::make_shared<Texture>(Texture::CreateCheckerboard(256, 32));
            AddDefaultCollider(*entity, "Plane", 10.0f);
            selectNew(entity);
        }
        if (ImGui::MenuItem("Terrain")) {
            RecordUndoSnapshot();
            auto* entity = scene.CreateEntity("Terrain");
            auto& mr = entity->AddComponent<MeshRendererComponent>();
            auto& terrain = entity->AddComponent<TerrainComponent>();
            terrain.RebuildMesh(mr);
            mr.texture = std::make_shared<Texture>(Texture::CreateCheckerboard(256, 32));
            if (auto* tr = entity->GetComponent<TransformComponent>())
                tr->position.y = 0.0f;
            selectNew(entity);
        }
        if (ImGui::BeginMenu("ProModeler")) {
            auto createProModeler = [&](const char* name, ProModelerComponent::Shape shape) {
                RecordUndoSnapshot();
                auto* entity = scene.CreateEntity(name);
                auto& mr = entity->AddComponent<MeshRendererComponent>();
                auto& pb = entity->AddComponent<ProModelerComponent>();
                if (shape == ProModelerComponent::Shape::Plane) pb.ResetPlane();
                else if (shape == ProModelerComponent::Shape::Ramp) pb.ResetRamp();
                else if (shape == ProModelerComponent::Shape::Stairs) pb.ResetStairs();
                else pb.ResetBox();
                pb.RebuildMesh(mr);
                mr.texture = std::make_shared<Texture>(Texture::CreateCheckerboard(128, 16));
                selectNew(entity);
            };
            if (ImGui::MenuItem("Cube")) createProModeler("ProModeler Cube", ProModelerComponent::Shape::Box);
            if (ImGui::MenuItem("Plane")) createProModeler("ProModeler Plane", ProModelerComponent::Shape::Plane);
            if (ImGui::MenuItem("Ramp")) createProModeler("ProModeler Ramp", ProModelerComponent::Shape::Ramp);
            if (ImGui::MenuItem("Stairs")) createProModeler("ProModeler Stairs", ProModelerComponent::Shape::Stairs);
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (ImGui::MenuItem("Camera")) {
        RecordUndoSnapshot();
        if (Entity* cam = CreateCamera(parentForNew)) {
            m_SelectedEntityID = cam->GetID();
            if (m_AssetBrowser) m_AssetBrowser->ClearAssetSelection();
        }
    }

    if (ImGui::MenuItem("Audio Source")) {
        RecordUndoSnapshot();
        auto* audioEntity = scene.CreateEntity("Audio Source");
        audioEntity->AddComponent<AudioSourceComponent>();
        selectNew(audioEntity);
    }

    if (ImGui::BeginMenu("Light")) {
        const glm::vec3 defaultPos{ 0.0f, 3.0f, 0.0f };
        if (ImGui::MenuItem("Point Light")) {
            RecordUndoSnapshot();
            if (Entity* light = CreatePointLight(parentForNew, defaultPos))
                selectNew(light);
        }
        if (ImGui::MenuItem("Spot Light")) {
            RecordUndoSnapshot();
            if (Entity* light = CreateSpotLight(parentForNew, defaultPos))
                selectNew(light);
        }
        if (ImGui::MenuItem("Directional Light")) {
            RecordUndoSnapshot();
            if (Entity* light = CreatePointLight(parentForNew, defaultPos)) {
                if (auto* l = light->GetComponent<LightComponent>()) {
                    l->type = LightType::Directional;
                    l->range = 0.0f;
                }
                if (auto* tr = light->GetComponent<TransformComponent>())
                    tr->rotation = { -50.0f, 30.0f, 0.0f };
                selectNew(light);
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::MenuItem("Post Process Volume")) {
        RecordUndoSnapshot();
        if (Entity* volume = CreatePostProcessVolume(parentForNew)) {
            selectNew(volume);
        }
    }

    if (ImGui::BeginMenu("UI")) {
        if (ImGui::MenuItem("Canvas")) {
            RecordUndoSnapshot();
            if (Entity* canvas = CreateUICanvas(parentForNew)) {
                m_SelectedEntityID = canvas->GetID();
                if (m_AssetBrowser) m_AssetBrowser->ClearAssetSelection();
            }
        }

        Entity* canvasParent = nullptr;
        if (parentForNew) {
            Entity* walk = parentForNew;
            while (walk) {
                if (walk->HasComponent<CanvasComponent>()) {
                    canvasParent = walk;
                    break;
                }
                walk = scene.FindEntity(walk->GetParentID());
            }
        }

        if (ImGui::MenuItem("Image", nullptr, false, canvasParent != nullptr)) {
            RecordUndoSnapshot();
            if (Entity* img = CreateUIImage(canvasParent)) {
                m_SelectedEntityID = img->GetID();
                if (m_AssetBrowser) m_AssetBrowser->ClearAssetSelection();
            }
        }
        if (ImGui::MenuItem("Text", nullptr, false, canvasParent != nullptr)) {
            RecordUndoSnapshot();
            if (Entity* txt = CreateUIText(canvasParent)) {
                m_SelectedEntityID = txt->GetID();
                if (m_AssetBrowser) m_AssetBrowser->ClearAssetSelection();
            }
        }
        if (ImGui::MenuItem("Button Group")) {
            RecordUndoSnapshot();
            Entity* parent = canvasParent;
            if (!parent)
                parent = CreateUICanvas(parentForNew);
            if (Entity* group = CreateUIButtonGroup(parent)) {
                m_SelectedEntityID = group->GetID();
                if (m_AssetBrowser) m_AssetBrowser->ClearAssetSelection();
            }
        }
        if (ImGui::MenuItem("Button")) {
            RecordUndoSnapshot();
            if (Entity* button = CreateUIButton(parentForNew)) {
                m_SelectedEntityID = button->GetID();
                if (m_AssetBrowser) m_AssetBrowser->ClearAssetSelection();
            }
        }
        if (ImGui::MenuItem("Audio Spectrum", nullptr, false, canvasParent != nullptr)) {
            RecordUndoSnapshot();
            if (Entity* spectrum = CreateUIAudioSpectrum(canvasParent)) {
                m_SelectedEntityID = spectrum->GetID();
                if (m_AssetBrowser) m_AssetBrowser->ClearAssetSelection();
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::MenuItem("First Person Controller")) {
        RecordUndoSnapshot();
        if (Entity* fps = CreateFirstPersonController(parentForNew)) {
            m_SelectedEntityID = fps->GetID();
            if (m_AssetBrowser) m_AssetBrowser->ClearAssetSelection();
        }
    }
}

Entity* EditorApp::CreatePointLight(Entity* parentForNew, const glm::vec3& worldPosition) {
    Scene& scene = m_Engine->GetScene();
    Entity* entity = scene.CreateEntity("Point Light");
    if (auto* transform = entity->GetComponent<TransformComponent>())
        transform->position = worldPosition;
    auto& light = entity->AddComponent<LightComponent>();
    light.type = LightType::Point;
    light.range = 10.0f;
    if (parentForNew)
        scene.SetParent(entity, parentForNew);
    return entity;
}

Entity* EditorApp::CreateSpotLight(Entity* parentForNew, const glm::vec3& worldPosition) {
    Scene& scene = m_Engine->GetScene();
    Entity* entity = scene.CreateEntity("Spot Light");
    if (auto* transform = entity->GetComponent<TransformComponent>()) {
        transform->position = worldPosition;
        transform->rotation = { -50.0f, 0.0f, 0.0f };
    }
    auto& light = entity->AddComponent<LightComponent>();
    light.type = LightType::Spot;
    light.range = 15.0f;
    light.spotAngle = 45.0f;
    light.spotInnerAngle = 30.0f;
    if (parentForNew)
        scene.SetParent(entity, parentForNew);
    return entity;
}

Entity* EditorApp::CreatePostProcessVolume(Entity* parentForNew) {
    Scene& scene = m_Engine->GetScene();
    Entity* entity = scene.CreateEntity("Post Process Volume");
    entity->AddComponent<PostProcessVolumeComponent>();
    if (parentForNew)
        scene.SetParent(entity, parentForNew);
    m_Engine->GetMipsRuntime().SyncEditSnapshot(scene);
    return entity;
}

Entity* EditorApp::CreateCamera(Entity* parentForNew) {
    Scene& scene = m_Engine->GetScene();

    Entity* cam = scene.CreateEntity("Camera");
    auto& cameraComp = cam->AddComponent<CameraComponent>();
    bool hasPrimary = false;
    for (const auto& entityPtr : scene.GetEntities()) {
        if (auto* c = entityPtr->GetComponent<CameraComponent>(); c && c->enabled && c->primary) {
            hasPrimary = true;
            break;
        }
    }
    cameraComp.primary = !hasPrimary;
    cameraComp.camera.SetPosition({ 0.0f, 2.0f, 6.0f });
    cameraComp.camera.LookAt({ 0.0f, 0.0f, 0.0f });
    if (auto* tr = cam->GetComponent<TransformComponent>())
        cameraComp.camera.SyncTransformFromCamera(*tr);

    if (cameraComp.primary)
        scene.SetPrimaryCamera(*cam);

    if (parentForNew)
        scene.SetParent(cam, parentForNew);

    m_Engine->GetMipsRuntime().SyncEditSnapshot(scene);
    return cam;
}

Entity* EditorApp::CreateUICanvas(Entity* parentForNew) {
    Scene& scene = m_Engine->GetScene();

    Entity* canvas = scene.CreateEntity("Canvas");
    canvas->AddComponent<CanvasComponent>();

    auto& rect = canvas->AddComponent<RectTransformComponent>();
    rect.anchorMin = { 0.0f, 0.0f };
    rect.anchorMax = { 1.0f, 1.0f };
    rect.pivot = { 0.5f, 0.5f };
    rect.anchoredPosition = { 0.0f, 0.0f };
    rect.sizeDelta = { 0.0f, 0.0f };

    if (parentForNew)
        scene.SetParent(canvas, parentForNew);

    m_Engine->GetMipsRuntime().SyncEditSnapshot(scene);
    return canvas;
}

Entity* EditorApp::CreateUIImage(Entity* canvasParent) {
    if (!canvasParent || !canvasParent->HasComponent<CanvasComponent>())
        return nullptr;

    Scene& scene = m_Engine->GetScene();
    Entity* image = scene.CreateEntity("Image");
    scene.SetParent(image, canvasParent);

    auto& rect = image->AddComponent<RectTransformComponent>();
    rect.anchorMin = { 0.5f, 0.5f };
    rect.anchorMax = { 0.5f, 0.5f };
    rect.pivot = { 0.5f, 0.5f };
    rect.anchoredPosition = { 0.0f, 0.0f };
    rect.sizeDelta = { 200.0f, 200.0f };

    auto& ui = image->AddComponent<UIImageComponent>();
    ui.color = { 0.15f, 0.45f, 0.85f, 0.92f };

    m_Engine->GetMipsRuntime().SyncEditSnapshot(scene);
    return image;
}

Entity* EditorApp::CreateUIText(Entity* canvasParent) {
    if (!canvasParent || !canvasParent->HasComponent<CanvasComponent>())
        return nullptr;

    Scene& scene = m_Engine->GetScene();
    Entity* textEntity = scene.CreateEntity("Text");
    scene.SetParent(textEntity, canvasParent);

    auto& rect = textEntity->AddComponent<RectTransformComponent>();
    rect.anchorMin = { 0.5f, 0.5f };
    rect.anchorMax = { 0.5f, 0.5f };
    rect.pivot = { 0.5f, 0.5f };
    rect.anchoredPosition = { 0.0f, 80.0f };
    rect.sizeDelta = { 320.0f, 48.0f };

    auto& text = textEntity->AddComponent<UITextComponent>();
    text.text = "New Text";
    text.fontSize = 24.0f;
    text.alignment = UITextAlignment::Center;

    m_Engine->GetMipsRuntime().SyncEditSnapshot(scene);
    return textEntity;
}

Entity* EditorApp::CreateUIButtonGroup(Entity* canvasParent) {
    if (!canvasParent || !canvasParent->HasComponent<CanvasComponent>())
        return nullptr;

    Scene& scene = m_Engine->GetScene();
    Entity* group = scene.CreateEntity("Button Group");
    scene.SetParent(group, canvasParent);

    auto& rect = group->AddComponent<RectTransformComponent>();
    rect.anchorMin = { 0.5f, 0.5f };
    rect.anchorMax = { 0.5f, 0.5f };
    rect.pivot = { 0.5f, 0.5f };
    rect.anchoredPosition = { 0.0f, 0.0f };
    rect.sizeDelta = { 360.0f, 240.0f };

    group->AddComponent<UIButtonGroupComponent>();

    m_Engine->GetMipsRuntime().SyncEditSnapshot(scene);
    return group;
}

Entity* EditorApp::CreateUIButton(Entity* parentOrCanvas) {
    Scene& scene = m_Engine->GetScene();

    Entity* groupParent = nullptr;
    if (parentOrCanvas && parentOrCanvas->HasComponent<UIButtonGroupComponent>()) {
        groupParent = parentOrCanvas;
    } else {
        Entity* walk = parentOrCanvas;
        while (walk) {
            if (walk->HasComponent<UIButtonGroupComponent>()) {
                groupParent = walk;
                break;
            }
            walk = scene.FindEntity(walk->GetParentID());
        }
    }

    if (!groupParent) {
        Entity* canvasParent = nullptr;
        if (parentOrCanvas && parentOrCanvas->HasComponent<CanvasComponent>())
            canvasParent = parentOrCanvas;
        else if (parentOrCanvas)
            canvasParent = FindCanvasAncestor(scene, parentOrCanvas);
        if (!canvasParent)
            canvasParent = CreateUICanvas(nullptr);
        groupParent = CreateUIButtonGroup(canvasParent);
    }

    if (!groupParent)
        return nullptr;

    Entity* button = scene.CreateEntity("Button");
    scene.SetParent(button, groupParent);

    auto& rect = button->AddComponent<RectTransformComponent>();
    rect.anchorMin = { 0.5f, 0.5f };
    rect.anchorMax = { 0.5f, 0.5f };
    rect.pivot = { 0.5f, 0.5f };
    const int buttonCount = static_cast<int>(std::count_if(
        groupParent->GetChildIDs().begin(), groupParent->GetChildIDs().end(),
        [&](uint32_t childId) {
            Entity* child = scene.FindEntity(childId);
            return child && child->HasComponent<UIButtonComponent>();
        }));
    rect.anchoredPosition = { 0.0f, -static_cast<float>(buttonCount) * 64.0f };
    rect.sizeDelta = { 260.0f, 54.0f };

    auto& uiButton = button->AddComponent<UIButtonComponent>();
    uiButton.label = "Button";

    Entity* textEntity = scene.CreateEntity("Text");
    scene.SetParent(textEntity, button);
    auto& textRect = textEntity->AddComponent<RectTransformComponent>();
    textRect.anchorMin = { 0.0f, 0.0f };
    textRect.anchorMax = { 1.0f, 1.0f };
    textRect.pivot = { 0.5f, 0.5f };
    textRect.anchoredPosition = { 0.0f, 0.0f };
    textRect.sizeDelta = { 0.0f, 0.0f };
    auto& text = textEntity->AddComponent<UITextComponent>();
    text.text = "Button";
    text.fontSize = 24.0f;
    text.alignment = UITextAlignment::Center;
    text.color = uiButton.textColor;

    m_Engine->GetMipsRuntime().SyncEditSnapshot(scene);
    return button;
}

Entity* EditorApp::CreateUIAudioSpectrum(Entity* canvasParent) {
    if (!canvasParent || !canvasParent->HasComponent<CanvasComponent>())
        return nullptr;
    Scene& scene = m_Engine->GetScene();
    Entity* entity = scene.CreateEntity("Audio Spectrum");
    scene.SetParent(entity, canvasParent);
    auto& rect = entity->AddComponent<RectTransformComponent>();
    rect.anchorMin = { 0.5f, 0.5f };
    rect.anchorMax = { 0.5f, 0.5f };
    rect.pivot = { 0.5f, 0.5f };
    rect.anchoredPosition = { 0.0f, -320.0f };
    rect.sizeDelta = { 640.0f, 180.0f };
    entity->AddComponent<UIAudioSpectrumComponent>();
    m_Engine->GetMipsRuntime().SyncEditSnapshot(scene);
    return entity;
}

void EditorApp::DrawUIButtonGroupCursorSlot(UIButtonGroupComponent& group) {
    if (DrawAssetReferenceField(this, "Cursor Sprite", AssetKind::Texture,
                                group.cursorTexturePath, "Default Cursor")) {
        RecordUndoSnapshot();
        group.cursorTexture = group.cursorTexturePath.empty()
            ? nullptr
            : AssetManager::Get().GetTexture(group.cursorTexturePath);
        m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
    }
}

void EditorApp::DrawUITextureSlot(UIImageComponent& image) {
    auto assignTexture = [&](const std::string& projectRelPath) {
        if (projectRelPath.empty() ||
            AssetBrowserPanel::ClassifyAssetByPath(projectRelPath) != AssetKind::Texture)
            return;
        RecordUndoSnapshot();
        image.texturePath = projectRelPath;
        image.texture = AssetManager::Get().GetTexture(image.texturePath);
        image.preserveAspect = true;
        FitRectTransformToTextureAspect(image.entity, image.texture.get());
        m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
        m_SceneDirty = true;
    };

    ImGui::Text("Texture:");
    ImGui::SameLine();
    const ImVec2 slotSize(220.0f, 32.0f);
    const std::string label = image.texturePath.empty() ? "<drop texture here>" : image.texturePath;

    ImGui::PushStyleColor(ImGuiCol_Button, EditorTheme::InputBg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorTheme::BtnFace);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, EditorTheme::BtnFaceLight);
    ImGui::PushStyleColor(ImGuiCol_Text,
                          image.texturePath.empty() ? EditorTheme::TextMuted : EditorTheme::TextPrimary);
    ImGui::Button(label.c_str(), slotSize);
    ImGui::PopStyleColor(4);

    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DragDrop::kAssetMove);
        if (!payload)
            payload = ImGui::AcceptDragDropPayload(DragDrop::kAssetTexture);
        if (payload) {
            const std::vector<std::string> paths = PathsFromDragPayload(payload);
            if (paths.size() == 1)
                assignTexture(paths[0]);
        }
        ImGui::EndDragDropTarget();
    }

    if (!image.texture && !image.texturePath.empty())
        image.texture = AssetManager::Get().GetTexture(image.texturePath);
    if (image.texture) {
        const float preview = 72.0f;
        ImGui::Image((ImTextureID)(intptr_t)image.texture->GetID(), ImVec2(preview, preview));
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextDisabled("%dx%d", image.texture->GetWidth(), image.texture->GetHeight());
        ImGui::TextWrapped("%s", image.texturePath.c_str());
        ImGui::EndGroup();
    }

    if (m_AssetBrowser && m_AssetBrowser->GetSelectedAssetCount() == 1 &&
        m_AssetBrowser->GetSelectedAssetKind() == AssetKind::Texture) {
        if (ImGui::Button("Assign Selected Texture", ImVec2(-1, 0)))
            assignTexture(m_AssetBrowser->GetSelectedAssetPath());
    }

    if (ImGui::Button("Clear Texture", ImVec2(-1, 0))) {
        RecordUndoSnapshot();
        image.texturePath.clear();
        image.texture.reset();
        m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
    }
}

void EditorApp::DrawUIButtonBackgroundSlot(UIButtonComponent& button) {
    if (DrawAssetReferenceField(this, "Background Sprite", AssetKind::Texture,
                                button.backgroundTexturePath, "Default Button")) {
        RecordUndoSnapshot();
        button.backgroundTexture = button.backgroundTexturePath.empty()
            ? nullptr
            : AssetManager::Get().GetTexture(button.backgroundTexturePath);
        if (button.backgroundTexture) {
            button.preserveAspect = true;
            FitRectTransformToTextureAspect(button.entity, button.backgroundTexture.get());
        }
        m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
        m_SceneDirty = true;
    }
}

Entity* EditorApp::CreateFirstPersonController(Entity* parentForNew) {
    Scene& scene = m_Engine->GetScene();

    Entity* fps = scene.CreateEntity("First Person Controller");
    if (auto* tr = fps->GetComponent<TransformComponent>())
        // Eye-height pivot. Capsule extends 1.7m below the pivot, so y=0.2 puts the
        // feet flush with the default floor at y=-1.5 (top surface).
        tr->position = { 0.0f, 0.2f, 0.0f };

    fps->AddComponent<CameraComponent>();
    scene.SetPrimaryCamera(*fps);

    auto& script = fps->AddComponent<MipsScriptComponent>();
    script.scriptPath = "assets/scripts/FirstPersonController.mips";
    ColliderUtils::EnsureFirstPersonPhysics(*fps);

    if (parentForNew) scene.SetParent(fps, parentForNew);

    m_Engine->GetMipsRuntime().SyncEditSnapshot(scene);
    return fps;
}

void EditorApp::DrawHierarchyEntityNode(Entity& entity) {
    Scene& scene = m_Engine->GetScene();

    auto* tagComp = entity.GetComponent<TagComponent>();
    std::string name = tagComp ? tagComp->tag : "Unknown";

    const bool hasChildren = !entity.GetChildIDs().empty();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;
    if (IsEntitySelected(entity.GetID())) flags |= ImGuiTreeNodeFlags_Selected;

    bool opened = ImGui::TreeNodeEx((void*)(uint64_t)entity.GetID(), flags, "%s", name.c_str());

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
        m_PendingHierarchyClickEntityID = entity.GetID();

    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        m_PendingHierarchyClickEntityID = 0;
        SelectSingleEntity(entity.GetID());
        FrameEntityInSceneView(entity);
    }

    if (m_PendingHierarchyClickEntityID == entity.GetID() && ImGui::IsItemHovered() &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
        !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        m_PendingHierarchyClickEntityID = 0;
        HandleHierarchySelection(entity.GetID());
    }

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        m_PendingHierarchyClickEntityID = 0;
        const std::vector<uint32_t> roots = IsEntitySelected(entity.GetID())
            ? GetSelectedHierarchyRootIDs(entity.GetID())
            : std::vector<uint32_t>{ entity.GetID() };
        ImGui::SetDragDropPayload(DragDrop::kEntityId, roots.data(),
                                  roots.size() * sizeof(uint32_t));
        ImGui::Text("%zu object%s", roots.size(), roots.size() == 1 ? "" : "s");
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* entityPayload = ImGui::AcceptDragDropPayload(DragDrop::kEntityId);
        if (entityPayload) {
            const ImGuiPayload* p = entityPayload;
            if (p->DataSize >= static_cast<int>(sizeof(uint32_t))) {
                const auto* ids = static_cast<const uint32_t*>(p->Data);
                const size_t count = static_cast<size_t>(p->DataSize) / sizeof(uint32_t);
                std::vector<uint32_t> children(ids, ids + count);
                if (std::find(children.begin(), children.end(), entity.GetID()) == children.end()) {
                    RecordUndoSnapshot();
                    const ImVec2 itemMin = ImGui::GetItemRectMin();
                    const ImVec2 itemMax = ImGui::GetItemRectMax();
                    const float itemH = std::max(1.0f, itemMax.y - itemMin.y);
                    const float t = (ImGui::GetIO().MousePos.y - itemMin.y) / itemH;
                    if (t < 0.25f) {
                        m_PendingReparentParent = entity.GetParentID();
                        m_PendingReparentSibling = entity.GetID();
                        m_PendingReparentAfterSibling = false;
                    } else if (t > 0.75f) {
                        m_PendingReparentParent = entity.GetParentID();
                        m_PendingReparentSibling = entity.GetID();
                        m_PendingReparentAfterSibling = true;
                    } else {
                        m_PendingReparentParent = entity.GetID();
                        m_PendingReparentSibling = 0;
                        m_PendingReparentAfterSibling = false;
                    }
                    m_PendingReparentChildren = std::move(children);
                }
            }
        } else {
            const ImGuiPayload* assetPayload =
                ImGui::AcceptDragDropPayload(DragDrop::kAssetMove);
            if (!assetPayload)
                assetPayload = ImGui::AcceptDragDropPayload(DragDrop::kAssetAudio);
            if (assetPayload) {
                const std::vector<std::string> paths = PathsFromDragPayload(assetPayload);
                if (paths.size() == 1 &&
                    AssetBrowserPanel::ClassifyAssetByPath(paths[0]) == AssetKind::Audio)
                    DispatchProjectAssetDrop(paths[0]);
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem()) {
        if (!IsEntitySelected(entity.GetID()))
            SelectSingleEntity(entity.GetID());
        DrawHierarchyContextMenu(&entity);
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate")) {
            RecordUndoSnapshot();
            Entity* dup = scene.DuplicateEntity(entity);
            if (dup) {
                Entity* parent = scene.FindEntity(entity.GetParentID());
                scene.SetParent(dup, parent);
                m_SelectedEntityID = dup->GetID();
                m_Engine->GetMipsRuntime().SyncEditSnapshot(scene);
            }
        }
        if (ImGui::MenuItem("Save as Prefab")) {
            std::string baseName = "Prefab";
            if (auto* tag = entity.GetComponent<TagComponent>())
                baseName = tag->tag;
            const std::string relFolder = "assets/prefabs";
            std::string relTarget = relFolder + "/" + baseName + ".nprefab";
            std::string absTarget = AssetManager::Get().ToAbsolute(relTarget);
            int counter = 1;
            while (std::filesystem::exists(std::filesystem::path(absTarget))) {
                relTarget = relFolder + "/" + baseName + "_" + std::to_string(counter) + ".nprefab";
                absTarget = AssetManager::Get().ToAbsolute(relTarget);
                ++counter;
            }
            std::string err;
            if (SceneIO::SaveEntityToFile(entity, scene, absTarget, err)) {
                const_cast<Entity&>(entity).SetPrefabSourcePath(absTarget);
                if (m_AssetBrowser) m_AssetBrowser->Refresh();
            } else {
                MIPSYNC_WARN("Save as Prefab failed: {}", err);
            }
        }
        if (ImGui::MenuItem("Unparent (move to root)") && entity.GetParentID() != 0) {
            RecordUndoSnapshot();
            m_PendingReparentChildren = { entity.GetID() };
            m_PendingReparentParent = 0;
            m_PendingReparentSibling = 0;
            m_PendingReparentAfterSibling = false;
        }
        if (ImGui::MenuItem("Delete Entity")) {
            RecordUndoSnapshot();
            m_PendingHierarchyDeleteIDs = GetSelectedHierarchyRootIDs(entity.GetID());
        }
        ImGui::EndPopup();
    }

    if (opened) {
        // Snapshot child IDs so reparenting/destruction during traversal is safe.
        std::vector<uint32_t> children = entity.GetChildIDs();
        for (uint32_t childId : children) {
            if (Entity* child = scene.FindEntity(childId))
                DrawHierarchyEntityNode(*child);
        }
        ImGui::TreePop();
    }
}

void EditorApp::DrawHierarchyPanel() {
    // Display title may include '*', but window ID must stay "Scene Hierarchy" for docking.
    const char* hierarchyTitle = m_SceneDirty
        ? "Scene Hierarchy*###Scene Hierarchy"
        : "Scene Hierarchy";
    ImGui::Begin(hierarchyTitle);

    Scene& scene = m_Engine->GetScene();

    m_HierarchyOrder.clear();
    for (const auto& entityPtr : scene.GetEntities()) {
        if (entityPtr->GetParentID() == 0)
            CollectHierarchyOrder(*entityPtr, m_HierarchyOrder);
    }

    m_PendingHierarchyDelete = nullptr;
    m_PendingHierarchyDeleteIDs.clear();
    m_PendingReparentChildren.clear();
    m_PendingReparentParent  = 0;
    m_PendingReparentSibling = 0;
    m_PendingReparentAfterSibling = false;

    for (const auto& entityPtr : scene.GetEntities()) {
        if (entityPtr->GetParentID() == 0)
            DrawHierarchyEntityNode(*entityPtr);
    }

    // Empty area: left-click deselect, right-click create menu, drag here to unparent.
    {
        const ImVec2 remaining = ImGui::GetContentRegionAvail();
        const float emptyH = std::max(remaining.y, ImGui::GetTextLineHeightWithSpacing() * 3.0f);
        ImGui::InvisibleButton("##hier_empty", ImVec2(-1.0f, emptyH));
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            ClearEntitySelection();
        if (ImGui::BeginPopupContextItem()) {
            ClearEntitySelection();
            DrawHierarchyContextMenu(nullptr);
            ImGui::EndPopup();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(DragDrop::kEntityId)) {
                if (p->DataSize >= static_cast<int>(sizeof(uint32_t))) {
                    RecordUndoSnapshot();
                    const auto* ids = static_cast<const uint32_t*>(p->Data);
                    const size_t count = static_cast<size_t>(p->DataSize) / sizeof(uint32_t);
                    m_PendingReparentChildren.assign(ids, ids + count);
                    m_PendingReparentParent = 0;
                    m_PendingReparentSibling = 0;
                    m_PendingReparentAfterSibling = false;
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    // Right-click on window padding / gaps between items (not on an entity row).
    if (ImGui::BeginPopupContextWindow("HierarchyPanelCtx",
            ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        ClearEntitySelection();
        DrawHierarchyContextMenu(nullptr);
        ImGui::EndPopup();
    }

    AcceptPrefabDrop();

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::GetIO().WantTextInput) {
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C) &&
            (!m_SelectedEntityIDs.empty() || m_SelectedEntityID != 0)) {
            m_HierarchyClipboardEntityIDs = GetSelectedHierarchyRootIDs(m_SelectedEntityID);
        }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V) &&
            !m_HierarchyClipboardEntityIDs.empty()) {
            std::vector<uint32_t> pastedIds;
            bool recordedUndo = false;
            for (uint32_t sourceId : m_HierarchyClipboardEntityIDs) {
                Entity* source = scene.FindEntity(sourceId);
                if (!source)
                    continue;
                if (!recordedUndo) {
                    RecordUndoSnapshot();
                    recordedUndo = true;
                }
                Entity* dup = scene.DuplicateEntity(*source);
                if (!dup)
                    continue;
                Entity* parent = scene.FindEntity(source->GetParentID());
                scene.SetParent(dup, parent);
                pastedIds.push_back(dup->GetID());
            }
            if (!pastedIds.empty()) {
                m_SelectedEntityIDs.clear();
                for (uint32_t id : pastedIds)
                    m_SelectedEntityIDs.insert(id);
                m_SelectedEntityID = pastedIds.back();
                m_HierarchySelectionAnchor = m_SelectedEntityID;
                m_Engine->GetMipsRuntime().SyncEditSnapshot(scene);
                m_SceneDirty = true;
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) &&
            (!m_SelectedEntityIDs.empty() || m_SelectedEntityID != 0)) {
            RecordUndoSnapshot();
            m_PendingHierarchyDeleteIDs = GetSelectedHierarchyRootIDs(m_SelectedEntityID);
        }
    }

    if (!m_PendingReparentChildren.empty()) {
        Entity* parent = scene.FindEntity(m_PendingReparentParent);
        Entity* sibling = scene.FindEntity(m_PendingReparentSibling);
        bool reparented = false;
        if (m_PendingReparentSibling != 0) {
            if (m_PendingReparentAfterSibling) {
                Entity* anchor = sibling;
                for (uint32_t cid : m_PendingReparentChildren) {
                    Entity* child = scene.FindEntity(cid);
                    if (!child)
                        continue;
                    if (scene.ReorderEntity(child, parent, anchor, true)) {
                        reparented = true;
                        anchor = child;
                    }
                }
            } else {
                for (uint32_t cid : m_PendingReparentChildren) {
                    Entity* child = scene.FindEntity(cid);
                    if (child)
                        reparented = scene.ReorderEntity(child, parent, sibling, false) || reparented;
                }
            }
        } else {
            for (uint32_t cid : m_PendingReparentChildren) {
                Entity* child = scene.FindEntity(cid);
                if (child)
                    reparented = scene.SetParent(child, parent) || reparented;
            }
        }
        if (reparented) {
            m_Engine->GetMipsRuntime().SyncEditSnapshot(scene);
            m_SceneDirty = true;
        }
    }

    if (m_PendingHierarchyDelete) {
        if (m_SelectedEntityID == m_PendingHierarchyDelete->GetID()) m_SelectedEntityID = 0;
        scene.DestroyEntity(m_PendingHierarchyDelete);
        m_Engine->GetMipsRuntime().SyncEditSnapshot(scene);
        m_SceneDirty = true;
    }
    if (!m_PendingHierarchyDeleteIDs.empty()) {
        const auto ids = m_PendingHierarchyDeleteIDs;
        bool deleted = false;
        for (uint32_t id : ids) {
            if (Entity* target = scene.FindEntity(id)) {
                scene.DestroyEntity(target);
                deleted = true;
            }
            m_SelectedEntityIDs.erase(id);
            if (m_SelectedEntityID == id) m_SelectedEntityID = 0;
        }
        if (deleted) {
            m_Engine->GetMipsRuntime().SyncEditSnapshot(scene);
            m_SceneDirty = true;
        }
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        m_PendingHierarchyClickEntityID = 0;

    ImGui::End();
}

std::vector<std::string> EditorApp::CollectProjectScripts() const {
    std::vector<std::string> out;
    const std::string& root = AssetManager::Get().GetProjectRoot();
    if (root.empty())
        return out;

    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path rootPath = PathUtf8::FromString(root);
    if (!fs::is_directory(rootPath, ec))
        return out;

    for (auto it = fs::recursive_directory_iterator(rootPath, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (!it->is_regular_file(ec)) continue;

        const std::string ext = PathUtf8::ToString(it->path().extension());
        if (ext != ".mips" && ext != ".MIPS") continue;

        const fs::path rel = fs::relative(it->path(), rootPath, ec);
        if (ec) { ec.clear(); continue; }
        std::string relStr = PathUtf8::ToString(rel);
        std::replace(relStr.begin(), relStr.end(), '\\', '/');
        out.push_back(relStr);
    }
    std::sort(out.begin(), out.end());
    return out;
}

void EditorApp::DrawAddComponentScriptMenu(Entity& entity) {
    if (!ImGui::BeginMenu("Scripts"))
        return;

    const auto scripts = CollectProjectScripts();
    if (scripts.empty()) {
        ImGui::TextDisabled("No .mips scripts found");
    } else {
        for (const std::string& path : scripts) {
            if (ImGui::MenuItem(path.c_str())) {
                RecordUndoSnapshot();
                auto& script = entity.AddComponent<MipsScriptComponent>();
                script.scriptPath = path;
                script.module.reset();
                script.fieldValues.clear();
                ImGui::CloseCurrentPopup();
            }
        }
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Empty (set path manually)")) {
        RecordUndoSnapshot();
        auto& script = entity.AddComponent<MipsScriptComponent>();
        script.scriptPath.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndMenu();
}

void EditorApp::FocusDockedWindow(const char* windowName) {
    ImGuiWindow* window = ImGui::FindWindowByName(windowName);
    if (!window)
        return;
    ImGui::FocusWindow(window);
}

void EditorApp::OpenAnimatorControllerWindow(const std::string& projectRelPath) {
    if (projectRelPath.empty())
        return;
    m_AnimatorControllerWindowPath = projectRelPath;
    FocusDockedWindow(kAnimatorControllerWindowTitle);
}

std::string EditorApp::FindModelPathForController(const std::string& controllerRelPath) const {
    if (controllerRelPath.empty() || !m_Engine)
        return {};

    const Scene& scene = m_Engine->GetScene();
    for (const auto& entity : scene.GetEntities()) {
        if (!entity)
            continue;
        auto* anim = entity->GetComponent<AnimatorComponent>();
        if (!anim || anim->controllerPath != controllerRelPath)
            continue;
        if (!anim->modelPath.empty())
            return anim->modelPath;
        if (auto* skinned = entity->GetComponent<SkinnedMeshRendererComponent>()) {
            if (!skinned->modelPath.empty())
                return skinned->modelPath;
        }
    }
    return {};
}

void EditorApp::DrawAnimatorControllerPanel() {
    if (!m_AnimatorControllerDockEnsured)
        EnsureAnimatorControllerDocked();

    ImGui::Begin(kAnimatorControllerWindowTitle);
    if (m_AnimatorControllerWindowPath.empty()) {
        ImGui::TextColored(EditorTheme::TextMuted,
                           "Select a .ncontroller in Project, or use Open Controller Window on an "
                           "Animator component.");
    } else {
        DrawAnimatorControllerEditor(this, m_AnimatorControllerWindowPath);
    }
    ImGui::End();
}

void EditorApp::DrawMipsScriptIdeActions(const std::string& projectRelPath) {
    ImGui::PushID(projectRelPath.c_str());
    const bool hasScript = !projectRelPath.empty();
    if (!hasScript)
        ImGui::BeginDisabled();

    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float buttonW = std::max(92.0f, (ImGui::GetContentRegionAvail().x - spacing) * 0.5f);
    if (EditorTheme::AeroButton("Open in IDE", ImVec2(buttonW, EditorTheme::ButtonHeight),
                                AeroButtonKind::Secondary)) {
        if (!MipsEditorIntegration::OpenScriptInIde(projectRelPath)) {
            EDITOR_WARN("[Mips# IDE] Failed to open script in IDE: {}", projectRelPath);
            TriggerBuildToast("Could not open script in IDE", false);
        }
    }
    ImGui::SameLine();
    if (EditorTheme::AeroButton("Validate", ImVec2(-1.0f, EditorTheme::ButtonHeight),
                                AeroButtonKind::Primary)) {
        const auto result = MipsEditorIntegration::ValidateScript(projectRelPath);
        MipsEditorIntegration::LogValidationResult(projectRelPath, result);
        if (result.success) {
            const std::string cls = result.module ? result.module->className : std::string{"Mips#"};
            TriggerBuildToast("Mips# OK: " + cls, true);
        } else {
            TriggerBuildToast("Mips# compile errors; see Console", false);
            if (!result.diagnostics.empty() && result.diagnostics.front().hasLocation) {
                const auto& first = result.diagnostics.front();
                MipsEditorIntegration::OpenScriptInIde(projectRelPath, first.line, first.column);
            }
        }
    }

    if (!hasScript)
        ImGui::EndDisabled();
    ImGui::PopID();
}

void EditorApp::DrawMipsScriptAssetInspector(const std::string& projectRelPath) {
    ImGui::TextColored(EditorTheme::TextSecondary, "Mips# Script Asset");
    ImGui::TextWrapped("%s", projectRelPath.c_str());
    ImGui::Spacing();

    DrawMipsScriptIdeActions(projectRelPath);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(EditorTheme::TextMuted, "IDE integration");
    ImGui::TextWrapped("Use the bundled VSCode/Cursor extension folder:");
    ImGui::TextWrapped("%s", MipsEditorIntegration::BuildVsCodeExtensionPathHint().c_str());
    ImGui::TextWrapped("Diagnostics use the same Mips# compiler as builds, so editor errors and build errors stay aligned.");
}

void EditorApp::DrawInspectorPanel() {
    ImGui::Begin("Inspector");

    const bool singleAssetSelected =
        m_AssetBrowser && m_AssetBrowser->GetSelectedAssetCount() == 1 &&
        !m_AssetBrowser->GetSelectedAssetPath().empty();

    if (singleAssetSelected &&
        m_AssetBrowser->GetSelectedAssetKind() == AssetKind::Material) {
        DrawMaterialAssetInspector(m_AssetBrowser->GetSelectedAssetPath());
        ImGui::End();
        return;
    }

    if (singleAssetSelected &&
        m_AssetBrowser->GetSelectedAssetKind() == AssetKind::AnimatorController) {
        const std::string& path = m_AssetBrowser->GetSelectedAssetPath();
        ImGui::TextColored(EditorTheme::TextSecondary, "Animator Controller Asset");
        ImGui::TextWrapped("%s", path.c_str());
        ImGui::Spacing();
        if (EditorTheme::AeroButton("Open Animator Controller Window",
                                     ImVec2(-1.0f, EditorTheme::ButtonHeight),
                                     AeroButtonKind::Primary)) {
            OpenAnimatorControllerWindow(path);
        }
        ImGui::End();
        return;
    }

    if (singleAssetSelected &&
        m_AssetBrowser->GetSelectedAssetKind() == AssetKind::AnimationClip) {
        std::string modelPath;
        std::string clipName;
        if (m_AssetBrowser->GetSelectedAnimationClip(modelPath, clipName)) {
            ImGui::TextColored(EditorTheme::TextSecondary, "Animation Clip");
            ImGui::TextWrapped("%s", clipName.c_str());
            ImGui::Spacing();
            ImGui::TextColored(EditorTheme::TextMuted, "Model");
            ImGui::TextWrapped("%s", modelPath.c_str());
            ImGui::Spacing();
            ImGui::TextColored(EditorTheme::TextMuted,
                               "Drag onto an Animator Controller graph to add a state.");
        }
        ImGui::End();
        return;
    }

    if (singleAssetSelected &&
        m_AssetBrowser->GetSelectedAssetKind() == AssetKind::Script) {
        DrawMipsScriptAssetInspector(m_AssetBrowser->GetSelectedAssetPath());
        ImGui::End();
        return;
    }

    if (m_SelectedEntityID != 0) {
        Entity* selectedEntity = GetSelectedEntity();

        if (selectedEntity) {
            ImGui::BeginChild("##unity_object_header", ImVec2(-1.0f, 82.0f),
                              ImGuiChildFlags_Borders);
            const ImVec2 iconMin = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(36.0f, 36.0f));
            DrawInspectorCubeIcon(ImGui::GetWindowDrawList(),
                                  ImVec2(iconMin.x + 18.0f, iconMin.y + 18.0f), 24.0f,
                                  ImGui::GetColorU32(EditorTheme::TextSecondary));
            ImGui::SameLine();
            bool active = selectedEntity->IsActive();
            ImGui::PushID("object_active");
            const bool activeChanged = DrawComponentEnabledToggle(active);
            ImGui::PopID();
            if (activeChanged)
                selectedEntity->SetActive(active);
            ImGui::SameLine();
            if (auto* tag = selectedEntity->GetComponent<TagComponent>()) {
                char nameBuffer[256]{};
                strncpy(nameBuffer, tag->tag.c_str(), sizeof(nameBuffer) - 1);
                ImGui::SetNextItemWidth(std::max(100.0f, ImGui::GetContentRegionAvail().x - 92.0f));
                if (ImGui::InputText("##object_name", nameBuffer, sizeof(nameBuffer)))
                    tag->tag = nameBuffer;
            }
            ImGui::SameLine();
            bool isStatic = selectedEntity->IsStatic();
            if (EditorTheme::Checkbox("Static", &isStatic))
                selectedEntity->SetStatic(isStatic);

            ImGui::SetCursorPosY(47.0f);
            ImGui::TextDisabled("Tag"); ImGui::SameLine(42.0f);
            ImGui::SetNextItemWidth(118.0f);
            const char* tags[] = { "Untagged", "Player", "MainCamera", "EditorOnly" };
            int tagIndex = 0;
            for (int i = 0; i < IM_ARRAYSIZE(tags); ++i)
                if (selectedEntity->GetEditorTag() == tags[i]) tagIndex = i;
            if (ImGui::Combo("##object_tag", &tagIndex, tags, IM_ARRAYSIZE(tags)))
                selectedEntity->SetEditorTag(tags[tagIndex]);
            ImGui::SameLine(); ImGui::TextDisabled("Layer"); ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0f);
            const char* layers[] = { "Default", "TransparentFX", "Ignore Raycast", "UI" };
            int layerIndex = 0;
            for (int i = 0; i < IM_ARRAYSIZE(layers); ++i)
                if (selectedEntity->GetEditorLayer() == layers[i]) layerIndex = i;
            if (ImGui::Combo("##object_layer", &layerIndex, layers, IM_ARRAYSIZE(layers)))
                selectedEntity->SetEditorLayer(layers[layerIndex]);
            ImGui::EndChild();
            ImGui::Spacing();

            // 隨渉隨渉 Prefab header 隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉
            if (selectedEntity->IsPrefabInstance()) {
                const std::string& srcPath = selectedEntity->GetPrefabSourcePath();
                ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorTheme::AccentMuted);
                ImGui::BeginChild("##prefab_header", ImVec2(-1, 0), ImGuiChildFlags_AutoResizeY);
                ImGui::PopStyleColor();

                ImGui::Spacing();
                ImGui::TextColored(EditorTheme::TextBrand, "[ Prefab Instance ]");
                const std::filesystem::path p(srcPath);
                ImGui::TextColored(EditorTheme::TextMuted, "%s", p.filename().string().c_str());
                ImGui::Spacing();

                const float btnW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
                if (EditorTheme::AeroButton("Apply", ImVec2(btnW, EditorTheme::ButtonHeight),
                                            AeroButtonKind::Primary)) {
                    std::string err;
                    if (!SceneIO::ApplyPrefabToFile(*selectedEntity, m_Engine->GetScene(), srcPath, err))
                        MIPSYNC_WARN("Apply Prefab failed: {}", err);
                    else if (m_AssetBrowser)
                        m_AssetBrowser->Refresh();
                }
                ImGui::SameLine();
                if (EditorTheme::AeroButton("Revert", ImVec2(btnW, EditorTheme::ButtonHeight),
                                            AeroButtonKind::Secondary)) {
                    std::string err;
                    Entity* fresh = SceneIO::RevertEntityFromPrefab(
                        m_Engine->GetScene(), *selectedEntity, err);
                    if (fresh)
                        m_SelectedEntityID = fresh->GetID();
                    else
                        MIPSYNC_WARN("Revert Prefab failed: {}", err);
                }
                ImGui::Spacing();
                ImGui::EndChild();
                DrawInspectorDivider();
            }
            // 隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉

            auto* transformComp = selectedEntity->GetComponent<TransformComponent>();
            if (transformComp && !selectedEntity->GetComponent<RectTransformComponent>()) {
                if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                    if (ImGui::DragFloat3("Position", glm::value_ptr(transformComp->position), 0.1f) ||
                        ImGui::DragFloat3("Rotation", glm::value_ptr(transformComp->rotation), 0.1f) ||
                        ImGui::DragFloat3("Scale", glm::value_ptr(transformComp->scale), 0.1f)) {
                        SyncSelectedCameraFromTransform(selectedEntity);
                        m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
                        SyncPhysicsAfterTransformEdit(selectedEntity);
                    }
                }
                DrawInspectorDivider();
            }

            bool removeComponent = false;

            auto* cameraComp = selectedEntity->GetComponent<CameraComponent>();
            if (cameraComp) {
                const bool open = BeginInspectableComponent("Camera", *cameraComp, removeComponent, this);
                if (!removeComponent && open) {
                    bool cameraChanged = false;
                    if (EditorTheme::Checkbox("Primary", &cameraComp->primary)) {
                        if (cameraComp->primary)
                            m_Engine->GetScene().SetPrimaryCamera(*selectedEntity);
                        cameraChanged = true;
                    }
                    cameraChanged |= ImGui::DragFloat("Field of View", &cameraComp->camera.fov, 0.5f, 15.0f, 120.0f);
                    cameraChanged |= ImGui::DragFloat("Near Clip", &cameraComp->camera.nearClip, 0.01f, 0.01f, 10.0f);
                    cameraChanged |= ImGui::DragFloat("Far Clip", &cameraComp->camera.farClip, 0.5f, 1.0f, 500.0f);
                    char bgBuffer[512]{};
                    strncpy(bgBuffer, cameraComp->prerenderedBackgroundPath.c_str(), sizeof(bgBuffer) - 1);
                    if (ImGui::InputText("Prerender BG", bgBuffer, sizeof(bgBuffer))) {
                        cameraComp->prerenderedBackgroundPath = bgBuffer;
                        m_PrerenderedBackgroundTextures.clear();
                        m_SceneDirty = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Clear##prerender_bg")) {
                        cameraComp->prerenderedBackgroundPath.clear();
                        m_PrerenderedBackgroundTextures.clear();
                        m_SceneDirty = true;
                    }
                    if (ImGui::InputInt("Shot Priority", &cameraComp->shotPriority))
                        m_SceneDirty = true;
                    if (cameraChanged)
                        SyncSelectedCameraFromTransform(selectedEntity);
                }
                EndInspectableComponent(*cameraComp);
                if (removeComponent) {
                    selectedEntity->RemoveComponent<CameraComponent>();
                    ImGui::End();
                    return;
                }
            }

            auto* lightComp = selectedEntity->GetComponent<LightComponent>();
            if (lightComp) {
                removeComponent = false;
                const bool open = BeginInspectableComponent("Light", *lightComp, removeComponent, this);
                if (!removeComponent && open) {
                    const char* typeNames[] = { "Directional", "Point", "Spot" };
                    int typeIdx = static_cast<int>(lightComp->type);
                    if (ImGui::Combo("Type", &typeIdx, typeNames, IM_ARRAYSIZE(typeNames)))
                        lightComp->type = static_cast<LightType>(typeIdx);
                    if (ImGui::ColorEdit3("Color", glm::value_ptr(lightComp->color)))
                        { /* live */ }
                    if (ImGui::DragFloat("Intensity", &lightComp->intensity, 0.05f, 0.0f, 10.0f))
                        { /* live */ }
                    if (lightComp->type != LightType::Directional) {
                        if (ImGui::DragFloat("Range", &lightComp->range, 0.1f, 0.1f, 200.0f))
                            { /* live */ }
                    }
                    if (lightComp->type == LightType::Spot) {
                        if (ImGui::DragFloat("Spot Angle", &lightComp->spotAngle, 0.5f, 1.0f, 89.0f))
                            lightComp->spotInnerAngle = std::min(lightComp->spotInnerAngle, lightComp->spotAngle);
                        if (ImGui::DragFloat("Inner Angle", &lightComp->spotInnerAngle, 0.5f, 1.0f,
                                              lightComp->spotAngle))
                            lightComp->spotInnerAngle = std::min(lightComp->spotInnerAngle, lightComp->spotAngle);
                    }
                    ImGui::TextColored(EditorTheme::TextMuted,
                        "Rotation aims the light (local -Z forward).");
                }
                EndInspectableComponent(*lightComp);
                if (removeComponent) {
                    selectedEntity->RemoveComponent<LightComponent>();
                    ImGui::End();
                    return;
                }
            }

            auto* postProcess = selectedEntity->GetComponent<PostProcessVolumeComponent>();
            if (postProcess) {
                removeComponent = false;
                const bool open = BeginInspectableComponent("Post Process Volume", *postProcess,
                                                            removeComponent, this);
                if (!removeComponent && open) {
                    if (EditorTheme::Checkbox("Is Global", &postProcess->isGlobal))
                        RecordUndoSnapshot();
                    if (ImGui::DragInt("Priority", &postProcess->priority, 1.0f, -100, 100))
                        RecordUndoSnapshot();
                    ImGui::TextColored(EditorTheme::TextMuted,
                        "Highest enabled priority volume drives the scene post-process.");

                    if (ImGui::CollapsingHeader("HDRI Skybox", ImGuiTreeNodeFlags_DefaultOpen)) {
                        if (EditorTheme::Checkbox("Enable Skybox", &postProcess->skyboxEnabled))
                            RecordUndoSnapshot();
                        ImGui::BeginDisabled(!postProcess->skyboxEnabled);
                        if (DrawAssetReferenceField(this, "HDRI Texture", AssetKind::Texture,
                                                    postProcess->skyboxTexturePath, "None (Texture)")) {
                            RecordUndoSnapshot();
                            postProcess->skyboxTexture = postProcess->skyboxTexturePath.empty()
                                ? nullptr
                                : AssetManager::Get().GetSkyboxTexture(postProcess->skyboxTexturePath);
                        }
                        if (ImGui::DragFloat("Rotation Y", &postProcess->skyboxRotationDegrees,
                                             0.5f, -360.0f, 360.0f, "%.1f deg"))
                            RecordUndoSnapshot();
                        if (ImGui::SliderFloat("Skybox Exposure", &postProcess->skyboxExposure, -4.0f, 4.0f))
                            RecordUndoSnapshot();
                        if (ImGui::ColorEdit3("Skybox Tint", glm::value_ptr(postProcess->skyboxTint)))
                            RecordUndoSnapshot();
                        ImGui::EndDisabled();
                    }

                    if (ImGui::CollapsingHeader("Fog", ImGuiTreeNodeFlags_DefaultOpen)) {
                        if (EditorTheme::Checkbox("Enable Fog", &postProcess->fogEnabled))
                            RecordUndoSnapshot();
                        ImGui::BeginDisabled(!postProcess->fogEnabled);
                        if (ImGui::ColorEdit3("Fog Color", glm::value_ptr(postProcess->fogColor)))
                            RecordUndoSnapshot();
                        if (ImGui::DragFloat("Fog Start", &postProcess->fogStart, 0.1f, 0.0f, 10000.0f))
                            RecordUndoSnapshot();
                        if (ImGui::DragFloat("Fog End", &postProcess->fogEnd, 0.1f, 0.1f, 10000.0f)) {
                            postProcess->fogEnd = std::max(postProcess->fogEnd, postProcess->fogStart + 0.1f);
                            RecordUndoSnapshot();
                        }
                        ImGui::EndDisabled();
                    }

                    if (ImGui::CollapsingHeader("Color Grading", ImGuiTreeNodeFlags_DefaultOpen)) {
                        if (EditorTheme::Checkbox("Enable Color Grading", &postProcess->colorGradingEnabled))
                            RecordUndoSnapshot();
                        ImGui::BeginDisabled(!postProcess->colorGradingEnabled);
                        if (ImGui::SliderFloat("Exposure", &postProcess->exposure, -4.0f, 4.0f))
                            RecordUndoSnapshot();
                        if (ImGui::SliderFloat("Contrast", &postProcess->contrast, 0.0f, 3.0f))
                            RecordUndoSnapshot();
                        if (ImGui::SliderFloat("Saturation", &postProcess->saturation, 0.0f, 3.0f))
                            RecordUndoSnapshot();
                        if (ImGui::ColorEdit3("Color Filter", glm::value_ptr(postProcess->colorFilter)))
                            RecordUndoSnapshot();
                        ImGui::EndDisabled();
                    }

                    if (ImGui::CollapsingHeader("Vignette", ImGuiTreeNodeFlags_DefaultOpen)) {
                        if (EditorTheme::Checkbox("Enable Vignette", &postProcess->vignetteEnabled))
                            RecordUndoSnapshot();
                        ImGui::BeginDisabled(!postProcess->vignetteEnabled);
                        if (ImGui::ColorEdit3("Vignette Color", glm::value_ptr(postProcess->vignetteColor)))
                            RecordUndoSnapshot();
                        if (ImGui::SliderFloat("Intensity", &postProcess->vignetteIntensity, 0.0f, 1.0f))
                            RecordUndoSnapshot();
                        if (ImGui::SliderFloat("Smoothness", &postProcess->vignetteSmoothness, 0.01f, 2.0f))
                            RecordUndoSnapshot();
                        ImGui::EndDisabled();
                    }
                }
                EndInspectableComponent(*postProcess);
                if (removeComponent) {
                    selectedEntity->RemoveComponent<PostProcessVolumeComponent>();
                    ImGui::End();
                    return;
                }
            }

            auto* skinnedComp = selectedEntity->GetComponent<SkinnedMeshRendererComponent>();
            if (skinnedComp) {
                removeComponent = false;
                const bool open =
                    BeginInspectableComponent("Skinned Mesh Renderer", *skinnedComp, removeComponent, this);
                if (!removeComponent && open) {
                    ImGui::ColorEdit4("Color", glm::value_ptr(skinnedComp->color));
                    if (DrawAssetReferenceField(this, "Model", AssetKind::Model,
                                                skinnedComp->modelPath, "None (Model)")) {
                        RecordUndoSnapshot();
                        if (!skinnedComp->modelPath.empty()) {
                            skinnedComp->SetModelFile(skinnedComp->modelPath);
                            ApplySkeletalModelFitScale(*selectedEntity, skinnedComp->modelPath);
                        } else {
                            skinnedComp->mesh.reset();
                        }
                    }
                    if (!skinnedComp->modelPath.empty()) {
                        const uint32_t indexCount =
                            skinnedComp->mesh ? skinnedComp->mesh->GetIndexCount() : 0;
                        ImGui::Text("GPU indices: %u", indexCount);
                        if (skinnedComp->meshPartIndex >= 0)
                            ImGui::Text("Mesh part index: %d", skinnedComp->meshPartIndex);
                        if (auto skel = AssetManager::Get().GetSkeletalModel(skinnedComp->modelPath)) {
                            if (skinnedComp->meshPartIndex >= 0 &&
                                static_cast<size_t>(skinnedComp->meshPartIndex) <
                                    skel->meshParts.size()) {
                                ImGui::Text("Part: %s",
                                            skel->meshParts[static_cast<size_t>(
                                                skinnedComp->meshPartIndex)]
                                                .name.c_str());
                            }
                            ImGui::Text("Bones: %zu  Clips: %zu  Materials: %zu", skel->bones.size(),
                                        skel->animationNames.size(), skel->materials.size());
                            const glm::vec3 ext = skel->boundsMax - skel->boundsMin;
                            ImGui::Text("Bounds size: %.2f x %.2f x %.2f", ext.x, ext.y, ext.z);
                            ImGui::Text("Fit scale: %.4f (baked in mesh, Transform=1)", skel->vertexBakeScale);
                        }
                        if (indexCount == 0)
                            ImGui::TextColored(EditorTheme::Error, "Skinned mesh has 0 indices");
                        if (ImGui::SmallButton("Reload Model")) {
                            RecordUndoSnapshot();
                            skinnedComp->SetModelFile(skinnedComp->modelPath);
                            ApplySkeletalModelFitScale(*selectedEntity, skinnedComp->modelPath);
                            if (auto* anim = selectedEntity->GetComponent<AnimatorComponent>()) {
                                if (anim->modelPath.empty())
                                    anim->modelPath = skinnedComp->modelPath;
                                anim->ReloadAssets();
                            }
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Fit Scale")) {
                            RecordUndoSnapshot();
                            ApplySkeletalModelFitScale(*selectedEntity, skinnedComp->modelPath);
                        }
                        if (auto* tr = selectedEntity->GetComponent<TransformComponent>()) {
                            const float s = tr->scale.x;
                            if (std::abs(s - 1.0f) > 0.02f)
                                ImGui::TextColored(EditorTheme::PsAccent,
                                                   "Transform scale should be 1 (baked in mesh)");
                        }
                    }
                    DrawMaterialSlot(*skinnedComp);

                    ImGui::SeparatorText("PS1 Export");
                    const char* ps1Modes[] = { "Off", "Static Pose", "Rigid Bones" };
                    int ps1Mode = static_cast<int>(skinnedComp->ps1ExportMode);
                    if (ps1Mode > 2) ps1Mode = 2; // clamp legacy
                    if (ImGui::Combo("PS1 Mode", &ps1Mode, ps1Modes, IM_ARRAYSIZE(ps1Modes))) {
                        RecordUndoSnapshot();
                        skinnedComp->ps1ExportMode =
                            static_cast<SkinnedMeshRendererComponent::Ps1ExportMode>(std::clamp(ps1Mode, 0, 2));
                        m_SceneDirty = true;
                    }
                    if (skinnedComp->ps1ExportMode == SkinnedMeshRendererComponent::Ps1ExportMode::Off) {
                        ImGui::TextColored(EditorTheme::TextMuted,
                                           "Skipped in PS1 builds. Use this for high-poly editor-only Mixamo models.");
                    } else {
                        ImGui::TextColored(EditorTheme::PsAccent,
                                           "PS1 uses rigid bone parts by default; no runtime skinning.");
                        bool ps1AnimChanged = false;
                        ps1AnimChanged |= ImGui::DragInt("PS1 Anim FPS", &skinnedComp->ps1VertexAnimFps,
                                                         1.0f, 1, 30);
                        ps1AnimChanged |= ImGui::DragInt("PS1 Max Frames", &skinnedComp->ps1VertexAnimMaxFrames,
                                                         1.0f, 1, 120);
                        if (ps1AnimChanged) {
                            skinnedComp->ps1VertexAnimFps =
                                std::clamp(skinnedComp->ps1VertexAnimFps, 1, 30);
                            skinnedComp->ps1VertexAnimMaxFrames =
                                std::clamp(skinnedComp->ps1VertexAnimMaxFrames, 1, 120);
                            RecordUndoSnapshot();
                            m_SceneDirty = true;
                        }
                    }

                }
                EndInspectableComponent(*skinnedComp);
                if (removeComponent) {
                    selectedEntity->RemoveComponent<SkinnedMeshRendererComponent>();
                    ImGui::End();
                    return;
                }
            }

            auto* audioSource = selectedEntity->GetComponent<AudioSourceComponent>();
            if (audioSource) {
                removeComponent = false;
                const bool open = BeginInspectableComponent("Audio Source", *audioSource,
                                                            removeComponent, this);
                if (!removeComponent && open) {
                    if (DrawAssetReferenceField(this, "Audio Clip", AssetKind::Audio,
                                                audioSource->clipPath, "None (Audio Clip)"))
                        RecordUndoSnapshot();
                    if (EditorTheme::Checkbox("Play On Awake", &audioSource->playOnAwake))
                        RecordUndoSnapshot();
                    if (EditorTheme::Checkbox("Loop", &audioSource->loop))
                        RecordUndoSnapshot();
                    if (EditorTheme::Checkbox("Mute", &audioSource->mute))
                        RecordUndoSnapshot();
                    if (ImGui::SliderFloat("Volume", &audioSource->volume, 0.0f, 1.0f))
                        RecordUndoSnapshot();
                }
                EndInspectableComponent(*audioSource);
                if (removeComponent) {
                    selectedEntity->RemoveComponent<AudioSourceComponent>();
                    ImGui::End();
                    return;
                }
            }

            auto* animatorComp = selectedEntity->GetComponent<AnimatorComponent>();
            if (animatorComp) {
                removeComponent = false;
                const bool open = BeginInspectableComponent("Animator", *animatorComp, removeComponent, this);
                if (!removeComponent && open) {
                    if (ImGui::Button("Reload Assets", ImVec2(-1, 0)))
                        animatorComp->ReloadAssets();

                    if (EditorTheme::Checkbox("Debug: Bind Pose Only", &animatorComp->debugBindPoseOnly))
                        RecordUndoSnapshot();

                    if (ImGui::DragFloat("Playback Speed", &animatorComp->speed, 0.01f, 0.0f, 10.0f))
                        RecordUndoSnapshot();
                    if (ImGui::DragFloat("Animation FPS", &animatorComp->animationFps, 0.5f, 1.0f,
                                         120.0f))
                        RecordUndoSnapshot();

                    if (DrawAssetReferenceField(this, "Controller", AssetKind::AnimatorController,
                                                animatorComp->controllerPath, "None (Controller)")) {
                        RecordUndoSnapshot();
                        animatorComp->ReloadAssets();
                        if (!animatorComp->controllerPath.empty())
                            OpenAnimatorControllerWindow(animatorComp->controllerPath);
                    }

                    std::string modelForController = animatorComp->modelPath;
                    if (modelForController.empty()) {
                        if (auto* skinned = selectedEntity->GetComponent<SkinnedMeshRendererComponent>())
                            modelForController = skinned->modelPath;
                    }
                    if (!modelForController.empty() &&
                        EditorTheme::AeroButton("Create from Model Clips", ImVec2(-1.0f, EditorTheme::ButtonHeight),
                                                 AeroButtonKind::Secondary)) {
                        CreateAndAssignControllerFromModel(this, *animatorComp, modelForController,
                                                         skinnedComp);
                    }

                    if (!animatorComp->controllerPath.empty() &&
                        EditorTheme::AeroButton("Open Controller Window",
                                                ImVec2(-1.0f, EditorTheme::ButtonHeight),
                                                AeroButtonKind::Primary)) {
                        OpenAnimatorControllerWindow(animatorComp->controllerPath);
                    }

                    ImGui::Text("State: %s", animatorComp->currentState.empty()
                                                   ? "<none>"
                                                   : animatorComp->currentState.c_str());
                    if (animatorComp->inTransition)
                        ImGui::Text("Transition -> %s (%.2f)", animatorComp->nextState.c_str(),
                                    animatorComp->transitionTime);

                    if (animatorComp->controller && !animatorComp->controller->parameters.empty()) {
                        ImGui::Separator();
                        ImGui::TextColored(EditorTheme::TextSecondary,
                                           "Controller Parameters (for transitions)");
                        for (const auto& p : animatorComp->controller->parameters) {
                            if (p.type == AnimatorParamType::Float) {
                                float v = animatorComp->parameters.floats.count(p.name)
                                    ? animatorComp->parameters.floats[p.name]
                                    : p.defaultFloat;
                                if (ImGui::DragFloat(p.name.c_str(), &v, 0.01f, -100.0f, 100.0f))
                                    animatorComp->parameters.floats[p.name] = v;
                            } else if (p.type == AnimatorParamType::Bool) {
                                bool v = animatorComp->parameters.bools.count(p.name)
                                    ? animatorComp->parameters.bools[p.name]
                                    : p.defaultBool;
                                if (EditorTheme::Checkbox(p.name.c_str(), &v))
                                    animatorComp->parameters.bools[p.name] = v;
                            } else if (p.type == AnimatorParamType::Int) {
                                int v = animatorComp->parameters.ints.count(p.name)
                                    ? animatorComp->parameters.ints[p.name]
                                    : p.defaultInt;
                                if (ImGui::DragInt(p.name.c_str(), &v))
                                    animatorComp->parameters.ints[p.name] = v;
                            }
                        }

                        if (animatorComp->model && !animatorComp->model->animationNames.empty()) {
                            ImGui::Separator();
                            ImGui::Text("Clips on model");
                            for (const std::string& clip : animatorComp->model->animationNames)
                                ImGui::BulletText("%s", clip.c_str());
                        }
                    }

                    DrawAnimatorRuntimeDiagnostics(*animatorComp, m_IsPlaying);
                }
                EndInspectableComponent(*animatorComp);
                if (removeComponent) {
                    selectedEntity->RemoveComponent<AnimatorComponent>();
                    ImGui::End();
                    return;
                }
            }

            auto* meshRendererComp = selectedEntity->GetComponent<MeshRendererComponent>();
            if (meshRendererComp) {
                removeComponent = false;
                const bool open = BeginInspectableComponent("Mesh Renderer", *meshRendererComp, removeComponent, this);
                if (!removeComponent && open) {
                    if (meshRendererComp->meshPrimitive == "File") {
                        if (DrawAssetReferenceField(this, "Mesh", AssetKind::Model,
                                                    meshRendererComp->meshPath, "None (Mesh)")) {
                            RecordUndoSnapshot();
                            if (!meshRendererComp->meshPath.empty())
                                meshRendererComp->SetMeshFile(meshRendererComp->meshPath);
                        }
                        if (ImGui::Button("Use Primitive Mesh", ImVec2(-1, 0)))
                            meshRendererComp->SetPrimitive("Cube", 1.0f);
                    } else {
                        const char* meshTypes[] = { "Cube", "Sphere", "Plane", "Terrain" };
                        int currentMeshType = 0;
                        if (meshRendererComp->meshPrimitive == "Sphere") currentMeshType = 1;
                        else if (meshRendererComp->meshPrimitive == "Plane") currentMeshType = 2;
                        else if (meshRendererComp->meshPrimitive == "Terrain") currentMeshType = 3;
                        if (ImGui::Combo("Mesh Type", &currentMeshType, meshTypes, IM_ARRAYSIZE(meshTypes))) {
                            meshRendererComp->SetPrimitive(meshTypes[currentMeshType], meshRendererComp->meshSize);
                            if (meshRendererComp->meshPrimitive == "Terrain") {
                                auto* terrain = selectedEntity->GetComponent<TerrainComponent>();
                                if (!terrain)
                                    terrain = &selectedEntity->AddComponent<TerrainComponent>();
                                terrain->size = meshRendererComp->meshSize;
                                terrain->RebuildMesh(*meshRendererComp);
                            }
                        }
                        if (ImGui::DragFloat("Mesh Size", &meshRendererComp->meshSize, 0.1f, 0.01f, 100.0f)) {
                            meshRendererComp->RebuildMesh();
                            if (auto* terrain = selectedEntity->GetComponent<TerrainComponent>();
                                terrain && meshRendererComp->meshPrimitive == "Terrain") {
                                terrain->size = meshRendererComp->meshSize;
                                terrain->RebuildMesh(*meshRendererComp);
                            }
                        }
                    }

                    DrawMaterialSlot(*meshRendererComp);
                    EditorTheme::Checkbox("Editor Only", &meshRendererComp->editorOnly);
                    EditorTheme::Checkbox("PS1 View Model", &meshRendererComp->viewModel);
                    if (meshRendererComp->viewModel) {
                        ImGui::TextWrapped("Draws this mesh in the PS1 first-person foreground pass.");
                    }

                }
                EndInspectableComponent(*meshRendererComp);
                if (removeComponent) {
                    selectedEntity->RemoveComponent<MeshRendererComponent>();
                    ImGui::End();
                    return;
                }
            }

            auto* terrainComp = selectedEntity->GetComponent<TerrainComponent>();

            if (terrainComp) {
                removeComponent = false;
                const bool open = BeginInspectableComponent("Terrain", *terrainComp, removeComponent, this);
                if (!removeComponent && open) {
                    bool changed = false;
                    ImGui::TextDisabled("Terrain Data");
                    changed |= ImGui::DragFloat("Size", &terrainComp->size, 0.25f, 0.1f, 512.0f);
                    const int oldSubdivisions = terrainComp->subdivisions;
                    changed |= ImGui::SliderInt("Resolution", &terrainComp->subdivisions, 1, 128);

                    if (changed)
                        terrainComp->subdivisions = std::clamp(terrainComp->subdivisions, 1, 128);

                    const bool flatten = ImGui::Button("Flatten Terrain", ImVec2(-1, 0));
                    if (flatten || oldSubdivisions != terrainComp->subdivisions)
                        terrainComp->ResetProcedural();

                    ImGui::SeparatorText("Terrain Tools");
                    bool brushSettingsChanged = false;
                    const char* brushModes[] = { "Raise/Lower", "Lower", "Smooth Height", "Paint Texture" };
                    int brushMode = static_cast<int>(terrainComp->brushMode);
                    const float tabWidth = std::max(78.0f, (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 3.0f) / 4.0f);
                    for (int i = 0; i < IM_ARRAYSIZE(brushModes); ++i) {
                        if (i > 0)
                            ImGui::SameLine();
                        const bool selected = brushMode == i;
                        if (selected)
                            ImGui::PushStyleColor(ImGuiCol_Button, EditorTheme::Accent);
                        if (ImGui::Button(brushModes[i], ImVec2(tabWidth, 0.0f))) {
                            brushMode = i;
                            terrainComp->brushMode = static_cast<TerrainComponent::BrushMode>(std::clamp(brushMode, 0, 3));
                            terrainComp->brushEnabled = true;
                            brushSettingsChanged = true;
                        }
                        if (selected)
                            ImGui::PopStyleColor();
                    }

                    ImGui::Spacing();
                    brushSettingsChanged |= EditorTheme::Checkbox("Enable Scene Brush", &terrainComp->brushEnabled);
                    ImGui::BeginDisabled(!terrainComp->brushEnabled);
                    brushSettingsChanged |= ImGui::SliderFloat("Brush Size", &terrainComp->brushRadius, 0.05f, 64.0f, "%.2f");
                    brushSettingsChanged |= ImGui::SliderFloat("Opacity", &terrainComp->brushStrength, 0.0f, 10.0f, "%.2f");
                    if (terrainComp->brushMode == TerrainComponent::BrushMode::Paint)
                        brushSettingsChanged |= ImGui::ColorEdit4("Terrain Layer Color", glm::value_ptr(terrainComp->brushColor));
                    ImGui::EndDisabled();

                    ImGui::TextDisabled("Scene View: left-drag to paint. Shift lowers in Raise/Lower mode.");
                    if (brushSettingsChanged)
                        m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());

                    ImGui::Columns(2, nullptr, false);
                    if (ImGui::Button("Clear Paint", ImVec2(-1, 0))) {
                        terrainComp->ClearPaint();
                        changed = true;
                    }
                    ImGui::NextColumn();
                    if (ImGui::Button("Fill Layer", ImVec2(-1, 0))) {
                        terrainComp->EnsureData();
                        std::fill(terrainComp->paintColors.begin(), terrainComp->paintColors.end(), terrainComp->brushColor);
                        changed = true;
                    }
                    ImGui::Columns(1);

                    if (changed || flatten) {
                        auto* mr = selectedEntity->GetComponent<MeshRendererComponent>();
                        if (!mr)
                            mr = &selectedEntity->AddComponent<MeshRendererComponent>();
                        terrainComp->RebuildMesh(*mr);
                        if (!mr->texture)
                            mr->texture = std::make_shared<Texture>(Texture::CreateCheckerboard(256, 32));
                        m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
                    }

                    ImGui::TextDisabled("%d x %d vertices, %d tris",
                        terrainComp->subdivisions + 1,
                        terrainComp->subdivisions + 1,
                        terrainComp->subdivisions * terrainComp->subdivisions * 2);
                }
                EndInspectableComponent(*terrainComp);
                if (removeComponent) {
                    selectedEntity->RemoveComponent<TerrainComponent>();
                    ImGui::End();
                    return;
                }
            }

            auto* collider = selectedEntity->GetComponent<ColliderComponent>();
            if (collider) {
                removeComponent = false;
                const bool open = BeginInspectableComponent("Collider", *collider, removeComponent, this);
                if (!removeComponent && open) {
                    const char* shapeNames[] = { "Box", "Sphere", "Capsule", "Mesh" };
                    int shapeIdx = static_cast<int>(collider->shape);
                    if (shapeIdx > 3) shapeIdx = 0;
                    if (ImGui::Combo("Shape", &shapeIdx, shapeNames, IM_ARRAYSIZE(shapeNames)))
                        collider->shape = static_cast<ColliderShape>(shapeIdx);

                    if (ImGui::Button("Fit to Mesh", ImVec2(-1, 0))) {
                        if (auto* mr = selectedEntity->GetComponent<MeshRendererComponent>();
                            mr && mr->mesh) {
                            ColliderUtils::FitColliderToMesh(*collider, *mr->mesh);
                            if (m_IsPlaying)
                                m_Engine->GetPhysicsWorld().RefreshBodies(m_Engine->GetScene());
                        }
                    }

                    ImGui::DragFloat3("Center", glm::value_ptr(collider->center), 0.01f);
                    if (collider->shape == ColliderShape::Box || collider->shape == ColliderShape::Mesh) {
                        if (collider->shape == ColliderShape::Mesh) {
                            if (EditorTheme::Checkbox("Convex", &collider->convex)) {
                                m_SceneDirty = true;
                                if (m_IsPlaying)
                                    m_Engine->GetPhysicsWorld().RefreshBodies(m_Engine->GetScene());
                            }
                            if (collider->convex) {
                                ImGui::TextDisabled("Uses a convex hull; AABB shown for edit");
                            } else {
                                ImGui::TextDisabled("Uses source triangles; static physics only");
                                if (auto* rb = selectedEntity->GetComponent<RigidbodyComponent>();
                                    rb && rb->enabled && rb->bodyType != RigidbodyType::Static) {
                                    ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
                                                       "Non-convex mesh colliders are forced static");
                                }
                            }
                        }
                        ImGui::DragFloat3("Half Extents", glm::value_ptr(collider->halfExtents), 0.01f, 0.01f, 100.0f);
                    } else {
                        ImGui::DragFloat("Radius", &collider->radius, 0.01f, 0.01f, 50.0f);
                        if (collider->shape == ColliderShape::Capsule)
                            ImGui::DragFloat("Height", &collider->capsuleHeight, 0.01f, 0.01f, 50.0f);
                    }
                    EditorTheme::Checkbox("Is Trigger", &collider->isTrigger);
                    if (EditorTheme::Checkbox("Camera Shot Trigger", &collider->cameraShotTrigger)) {
                        if (collider->cameraShotTrigger)
                            collider->isTrigger = true;
                        ResetShotCameraState();
                        m_SceneDirty = true;
                    }
                }
                EndInspectableComponent(*collider);
                if (removeComponent) {
                    selectedEntity->RemoveComponent<ColliderComponent>();
                    if (m_IsPlaying)
                        m_Engine->GetPhysicsWorld().RefreshBodies(m_Engine->GetScene());
                    ImGui::End();
                    return;
                }
            }

            auto* rigidbody = selectedEntity->GetComponent<RigidbodyComponent>();
            if (rigidbody) {
                removeComponent = false;
                const bool open = BeginInspectableComponent("Rigidbody", *rigidbody, removeComponent, this);
                if (!removeComponent && open) {
                    const char* bodyNames[] = { "Static", "Kinematic", "Dynamic" };
                    int bodyIdx = static_cast<int>(rigidbody->bodyType);
                    if (ImGui::Combo("Body Type", &bodyIdx, bodyNames, IM_ARRAYSIZE(bodyNames)))
                        rigidbody->bodyType = static_cast<RigidbodyType>(bodyIdx);
                    if (rigidbody->bodyType == RigidbodyType::Dynamic) {
                        ImGui::DragFloat("Mass", &rigidbody->mass, 0.1f, 0.001f, 10000.0f);
                        EditorTheme::Checkbox("Use Gravity", &rigidbody->useGravity);
                        ImGui::DragFloat("Linear Drag", &rigidbody->linearDrag, 0.01f, 0.0f, 10.0f);
                        ImGui::DragFloat("Bounciness", &rigidbody->bounciness, 0.01f, 0.0f, 1.0f);
                        EditorTheme::Checkbox("Freeze Rotation", &rigidbody->freezeRotation);
                    } else if (rigidbody->bodyType == RigidbodyType::Kinematic) {
                        if (rigidbody->characterController)
                            ImGui::TextDisabled("Character controller (Jolt CharacterVirtual)");
                        else
                            ImGui::TextDisabled("Moved by transform / scripts");
                    }
                }
                EndInspectableComponent(*rigidbody);
                if (removeComponent) {
                    selectedEntity->RemoveComponent<RigidbodyComponent>();
                    if (m_IsPlaying)
                        m_Engine->GetPhysicsWorld().RefreshBodies(m_Engine->GetScene());
                    ImGui::End();
                    return;
                }
            }

            auto* rectTransform = selectedEntity->GetComponent<RectTransformComponent>();
            if (rectTransform) {
                removeComponent = false;
                const bool open = BeginInspectableComponent("Rect Transform", *rectTransform, removeComponent, this);
                if (!removeComponent && open) {
                    ImGui::DragFloat2("Anchor Min", glm::value_ptr(rectTransform->anchorMin), 0.01f, 0.0f, 1.0f);
                    ImGui::DragFloat2("Anchor Max", glm::value_ptr(rectTransform->anchorMax), 0.01f, 0.0f, 1.0f);
                    ImGui::DragFloat2("Pivot", glm::value_ptr(rectTransform->pivot), 0.01f, 0.0f, 1.0f);
                    ImGui::DragFloat2("Anchored Position", glm::value_ptr(rectTransform->anchoredPosition), 1.0f);
                    ImGui::DragFloat2("Size Delta", glm::value_ptr(rectTransform->sizeDelta), 1.0f);
                }
                EndInspectableComponent(*rectTransform);
                if (removeComponent) {
                    selectedEntity->RemoveComponent<RectTransformComponent>();
                    ImGui::End();
                    return;
                }
            }

            auto* canvas = selectedEntity->GetComponent<CanvasComponent>();
            if (canvas) {
                removeComponent = false;
                const bool open = BeginInspectableComponent("Canvas", *canvas, removeComponent, this);
                if (!removeComponent && open) {
                    const char* renderModes[] = { "Screen Space - Overlay", "Screen Space - Camera", "World Space" };
                    int renderMode = static_cast<int>(canvas->renderMode);
                    if (ImGui::Combo("Render Mode", &renderMode, renderModes, IM_ARRAYSIZE(renderModes)))
                        canvas->renderMode = static_cast<UICanvasRenderMode>(renderMode);

                    const char* scaleModes[] = { "Constant Pixel Size", "Scale With Screen Size" };
                    int scaleMode = static_cast<int>(canvas->scaleMode);
                    if (ImGui::Combo("UI Scale Mode", &scaleMode, scaleModes, IM_ARRAYSIZE(scaleModes)))
                        canvas->scaleMode = static_cast<UICanvasScaleMode>(scaleMode);

                    ImGui::DragInt("Sort Order", &canvas->sortOrder, 1.0f, -100, 100);
                    ImGui::DragFloat2("Reference Resolution", glm::value_ptr(canvas->referenceResolution), 1.0f, 1.0f, 8192.0f);
                    ImGui::SliderFloat("Match Width Or Height", &canvas->matchWidthOrHeight, 0.0f, 1.0f);

                    if (canvas->renderMode == UICanvasRenderMode::ScreenSpaceCamera ||
                        canvas->renderMode == UICanvasRenderMode::WorldSpace) {
                        int camId = static_cast<int>(canvas->eventCameraEntityId);
                        if (ImGui::InputInt("Event Camera Entity ID", &camId)) {
                            canvas->eventCameraEntityId = static_cast<uint32_t>(std::max(camId, 0));
                        }
                        ImGui::TextDisabled("0 = primary camera");
                    }
                    if (canvas->renderMode == UICanvasRenderMode::ScreenSpaceCamera)
                        ImGui::DragFloat("Plane Distance", &canvas->planeDistance, 0.5f, 0.1f, 500.0f);
                }
                EndInspectableComponent(*canvas);
                if (removeComponent) {
                    selectedEntity->RemoveComponent<CanvasComponent>();
                    ImGui::End();
                    return;
                }
            }

            auto* uiImage = selectedEntity->GetComponent<UIImageComponent>();
            if (uiImage) {
                removeComponent = false;
                const bool open = BeginInspectableComponent("Image", *uiImage, removeComponent, this);
                if (!removeComponent && open) {
                    ImGui::ColorEdit4("Color", glm::value_ptr(uiImage->color));
                    if (EditorTheme::Checkbox("Preserve Aspect", &uiImage->preserveAspect))
                        RecordUndoSnapshot();
                    DrawUITextureSlot(*uiImage);
                }
                EndInspectableComponent(*uiImage);
                if (removeComponent) {
                    selectedEntity->RemoveComponent<UIImageComponent>();
                    ImGui::End();
                    return;
                }
            }

            auto* uiText = selectedEntity->GetComponent<UITextComponent>();
            if (uiText) {
                removeComponent = false;
                const bool open = BeginInspectableComponent("Text", *uiText, removeComponent, this);
                if (!removeComponent && open) {
                    char textBuffer[1024];
                    memset(textBuffer, 0, sizeof(textBuffer));
                    strncpy(textBuffer, uiText->text.c_str(), sizeof(textBuffer) - 1);
                    if (ImGui::InputTextMultiline("##TextContent", textBuffer, sizeof(textBuffer), ImVec2(-1, 60)))
                        uiText->text = textBuffer;
                    ImGui::ColorEdit4("Color", glm::value_ptr(uiText->color));
                    ImGui::DragFloat("Font Size", &uiText->fontSize, 0.5f, 8.0f, 96.0f);
                    const char* alignments[] = { "Left", "Center", "Right" };
                    int align = static_cast<int>(uiText->alignment);
                    if (ImGui::Combo("Alignment", &align, alignments, IM_ARRAYSIZE(alignments)))
                        uiText->alignment = static_cast<UITextAlignment>(align);
                }
                EndInspectableComponent(*uiText);
                if (removeComponent) {
                    selectedEntity->RemoveComponent<UITextComponent>();
                    ImGui::End();
                    return;
                }
            }

            auto* uiButtonGroup = selectedEntity->GetComponent<UIButtonGroupComponent>();
            if (uiButtonGroup) {
                removeComponent = false;
                const bool open = BeginInspectableComponent("Button Group", *uiButtonGroup, removeComponent, this);
                if (!removeComponent && open) {
                    ImGui::DragInt("Selected Index", &uiButtonGroup->selectedIndex, 1.0f, 0, 999);
                    EditorTheme::Checkbox("Wrap Navigation", &uiButtonGroup->wrapNavigation);
                    EditorTheme::Checkbox("Keyboard Navigation", &uiButtonGroup->keyboardNavigation);
                    EditorTheme::Checkbox("Gamepad Navigation", &uiButtonGroup->gamepadNavigation);
                    ImGui::SeparatorText("Confirm");
                    EditorTheme::Checkbox("Keyboard Confirm", &uiButtonGroup->keyboardConfirm);
                    const char* keyNames[] = { "Enter", "Space", "Z", "X", "E", "F" };
                    int key = static_cast<int>(uiButtonGroup->confirmKey);
                    if (ImGui::Combo("PC Key", &key, keyNames, IM_ARRAYSIZE(keyNames)))
                        uiButtonGroup->confirmKey = static_cast<UIConfirmKey>(key);
                    EditorTheme::Checkbox("Gamepad Confirm", &uiButtonGroup->gamepadConfirm);
                    const char* buttonNames[] = { "South / Cross / A", "East / Circle / B",
                                                  "West / Square / X", "North / Triangle / Y", "Start" };
                    int button = static_cast<int>(uiButtonGroup->confirmButton);
                    if (ImGui::Combo("Controller Button", &button, buttonNames, IM_ARRAYSIZE(buttonNames)))
                        uiButtonGroup->confirmButton = static_cast<UIConfirmGamepadButton>(button);
                    ImGui::SeparatorText("Cursor");
                    DrawUIButtonGroupCursorSlot(*uiButtonGroup);
                    ImGui::DragFloat2("Cursor Offset", glm::value_ptr(uiButtonGroup->cursorOffset), 1.0f);
                    ImGui::DragFloat2("Cursor Size", glm::value_ptr(uiButtonGroup->cursorSize), 1.0f, 1.0f, 512.0f);
                    EditorTheme::Checkbox("Pressed This Frame", &uiButtonGroup->pressedThisFrame);
                    ImGui::TextDisabled("Pressed This Frame is runtime state; useful for quick debugging.");
                }
                EndInspectableComponent(*uiButtonGroup);
                if (removeComponent) {
                    selectedEntity->RemoveComponent<UIButtonGroupComponent>();
                    ImGui::End();
                    return;
                }
            }

            auto* uiButton = selectedEntity->GetComponent<UIButtonComponent>();
            if (uiButton) {
                removeComponent = false;
                const bool open = BeginInspectableComponent("Button", *uiButton, removeComponent, this);
                if (!removeComponent && open) {
                    bool interactable = uiButton->interactable;
                    if (EditorTheme::Checkbox("Interactable", &interactable)) {
                        RecordUndoSnapshot();
                        uiButton->interactable = interactable;
                        m_SceneDirty = true;
                    }
                    DrawUIButtonBackgroundSlot(*uiButton);
                    if (EditorTheme::Checkbox("Preserve Aspect", &uiButton->preserveAspect))
                        RecordUndoSnapshot();
                    ImGui::ColorEdit4("Normal", glm::value_ptr(uiButton->normalColor));
                    ImGui::ColorEdit4("Selected", glm::value_ptr(uiButton->selectedColor));
                    ImGui::ColorEdit4("Pressed", glm::value_ptr(uiButton->pressedColor));
                    bool hasTextChild = false;
                    for (uint32_t childId : selectedEntity->GetChildIDs()) {
                        Entity* child = m_Engine->GetScene().FindEntity(childId);
                        if (child && child->HasComponent<UITextComponent>()) {
                            hasTextChild = true;
                            break;
                        }
                    }
                    ImGui::TextDisabled("Button text is a child Text object.");
                    if (!hasTextChild) {
                        if (EditorTheme::AeroButton("Create Child Text",
                                                     ImVec2(-1.0f, EditorTheme::ButtonHeight),
                                                     AeroButtonKind::Secondary)) {
                            RecordUndoSnapshot();
                            Scene& scene = m_Engine->GetScene();
                            Entity* textEntity = scene.CreateEntity("Text");
                            scene.SetParent(textEntity, selectedEntity);
                            auto& textRect = textEntity->AddComponent<RectTransformComponent>();
                            textRect.anchorMin = { 0.0f, 0.0f };
                            textRect.anchorMax = { 1.0f, 1.0f };
                            textRect.pivot = { 0.5f, 0.5f };
                            textRect.anchoredPosition = { 0.0f, 0.0f };
                            textRect.sizeDelta = { 0.0f, 0.0f };
                            auto& text = textEntity->AddComponent<UITextComponent>();
                            text.text = uiButton->label.empty() ? "Button" : uiButton->label;
                            text.fontSize = uiButton->fontSize;
                            text.alignment = UITextAlignment::Center;
                            text.color = uiButton->textColor;
                            m_Engine->GetMipsRuntime().SyncEditSnapshot(scene);
                            m_SceneDirty = true;
                        }
                    }
                    if (!FindButtonGroupAncestor(m_Engine->GetScene(), selectedEntity))
                        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
                                           "Button is not under a Button Group.");

                    ImGui::Spacing();
                    ImGui::SeparatorText("On Click ()");
                    ImGui::TextDisabled("Persistent listeners run in Play mode.");

                    int removeListener = -1;
                    Scene& scene = m_Engine->GetScene();
                    for (size_t listenerIndex = 0; listenerIndex < uiButton->onClick.size(); ++listenerIndex) {
                        UIButtonClickEvent& listener = uiButton->onClick[listenerIndex];
                        ImGui::PushID(static_cast<int>(listenerIndex));
                        ImGui::BeginGroup();

                        bool listenerEnabled = listener.enabled;
                        if (EditorTheme::Checkbox("##Enabled", &listenerEnabled)) {
                            RecordUndoSnapshot();
                            listener.enabled = listenerEnabled;
                            m_SceneDirty = true;
                        }
                        ImGui::SameLine();
                        ImGui::TextDisabled("Runtime Only");
                        ImGui::SameLine();
                        const float removeWidth = 24.0f;
                        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
                            ImGui::GetWindowContentRegionMax().x - removeWidth));
                        if (ImGui::SmallButton("-") )
                            removeListener = static_cast<int>(listenerIndex);

                        Entity* target = listener.targetEntityId != 0
                            ? scene.FindEntity(listener.targetEntityId) : nullptr;
                        std::string targetLabel = "None (Object)";
                        if (target) {
                            if (const auto* tag = target->GetComponent<TagComponent>())
                                targetLabel = tag->tag;
                            else
                                targetLabel = "Entity #" + std::to_string(target->GetID());
                        }
                        ImGui::SetNextItemWidth(-1.0f);
                        ImGui::Button(targetLabel.c_str(), ImVec2(-1.0f, 0.0f));
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload =
                                    ImGui::AcceptDragDropPayload(DragDrop::kEntityId)) {
                                if (payload->DataSize >= static_cast<int>(sizeof(uint32_t))) {
                                    const uint32_t droppedId =
                                        *static_cast<const uint32_t*>(payload->Data);
                                    if (scene.FindEntity(droppedId)) {
                                        RecordUndoSnapshot();
                                        listener.targetEntityId = droppedId;
                                        listener.scriptPath.clear();
                                        listener.methodName.clear();
                                        m_SceneDirty = true;
                                    }
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }
                        if (target && ImGui::BeginPopupContextItem("ObjectContext")) {
                            if (ImGui::MenuItem("Clear")) {
                                RecordUndoSnapshot();
                                listener.targetEntityId = 0;
                                listener.scriptPath.clear();
                                listener.methodName.clear();
                                m_SceneDirty = true;
                            }
                            ImGui::EndPopup();
                        }

                        const std::string functionPreview = listener.methodName.empty()
                            ? "No Function" : listener.methodName;
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::BeginCombo("##Function", functionPreview.c_str())) {
                            if (ImGui::Selectable("No Function", listener.methodName.empty())) {
                                RecordUndoSnapshot();
                                listener.scriptPath.clear();
                                listener.methodName.clear();
                                m_SceneDirty = true;
                            }
                            if (target) {
                                for (MipsScriptComponent* script :
                                     target->GetComponents<MipsScriptComponent>()) {
                                    std::vector<std::string> errors;
                                    if (!Mips::MipsRuntime::EnsureScriptReady(*script, errors) ||
                                        !script->module)
                                        continue;
                                    bool openedClass = false;
                                    for (const Mips::CompiledMethod& method : script->module->methods) {
                                        if (!IsUIButtonCallableMethod(method))
                                            continue;
                                        if (!openedClass) {
                                            ImGui::SeparatorText(script->module->className.c_str());
                                            openedClass = true;
                                        }
                                        const bool selected = listener.scriptPath == script->scriptPath &&
                                                              listener.methodName == method.name;
                                        if (ImGui::Selectable(method.name.c_str(), selected)) {
                                            RecordUndoSnapshot();
                                            listener.scriptPath = script->scriptPath;
                                            listener.methodName = method.name;
                                            m_SceneDirty = true;
                                        }
                                    }
                                }
                            }
                            ImGui::EndCombo();
                        }
                        if (!target)
                            ImGui::TextDisabled("Drag a scene object here.");
                        else if (target->GetComponents<MipsScriptComponent>().empty())
                            ImGui::TextDisabled("The object has no Mips# Script.");

                        ImGui::EndGroup();
                        ImGui::Separator();
                        ImGui::PopID();
                    }
                    if (removeListener >= 0) {
                        RecordUndoSnapshot();
                        uiButton->onClick.erase(uiButton->onClick.begin() + removeListener);
                        m_SceneDirty = true;
                    }
                    if (EditorTheme::AeroButton("+ Add On Click Listener",
                                                ImVec2(-1.0f, EditorTheme::ButtonHeight),
                                                AeroButtonKind::Secondary)) {
                        RecordUndoSnapshot();
                        uiButton->onClick.emplace_back();
                        m_SceneDirty = true;
                    }
                }
                EndInspectableComponent(*uiButton);
                if (removeComponent) {
                    selectedEntity->RemoveComponent<UIButtonComponent>();
                    ImGui::End();
                    return;
                }
            }

            auto* uiSpectrum = selectedEntity->GetComponent<UIAudioSpectrumComponent>();
            if (uiSpectrum) {
                removeComponent = false;
                const bool open = BeginInspectableComponent("Audio Spectrum", *uiSpectrum, removeComponent, this);
                if (!removeComponent && open) {
                    int sourceId = static_cast<int>(uiSpectrum->sourceEntityId);
                    if (ImGui::InputInt("Audio Source Entity ID", &sourceId))
                        uiSpectrum->sourceEntityId = static_cast<uint32_t>(std::max(sourceId, 0));
                    ImGui::TextDisabled("0 = first playing Audio Source");
                    ImGui::ColorEdit4("Bar Color", glm::value_ptr(uiSpectrum->color));
                    ImGui::ColorEdit4("Background", glm::value_ptr(uiSpectrum->backgroundColor));
                    ImGui::SliderInt("Bars", &uiSpectrum->barCount, 4, 32);
                    ImGui::DragFloat("Gap", &uiSpectrum->barGap, 0.25f, 0.0f, 32.0f);
                    ImGui::DragFloat("Sensitivity", &uiSpectrum->sensitivity, 0.02f, 0.1f, 8.0f);
                    ImGui::SliderFloat("Smoothing", &uiSpectrum->smoothing, 0.0f, 0.98f);
                }
                EndInspectableComponent(*uiSpectrum);
                if (removeComponent) {
                    selectedEntity->RemoveComponent<UIAudioSpectrumComponent>();
                    ImGui::End();
                    return;
                }
            }

            const auto mipsScripts = selectedEntity->GetComponents<MipsScriptComponent>();
            for (auto* mipsScript : mipsScripts) {
                removeComponent = false;
                const bool open = BeginInspectableComponent("Mips# Script", *mipsScript, removeComponent, this);
                if (!removeComponent && open) {
                    if (DrawAssetReferenceField(this, "Script", AssetKind::Script,
                                                mipsScript->scriptPath, "None (Mips# Script)")) {
                        RecordUndoSnapshot();
                        mipsScript->module.reset();
                        mipsScript->fieldValues.clear();
                        mipsScript->fieldAssetPaths.clear();
                    }
                    DrawMipsScriptIdeActions(mipsScript->scriptPath);
                    ImGui::Spacing();

                    std::vector<std::string> compileErrors;
                    const bool compiled =
                        Mips::MipsRuntime::EnsureScriptReady(*mipsScript, compileErrors);
                    for (const auto& err : compileErrors)
                        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s", err.c_str());

                    if (compiled && mipsScript->module) {
                        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Class: %s",
                                           mipsScript->module->className.c_str());

                        if (m_IsPlaying)
                            ImGui::BeginDisabled();

                        for (size_t i = 0; i < mipsScript->module->fields.size(); ++i) {
                            const auto& field = mipsScript->module->fields[i];
                            const bool isFloat = field.typeName == "float";
                            const bool isInt = field.typeName == "int";
                            const bool isBool = field.typeName == "bool";
                            const bool isAudio = field.valueKind == Mips::FieldValueKind::AudioClip;
                            const bool isArray = field.valueKind == Mips::FieldValueKind::Array;
                            const bool isEntityReference =
                                field.valueKind == Mips::FieldValueKind::EntityReference;
                            if (isAudio) {
                                if (i >= mipsScript->fieldAssetPaths.size())
                                    mipsScript->fieldAssetPaths.resize(mipsScript->module->fields.size());
                                if (DrawAssetReferenceField(this, field.name.c_str(), AssetKind::Audio,
                                                            mipsScript->fieldAssetPaths[i],
                                                            "None (Audio Clip)"))
                                    RecordUndoSnapshot();
                                continue;
                            }
                            if (isArray) {
                                ImGui::Text("%s", field.name.c_str());
                                ImGui::SameLine();
                                ImGui::TextDisabled("Array (initialized by script)");
                                continue;
                            }
                            if (isEntityReference) {
                                Scene& scene = m_Engine->GetScene();
                                const uint32_t referencedId = i < mipsScript->fieldValues.size()
                                    ? static_cast<uint32_t>(std::max(0.0, std::round(mipsScript->fieldValues[i])))
                                    : 0u;
                                Entity* referenced = referencedId != 0
                                    ? scene.FindEntity(referencedId) : nullptr;
                                std::string label = "None (" + field.typeName + ")";
                                if (referenced) {
                                    if (const auto* tag = referenced->GetComponent<TagComponent>())
                                        label = tag->tag + " (" + field.typeName + ")";
                                    else
                                        label = "Entity #" + std::to_string(referencedId);
                                }

                                ImGui::PushID(static_cast<int>(i));
                                ImGui::AlignTextToFramePadding();
                                ImGui::TextUnformatted(field.name.c_str());
                                ImGui::SameLine();
                                ImGui::SetNextItemWidth(-1.0f);
                                ImGui::Button(label.c_str(), ImVec2(-1.0f, 0.0f));
                                if (ImGui::BeginDragDropTarget()) {
                                    if (const ImGuiPayload* payload =
                                            ImGui::AcceptDragDropPayload(DragDrop::kEntityId)) {
                                        if (payload->DataSize >= static_cast<int>(sizeof(uint32_t))) {
                                            const uint32_t droppedId =
                                                *static_cast<const uint32_t*>(payload->Data);
                                            Entity* dropped = scene.FindEntity(droppedId);
                                            const bool compatible = dropped &&
                                                (field.typeName != "Camera" ||
                                                 dropped->HasComponent<CameraComponent>());
                                            if (compatible) {
                                                RecordUndoSnapshot();
                                                mipsScript->fieldValues[i] =
                                                    static_cast<double>(droppedId);
                                                m_SceneDirty = true;
                                            }
                                        }
                                    }
                                    ImGui::EndDragDropTarget();
                                }
                                if (referenced && ImGui::BeginPopupContextItem("ReferenceContext")) {
                                    if (ImGui::MenuItem("Clear")) {
                                        RecordUndoSnapshot();
                                        mipsScript->fieldValues[i] = 0.0;
                                        m_SceneDirty = true;
                                    }
                                    ImGui::EndPopup();
                                }
                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) &&
                                    field.typeName == "Camera")
                                    ImGui::SetTooltip("Drag a Scene object with a Camera component here.");
                                ImGui::PopID();
                                continue;
                            }
                            if (isBool) {
                                bool value = mipsScript->fieldValues[i] != 0.0;
                                if (EditorTheme::Checkbox(field.name.c_str(), &value))
                                    mipsScript->fieldValues[i] = value ? 1.0 : 0.0;
                                continue;
                            }
                            if (!isFloat && !isInt)
                                continue;

                            bool valueChanged = false;
                            if (isInt) {
                                int value = static_cast<int>(std::lround(mipsScript->fieldValues[i]));
                                if (ImGui::DragInt(field.name.c_str(), &value)) {
                                    mipsScript->fieldValues[i] = static_cast<double>(value);
                                    valueChanged = true;
                                }
                            } else {
                                float value = static_cast<float>(mipsScript->fieldValues[i]);
                                if (ImGui::DragFloat(field.name.c_str(), &value, 0.1f)) {
                                    mipsScript->fieldValues[i] = static_cast<double>(value);
                                    valueChanged = true;
                                }
                            }
                            if (valueChanged) {
                                if (m_IsPlaying)
                                    m_Engine->GetMipsRuntime().ApplyFieldOverrides(
                                        selectedEntity, *mipsScript);
                            }
                        }

                        if (m_IsPlaying)
                            ImGui::EndDisabled();
                    } else if (!mipsScript->scriptPath.empty()) {
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "Compile failed");
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "No script assigned");
                    }
                }
                EndInspectableComponent(*mipsScript);
                if (removeComponent) {
                    selectedEntity->RemoveComponent(mipsScript);
                    break;
                }
            }

            ImGui::Spacing();
            if (ImGui::Button("Add Component", ImVec2(-1, 0))) {
                ImGui::OpenPopup("AddComponentPopup");
            }

            if (ImGui::BeginPopup("AddComponentPopup")) {
                if (ImGui::BeginMenu("Mesh")) {
                  if (!selectedEntity->HasComponent<MeshRendererComponent>()) {
                    if (ImGui::MenuItem("Mesh Renderer")) {
                        RecordUndoSnapshot();
                        auto& mr = selectedEntity->AddComponent<MeshRendererComponent>();
                        mr.SetPrimitive("Cube", 1.0f);
                        mr.texture = std::make_shared<Texture>(Texture::CreateCheckerboard(128, 16));
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!selectedEntity->HasComponent<SkinnedMeshRendererComponent>()) {
                    if (ImGui::MenuItem("Skinned Mesh Renderer")) {
                        RecordUndoSnapshot();
                        auto& sk = selectedEntity->AddComponent<SkinnedMeshRendererComponent>();
                        sk.texture = std::make_shared<Texture>(Texture::CreateCheckerboard(128, 16));
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!selectedEntity->HasComponent<TerrainComponent>()) {
                    if (ImGui::MenuItem("Terrain")) {
                        RecordUndoSnapshot();
                        auto& terrain = selectedEntity->AddComponent<TerrainComponent>();
                        auto* mr = selectedEntity->GetComponent<MeshRendererComponent>();
                        if (!mr) {
                            mr = &selectedEntity->AddComponent<MeshRendererComponent>();
                            mr->texture = std::make_shared<Texture>(Texture::CreateCheckerboard(256, 32));
                        }
                        terrain.RebuildMesh(*mr);
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!selectedEntity->HasComponent<ProModelerComponent>()) {
                    if (ImGui::MenuItem("ProModeler Mesh")) {
                        RecordUndoSnapshot();
                        auto& pb = selectedEntity->AddComponent<ProModelerComponent>();
                        auto* mr = selectedEntity->GetComponent<MeshRendererComponent>();
                        if (!mr) {
                            mr = &selectedEntity->AddComponent<MeshRendererComponent>();
                            mr->texture = std::make_shared<Texture>(Texture::CreateCheckerboard(128, 16));
                        }
                        pb.ResetBox();
                        pb.RebuildMesh(*mr);
                        ImGui::CloseCurrentPopup();
                    }
                }
                  ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Animation")) {
                  if (!selectedEntity->HasComponent<AnimatorComponent>()) {
                    if (ImGui::MenuItem("Animator")) {
                        RecordUndoSnapshot();
                        auto& anim = selectedEntity->AddComponent<AnimatorComponent>();
                        if (auto* sk = selectedEntity->GetComponent<SkinnedMeshRendererComponent>()) {
                            anim.modelPath = sk->modelPath;
                            anim.ReloadAssets();
                        }
                        ImGui::CloseCurrentPopup();
                    }
                }
                  ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Rendering")) {
                  if (!selectedEntity->HasComponent<CameraComponent>()) {
                    if (ImGui::MenuItem("Camera")) {
                        RecordUndoSnapshot();
                        selectedEntity->AddComponent<CameraComponent>();
                        SyncSelectedCameraFromTransform(selectedEntity);
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!selectedEntity->HasComponent<PostProcessVolumeComponent>()) {
                    if (ImGui::MenuItem("Post Process Volume")) {
                        RecordUndoSnapshot();
                        selectedEntity->AddComponent<PostProcessVolumeComponent>();
                        m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!selectedEntity->HasComponent<LightComponent>()) {
                    if (ImGui::BeginMenu("Light")) {
                        if (ImGui::MenuItem("Directional")) {
                            RecordUndoSnapshot();
                            auto& light = selectedEntity->AddComponent<LightComponent>();
                            light.type = LightType::Directional;
                            ImGui::CloseCurrentPopup();
                        }
                        if (ImGui::MenuItem("Point")) {
                            RecordUndoSnapshot();
                            auto& light = selectedEntity->AddComponent<LightComponent>();
                            light.type = LightType::Point;
                            ImGui::CloseCurrentPopup();
                        }
                        if (ImGui::MenuItem("Spot")) {
                            RecordUndoSnapshot();
                            auto& light = selectedEntity->AddComponent<LightComponent>();
                            light.type = LightType::Spot;
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndMenu();
                    }
                }
                  ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Audio")) {
                  if (!selectedEntity->HasComponent<AudioSourceComponent>()) {
                    if (ImGui::MenuItem("Audio Source")) {
                        RecordUndoSnapshot();
                        selectedEntity->AddComponent<AudioSourceComponent>();
                        ImGui::CloseCurrentPopup();
                    }
                  }
                  ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Physics")) {
                  if (!selectedEntity->HasComponent<ColliderComponent>()) {
                    if (ImGui::BeginMenu("Collider")) {
                    auto addCollider = [&](ColliderShape shape) {
                        RecordUndoSnapshot();
                        auto& col = selectedEntity->AddComponent<ColliderComponent>();
                        col.shape = shape;
                        if (auto* mr = selectedEntity->GetComponent<MeshRendererComponent>();
                            mr && mr->mesh)
                            ColliderUtils::FitColliderToMesh(col, *mr->mesh);
                        else if (shape == ColliderShape::Capsule) {
                            col.radius = 0.35f;
                            col.capsuleHeight = 1.0f;
                        }
                        ImGui::CloseCurrentPopup();
                    };
                    if (ImGui::MenuItem("Box"))
                        addCollider(ColliderShape::Box);
                    if (ImGui::MenuItem("Sphere"))
                        addCollider(ColliderShape::Sphere);
                    if (ImGui::MenuItem("Capsule"))
                        addCollider(ColliderShape::Capsule);
                    if (ImGui::MenuItem("Mesh Collider"))
                        addCollider(ColliderShape::Mesh);
                    ImGui::EndMenu();
                    }
                }
                if (!selectedEntity->HasComponent<RigidbodyComponent>()) {
                    if (ImGui::MenuItem("Rigidbody")) {
                        RecordUndoSnapshot();
                        selectedEntity->AddComponent<RigidbodyComponent>();
                        ImGui::CloseCurrentPopup();
                    }
                }
                  ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("UI")) {
                  if (!selectedEntity->HasComponent<CanvasComponent>()) {
                    if (ImGui::MenuItem("Canvas")) {
                        RecordUndoSnapshot();
                        selectedEntity->AddComponent<CanvasComponent>();
                        if (!selectedEntity->HasComponent<RectTransformComponent>()) {
                            auto& rect = selectedEntity->AddComponent<RectTransformComponent>();
                            rect.anchorMin = { 0.0f, 0.0f };
                            rect.anchorMax = { 1.0f, 1.0f };
                            rect.sizeDelta = { 0.0f, 0.0f };
                        }
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!selectedEntity->HasComponent<RectTransformComponent>()) {
                    if (ImGui::MenuItem("Rect Transform")) {
                        RecordUndoSnapshot();
                        selectedEntity->AddComponent<RectTransformComponent>();
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!selectedEntity->HasComponent<UIImageComponent>()) {
                    if (ImGui::MenuItem("Image")) {
                        RecordUndoSnapshot();
                        selectedEntity->AddComponent<UIImageComponent>();
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!selectedEntity->HasComponent<UITextComponent>()) {
                    if (ImGui::MenuItem("Text")) {
                        RecordUndoSnapshot();
                        selectedEntity->AddComponent<UITextComponent>();
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!selectedEntity->HasComponent<UIButtonGroupComponent>()) {
                    if (ImGui::MenuItem("Button Group")) {
                        RecordUndoSnapshot();
                        Scene& scene = m_Engine->GetScene();
                        Entity* canvas = selectedEntity->HasComponent<CanvasComponent>()
                            ? selectedEntity
                            : FindCanvasAncestor(scene, selectedEntity);
                        if (!canvas) {
                            canvas = CreateUICanvas(nullptr);
                            scene.SetParent(selectedEntity, canvas);
                        }
                        if (!selectedEntity->HasComponent<RectTransformComponent>())
                            selectedEntity->AddComponent<RectTransformComponent>();
                        selectedEntity->AddComponent<UIButtonGroupComponent>();
                        m_Engine->GetMipsRuntime().SyncEditSnapshot(scene);
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!selectedEntity->HasComponent<UIButtonComponent>()) {
                    if (ImGui::MenuItem("Button")) {
                        RecordUndoSnapshot();
                        Scene& scene = m_Engine->GetScene();
                        if (selectedEntity->HasComponent<CanvasComponent>() ||
                            selectedEntity->HasComponent<UIButtonGroupComponent>()) {
                            if (Entity* button = CreateUIButton(selectedEntity))
                                SelectSingleEntity(button->GetID());
                        } else {
                            Entity* group = FindButtonGroupAncestor(scene, selectedEntity);
                            if (!group) {
                                Entity* canvas = FindCanvasAncestor(scene, selectedEntity);
                                if (!canvas)
                                    canvas = CreateUICanvas(nullptr);
                                group = CreateUIButtonGroup(canvas);
                            }
                            if (group)
                                scene.SetParent(selectedEntity, group);
                            if (!selectedEntity->HasComponent<RectTransformComponent>())
                                selectedEntity->AddComponent<RectTransformComponent>();
                            auto& button = selectedEntity->AddComponent<UIButtonComponent>();
                            Entity* textEntity = scene.CreateEntity("Text");
                            scene.SetParent(textEntity, selectedEntity);
                            auto& textRect = textEntity->AddComponent<RectTransformComponent>();
                            textRect.anchorMin = { 0.0f, 0.0f };
                            textRect.anchorMax = { 1.0f, 1.0f };
                            textRect.pivot = { 0.5f, 0.5f };
                            textRect.anchoredPosition = { 0.0f, 0.0f };
                            textRect.sizeDelta = { 0.0f, 0.0f };
                            auto& text = textEntity->AddComponent<UITextComponent>();
                            text.text = "Button";
                            text.fontSize = 24.0f;
                            text.alignment = UITextAlignment::Center;
                            text.color = button.textColor;
                            m_Engine->GetMipsRuntime().SyncEditSnapshot(scene);
                        }
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!selectedEntity->HasComponent<UIAudioSpectrumComponent>()) {
                    if (ImGui::MenuItem("Audio Spectrum")) {
                        RecordUndoSnapshot();
                        selectedEntity->AddComponent<UIAudioSpectrumComponent>();
                        if (!selectedEntity->HasComponent<RectTransformComponent>())
                            selectedEntity->AddComponent<RectTransformComponent>();
                        ImGui::CloseCurrentPopup();
                    }
                }
                  ImGui::EndMenu();
                }
                DrawAddComponentScriptMenu(*selectedEntity);
                ImGui::EndPopup();
            }
        }
    } else {
        if (ImGui::CollapsingHeader("PS1 Render Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& settings = m_Engine->GetRenderer().GetPS1Settings();
            ImGui::DragFloat("Vertex Jitter (Res)", &settings.vertexJitter, 1.0f, 16.0f, 640.0f);
            EditorTheme::Checkbox("Affine Mapping (Warp)", &settings.affineMapping);
            EditorTheme::Checkbox("15-bit Color Limit", &settings.colorDepthLimit);
            EditorTheme::Checkbox("Bayer Dithering", &settings.ditheringEnabled);
            EditorTheme::Checkbox("Wireframe", &settings.wireframeMode);

            ImGui::Separator();
            ImGui::TextColored(EditorTheme::TextMuted,
                "Fog / Color Grading / Vignette are configured by a Post Process Volume object.");
        }
    }

    ImGui::End();
}

void EditorApp::DrawProjectPanel() {
    if (m_AssetBrowser)
        m_AssetBrowser->OnImGuiRender();
    EnsureProjectTabSelected();
}

void EditorApp::SpawnModelFromProjectAsset(const std::string& projectRelPath,
                                           const ImVec2* viewMin,
                                           const ImVec2* viewSize) {
    RecordUndoSnapshot();
    std::string err;
    Entity* spawned = SceneIO::SpawnModelFromAsset(m_Engine->GetScene(), projectRelPath, err);
    if (!spawned) {
        MIPSYNC_WARN("Model spawn failed: {}", err);
        return;
    }

    if (viewMin && viewSize && viewSize->x > 0.0f && viewSize->y > 0.0f) {
        const ImGuiIO& io = ImGui::GetIO();
        const glm::vec3 pos = PickPointOnPlane(
            m_SceneCamera.GetCamera(), io.MousePos.x, io.MousePos.y,
            viewMin->x, viewMin->y, viewSize->x, viewSize->y, 0.0f);
        if (auto* transform = spawned->GetComponent<TransformComponent>())
            transform->position = pos;
    }

    m_SelectedEntityID = spawned->GetID();
    if (m_AssetBrowser)
        m_AssetBrowser->ClearAssetSelection();
    m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
    UpdateSceneDirtyState();
    MIPSYNC_INFO("Model placed in scene: {}", projectRelPath);
}

void EditorApp::DispatchProjectAssetDrop(const std::string& projectRelPath,
                                         const ImVec2* viewMin,
                                         const ImVec2* viewSize) {
    switch (AssetBrowserPanel::ClassifyAssetByPath(projectRelPath)) {
    case AssetKind::Prefab: {
        RecordUndoSnapshot();
        const std::string abs = AssetManager::Get().ToAbsolute(projectRelPath);
        std::string err;
        if (Entity* spawned = SceneIO::InstantiateFromFile(m_Engine->GetScene(), abs, err)) {
            m_SelectedEntityID = spawned->GetID();
            if (m_AssetBrowser) m_AssetBrowser->ClearAssetSelection();
            m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
            MIPSYNC_INFO("Prefab instantiated: {}", projectRelPath);
        } else {
            MIPSYNC_WARN("Prefab load failed: {}", err);
        }
        break;
    }
    case AssetKind::Model:
        SpawnModelFromProjectAsset(projectRelPath, viewMin, viewSize);
        break;
    case AssetKind::Audio: {
        RecordUndoSnapshot();
        const std::filesystem::path clipPath = PathUtf8::FromString(projectRelPath);
        std::string entityName = PathUtf8::ToString(clipPath.stem());
        if (entityName.empty()) entityName = "Audio Source";
        Entity* audioEntity = m_Engine->GetScene().CreateEntity(entityName);
        auto& audio = audioEntity->AddComponent<AudioSourceComponent>();
        audio.clipPath = projectRelPath;
        if (viewMin && viewSize && viewSize->x > 0.0f && viewSize->y > 0.0f) {
            const ImGuiIO& io = ImGui::GetIO();
            if (auto* transform = audioEntity->GetComponent<TransformComponent>()) {
                transform->position = PickPointOnPlane(
                    m_SceneCamera.GetCamera(), io.MousePos.x, io.MousePos.y,
                    viewMin->x, viewMin->y, viewSize->x, viewSize->y, 0.0f);
            }
        }
        SelectSingleEntity(audioEntity->GetID());
        m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
        m_SceneDirty = true;
        MIPSYNC_INFO("Audio Source created from clip: {}", projectRelPath);
        break;
    }
    default:
        break;
    }
}

void EditorApp::AcceptPrefabDrop() {
    if (!ImGui::BeginDragDropTarget())
        return;

    const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DragDrop::kAssetMove);
    if (!payload)
        payload = ImGui::AcceptDragDropPayload(DragDrop::kAssetPrefab);
    if (!payload)
        payload = ImGui::AcceptDragDropPayload(DragDrop::kAssetAudio);
    if (payload) {
        const std::vector<std::string> paths = PathsFromDragPayload(payload);
        if (paths.size() == 1)
            DispatchProjectAssetDrop(paths[0]);
    }

    ImGui::EndDragDropTarget();
}

void EditorApp::AcceptSceneViewDrops(const ImVec2& imageMin, const ImVec2& imageSize) {
    const ImRect viewportRect(imageMin, ImVec2(imageMin.x + imageSize.x,
                                               imageMin.y + imageSize.y));
    if (!ImGui::BeginDragDropTargetCustom(viewportRect,
                                           ImGui::GetID("##SceneViewAssetDropTarget")))
        return;

    ImGuiIO& io = ImGui::GetIO();
    auto pickAtCursor = [&]() -> Entity* {
        return PickEntityAtPoint(
            m_Engine->GetScene(),
            m_SceneCamera.GetCamera(),
            io.MousePos.x, io.MousePos.y,
            imageMin.x, imageMin.y,
            imageSize.x, imageSize.y);
    };

    const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DragDrop::kAssetMove);
    if (!payload)
        payload = ImGui::AcceptDragDropPayload(DragDrop::kAssetMaterial);
    if (!payload)
        payload = ImGui::AcceptDragDropPayload(DragDrop::kAssetTexture);
    if (!payload)
        payload = ImGui::AcceptDragDropPayload(DragDrop::kAssetPrefab);
    if (!payload)
        payload = ImGui::AcceptDragDropPayload(DragDrop::kAssetModel);
    if (!payload)
        payload = ImGui::AcceptDragDropPayload(DragDrop::kAssetAudio);

    if (payload) {
        const std::vector<std::string> paths = PathsFromDragPayload(payload);
        if (paths.size() == 1) {
            const std::string& projectRel = paths[0];
            switch (AssetBrowserPanel::ClassifyAssetByPath(projectRel)) {
            case AssetKind::Material:
                if (m_ProModelerEditMode == 3 && !m_ProModelerSelectedFaceTriangles.empty()) {
                    Entity* selected = GetSelectedEntity();
                    auto* pb = selected ? selected->GetComponent<ProModelerComponent>() : nullptr;
                    if (pb && selected->GetID() == m_ProModelerSelectionEntityID) {
                        RecordUndoSnapshot();
                        pb->EnsureFaceTopology();
                        for (size_t triangleStart : m_ProModelerSelectedFaceTriangles) {
                            const size_t triangleIndex = triangleStart / 3u;
                            if (triangleIndex < pb->triangleFaceIds.size())
                                pb->faceMaterialPaths[pb->triangleFaceIds[triangleIndex]] = projectRel;
                        }
                        m_SceneDirty = true;
                        if (m_AssetBrowser) m_AssetBrowser->ClearAssetSelection();
                        m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
                        MIPSYNC_INFO("Material applied to selected ProModeler face(s): {}", projectRel);
                        break;
                    }
                }
                if (Entity* target = pickAtCursor()) {
                    if (auto* mr = target->GetComponent<MeshRendererComponent>()) {
                        Material mat;
                        std::string err;
                        if (Material::Load(AssetManager::Get().ToAbsolute(projectRel), mat, err)) {
                            AssetManager::Get().ApplyMaterialToMeshRenderer(*mr, mat, projectRel);
                            m_SelectedEntityID = target->GetID();
                            if (m_AssetBrowser) m_AssetBrowser->ClearAssetSelection();
                            m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
                            MIPSYNC_INFO("Material applied: {} 遶翫・{}", projectRel, target->GetID());
                        } else {
                            MIPSYNC_WARN("Material load failed: {}", err);
                        }
                    }
                }
                break;
            case AssetKind::Texture:
                if (Entity* target = pickAtCursor()) {
                    if (auto* uiButton = target->GetComponent<UIButtonComponent>()) {
                        RecordUndoSnapshot();
                        uiButton->backgroundTexturePath = projectRel;
                        uiButton->backgroundTexture = AssetManager::Get().GetTexture(projectRel);
                        uiButton->preserveAspect = true;
                        FitRectTransformToTextureAspect(target, uiButton->backgroundTexture.get());
                        m_SelectedEntityID = target->GetID();
                        if (m_AssetBrowser) m_AssetBrowser->ClearAssetSelection();
                        m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
                        MIPSYNC_INFO("UI button background applied: {}", projectRel);
                    } else if (auto* uiImage = target->GetComponent<UIImageComponent>()) {
                        RecordUndoSnapshot();
                        uiImage->texturePath = projectRel;
                        uiImage->texture = AssetManager::Get().GetTexture(projectRel);
                        uiImage->preserveAspect = true;
                        FitRectTransformToTextureAspect(target, uiImage->texture.get());
                        m_SelectedEntityID = target->GetID();
                        if (m_AssetBrowser) m_AssetBrowser->ClearAssetSelection();
                        m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
                        MIPSYNC_INFO("UI image texture applied: {} 遶翫・{}", projectRel, target->GetID());
                    } else if (target->GetComponent<CanvasComponent>()) {
                        RecordUndoSnapshot();
                        if (Entity* img = CreateUIImage(target)) {
                            if (auto* uiImage = img->GetComponent<UIImageComponent>()) {
                                uiImage->texturePath = projectRel;
                                uiImage->texture = AssetManager::Get().GetTexture(projectRel);
                                uiImage->color = { 1.0f, 1.0f, 1.0f, 1.0f };
                                uiImage->preserveAspect = true;
                                FitRectTransformToTextureAspect(img, uiImage->texture.get());
                            }
                            m_SelectedEntityID = img->GetID();
                            if (m_AssetBrowser) m_AssetBrowser->ClearAssetSelection();
                            m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
                            MIPSYNC_INFO("UI image created from texture: {}", projectRel);
                        }
                    }
                }
                break;
            case AssetKind::Prefab:
            case AssetKind::Model:
            case AssetKind::Audio:
                DispatchProjectAssetDrop(projectRel, &imageMin, &imageSize);
                break;
            default:
                break;
            }
        }
    }

    ImGui::EndDragDropTarget();
}

void EditorApp::DrawMaterialAssetInspector(const std::string& projectRelPath) {
    static std::string loadedPath;
    static Material mat;
    static bool loadFailed = false;

    if (loadedPath != projectRelPath) {
        loadedPath = projectRelPath;
        loadFailed = false;
        std::string err;
        if (!Material::Load(AssetManager::Get().ToAbsolute(projectRelPath), mat, err)) {
            MIPSYNC_WARN("Material load failed: {}", err);
            loadFailed = true;
        }
    }

    if (loadFailed) {
        ImGui::TextColored(EditorTheme::Error, "Failed to load material.");
        return;
    }

    const size_t slash = projectRelPath.find_last_of('/');
    const char* fileName = slash == std::string::npos
        ? projectRelPath.c_str()
        : projectRelPath.c_str() + slash + 1;

    ImGui::TextColored(EditorTheme::TextSecondary, "Material Asset");
    ImGui::Text("%s", fileName);
    ImGui::Separator();

    bool changed = false;
    if (ImGui::ColorEdit4("Color", glm::value_ptr(mat.color)))
        changed = true;

    ImGui::Spacing();
    ImGui::Text("Texture");

    const float preview = 128.0f;
    if (!mat.texturePath.empty()) {
        if (auto tex = AssetManager::Get().GetTexture(mat.texturePath)) {
            if (tex->GetID() != 0)
                ImGui::Image((ImTextureID)(intptr_t)tex->GetID(), ImVec2(preview, preview));
        }
        ImGui::TextWrapped("%s", mat.texturePath.c_str());
    } else {
        ImGui::TextColored(EditorTheme::TextMuted, "(none)");
    }

    ImGui::PushStyleColor(ImGuiCol_Button, EditorTheme::InputBg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorTheme::BtnFace);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, EditorTheme::BtnFaceLight);
    ImGui::Button("<drop texture here>", ImVec2(-1.0f, 28.0f));
    ImGui::PopStyleColor(3);

    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* p = ImGui::AcceptDragDropPayload(DragDrop::kAssetMove);
        if (!p)
            p = ImGui::AcceptDragDropPayload(DragDrop::kAssetTexture);
        if (p) {
            const std::vector<std::string> paths = PathsFromDragPayload(p);
            if (paths.size() == 1 &&
                AssetBrowserPanel::ClassifyAssetByPath(paths[0]) == AssetKind::Texture) {
                mat.texturePath = paths[0];
                changed = true;
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (!mat.texturePath.empty() && ImGui::SmallButton("Clear texture")) {
        mat.texturePath.clear();
        changed = true;
    }

    ImGui::Spacing();
    ImGui::TextColored(EditorTheme::TextSecondary, "Main Maps");
    if (ImGui::DragFloat2("Tiling", glm::value_ptr(mat.mainTextureTiling), 0.01f, 0.001f, 100.0f))
        changed = true;
    if (ImGui::DragFloat2("Offset", glm::value_ptr(mat.mainTextureOffset), 0.01f, -100.0f, 100.0f))
        changed = true;

    auto commitMaterial = [&]() {
        AssetManager::Get().ApplyMaterialToSceneUsers(m_Engine->GetScene(), projectRelPath, mat);
        std::string err;
        if (Material::Save(AssetManager::Get().ToAbsolute(projectRelPath), mat, err)) {
            AssetThumbnail::Get().DropMaterialThumbnail(projectRelPath);
            MIPSYNC_INFO("Material saved: {}", projectRelPath);
        } else {
            MIPSYNC_WARN("Material save failed: {}", err);
        }
    };

    if (changed)
        commitMaterial();

    ImGui::Spacing();
    if (EditorTheme::AeroButton("Save Material", ImVec2(-1.0f, EditorTheme::ButtonHeight),
                                AeroButtonKind::Primary))
        commitMaterial();
}

void EditorApp::DrawMaterialSlot(MeshRendererComponent& mr) {
    ImGui::Text("Material:");
    ImGui::SameLine();
    const ImVec2 slotSize(160.0f, 28.0f);
    const std::string label = mr.materialPath.empty() ? "<drop material here>" : mr.materialPath;

    ImGui::PushStyleColor(ImGuiCol_Button, EditorTheme::InputBg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorTheme::BtnFace);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, EditorTheme::BtnFaceLight);
    ImGui::PushStyleColor(ImGuiCol_Text, mr.materialPath.empty() ? EditorTheme::TextMuted : EditorTheme::TextPrimary);
    ImGui::Button(label.c_str(), slotSize);
    ImGui::PopStyleColor(4);

    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* p = ImGui::AcceptDragDropPayload(DragDrop::kAssetMove);
        if (!p)
            p = ImGui::AcceptDragDropPayload(DragDrop::kAssetMaterial);
        if (p) {
            const std::vector<std::string> paths = PathsFromDragPayload(p);
            if (paths.size() == 1 &&
                AssetBrowserPanel::ClassifyAssetByPath(paths[0]) == AssetKind::Material) {
                Material mat;
                std::string err;
                if (Material::Load(AssetManager::Get().ToAbsolute(paths[0]), mat, err)) {
                    RecordUndoSnapshot();
                    AssetManager::Get().ApplyMaterialToMeshRenderer(mr, mat, paths[0]);
                    m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
                } else {
                    MIPSYNC_WARN("Material load failed: {}", err);
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("X##clearmat")) {
        RecordUndoSnapshot();
        AssetManager::Get().ClearMeshRendererMaterial(mr);
        m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
    }

}

void EditorApp::DrawMaterialSlot(SkinnedMeshRendererComponent& mr) {
    ImGui::Text("Material:");
    ImGui::SameLine();
    const ImVec2 slotSize(160.0f, 28.0f);
    const std::string label = mr.materialPath.empty() ? "<drop material here>" : mr.materialPath;

    ImGui::PushStyleColor(ImGuiCol_Button, EditorTheme::InputBg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorTheme::BtnFace);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, EditorTheme::BtnFaceLight);
    ImGui::PushStyleColor(ImGuiCol_Text, mr.materialPath.empty() ? EditorTheme::TextMuted : EditorTheme::TextPrimary);
    ImGui::Button(label.c_str(), slotSize);
    ImGui::PopStyleColor(4);

    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* p = ImGui::AcceptDragDropPayload(DragDrop::kAssetMove);
        if (!p)
            p = ImGui::AcceptDragDropPayload(DragDrop::kAssetMaterial);
        if (p) {
            const std::vector<std::string> paths = PathsFromDragPayload(p);
            if (paths.size() == 1 &&
                AssetBrowserPanel::ClassifyAssetByPath(paths[0]) == AssetKind::Material) {
                Material mat;
                std::string err;
                if (Material::Load(AssetManager::Get().ToAbsolute(paths[0]), mat, err)) {
                    RecordUndoSnapshot();
                    AssetManager::Get().ApplyMaterialToSkinnedMeshRenderer(mr, mat, paths[0]);
                    m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
                } else {
                    MIPSYNC_WARN("Material load failed: {}", err);
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("X##clearskmat")) {
        RecordUndoSnapshot();
        AssetManager::Get().ClearSkinnedMeshRendererMaterial(mr);
        m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
    }

    if (!mr.materialPath.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Save##sksavemat")) {
            Material mat;
            mat.color = mr.color;
            mat.texturePath = mr.texturePath;
            mat.mainTextureTiling = mr.textureTiling;
            mat.mainTextureOffset = mr.textureOffset;
            std::string err;
            if (Material::Save(AssetManager::Get().ToAbsolute(mr.materialPath), mat, err)) {
                AssetThumbnail::Get().DropMaterialThumbnail(mr.materialPath);
                MIPSYNC_INFO("Material saved: {}", mr.materialPath);
            } else {
                MIPSYNC_WARN("Material save failed: {}", err);
            }
        }
    }
}

void EditorApp::DrawConsolePanel() {
    ImGui::Begin("Console");

    if (EditorTheme::AeroButton("Clear", ImVec2(64.0f, EditorTheme::ButtonHeight * 0.85f),
                                 AeroButtonKind::Secondary)) {
        Log::ClearEntries();
    }
    ImGui::SameLine();
    ImGui::TextColored(EditorTheme::TextMuted, "%zu entries", Log::GetRecentEntries().size());
    ImGui::Separator();

    auto entries = Log::GetRecentEntries();
    
    const float commandHeight = m_CommandHost ? ImGui::GetFrameHeightWithSpacing() : 0.0f;
    ImGui::BeginChild("LogRegion", ImVec2(0, -commandHeight), false, ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto& entry : entries) {
        ImVec4 color = EditorTheme::TextPrimary;
        if (entry.level == spdlog::level::trace) color = EditorTheme::TextSecondary;
        else if (entry.level == spdlog::level::warn) color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
        else if (entry.level == spdlog::level::err || entry.level == spdlog::level::critical) color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);

        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(entry.message.c_str());
        ImGui::PopStyleColor();
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);

    ImGui::EndChild();

    if (m_CommandHost) {
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputTextWithHint("##CommandInput", "Command Platform: help, search, describe, entity list...",
                                     m_CommandInput, sizeof(m_CommandInput),
                                     ImGuiInputTextFlags_EnterReturnsTrue)) {
            const std::string command = m_CommandInput;
            m_CommandInput[0] = '\0';
            if (!command.empty()) {
                MIPSYNC_INFO("> {}", command);
                const std::string output = m_CommandHost->ExecuteConsoleLine(command);
                if (!output.empty()) MIPSYNC_INFO("{}", output);
            }
            ImGui::SetKeyboardFocusHere(-1);
        }
    }
    ImGui::End();
}

} // namespace MipsyncEngine
