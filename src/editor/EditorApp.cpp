#include "EditorApp.h"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include "AssetBrowserPanel.h"
#include "EditorAnimatorControllerInspector.h"
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
#include <functional>
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

struct InspectorComponentCard {
    ImVec2 min{};
    float maxX = 0.0f;
    float headerMaxY = 0.0f;
    bool open = false;
};

std::vector<InspectorComponentCard> s_InspectorComponentCards;

struct InspectorMultiEditState {
    Component* primary = nullptr;
    size_t primarySize = 0;
    std::vector<Component*> peers;
};

InspectorMultiEditState s_InspectorMultiEdit;

size_t InspectorComponentSize(const Component& component) {
#define MIPSYNC_COMPONENT_SIZE(Type) \
    if (dynamic_cast<const Type*>(&component)) return sizeof(Type)
    MIPSYNC_COMPONENT_SIZE(TagComponent);
    MIPSYNC_COMPONENT_SIZE(TransformComponent);
    MIPSYNC_COMPONENT_SIZE(SkinnedMeshRendererComponent);
    MIPSYNC_COMPONENT_SIZE(BoneComponent);
    MIPSYNC_COMPONENT_SIZE(AnimatorComponent);
    MIPSYNC_COMPONENT_SIZE(MeshRendererComponent);
    MIPSYNC_COMPONENT_SIZE(MeshSubdividerComponent);
    MIPSYNC_COMPONENT_SIZE(TerrainComponent);
    MIPSYNC_COMPONENT_SIZE(ProModelerComponent);
    MIPSYNC_COMPONENT_SIZE(CameraComponent);
    MIPSYNC_COMPONENT_SIZE(DistanceCullComponent);
    MIPSYNC_COMPONENT_SIZE(FrustumCullComponent);
    MIPSYNC_COMPONENT_SIZE(MipsScriptComponent);
    MIPSYNC_COMPONENT_SIZE(ColliderComponent);
    MIPSYNC_COMPONENT_SIZE(RigidbodyComponent);
    MIPSYNC_COMPONENT_SIZE(LightComponent);
    MIPSYNC_COMPONENT_SIZE(PostProcessVolumeComponent);
    MIPSYNC_COMPONENT_SIZE(AudioSourceComponent);
    MIPSYNC_COMPONENT_SIZE(CanvasComponent);
    MIPSYNC_COMPONENT_SIZE(RectTransformComponent);
    MIPSYNC_COMPONENT_SIZE(UIImageComponent);
    MIPSYNC_COMPONENT_SIZE(UITextComponent);
    MIPSYNC_COMPONENT_SIZE(UIButtonGroupComponent);
    MIPSYNC_COMPONENT_SIZE(UIButtonComponent);
    MIPSYNC_COMPONENT_SIZE(UIAudioSpectrumComponent);
#undef MIPSYNC_COMPONENT_SIZE
    return sizeof(Component);
}

Component* FindMatchingInspectorComponent(Entity& entity, const Component& source,
                                          size_t occurrence) {
    size_t current = 0;
    for (Component* component : entity.GetComponentsInOrder()) {
        if (typeid(*component) != typeid(source))
            continue;
        if (current++ == occurrence)
            return component;
    }
    return nullptr;
}

size_t InspectorComponentOccurrence(const Entity& entity, const Component& source) {
    size_t occurrence = 0;
    for (const Component* component : entity.GetComponentsInOrder()) {
        if (component == &source)
            break;
        if (typeid(*component) == typeid(source))
            ++occurrence;
    }
    return occurrence;
}

void BeginInspectorMultiEdit(Component& primary, std::vector<Component*> peers) {
    s_InspectorMultiEdit.primary = &primary;
    s_InspectorMultiEdit.primarySize = InspectorComponentSize(primary);
    s_InspectorMultiEdit.peers = std::move(peers);
}

void EndInspectorMultiEdit() {
    s_InspectorMultiEdit = {};
}

template <typename T>
bool InspectorValuesMixed(const T* primary, size_t count = 1) {
    if (!s_InspectorMultiEdit.primary || s_InspectorMultiEdit.peers.empty())
        return false;
    const auto base = reinterpret_cast<uintptr_t>(s_InspectorMultiEdit.primary);
    const auto address = reinterpret_cast<uintptr_t>(primary);
    const size_t bytes = sizeof(T) * count;
    if (address < base || address - base > s_InspectorMultiEdit.primarySize ||
        bytes > s_InspectorMultiEdit.primarySize - (address - base))
        return false;
    const size_t offset = address - base;
    for (const Component* peer : s_InspectorMultiEdit.peers) {
        const auto* peerValue = reinterpret_cast<const T*>(
            reinterpret_cast<const unsigned char*>(peer) + offset);
        if (std::memcmp(primary, peerValue, bytes) != 0)
            return true;
    }
    return false;
}

template <typename T>
void PropagateInspectorValues(const T* primary, size_t count = 1) {
    if (!s_InspectorMultiEdit.primary || s_InspectorMultiEdit.peers.empty())
        return;
    const auto base = reinterpret_cast<uintptr_t>(s_InspectorMultiEdit.primary);
    const auto address = reinterpret_cast<uintptr_t>(primary);
    const size_t bytes = sizeof(T) * count;
    if (address < base || address - base > s_InspectorMultiEdit.primarySize ||
        bytes > s_InspectorMultiEdit.primarySize - (address - base))
        return;
    const size_t offset = address - base;
    for (Component* peer : s_InspectorMultiEdit.peers) {
        auto* peerValue = reinterpret_cast<T*>(reinterpret_cast<unsigned char*>(peer) + offset);
        std::memcpy(peerValue, primary, bytes);
    }
}

bool InspectorStringMixed(const std::string* primary) {
    if (!s_InspectorMultiEdit.primary || s_InspectorMultiEdit.peers.empty())
        return false;
    const auto base = reinterpret_cast<uintptr_t>(s_InspectorMultiEdit.primary);
    const auto address = reinterpret_cast<uintptr_t>(primary);
    if (address < base || address - base > s_InspectorMultiEdit.primarySize ||
        sizeof(std::string) > s_InspectorMultiEdit.primarySize - (address - base))
        return false;
    const size_t offset = address - base;
    for (const Component* peer : s_InspectorMultiEdit.peers) {
        const auto* peerValue = reinterpret_cast<const std::string*>(
            reinterpret_cast<const unsigned char*>(peer) + offset);
        if (*peerValue != *primary)
            return true;
    }
    return false;
}

void PropagateInspectorString(const std::string* primary) {
    if (!s_InspectorMultiEdit.primary || s_InspectorMultiEdit.peers.empty())
        return;
    const auto base = reinterpret_cast<uintptr_t>(s_InspectorMultiEdit.primary);
    const auto address = reinterpret_cast<uintptr_t>(primary);
    if (address < base || address - base > s_InspectorMultiEdit.primarySize ||
        sizeof(std::string) > s_InspectorMultiEdit.primarySize - (address - base))
        return;
    const size_t offset = address - base;
    for (Component* peer : s_InspectorMultiEdit.peers) {
        auto* peerValue = reinterpret_cast<std::string*>(
            reinterpret_cast<unsigned char*>(peer) + offset);
        *peerValue = *primary;
    }
}

void RemoveCurrentInspectorComponents(Entity& primaryEntity, Component* primaryComponent) {
    const std::vector<Component*> peers = s_InspectorMultiEdit.peers;
    EndInspectorMultiEdit();
    for (Component* peer : peers) {
        if (peer && peer->entity)
            peer->entity->RemoveComponent(peer);
    }
    primaryEntity.RemoveComponent(primaryComponent);
}

void DrawStraightDockTabOverlines() {
    ImGuiContext& context = *ImGui::GetCurrentContext();
    const ImGuiStyle& style = context.Style;
    const float thickness = 2.0f;
    const ImU32 color = ImGui::GetColorU32(EditorTheme::Accent);

    for (ImGuiWindow* window : context.Windows) {
        ImGuiDockNode* node = window ? window->DockNode : nullptr;
        if (!node || node->VisibleWindow != window || !node->TabBar || !node->HostWindow)
            continue;

        ImGuiTabBar* tabBar = node->TabBar;
        ImGuiTabItem* tab = ImGui::TabBarFindTabByID(tabBar, tabBar->SelectedTabId);
        if (!tab)
            continue;

        const float x1 = tabBar->BarRect.Min.x + tab->Offset;
        const float x2 = x1 + tab->Width;
        const float y1 = tabBar->BarRect.Min.y;
        const float y2 = y1 + thickness;
        const float radius = std::min({std::max(0.0f, style.TabRounding),
                                       (x2 - x1) * 0.5f,
                                       tabBar->BarRect.GetHeight()});
        ImDrawList* drawList = node->HostWindow->DrawList;
        drawList->PushClipRect(ImVec2(x1, tabBar->BarRect.Min.y),
                               ImVec2(x2, tabBar->BarRect.Max.y), true);

        // This is the geometric intersection of a full-width straight strip
        // and the tab's rounded top edge: the ImGui equivalent of a child bar
        // clipped by a rounded parent with CSS overflow:hidden. A fixed inset
        // leaves the line visibly cut short and makes it look painted on top.
        if (radius <= 0.0f) {
            drawList->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2), color);
        } else {
            constexpr int curveSteps = 4;
            ImVec2 points[curveSteps * 2 + 4];
            int pointCount = 0;
            points[pointCount++] = ImVec2(x1 + radius, y1);
            for (int step = 1; step <= curveSteps; ++step) {
                const float y = y1 + thickness * (static_cast<float>(step) / curveSteps);
                const float dy = std::min(radius, y - y1);
                const float inset = radius - std::sqrt(std::max(
                    0.0f, radius * radius - (radius - dy) * (radius - dy)));
                points[pointCount++] = ImVec2(x1 + inset, y);
            }
            points[pointCount++] = ImVec2(x2 - (points[pointCount - 1].x - x1), y2);
            for (int step = curveSteps - 1; step >= 1; --step) {
                const float y = y1 + thickness * (static_cast<float>(step) / curveSteps);
                const float dy = std::min(radius, y - y1);
                const float inset = radius - std::sqrt(std::max(
                    0.0f, radius * radius - (radius - dy) * (radius - dy)));
                points[pointCount++] = ImVec2(x2 - inset, y);
            }
            points[pointCount++] = ImVec2(x2 - radius, y1);
            drawList->AddConvexPolyFilled(points, pointCount, color);
        }
        drawList->PopClipRect();
    }
}

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

void AddProModelerMeshCollider(Entity& entity, const MeshRendererComponent& renderer) {
    if (entity.HasComponent<ColliderComponent>())
        return;
    auto& collider = entity.AddComponent<ColliderComponent>();
    collider.shape = ColliderShape::Mesh;
    collider.convex = false;
    if (renderer.mesh)
        ColliderUtils::FitColliderToMesh(collider, *renderer.mesh);
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
    const bool mixed = InspectorValuesMixed(&enabled);
    if (mixed)
        ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
    const bool pressed = EditorTheme::Checkbox("##enabled", &enabled);
    if (mixed)
        ImGui::PopItemFlag();
    if (pressed)
        PropagateInspectorValues(&enabled);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(mixed ? "Mixed component state" :
                          (enabled ? "Disable component" : "Enable component"));
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
                               EditorApp* editorForUndo,
                               const std::function<void()>& drawComponentMenu = {}) {
    ImGui::PushID(&comp);

    // Godot-style inspector section: one coherent StyleBox row contains the
    // foldout, enabled state and context action. Keep the existing positions
    // and height, but remove the visually disconnected collection of widgets.
    const ImVec2 rawCursor = ImGui::GetCursorScreenPos();
    ImGuiWindow* inspectorWindow = ImGui::GetCurrentWindow();
    // CursorScreenPos can sit outside InnerClipRect after returning from the
    // auto-sized component child. That made the nominal 5 px inset get
    // clipped down to 1 px. Anchor cards to the window's actual visible work
    // rectangle instead of trusting the inherited cursor X coordinate.
    const float visibleMinX = std::max(inspectorWindow->WorkRect.Min.x,
                                       inspectorWindow->InnerClipRect.Min.x + 1.0f);
    const float visibleMaxX = std::min(inspectorWindow->WorkRect.Max.x,
                                       inspectorWindow->InnerClipRect.Max.x - 1.0f);
    const ImVec2 rowMin(visibleMinX, rawCursor.y);
    const float rowHeight = ImGui::GetFrameHeight();
    const ImVec2 rowMax(std::max(rowMin.x + 1.0f, visibleMaxX),
                        rowMin.y + rowHeight);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(rowMin, rowMax,
                            ImGui::GetColorU32(EditorTheme::SurfaceContainerHigh),
                            EditorTheme::ShapeCornerSmall);

    constexpr float checkboxSidePadding = 5.0f;
    ImGui::SetCursorScreenPos(ImVec2(rowMin.x + checkboxSidePadding, rowMin.y));
    DrawComponentEnabledToggle(comp.enabled);
    const float checkboxMaxX = ImGui::GetItemRectMax().x;

    constexpr float menuSide = 14.0f;
    const ImVec2 menuMin(rowMax.x - checkboxSidePadding - menuSide,
                         rowMin.y + (rowHeight - menuSide) * 0.5f);
    const ImVec2 menuSize(menuSide, menuSide);

    // Do not use CollapsingHeader here. It owns undocumented arrow padding,
    // which made visually equal checkbox margins impossible. A fixed hit rect
    // and a manually drawn arrow make the 5 px measurements exact.
    const ImVec2 headerHitMin(checkboxMaxX + checkboxSidePadding, rowMin.y);
    const ImVec2 headerHitMax(std::max(headerHitMin.x + 1.0f, menuMin.x - 2.0f),
                              rowMax.y);
    ImGuiStorage* headerStorage = ImGui::GetStateStorage();
    const ImGuiID headerId = ImGui::GetID("##component_header");
    bool open = headerStorage->GetBool(headerId, true);
    ImGui::SetCursorScreenPos(headerHitMin);
    if (ImGui::InvisibleButton("##component_header",
                               ImVec2(headerHitMax.x - headerHitMin.x,
                                      headerHitMax.y - headerHitMin.y))) {
        open = !open;
        headerStorage->SetBool(headerId, open);
    }
    const bool headerHovered = ImGui::IsItemHovered();
    const bool headerHeld = ImGui::IsItemActive();
    const bool headerContext = ImGui::BeginPopupContextItem("##component_context");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s component", title);

    if (headerHovered || headerHeld) {
        drawList->AddRectFilled(headerHitMin, headerHitMax,
                                ImGui::GetColorU32(headerHeld ? EditorTheme::BtnDarkShadow
                                                             : EditorTheme::SurfaceHover));
    }

    const float arrowLeft = checkboxMaxX + checkboxSidePadding;
    const ImVec2 arrowCenter(arrowLeft + 4.0f, rowMin.y + rowHeight * 0.5f);
    const ImU32 headerTextColor = ImGui::GetColorU32(EditorTheme::TextPrimary);
    if (open) {
        drawList->AddTriangleFilled(ImVec2(arrowCenter.x - 4.0f, arrowCenter.y - 2.0f),
                                    ImVec2(arrowCenter.x + 4.0f, arrowCenter.y - 2.0f),
                                    ImVec2(arrowCenter.x, arrowCenter.y + 3.0f),
                                    headerTextColor);
    } else {
        drawList->AddTriangleFilled(ImVec2(arrowCenter.x - 2.0f, arrowCenter.y - 4.0f),
                                    ImVec2(arrowCenter.x - 2.0f, arrowCenter.y + 4.0f),
                                    ImVec2(arrowCenter.x + 3.0f, arrowCenter.y),
                                    headerTextColor);
    }
    const ImVec2 titleSize = ImGui::CalcTextSize(title);
    const ImVec2 titlePos(arrowLeft + 12.0f,
                          rowMin.y + std::floor((rowHeight - titleSize.y) * 0.5f));
    drawList->PushClipRect(titlePos, ImVec2(menuMin.x - 3.0f, rowMax.y), true);
    drawList->AddText(titlePos, headerTextColor, title);
    drawList->PopClipRect();

    ImGui::SetCursorScreenPos(menuMin);
    if (ImGui::InvisibleButton("##component_menu_button", menuSize))
        ImGui::OpenPopup("##component_menu");
    const bool menuHovered = ImGui::IsItemHovered();
    const bool menuHeld = ImGui::IsItemActive();
    if (menuHovered || menuHeld) {
        drawList->AddRectFilled(menuMin,
                                ImVec2(menuMin.x + menuSize.x, menuMin.y + menuSize.y),
                                ImGui::GetColorU32(menuHeld ? EditorTheme::BtnDarkShadow
                                                           : EditorTheme::SurfaceHover),
                                EditorTheme::ShapeCornerSmall);
    }
    const ImVec2 menuCenter(menuMin.x + menuSize.x * 0.5f,
                            menuMin.y + menuSize.y * 0.5f);
    const ImU32 dotColor = ImGui::GetColorU32(EditorTheme::TextSecondary);
    for (int dot = -1; dot <= 1; ++dot)
        drawList->AddCircleFilled(ImVec2(menuCenter.x + dot * 3.2f, menuCenter.y),
                                  1.0f, dotColor, 8);

    if (headerContext || ImGui::BeginPopup("##component_menu")) {
        if (drawComponentMenu) {
            drawComponentMenu();
            ImGui::Separator();
        }
        if (ImGui::MenuItem("Remove Component")) {
            if (editorForUndo)
                editorForUndo->RecordUndoSnapshot();
            removeRequested = true;
        }
        ImGui::EndPopup();
    }
    const ImVec2 afterHeaderCursor(rowMin.x,
                                   rowMax.y + ImGui::GetStyle().ItemSpacing.y);
    ImGui::SetCursorScreenPos(open ? ImVec2(rowMin.x, rowMax.y)
                                   : afterHeaderCursor);
    s_InspectorComponentCards.push_back(
        InspectorComponentCard{rowMin, rowMax.x, rowMax.y, open});
    if (!comp.enabled)
        ImGui::BeginDisabled();
    if (open) {
        // Constrain every inspector field to the same inset as the header
        // controls. Negative-width text/combo fields now resolve against this
        // child work rect instead of extending to the parent window edge.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 8.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorTheme::PanelFace);
        ImGui::BeginChild("##component_body",
                          ImVec2(rowMax.x - rowMin.x, 0.0f),
                          ImGuiChildFlags_AutoResizeY |
                              ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        const ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                            ImVec2(style.ItemSpacing.x, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(style.FramePadding.x, 2.0f));
    }
    return open;
}

void EndInspectableComponent(const Component& comp, bool open) {
    ImVec2 bodyMax{};
    if (open) {
        // AutoResizeY does not reserve trailing WindowPadding in every ImGui
        // content pattern (notably after SameLine). Materialize it explicitly.
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::PopStyleVar(2);
        ImGui::EndChild();
        bodyMax = ImGui::GetItemRectMax();
    }
    if (!comp.enabled)
        ImGui::EndDisabled();

    if (!s_InspectorComponentCards.empty()) {
        const InspectorComponentCard card = s_InspectorComponentCards.back();
        s_InspectorComponentCards.pop_back();
        const float maxY = open ? std::max(card.headerMaxY, bodyMax.y)
                                : card.headerMaxY;
        ImGui::GetWindowDrawList()->AddRect(
            card.min, ImVec2(card.maxX, maxY),
            ImGui::GetColorU32(EditorTheme::BorderLight),
            EditorTheme::ShapeCornerSmall, 0, 1.0f);
    }
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

template <typename DrawControl>
bool DrawInspectorPropertyRow(const char* label, DrawControl&& drawControl) {
    ImGui::PushID(label);
    const float available = ImGui::GetContentRegionAvail().x;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float labelWidth = std::clamp(std::floor(available * 0.36f), 78.0f, 122.0f);
    const ImVec2 labelMin = ImGui::GetCursorScreenPos();
    const float rowHeight = ImGui::GetFrameHeight();
    ImGui::Dummy(ImVec2(std::max(1.0f, labelWidth - spacing), rowHeight));
    ImGui::RenderTextClipped(labelMin,
                             ImVec2(labelMin.x + labelWidth - spacing, labelMin.y + rowHeight),
                             label, nullptr, nullptr, ImVec2(0.0f, 0.5f));
    ImGui::SameLine(0.0f, spacing);
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool changed = drawControl();
    ImGui::PopID();
    return changed;
}

bool InspectorCheckbox(const char* label, bool* value, bool forceMixed = false) {
    return DrawInspectorPropertyRow(label, [&] {
        const bool mixed = forceMixed || InspectorValuesMixed(value);
        if (mixed)
            ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
        const bool changed = EditorTheme::Checkbox("##value", value);
        if (mixed)
            ImGui::PopItemFlag();
        if (changed)
            PropagateInspectorValues(value);
        return changed;
    });
}

bool InspectorDragFloat(const char* label, float* value, float speed = 1.0f,
                        float min = 0.0f, float max = 0.0f,
                        const char* format = "%.3f", bool forceMixed = false) {
    return DrawInspectorPropertyRow(label, [&] {
        const bool mixed = forceMixed || InspectorValuesMixed(value);
        const bool changed = ImGui::DragFloat("##value", value, speed, min, max,
                                              mixed ? "-" : format);
        if (changed)
            PropagateInspectorValues(value);
        return changed;
    });
}

bool InspectorDragInt(const char* label, int* value, float speed = 1.0f,
                      int min = 0, int max = 0, bool forceMixed = false) {
    return DrawInspectorPropertyRow(label, [&] {
        const bool mixed = forceMixed || InspectorValuesMixed(value);
        const bool changed = ImGui::DragInt("##value", value, speed, min, max,
                                            mixed ? "-" : "%d");
        if (changed)
            PropagateInspectorValues(value);
        return changed;
    });
}

bool InspectorSliderFloat(const char* label, float* value, float min, float max,
                          const char* format = "%.3f") {
    return DrawInspectorPropertyRow(label, [&] {
        const bool mixed = InspectorValuesMixed(value);
        const bool changed = ImGui::SliderFloat("##value", value, min, max,
                                                mixed ? "-" : format);
        if (changed)
            PropagateInspectorValues(value);
        return changed;
    });
}

bool InspectorSliderInt(const char* label, int* value, int min, int max) {
    return DrawInspectorPropertyRow(label, [&] {
        const bool mixed = InspectorValuesMixed(value);
        const bool changed = ImGui::SliderInt("##value", value, min, max,
                                              mixed ? "-" : "%d");
        if (changed)
            PropagateInspectorValues(value);
        return changed;
    });
}

bool InspectorInputInt(const char* label, int* value) {
    return DrawInspectorPropertyRow(label, [&] {
        const bool mixed = InspectorValuesMixed(value);
        if (mixed) {
            int working = *value;
            const bool changed = ImGui::DragInt("##value", &working, 1.0f, 0, 0, "-");
            if (changed) {
                *value = working;
                PropagateInspectorValues(value);
            }
            return changed;
        }
        const bool changed = ImGui::InputInt("##value", value);
        if (changed)
            PropagateInspectorValues(value);
        return changed;
    });
}

bool InspectorInputText(const char* label, char* value, size_t size) {
    return DrawInspectorPropertyRow(label, [&] {
        return ImGui::InputText("##value", value, size);
    });
}

bool InspectorCombo(const char* label, int* value, const char* const items[], int count,
                    bool forceMixed = false) {
    return DrawInspectorPropertyRow(label, [&] {
        const bool mixed = forceMixed || InspectorValuesMixed(value);
        const char* preview = mixed ? "-" :
            ((*value >= 0 && *value < count) ? items[*value] : "");
        bool changed = false;
        if (ImGui::BeginCombo("##value", preview)) {
            for (int index = 0; index < count; ++index) {
                const bool selected = !mixed && *value == index;
                if (ImGui::Selectable(items[index], selected)) {
                    *value = index;
                    PropagateInspectorValues(value);
                    changed = true;
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        return changed;
    });
}

void DrawColorEyedropperGlyph(ImDrawList* drawList, const ImVec2& center, ImU32 color) {
    const ImVec2 tip(center.x - 3.5f, center.y + 3.5f);
    const ImVec2 neck(center.x + 2.5f, center.y - 2.5f);
    drawList->AddLine(tip, neck, color, 1.25f);
    drawList->AddLine(ImVec2(tip.x - 1.0f, tip.y + 1.0f),
                      ImVec2(tip.x + 1.2f, tip.y - 1.2f), color, 1.25f);
    drawList->AddRect(ImVec2(center.x + 1.0f, center.y - 4.0f),
                      ImVec2(center.x + 4.0f, center.y - 1.0f), color,
                      0.7f, 0, 1.1f);
}

bool UnityColorField(const char* id, float* value, int components) {
    const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const float height = ImGui::GetFrameHeight();
    const ImVec4 color(value[0], value[1], value[2], components == 4 ? value[3] : 1.0f);
    ImGuiColorEditFlags previewFlags =
        ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop;
    if (components == 4)
        previewFlags |= ImGuiColorEditFlags_AlphaPreview;

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 1.0f);
    const bool openPicker = ImGui::ColorButton(id, color, previewFlags,
                                               ImVec2(width, height));
    ImGui::PopStyleVar();

    const ImVec2 fieldMin = ImGui::GetItemRectMin();
    const ImVec2 fieldMax = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float toolWidth = std::min(17.0f, width);
    const ImVec2 toolMin(fieldMax.x - toolWidth, fieldMin.y);
    drawList->AddRectFilled(toolMin, fieldMax,
                            ImGui::GetColorU32(UiTokens::Rgba(0x2C2C2C, 0xC8)),
                            1.0f, ImDrawFlags_RoundCornersRight);
    drawList->AddLine(toolMin, ImVec2(toolMin.x, fieldMax.y),
                      ImGui::GetColorU32(EditorTheme::BorderDark), 1.0f);
    DrawColorEyedropperGlyph(
        drawList,
        ImVec2(toolMin.x + toolWidth * 0.5f, (fieldMin.y + fieldMax.y) * 0.5f),
        ImGui::GetColorU32(EditorTheme::TextSecondary));
    drawList->AddRect(fieldMin, fieldMax, ImGui::GetColorU32(EditorTheme::BorderLight),
                      1.0f, 0, 1.0f);

    if (openPicker)
        ImGui::OpenPopup("##unity_color_picker");

    bool changed = false;
    if (ImGui::BeginPopup("##unity_color_picker")) {
        float edited[4] = { value[0], value[1], value[2],
                            components == 4 ? value[3] : 1.0f };
        ImGuiColorEditFlags pickerFlags =
            ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_DisplayRGB |
            ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_DisplayHex;
        if (components == 4)
            pickerFlags |= ImGuiColorEditFlags_AlphaBar;
        if (ImGui::ColorPicker4("##picker", edited, pickerFlags)) {
            value[0] = edited[0];
            value[1] = edited[1];
            value[2] = edited[2];
            if (components == 4)
                value[3] = edited[3];
            changed = true;
        }
        ImGui::EndPopup();
    }
    return changed;
}

bool InspectorColorEdit3(const char* label, float* value) {
    return DrawInspectorPropertyRow(label, [&] {
        const bool changed = UnityColorField("##value", value, 3);
        if (changed)
            PropagateInspectorValues(value, 3);
        return changed;
    });
}

bool InspectorColorEdit4(const char* label, float* value) {
    return DrawInspectorPropertyRow(label, [&] {
        const bool changed = UnityColorField("##value", value, 4);
        if (changed)
            PropagateInspectorValues(value, 4);
        return changed;
    });
}

bool InspectorDragFloatN(const char* label, float* values, int components,
                         float speed = 1.0f, float min = 0.0f, float max = 0.0f,
                         const char* format = "%.3f",
                         const bool* forceMixedAxes = nullptr) {
    return DrawInspectorPropertyRow(label, [&] {
        static constexpr const char* axes[] = { "X", "Y", "Z", "W" };
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        constexpr float axisFieldGap = 2.0f;
        const float axisWidth = ImGui::CalcTextSize("W").x + 2.0f;
        const float available = ImGui::GetContentRegionAvail().x;
        const float fieldWidth = std::max(24.0f,
            (available - components * (axisWidth + axisFieldGap) -
             (components - 1) * spacing) /
                components);
        bool changed = false;
        for (int axis = 0; axis < components; ++axis) {
            if (axis > 0)
                ImGui::SameLine(0.0f, spacing);
            const ImVec2 axisMin = ImGui::GetCursorScreenPos();
            const float frameHeight = ImGui::GetFrameHeight();
            const ImVec2 axisTextSize = ImGui::CalcTextSize(axes[axis]);
            ImGui::Dummy(ImVec2(axisWidth, frameHeight));
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(axisMin.x + std::floor((axisWidth - axisTextSize.x) * 0.5f),
                       axisMin.y + std::floor((frameHeight - axisTextSize.y) * 0.5f)),
                ImGui::GetColorU32(EditorTheme::TextMuted), axes[axis]);
            ImGui::SameLine(0.0f, axisFieldGap);
            ImGui::PushID(axis);
            ImGui::SetNextItemWidth(fieldWidth);
            const bool mixed = (forceMixedAxes && forceMixedAxes[axis]) ||
                               InspectorValuesMixed(&values[axis]);
            const bool axisChanged = ImGui::DragFloat("##axis", &values[axis], speed, min, max,
                                                      mixed ? "-" : format);
            if (axisChanged)
                PropagateInspectorValues(&values[axis]);
            changed |= axisChanged;
            ImGui::PopID();
        }
        return changed;
    });
}

bool InspectorDragFloat2(const char* label, float* values, float speed = 1.0f,
                         float min = 0.0f, float max = 0.0f,
                         const char* format = "%.3f",
                         const bool* forceMixedAxes = nullptr) {
    return InspectorDragFloatN(label, values, 2, speed, min, max, format,
                               forceMixedAxes);
}

bool InspectorDragFloat3(const char* label, float* values, float speed = 1.0f,
                         float min = 0.0f, float max = 0.0f,
                         const char* format = "%.3f",
                         const bool* forceMixedAxes = nullptr) {
    return InspectorDragFloatN(label, values, 3, speed, min, max, format,
                               forceMixedAxes);
}

std::shared_ptr<Texture> GetInspectorObjectReferenceIcon() {
    static std::shared_ptr<Texture> icon;
    static bool attempted = false;
    if (attempted)
        return icon;
    attempted = true;

    const std::filesystem::path path =
        GetBundledResourcesDirectory() / "icons" / "project" / "object_reference.png";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec))
        return nullptr;

    TextureParams params;
    params.nearestFilter = false;
    params.maxSize = 0;
    icon = std::make_shared<Texture>(PathUtf8::ToString(path), params);
    if (icon->GetID() == 0)
        icon.reset();
    return icon;
}

bool DrawInspectorObjectReferenceButton(const char* display, float width = -1.0f) {
    const bool pressed = ImGui::Button("##object_reference", ImVec2(width, 0.0f));
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImGui::RenderTextClipped(ImVec2(min.x + 5.0f, min.y),
                             ImVec2(max.x - 20.0f, max.y),
                             display, nullptr, nullptr, ImVec2(0.0f, 0.5f));

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 center(std::floor(max.x - 10.0f) + 0.5f,
                        std::floor((min.y + max.y) * 0.5f) + 0.5f);
    const ImU32 color = ImGui::GetColorU32(EditorTheme::TextSecondary);
    if (const auto icon = GetInspectorObjectReferenceIcon(); icon && icon->GetID() != 0) {
        constexpr float iconSize = 10.0f;
        const ImVec2 iconMin(std::floor(center.x - iconSize * 0.5f),
                             std::floor(center.y - iconSize * 0.5f));
        const ImVec2 iconMax(iconMin.x + iconSize, iconMin.y + iconSize);
        // The supplied PNG is 258x324 with the actual 237x237 mark at
        // x=10..246, y=64..300. Keep the original file intact and crop only
        // through UVs. Texture loads vertically flipped for OpenGL, hence V is
        // reversed here.
        constexpr ImVec2 uvTopLeft(10.0f / 258.0f, 1.0f - 64.0f / 324.0f);
        constexpr ImVec2 uvBottomRight(247.0f / 258.0f, 1.0f - 301.0f / 324.0f);
        drawList->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(icon->GetID())),
                           iconMin, iconMax, uvTopLeft, uvBottomRight, color);
    } else {
        // Keep a functional fallback for incomplete development deployments.
        drawList->AddCircle(center, 4.0f, color, 12, 1.0f);
        drawList->AddLine(ImVec2(center.x - 3.0f, center.y),
                          ImVec2(center.x + 3.0f, center.y), color, 1.0f);
        drawList->AddLine(ImVec2(center.x, center.y - 3.0f),
                          ImVec2(center.x, center.y + 3.0f), color, 1.0f);
    }
    return pressed;
}

struct InspectorAssetPickerState {
    AssetKind kind = AssetKind::Other;
    std::string root;
    std::string selected;
    std::vector<AssetEntry> entries;
    char search[128]{};
};

InspectorAssetPickerState s_InspectorAssetPicker;
char s_InspectorObjectSearch[128]{};
uint32_t s_InspectorObjectCandidate = 0;

const char* InspectorAssetKindLabel(AssetKind kind) {
    switch (kind) {
    case AssetKind::Scene: return "Scene";
    case AssetKind::Script: return "Mips#";
    case AssetKind::Texture: return "Texture";
    case AssetKind::Material: return "Material";
    case AssetKind::AnimatorController: return "Controller";
    case AssetKind::Prefab: return "Prefab";
    case AssetKind::Model: return "Model";
    case AssetKind::AnimationClip: return "Clip";
    case AssetKind::Audio: return "Audio";
    default: return "Asset";
    }
}

void RefreshInspectorAssetPicker(AssetKind kind, const std::string& selected) {
    namespace fs = std::filesystem;
    InspectorAssetPickerState& state = s_InspectorAssetPicker;
    state.kind = kind;
    state.root = AssetManager::Get().GetProjectRoot();
    state.selected = selected;
    state.entries.clear();
    state.search[0] = '\0';

    fs::path scanRoot = PathUtf8::FromString(state.root);
    scanRoot /= kind == AssetKind::Scene ? "scenes" : "assets";
    std::error_code ec;
    if (!fs::exists(scanRoot, ec))
        scanRoot = PathUtf8::FromString(state.root);

    fs::recursive_directory_iterator it(
        scanRoot, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec))
            continue;
        const std::string absolute = PathUtf8::ToString(it->path());
        const std::string relative = AssetManager::Get().ToProjectRelative(absolute);
        if (AssetBrowserPanel::ClassifyAssetByPath(relative) != kind)
            continue;
        AssetEntry entry;
        entry.name = PathUtf8::ToString(it->path().filename());
        entry.projectRelPath = relative;
        entry.absolutePath = absolute;
        entry.kind = kind;
        state.entries.push_back(std::move(entry));
    }
    std::sort(state.entries.begin(), state.entries.end(),
              [](const AssetEntry& a, const AssetEntry& b) {
                  return a.name < b.name;
              });
}

std::shared_ptr<Texture> GetInspectorAssetThumbnail(EditorApp* editor,
                                                    const AssetEntry& entry) {
    if (!editor || !editor->GetEngine())
        return nullptr;
    switch (entry.kind) {
    case AssetKind::Texture:
        return AssetManager::Get().GetTexture(entry.projectRelPath);
    case AssetKind::Model:
        return AssetThumbnail::Get().GetMeshThumbnail(
            entry.projectRelPath, editor->GetEngine()->GetRenderer());
    case AssetKind::Material:
        return AssetThumbnail::Get().GetMaterialThumbnail(
            entry.projectRelPath, editor->GetEngine()->GetRenderer());
    case AssetKind::Audio:
        return AssetThumbnail::Get().GetAudioThumbnail(entry.projectRelPath);
    case AssetKind::Prefab:
        return AssetThumbnail::Get().GetPrefabThumbnail(
            entry.projectRelPath, editor->GetEngine()->GetRenderer());
    default:
        return nullptr;
    }
}

void DrawInspectorThumbnailImage(ImDrawList* drawList, const Texture& texture,
                                 const ImVec2& min, const ImVec2& max) {
    // Use the same aspect-fit and OpenGL UV orientation as the Project window.
    DrawTexturedImageAspectFit(drawList, texture, min, max);
}

void DrawInspectorAssetKindIcon(ImDrawList* drawList, const AssetEntry& entry,
                                const ImVec2& min, const ImVec2& max) {
    ImVec2 iconMin = min;
    ImVec2 iconMax = max;
    const bool compact =
        entry.kind == AssetKind::AnimationClip ||
        entry.kind == AssetKind::AnimatorController ||
        entry.kind == AssetKind::Scene ||
        entry.kind == AssetKind::Script;
    if (compact) {
        constexpr float kIconScale = 0.65f;
        const float width = max.x - min.x;
        const float height = max.y - min.y;
        const float insetX = width * (1.0f - kIconScale) * 0.5f;
        const float insetY = height * (1.0f - kIconScale) * 0.5f;
        iconMin = ImVec2(min.x + insetX, min.y + insetY);
        iconMax = ImVec2(max.x - insetX, max.y - insetY);
    }

    if (TryDrawProjectAssetIcon(drawList, entry.kind, iconMin, iconMax,
                                entry.projectRelPath))
        return;
    if (entry.kind == AssetKind::Script) {
        DrawScriptIcon(drawList, iconMin, iconMax);
        return;
    }
    const char* label = InspectorAssetKindLabel(entry.kind);
    ImGui::RenderTextClipped(min, max, label, nullptr, nullptr, ImVec2(0.5f, 0.5f));
}

bool DrawInspectorAssetPickerPopup(EditorApp* editor, std::string& selectedPath) {
    bool changed = false;
    const ImVec2 pickerSize(510.0f, 410.0f);
    ImGui::SetNextWindowSize(pickerSize, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(pickerSize, pickerSize);
    if (!ImGui::BeginPopup("##asset_picker", ImGuiWindowFlags_NoResize))
        return false;

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##search", "Search assets...",
                             s_InspectorAssetPicker.search,
                             sizeof(s_InspectorAssetPicker.search));
    ImGui::Separator();

    std::string search = s_InspectorAssetPicker.search;
    std::transform(search.begin(), search.end(), search.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    ImGui::BeginChild("##asset_grid", ImVec2(0.0f, 0.0f), false);
    constexpr float cellWidth = 92.0f;
    constexpr float cellHeight = 92.0f;
    constexpr float thumbSize = 64.0f;
    const int columns = std::max(1, static_cast<int>(
        ImGui::GetContentRegionAvail().x / (cellWidth + ImGui::GetStyle().ItemSpacing.x)));
    int column = 0;
    for (const AssetEntry& entry : s_InspectorAssetPicker.entries) {
        std::string lower = entry.name;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!search.empty() && lower.find(search) == std::string::npos)
            continue;

        ImGui::PushID(entry.projectRelPath.c_str());
        const ImVec2 cellMin = ImGui::GetCursorScreenPos();
        const bool selected = s_InspectorAssetPicker.selected == entry.projectRelPath;
        const bool activated = ImGui::Selectable(
            "##asset", selected, ImGuiSelectableFlags_AllowDoubleClick,
            ImVec2(cellWidth, cellHeight));
        if (activated)
            s_InspectorAssetPicker.selected = entry.projectRelPath;
        if (activated && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            selectedPath = entry.projectRelPath;
            changed = true;
            ImGui::CloseCurrentPopup();
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 thumbMin(cellMin.x + (cellWidth - thumbSize) * 0.5f,
                              cellMin.y + 3.0f);
        const ImVec2 thumbMax(thumbMin.x + thumbSize, thumbMin.y + thumbSize);
        drawList->AddRectFilled(thumbMin, thumbMax,
                                ImGui::GetColorU32(EditorTheme::SurfaceContainerHigh), 2.0f);
        if (auto texture = GetInspectorAssetThumbnail(editor, entry);
            texture && texture->GetID() != 0) {
            DrawInspectorThumbnailImage(drawList, *texture, thumbMin, thumbMax);
        } else if (entry.kind != AssetKind::Prefab) {
            // Project intentionally leaves model-less prefab tiles empty; all
            // other fallbacks use its exact bundled icon and compact scale.
            DrawInspectorAssetKindIcon(drawList, entry, thumbMin, thumbMax);
        }
        ImGui::RenderTextClipped(ImVec2(cellMin.x + 2.0f, thumbMax.y + 3.0f),
                                 ImVec2(cellMin.x + cellWidth - 2.0f,
                                        cellMin.y + cellHeight),
                                 entry.name.c_str(), nullptr, nullptr,
                                 ImVec2(0.5f, 0.0f));
        ImGui::PopID();
        ++column;
        if (column < columns)
            ImGui::SameLine();
        else
            column = 0;
    }
    ImGui::EndChild();
    ImGui::EndPopup();
    return changed;
}

bool DrawInspectorSceneObjectPickerPopup(Scene& scene, const std::string& expectedType,
                                         uint32_t& selectedId) {
    bool changed = false;
    const ImVec2 pickerSize(390.0f, 410.0f);
    ImGui::SetNextWindowSize(pickerSize, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(pickerSize, pickerSize);
    if (!ImGui::BeginPopup("##scene_object_picker", ImGuiWindowFlags_NoResize))
        return false;

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##search", "Search scene objects...",
                             s_InspectorObjectSearch, sizeof(s_InspectorObjectSearch));
    ImGui::SeparatorText("Scene");
    std::string search = s_InspectorObjectSearch;
    std::transform(search.begin(), search.end(), search.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    ImGui::BeginChild("##object_list", ImVec2(0.0f, -34.0f), true);
    auto drawObject = [&](uint32_t id, const std::string& name) {
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!search.empty() && lower.find(search) == std::string::npos)
            return;
        ImGui::PushID(static_cast<int>(id));
        const bool activated = ImGui::Selectable(
            name.c_str(), s_InspectorObjectCandidate == id,
            ImGuiSelectableFlags_AllowDoubleClick);
        if (activated)
            s_InspectorObjectCandidate = id;
        if (activated && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            selectedId = id;
            changed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopID();
    };

    drawObject(0, "None");
    for (const auto& entityPtr : scene.GetEntities()) {
        Entity* entity = entityPtr.get();
        if (!entity)
            continue;
        if (expectedType == "Camera" && !entity->HasComponent<CameraComponent>())
            continue;
        std::string name = "Entity #" + std::to_string(entity->GetID());
        if (const auto* tag = entity->GetComponent<TagComponent>())
            name = tag->tag;
        drawObject(entity->GetID(), name);
    }
    ImGui::EndChild();
    if (Entity* candidate = scene.FindEntity(s_InspectorObjectCandidate)) {
        const auto* tag = candidate->GetComponent<TagComponent>();
        ImGui::TextUnformatted(tag ? tag->tag.c_str() : "Scene Object");
    } else {
        ImGui::TextUnformatted("None");
    }
    ImGui::EndPopup();
    return changed;
}

bool DrawAssetReferenceField(EditorApp* editor, const char* label, AssetKind expectedKind,
                             std::string& path, const char* emptyText) {
    bool changed = false;
    const bool mixed = InspectorStringMixed(&path);
    std::string display = mixed ? "-" : emptyText;
    if (!mixed && !path.empty()) {
        display = PathUtf8::ToString(PathUtf8::FromString(path).filename());
        if (display.empty()) display = path;
    }

    DrawInspectorPropertyRow(label, [&] {
        const float clearWidth = ImGui::GetFrameHeight();
        const float fieldWidth = std::max(
            48.0f, ImGui::GetContentRegionAvail().x - clearWidth - 4.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, EditorTheme::InputBg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorTheme::BtnFaceLight);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, EditorTheme::BtnFace);
        const bool openPicker =
            DrawInspectorObjectReferenceButton(display.c_str(), fieldWidth);
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
        ImGui::BeginDisabled(path.empty() && !mixed);
        if (ImGui::Button("x", ImVec2(clearWidth, 0.0f))) {
            path.clear();
            changed = true;
        }
        ImGui::EndDisabled();

        if (openPicker && editor) {
            RefreshInspectorAssetPicker(expectedKind, mixed ? std::string{} : path);
            ImGui::OpenPopup("##asset_picker");
        }
        if (DrawInspectorAssetPickerPopup(editor, path))
            changed = true;
        return false;
    });
    if (changed)
        PropagateInspectorString(&path);
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
    // ImGui_ImplGlfw_NewFrame has already populated DisplaySize in window
    // coordinates and DisplayFramebufferScale in physical pixels.  Window::
    // GetWidth/GetHeight are framebuffer dimensions; overwriting DisplaySize
    // with them breaks the projection on scaled displays and blurs all chrome.
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

    DrawStraightDockTabOverlines();

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
            const bool canBuild = m_BuildSettings && !m_BuildSettings->IsBuildInProgress();
            if (ImGui::MenuItem("Build PS1", nullptr, false, canBuild)) {
                std::string msg;
                const bool ok = m_BuildSettings->RunBuild(msg);
                if (!ok)
                    TriggerBuildToast(msg.empty() ? "Could not start build." : msg, false);
            }
            if (ImGui::MenuItem("Build PS1 && Run in Emulator", nullptr, false, canBuild)) {
                std::string msg;
                const bool ok = m_BuildSettings->RunBuildAndRun(msg);
                if (!ok)
                    TriggerBuildToast(msg.empty() ? "Could not start build." : msg, false);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Build PC Native", nullptr, false, canBuild)) {
                std::string msg;
                const bool ok = m_BuildSettings->RunPcNativeBuild(msg);
                if (!ok)
                    TriggerBuildToast(msg.empty() ? "Could not start build." : msg, false);
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

    ImGuiIO& io = ImGui::GetIO();
    const bool ctrl = Input::IsKeyPressed(GLFW_KEY_LEFT_CONTROL) ||
                      Input::IsKeyPressed(GLFW_KEY_RIGHT_CONTROL);
    if (io.WantTextInput)
        return;

    const bool shift = Input::IsKeyPressed(GLFW_KEY_LEFT_SHIFT) ||
                       Input::IsKeyPressed(GLFW_KEY_RIGHT_SHIFT);
    if (ctrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_S, false))
            SaveScene();
        if (ImGui::IsKeyPressed(ImGuiKey_O, false))
            LoadScene();

        if (shift && ImGui::IsKeyPressed(ImGuiKey_B, false))
            OpenBuildSettings();
        if (shift && ImGui::IsKeyPressed(ImGuiKey_P, false) && m_BuildSettings) {
            std::string msg;
            const bool ok = m_BuildSettings->RunBuild(msg);
            if (!ok)
                TriggerBuildToast(msg.empty() ? "Could not start build." : msg, false);
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

    // Entity editing shortcuts belong to the selection, not to the panel that
    // happened to create it. Scene View and Hierarchy now feed the same IDs
    // into this one path, so no second click is required to make the
    // Hierarchy window own keyboard focus.
    Scene& scene = m_Engine->GetScene();
    const bool hasEntitySelection =
        m_SelectedEntityID != 0 || !m_SelectedEntityIDs.empty();
    if (ctrl && hasEntitySelection && ImGui::IsKeyPressed(ImGuiKey_C, false))
        m_HierarchyClipboardEntityIDs = GetSelectedHierarchyRootIDs(m_SelectedEntityID);

    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false) &&
        !m_HierarchyClipboardEntityIDs.empty()) {
        std::vector<uint32_t> pastedIds;
        RecordUndoSnapshot();
        for (uint32_t sourceId : m_HierarchyClipboardEntityIDs) {
            Entity* source = scene.FindEntity(sourceId);
            if (!source)
                continue;
            Entity* duplicate = scene.DuplicateEntity(*source);
            if (!duplicate)
                continue;
            scene.SetParent(duplicate, scene.FindEntity(source->GetParentID()));
            pastedIds.push_back(duplicate->GetID());
        }
        if (!pastedIds.empty()) {
            m_SelectedEntityIDs.clear();
            m_SelectedEntityIDs.insert(pastedIds.begin(), pastedIds.end());
            m_SelectedEntityID = pastedIds.back();
            m_HierarchySelectionAnchor = m_SelectedEntityID;
            m_Engine->GetMipsRuntime().SyncEditSnapshot(scene);
            m_SceneDirty = true;
        }
    }

    if (!hasEntitySelection)
        return;

    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
        const auto sourceIds = GetSelectedHierarchyRootIDs(m_SelectedEntityID);
        std::vector<uint32_t> duplicateIds;
        RecordUndoSnapshot();
        for (uint32_t sourceId : sourceIds) {
            if (Entity* source = scene.FindEntity(sourceId)) {
                if (Entity* duplicate = scene.DuplicateEntity(*source))
                    duplicateIds.push_back(duplicate->GetID());
            }
        }
        if (!duplicateIds.empty()) {
            m_SelectedEntityIDs.clear();
            m_SelectedEntityIDs.insert(duplicateIds.begin(), duplicateIds.end());
            m_SelectedEntityID = duplicateIds.back();
            m_HierarchySelectionAnchor = m_SelectedEntityID;
            m_Engine->GetMipsRuntime().SyncEditSnapshot(scene);
            m_SceneDirty = true;
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        if (m_ProModelerEditMode == 3 &&
            !m_ProModelerSelectedFaceTriangles.empty() &&
            DeleteSelectedProModelerFaces()) {
            return;
        }
        const auto deleteIds = GetSelectedHierarchyRootIDs(m_SelectedEntityID);
        RecordUndoSnapshot();
        for (uint32_t id : deleteIds) {
            if (Entity* target = scene.FindEntity(id))
                scene.DestroyEntity(target);
        }
        ClearEntitySelection();
        ClearProModelerSelection();
        m_Engine->GetMipsRuntime().SyncEditSnapshot(scene);
        m_SceneDirty = true;
        return;
    }

    if (!ctrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_W, false)) m_GizmoOperation = ImGuizmo::TRANSLATE;
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) m_GizmoOperation = ImGuizmo::ROTATE;
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) m_GizmoOperation = ImGuizmo::SCALE;
        if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
            if (Entity* selected = GetSelectedEntity())
                FrameEntityInSceneView(*selected);
        }
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

static constexpr int kFontSizeDefaultVersion = 1;

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
        // 12px was the old untouched default. Move only that legacy value to
        // the new 13px baseline once; explicit larger/smaller choices survive.
        const int defaultVersion = j.value("fontSizeDefaultVersion", 0);
        if (defaultVersion < kFontSizeDefaultVersion && m_FontSize == 12.0f) {
            m_FontSize = 13.0f;
            SaveEditorSettings();
        }
    } catch (...) {
        m_FontSize = EditorTheme::GetDefaultFontSizeForDisplay();
    }
}

void EditorApp::SaveEditorSettings() {
    using json = nlohmann::json;
    json j;
    j["fontSize"] = m_FontSize;
    j["fontSizeDefaultVersion"] = kFontSizeDefaultVersion;
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
    // The editor is an authoring and inspection surface, not a PS1 rasterizer
    // emulator. Keep both Scene View and Game View perspective-correct and
    // full-color so texture seams, topology and skinning defects stay visible.
    psx.vertexJitter = 0.0f;
    psx.affineMapping = false;
    psx.colorDepthLimit = false;
    psx.ditheringEnabled = false;

    if (sceneView3D) {
        // Avoid the short PS1 fog range swallowing the editor scene while the
        // orbit camera is pulled back. Game View keeps the authored fog.
        const float sceneFar = m_SceneCamera.GetCamera().farClip;
        if (psx.fogEnabled && sceneFar > psx.fogEnd)
            psx.fogEnd = sceneFar * 0.9f;
    }

    // Scene View and Game View must expose identical export-time topology.
    // Keeping this unconditional also ignores legacy SceneViewPsxMesh=0
    // layout entries that could silently restore the high-poly source mesh.
    constexpr bool usePs1PreviewMeshes = true;
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

glm::vec3 EditorApp::GetSceneViewSpawnPosition() const {
    const Camera& camera = m_SceneCamera.GetCamera();
    // Follow the Scene View zoom level while keeping a newly created unit
    // object large enough to see and far enough not to intersect the camera.
    const float distance = std::clamp(m_SceneCamera.GetDistance(), 2.0f, 10.0f);
    return camera.GetPosition() + camera.GetForward() * distance;
}

void EditorApp::PlaceNewSceneEntity(Entity& entity, Entity* parentForNew) {
    Scene& scene = m_Engine->GetScene();
    if (parentForNew)
        scene.SetParent(&entity, parentForNew);
    // Set after parenting: TransformComponent stores a local position.
    scene.SetWorldPosition(entity, GetSceneViewSpawnPosition());
}

void EditorApp::SelectSingleEntity(uint32_t entityId) {
    m_SelectedEntityID = entityId;
    m_SelectedEntityIDs.clear();
    if (entityId != 0)
        m_SelectedEntityIDs.insert(entityId);
    m_HierarchySelectionAnchor = entityId;
    QueueHierarchyReveal(entityId);
    if (m_AssetBrowser)
        m_AssetBrowser->ClearAssetSelection();
    SyncAnimatorWindowToSelectedEntity();
}

void EditorApp::QueueHierarchyReveal(uint32_t entityId) {
    Scene& scene = m_Engine->GetScene();
    Entity* current = scene.FindEntity(entityId);
    while (current && current->GetParentID() != 0) {
        const uint32_t parentId = current->GetParentID();
        m_PendingHierarchyExpansionIDs.insert(parentId);
        current = scene.FindEntity(parentId);
    }
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
    QueueHierarchyReveal(m_SelectedEntityID);
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

void EditorApp::CreateEmptyParentForSelection() {
    Scene& scene = m_Engine->GetScene();
    const std::vector<uint32_t> roots =
        GetSelectedHierarchyRootIDs(m_SelectedEntityID);
    if (roots.empty())
        return;

    std::vector<Entity*> children;
    children.reserve(roots.size());
    glm::vec3 worldCenter(0.0f);
    for (uint32_t id : roots) {
        Entity* child = scene.FindEntity(id);
        if (!child)
            continue;
        children.push_back(child);
        worldCenter += scene.GetWorldPosition(*child);
    }
    if (children.empty())
        return;
    worldCenter /= static_cast<float>(children.size());

    uint32_t commonParentId = children.front()->GetParentID();
    for (Entity* child : children) {
        if (child->GetParentID() != commonParentId) {
            commonParentId = 0;
            break;
        }
    }
    Entity* commonParent = scene.FindEntity(commonParentId);
    Entity* firstChild = children.front();

    RecordUndoSnapshot();
    Entity* emptyParent = scene.CreateEntity("Empty Parent");
    if (commonParent)
        scene.SetParent(emptyParent, commonParent);
    scene.SetWorldPosition(*emptyParent, worldCenter);

    // Keep the group where the first selected root used to appear instead of
    // appending it at the bottom of the hierarchy.
    scene.ReorderEntity(emptyParent, commonParent, firstChild, false, true);

    for (Entity* child : children)
        scene.SetParent(child, emptyParent, true);

    SelectSingleEntity(emptyParent->GetID());
    m_Engine->GetMipsRuntime().SyncEditSnapshot(scene);
    m_SceneDirty = true;
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
    glm::mat4 centerMatrix = entityWorld;
    for (int column = 0; column < 3; ++column) {
        glm::vec3 axis(centerMatrix[column]);
        const float axisLength = glm::length(axis);
        centerMatrix[column] = glm::vec4(
            axisLength > 1e-6f ? axis / axisLength
                               : glm::vec3(column == 0, column == 1, column == 2),
            0.0f);
    }
    centerMatrix[3] = glm::vec4(worldCenter, 1.0f);

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
        m_GizmoMode,
        matrix);
    const bool gizmoUsing = ImGuizmo::IsUsing();
    const bool gizmoStarted = gizmoUsing && !wasSubGizmoUsing && !gizmoUsingBefore;
    const bool gizmoEnded = !gizmoUsing && wasSubGizmoUsing;
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

    if (gizmoEnded && RecenterProModelerPivot(*selected, *pb)) {
        pb->RebuildMesh(*meshRenderer);
        m_Engine->GetMipsRuntime().SyncEditSnapshot(scene);
        m_SceneDirty = true;
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

bool EditorApp::RecenterProModelerPivot(Entity& entity,
                                        ProModelerComponent& proModeler) {
    if (proModeler.vertices.empty())
        return false;
    auto* transform = entity.GetComponent<TransformComponent>();
    if (!transform)
        return false;

    glm::vec3 boundsMin = proModeler.vertices.front().position;
    glm::vec3 boundsMax = boundsMin;
    for (const ProModelerVertex& vertex : proModeler.vertices) {
        boundsMin = glm::min(boundsMin, vertex.position);
        boundsMax = glm::max(boundsMax, vertex.position);
    }
    const glm::vec3 localCenter = (boundsMin + boundsMax) * 0.5f;
    const float epsilon = std::max(1e-5f, glm::length(boundsMax - boundsMin) * 1e-6f);
    if (glm::length(localCenter) <= epsilon)
        return false;

    // Move the Transform to the geometric center, then offset all local data
    // by the inverse amount. The rendered geometry remains exactly where it
    // was while the entity origin becomes useful again.
    const glm::mat4 localLinear =
        TransformComponent::RotationMatrixFromEuler(transform->rotation) *
        glm::scale(glm::mat4(1.0f), transform->scale);
    transform->position += glm::vec3(localLinear * glm::vec4(localCenter, 0.0f));
    for (ProModelerVertex& vertex : proModeler.vertices)
        vertex.position -= localCenter;

    if (auto* collider = entity.GetComponent<ColliderComponent>())
        collider->center -= localCenter;

    // Re-centering a parent's pivot must not drag its existing children away
    // from the geometry they were attached to.
    Scene& scene = m_Engine->GetScene();
    for (const auto& childPtr : scene.GetEntities()) {
        if (!childPtr || childPtr->GetParentID() != entity.GetID())
            continue;
        if (auto* childTransform = childPtr->GetComponent<TransformComponent>())
            childTransform->position -= localCenter;
    }
    return true;
}

bool EditorApp::DeleteSelectedProModelerFaces() {
    Entity* selected = GetSelectedEntity();
    if (!selected || selected->GetID() != m_ProModelerSelectionEntityID)
        return false;
    auto* proModeler = selected->GetComponent<ProModelerComponent>();
    auto* meshRenderer = selected->GetComponent<MeshRendererComponent>();
    if (!proModeler || !meshRenderer || m_ProModelerSelectedFaceTriangles.empty())
        return false;

    RecordUndoSnapshot();
    if (!proModeler->DeleteFaces(m_ProModelerSelectedFaceTriangles))
        return false;

    RecenterProModelerPivot(*selected, *proModeler);
    proModeler->RebuildMesh(*meshRenderer);
    ClearProModelerSelection();
    m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
    m_SceneDirty = true;
    return true;
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

    // Repair existing off-center ProModeler pivots as soon as the object is
    // selected in object mode. Sub-object edits recenter when committed.
    if (m_ProModelerSelectedVertices.empty() &&
        RecenterProModelerPivot(*selectedEntity, *proModelerComp)) {
        if (auto* meshRenderer = selectedEntity->GetComponent<MeshRendererComponent>())
            proModelerComp->RebuildMesh(*meshRenderer);
        m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
        m_SceneDirty = true;
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
    const char* shapeNames[] = { "Box", "Plane", "Ramp", "Stairs", "Custom", "Cylinder" };
    int shape = static_cast<int>(proModelerComp->shape);
    if (ImGui::Combo("Preset##combo", &shape, shapeNames, IM_ARRAYSIZE(shapeNames))) {
        RecordUndoSnapshot();
        proModelerComp->shape = static_cast<ProModelerComponent::Shape>(std::clamp(shape, 0, 5));
        if      (proModelerComp->shape == ProModelerComponent::Shape::Plane)  proModelerComp->ResetPlane();
        else if (proModelerComp->shape == ProModelerComponent::Shape::Ramp)   proModelerComp->ResetRamp();
        else if (proModelerComp->shape == ProModelerComponent::Shape::Stairs) proModelerComp->ResetStairs();
        else if (proModelerComp->shape == ProModelerComponent::Shape::Cylinder) proModelerComp->ResetCylinder();
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
    if (proModelerComp->shape == ProModelerComponent::Shape::Cylinder) {
        if (ImGui::SliderInt("Sides", &proModelerComp->sides, 3, 64)) changed = true;
        if (ImGui::IsItemActivated()) RecordUndoSnapshot();
    }

    // Shape preset icon buttons
    const char* shapeTooltips[] = { "Box", "Plane", "Ramp", "Stairs", "Cylinder" };
    const ProModelerComponent::Shape shapeButtons[] = {
        ProModelerComponent::Shape::Box,
        ProModelerComponent::Shape::Plane,
        ProModelerComponent::Shape::Ramp,
        ProModelerComponent::Shape::Stairs,
        ProModelerComponent::Shape::Cylinder,
    };
    int activeShapeButton = -1;
    for (int i = 0; i < IM_ARRAYSIZE(shapeButtons); ++i)
        if (proModelerComp->shape == shapeButtons[i]) activeShapeButton = i;
    for (int i = 0; i < IM_ARRAYSIZE(shapeButtons); ++i) {
        if (i > 0) ImGui::SameLine();
        char id[32]; std::snprintf(id, sizeof(id), "##shape_%d", i);
        if (drawIconButton(id, i, activeShapeButton, 1, shapeTooltips[i])) {
            RecordUndoSnapshot();
            proModelerComp->shape = shapeButtons[i];
            if      (i == 0) proModelerComp->ResetBox();
            else if (i == 1) proModelerComp->ResetPlane();
            else if (i == 2) proModelerComp->ResetRamp();
            else if (i == 3) proModelerComp->ResetStairs();
            else if (i == 4) proModelerComp->ResetCylinder();
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
    if (ImGui::Button("Delete Selected Faces", ImVec2(-1.0f, 0.0f))) {
        DeleteSelectedProModelerFaces();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Delete selected polygon faces (Delete)");
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
    ImGui::DragFloat("Bevel Amount", &proModelerComp->bevelAmount,
                     0.01f, 0.001f, 64.0f, "%.3f");
    ImGui::TextDisabled("Select one edge for a simple one-segment bevel.");
    ImGui::BeginDisabled(m_ProModelerSelectedEdges.size() != 1);
    if (ImGui::Button("Bevel Selected Edge", ImVec2(-1.0f, 0.0f))) {
        RecordUndoSnapshot();
        std::vector<std::pair<uint32_t, uint32_t>> beveledEdges;
        if (proModelerComp->BevelEdge(m_ProModelerSelectedEdges.front(),
                                      proModelerComp->bevelAmount,
                                      m_ProModelerSelectedVertices,
                                      beveledEdges)) {
            m_ProModelerSelectedEdges = std::move(beveledEdges);
            m_ProModelerSelectedFaceTriangles.clear();
            m_ProModelerSelectionIsExtrudedCap = false;
            rebuild = true;
        }
    }
    ImGui::EndDisabled();

    ImGui::TextDisabled("Select two opposite edges with Ctrl/Shift-click for a loop.");
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
        else if (proModelerComp->shape == ProModelerComponent::Shape::Cylinder) proModelerComp->ResetCylinder();
        else                                                                   proModelerComp->ResetBox();
        rebuild = true;
    }

    if (rebuild) {
        auto* mr = selectedEntity->GetComponent<MeshRendererComponent>();
        if (!mr)
            mr = &selectedEntity->AddComponent<MeshRendererComponent>();
        proModelerComp->steps = std::clamp(proModelerComp->steps, 1, 32);
        RecenterProModelerPivot(*selectedEntity, *proModelerComp);
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
    if (!ImGui::Begin("Scene View")) {
        // A docked tab that is not selected still receives Begin/End calls.
        // Match Godot CanvasItem visibility culling and avoid rendering its
        // framebuffer, gizmos and picking pass until it becomes visible.
        m_SceneViewHovered = false;
        m_SceneViewFocused = false;
        ImGui::End();
        ImGui::PopStyleVar();
        return;
    }

    m_SceneViewFocused =
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    DrawGizmoToolbar();

    ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
    if (viewportPanelSize.x > 0.0f && viewportPanelSize.y > 0.0f) {
        m_LastSceneViewAspect = viewportPanelSize.x / viewportPanelSize.y;
        m_SceneCamera.SetViewportAspect(m_LastSceneViewAspect);
        m_Engine->GetRenderer().SetRenderMode(m_SceneRenderMode);

        RenderSceneToFramebuffer(m_SceneViewFBO, m_SceneCamera.GetCamera(), viewportPanelSize,
                                 m_SelectedEntityID, 0, true, m_GameViewSettings.renderWidth,
                                 m_GameViewSettings.renderHeight);

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
        DrawMeshSubdivisionPreview(imageMin, imageSize, m_SceneCamera.GetCamera());
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

            // The framebuffer image is drawn directly and is not an ImGui
            // item, so clicking it did not reliably transfer keyboard focus
            // away from the Hierarchy dock. Make the Scene View authoritative
            // immediately so W/E/R, F, Delete and Ctrl+D work on the entity
            // selected by this same click.
            ImGui::FocusWindow(ImGui::GetCurrentWindow());
            m_SceneViewFocused = true;

            bool gizmoBlocks = m_SelectedEntityID != 0 && (ImGuizmo::IsOver() || ImGuizmo::IsUsing());

            if (!gizmoBlocks) {
                Entity* picked = PickEntityAtPoint(
                    m_Engine->GetScene(),
                    m_SceneCamera.GetCamera(),
                    io.MousePos.x, io.MousePos.y,
                    imageMin.x, imageMin.y,
                    imageSize.x, imageSize.y,
                    m_GameViewSettings.renderWidth,
                    m_GameViewSettings.renderHeight,
                    m_SelectedEntityID);
                const uint32_t pickedId = picked ? picked->GetID() : 0;
                const bool ctrlSelect = io.KeyCtrl;
                if (pickedId != m_SelectedEntityID)
                    ClearProModelerSelection();
                if (ctrlSelect && pickedId != 0) {
                    /* Scene View uses the same selection model as Hierarchy:
                     * Ctrl-click adds/removes an entity and makes newly added
                     * entities the active member of the selection. */
                    HandleHierarchySelection(pickedId);
                } else if (!ctrlSelect) {
                    SelectSingleEntity(pickedId);
                }
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
    // Keep the visual toolbar row while the PS1 Game View remains fixed to 4:3.
    ImGui::Dummy(ImVec2(0.0f, ImGui::GetFrameHeight()));
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
    if (!ImGui::Begin("Game View")) {
        m_GameViewHovered = false;
        m_Engine->GetUIRenderer().SetPointerState(false, 0.0f, 0.0f, false, false, false);
        ImGui::End();
        ImGui::PopStyleVar();
        return;
    }

    DrawGameViewToolbar();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });
    ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
    if (viewportPanelSize.x > 0.0f && viewportPanelSize.y > 0.0f) {
        Scene& scene = m_Engine->GetScene();
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
        if (!e)
            return;
        PlaceNewSceneEntity(*e, parentForNew);
        SelectSingleEntity(e->GetID());
    };

    if (!GetSelectedHierarchyRootIDs(m_SelectedEntityID).empty()) {
        if (ImGui::MenuItem("Create Empty Parent"))
            CreateEmptyParentForSelection();
        ImGui::Separator();
    }

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
                else if (shape == ProModelerComponent::Shape::Cylinder) pb.ResetCylinder();
                else pb.ResetBox();
                pb.RebuildMesh(mr);
                AddProModelerMeshCollider(*entity, mr);
                mr.texture = std::make_shared<Texture>(Texture::CreateCheckerboard(128, 16));
                selectNew(entity);
            };
            if (ImGui::MenuItem("Cube")) createProModeler("ProModeler Cube", ProModelerComponent::Shape::Box);
            if (ImGui::MenuItem("Plane")) createProModeler("ProModeler Plane", ProModelerComponent::Shape::Plane);
            if (ImGui::MenuItem("Cylinder")) createProModeler("ProModeler Cylinder", ProModelerComponent::Shape::Cylinder);
            if (ImGui::MenuItem("Ramp")) createProModeler("ProModeler Ramp", ProModelerComponent::Shape::Ramp);
            if (ImGui::MenuItem("Stairs")) createProModeler("ProModeler Stairs", ProModelerComponent::Shape::Stairs);
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (ImGui::MenuItem("Camera")) {
        RecordUndoSnapshot();
        if (Entity* cam = CreateCamera(parentForNew)) {
            SelectSingleEntity(cam->GetID());
        }
    }

    if (ImGui::MenuItem("Audio Source")) {
        RecordUndoSnapshot();
        auto* audioEntity = scene.CreateEntity("Audio Source");
        audioEntity->AddComponent<AudioSourceComponent>();
        selectNew(audioEntity);
    }

    if (ImGui::BeginMenu("Light")) {
        const glm::vec3 defaultPos = GetSceneViewSpawnPosition();
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
                SelectSingleEntity(canvas->GetID());
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
                SelectSingleEntity(img->GetID());
            }
        }
        if (ImGui::MenuItem("Text", nullptr, false, canvasParent != nullptr)) {
            RecordUndoSnapshot();
            if (Entity* txt = CreateUIText(canvasParent)) {
                SelectSingleEntity(txt->GetID());
            }
        }
        if (ImGui::MenuItem("Button Group")) {
            RecordUndoSnapshot();
            Entity* parent = canvasParent;
            if (!parent)
                parent = CreateUICanvas(parentForNew);
            if (Entity* group = CreateUIButtonGroup(parent)) {
                SelectSingleEntity(group->GetID());
            }
        }
        if (ImGui::MenuItem("Button")) {
            RecordUndoSnapshot();
            if (Entity* button = CreateUIButton(parentForNew)) {
                SelectSingleEntity(button->GetID());
            }
        }
        if (ImGui::MenuItem("Audio Spectrum", nullptr, false, canvasParent != nullptr)) {
            RecordUndoSnapshot();
            if (Entity* spectrum = CreateUIAudioSpectrum(canvasParent)) {
                SelectSingleEntity(spectrum->GetID());
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::MenuItem("First Person Controller")) {
        RecordUndoSnapshot();
        if (Entity* fps = CreateFirstPersonController(parentForNew)) {
            SelectSingleEntity(fps->GetID());
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
    const Camera& sceneCamera = m_SceneCamera.GetCamera();
    const glm::vec3 spawnPosition = GetSceneViewSpawnPosition();
    cameraComp.camera.SetPosition(spawnPosition);
    cameraComp.camera.LookAt(spawnPosition + sceneCamera.GetForward());
    if (auto* tr = cam->GetComponent<TransformComponent>())
        cameraComp.camera.SyncTransformFromCamera(*tr);

    if (cameraComp.primary)
        scene.SetPrimaryCamera(*cam);

    PlaceNewSceneEntity(*cam, parentForNew);

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
    if (DrawAssetReferenceField(this, "Texture", AssetKind::Texture,
                                image.texturePath, "None (Texture)")) {
        RecordUndoSnapshot();
        image.texture = image.texturePath.empty()
            ? nullptr
            : AssetManager::Get().GetTexture(image.texturePath);
        if (image.texture) {
            image.preserveAspect = true;
            FitRectTransformToTextureAspect(image.entity, image.texture.get());
        }
        m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
        m_SceneDirty = true;
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
    PlaceNewSceneEntity(*fps, parentForNew);

    fps->AddComponent<CameraComponent>();
    scene.SetPrimaryCamera(*fps);

    auto& script = fps->AddComponent<MipsScriptComponent>();
    script.scriptPath = "assets/scripts/FirstPersonController.mips";
    ColliderUtils::EnsureFirstPersonPhysics(*fps);

    m_Engine->GetMipsRuntime().SyncEditSnapshot(scene);
    return fps;
}

void EditorApp::DrawHierarchyEntityNode(Entity& entity) {
    Scene& scene = m_Engine->GetScene();

    auto* tagComp = entity.GetComponent<TagComponent>();
    std::string name = tagComp ? tagComp->tag : "Unknown";

    const bool hasChildren = !entity.GetChildIDs().empty();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!hasChildren) {
        flags |= ImGuiTreeNodeFlags_Leaf;
        // Match the Project tree: a leaf has no branch to communicate, so do
        // not draw ImGui's short terminal connector in the otherwise empty
        // foldout gutter.
        flags |= ImGuiTreeNodeFlags_DrawLinesNone;
    }
    if (IsEntitySelected(entity.GetID())) flags |= ImGuiTreeNodeFlags_Selected;

    if (hasChildren && m_PendingHierarchyExpansionIDs.count(entity.GetID()) != 0)
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);

    bool opened = ImGui::TreeNodeEx((void*)(uint64_t)entity.GetID(), flags, "%s", name.c_str());

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
        const ImGuiIO& io = ImGui::GetIO();
        const uint32_t clickedId = entity.GetID();
        if (!IsEntitySelected(clickedId) || io.KeyCtrl || io.KeyShift) {
            /* Make a newly clicked row authoritative on mouse-down.  Waiting
             * for release left the row visually selected while gizmos and
             * other consumers still used the previous primary selection. */
            m_PendingHierarchyClickEntityID = 0;
            HandleHierarchySelection(clickedId);
        } else {
            /* Preserve an existing multi-selection while a drag may start,
             * but immediately make this blue row the primary/active entity. */
            m_SelectedEntityID = clickedId;
            m_SelectedEntityIDs.insert(clickedId);
            if (m_AssetBrowser)
                m_AssetBrowser->ClearAssetSelection();
            SyncAnimatorWindowToSelectedEntity();
            m_PendingHierarchyClickEntityID = clickedId;
        }
    }

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
                SelectSingleEntity(dup->GetID());
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
    m_PendingHierarchyExpansionIDs.clear();

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
                    if (scene.ReorderEntity(child, parent, anchor, true, true)) {
                        reparented = true;
                        anchor = child;
                    }
                }
            } else {
                for (uint32_t cid : m_PendingReparentChildren) {
                    Entity* child = scene.FindEntity(cid);
                    if (child)
                        reparented = scene.ReorderEntity(child, parent, sibling, false, true) || reparented;
                }
            }
        } else {
            for (uint32_t cid : m_PendingReparentChildren) {
                Entity* child = scene.FindEntity(cid);
                if (child)
                    reparented = scene.SetParent(child, parent, true) || reparented;
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
    const fs::path scriptsPath = rootPath / "assets" / "scripts";
    if (!fs::is_directory(scriptsPath, ec))
        return out;

    for (auto it = fs::recursive_directory_iterator(scriptsPath, fs::directory_options::skip_permission_denied, ec);
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

void EditorApp::DrawAddComponentScriptMenu(const std::vector<Entity*>& entities) {
    if (!ImGui::BeginMenu("Scripts"))
        return;

    static char newScriptName[128] = "NewScript";
    constexpr float createScriptControlWidth = 220.0f;
    ImGui::TextDisabled("Create and Add");
    ImGui::SetNextItemWidth(createScriptControlWidth);
    const bool createOnEnter = ImGui::InputTextWithHint(
        "##new_component_script", "Script name", newScriptName,
        sizeof(newScriptName), ImGuiInputTextFlags_EnterReturnsTrue);
    const bool createClicked = ImGui::Button(
        "Create New", ImVec2(createScriptControlWidth, 0.0f));
    if (createOnEnter || createClicked) {
        std::string scriptPath;
        std::string error;
        if (m_AssetBrowser &&
            m_AssetBrowser->CreateScriptAsset(newScriptName, scriptPath, error)) {
            RecordUndoSnapshot();
            for (Entity* entity : entities) {
                auto& script = entity->AddComponent<MipsScriptComponent>();
                script.scriptPath = scriptPath;
                script.module.reset();
                script.fieldValues.clear();
            }
            m_AssetBrowser->RevealAndSelectAsset(scriptPath);
            m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
            m_SceneDirty = true;
            std::strncpy(newScriptName, "NewScript", sizeof(newScriptName) - 1);
            newScriptName[sizeof(newScriptName) - 1] = '\0';
            ImGui::CloseCurrentPopup();
        } else {
            MIPSYNC_WARN("Create New Mips# script failed: {}", error);
        }
    }

    ImGui::SeparatorText("Existing Scripts");

    const auto scripts = CollectProjectScripts();
    if (scripts.empty()) {
        ImGui::TextDisabled("No .mips scripts found");
    } else {
        for (const std::string& path : scripts) {
            if (ImGui::MenuItem(path.c_str())) {
                RecordUndoSnapshot();
                for (Entity* entity : entities) {
                    auto& script = entity->AddComponent<MipsScriptComponent>();
                    script.scriptPath = path;
                    script.module.reset();
                    script.fieldValues.clear();
                }
                m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
                m_SceneDirty = true;
                ImGui::CloseCurrentPopup();
            }
        }
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Empty (set path manually)")) {
        RecordUndoSnapshot();
        for (Entity* entity : entities) {
            auto& script = entity->AddComponent<MipsScriptComponent>();
            script.scriptPath.clear();
        }
        m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
        m_SceneDirty = true;
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

void EditorApp::DrawMipsScriptIdeMenuItems(const std::string& projectRelPath) {
    const bool hasScript = !projectRelPath.empty();
    if (!hasScript)
        ImGui::BeginDisabled();

    if (ImGui::MenuItem("Open in IDE")) {
        if (!MipsEditorIntegration::OpenScriptInIde(projectRelPath)) {
            EDITOR_WARN("[Mips# IDE] Failed to open script in IDE: {}", projectRelPath);
            TriggerBuildToast("Could not open script in IDE", false);
        }
    }
    if (ImGui::MenuItem("Validate")) {
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
            std::vector<Entity*> inspectorEntities;
            inspectorEntities.push_back(selectedEntity);
            for (uint32_t entityId : m_SelectedEntityIDs) {
                Entity* entity = m_Engine->GetScene().FindEntity(entityId);
                if (entity && entity != selectedEntity)
                    inspectorEntities.push_back(entity);
            }
            const bool multiSelection = inspectorEntities.size() > 1;

            ImGui::BeginChild("##unity_object_header", ImVec2(-1.0f, 82.0f),
                              ImGuiChildFlags_Borders);
            const ImVec2 iconMin = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(36.0f, 36.0f));
            DrawInspectorCubeIcon(ImGui::GetWindowDrawList(),
                                  ImVec2(iconMin.x + 18.0f, iconMin.y + 18.0f), 24.0f,
                                  ImGui::GetColorU32(EditorTheme::TextSecondary));
            ImGui::SameLine();
            bool active = selectedEntity->IsActive();
            const bool activeMixed = std::any_of(
                inspectorEntities.begin() + 1, inspectorEntities.end(),
                [&](const Entity* entity) { return entity->IsActive() != active; });
            ImGui::PushID("object_active");
            if (activeMixed)
                ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
            const bool activeChanged = DrawComponentEnabledToggle(active);
            if (activeMixed)
                ImGui::PopItemFlag();
            ImGui::PopID();
            if (activeChanged) {
                RecordUndoSnapshot();
                for (Entity* target : inspectorEntities)
                    target->SetActive(active);
                m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
                m_SceneDirty = true;
            }
            ImGui::SameLine();
            if (auto* tag = selectedEntity->GetComponent<TagComponent>()) {
                char nameBuffer[256]{};
                const bool nameMixed = std::any_of(
                    inspectorEntities.begin() + 1, inspectorEntities.end(),
                    [&](Entity* entity) {
                        auto* other = entity->GetComponent<TagComponent>();
                        return !other || other->tag != tag->tag;
                    });
                strncpy(nameBuffer, nameMixed ? "-" : tag->tag.c_str(), sizeof(nameBuffer) - 1);
                ImGui::SetNextItemWidth(std::max(100.0f, ImGui::GetContentRegionAvail().x - 92.0f));
                if (ImGui::InputText("##object_name", nameBuffer, sizeof(nameBuffer),
                                     nameMixed ? ImGuiInputTextFlags_AutoSelectAll : 0)) {
                    RecordUndoSnapshot();
                    for (Entity* target : inspectorEntities) {
                        if (auto* targetTag = target->GetComponent<TagComponent>())
                            targetTag->tag = nameBuffer;
                    }
                    m_SceneDirty = true;
                }
            }
            ImGui::SameLine();
            bool isStatic = selectedEntity->IsStatic();
            const bool staticMixed = std::any_of(
                inspectorEntities.begin() + 1, inspectorEntities.end(),
                [&](const Entity* entity) { return entity->IsStatic() != isStatic; });
            if (staticMixed)
                ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
            if (EditorTheme::Checkbox("Static", &isStatic)) {
                RecordUndoSnapshot();
                for (Entity* target : inspectorEntities)
                    target->SetStatic(isStatic);
                m_SceneDirty = true;
            }
            if (staticMixed)
                ImGui::PopItemFlag();

            ImGui::SetCursorPosY(47.0f);
            ImGui::TextDisabled("Tag"); ImGui::SameLine(42.0f);
            ImGui::SetNextItemWidth(118.0f);
            const char* tags[] = { "Untagged", "Player", "MainCamera", "EditorOnly" };
            int tagIndex = 0;
            for (int i = 0; i < IM_ARRAYSIZE(tags); ++i)
                if (selectedEntity->GetEditorTag() == tags[i]) tagIndex = i;
            const bool editorTagMixed = std::any_of(
                inspectorEntities.begin() + 1, inspectorEntities.end(),
                [&](const Entity* entity) {
                    return entity->GetEditorTag() != selectedEntity->GetEditorTag();
                });
            if (ImGui::BeginCombo("##object_tag", editorTagMixed ? "-" : tags[tagIndex])) {
                for (int index = 0; index < IM_ARRAYSIZE(tags); ++index) {
                    if (ImGui::Selectable(tags[index], !editorTagMixed && tagIndex == index)) {
                        RecordUndoSnapshot();
                        for (Entity* target : inspectorEntities)
                            target->SetEditorTag(tags[index]);
                        m_SceneDirty = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine(); ImGui::TextDisabled("Layer"); ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0f);
            const char* layers[] = { "Default", "TransparentFX", "Ignore Raycast", "UI" };
            int layerIndex = 0;
            for (int i = 0; i < IM_ARRAYSIZE(layers); ++i)
                if (selectedEntity->GetEditorLayer() == layers[i]) layerIndex = i;
            const bool editorLayerMixed = std::any_of(
                inspectorEntities.begin() + 1, inspectorEntities.end(),
                [&](const Entity* entity) {
                    return entity->GetEditorLayer() != selectedEntity->GetEditorLayer();
                });
            if (ImGui::BeginCombo("##object_layer", editorLayerMixed ? "-" : layers[layerIndex])) {
                for (int index = 0; index < IM_ARRAYSIZE(layers); ++index) {
                    if (ImGui::Selectable(layers[index], !editorLayerMixed && layerIndex == index)) {
                        RecordUndoSnapshot();
                        for (Entity* target : inspectorEntities)
                            target->SetEditorLayer(layers[index]);
                        m_SceneDirty = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::EndChild();
            ImGui::Spacing();

            // 隨渉隨渉 Prefab header 隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉
            if (!multiSelection && selectedEntity->IsPrefabInstance()) {
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
                        SelectSingleEntity(fresh->GetID());
                    else
                        MIPSYNC_WARN("Revert Prefab failed: {}", err);
                }
                ImGui::Spacing();
                ImGui::EndChild();
                DrawInspectorDivider();
            }
            // 隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉隨渉

            bool removeComponent = false;
            for (Component* orderedComponent : selectedEntity->GetComponentsInOrder()) {
            const size_t occurrence = InspectorComponentOccurrence(*selectedEntity, *orderedComponent);
            std::vector<Component*> peerComponents;
            peerComponents.reserve(inspectorEntities.size() - 1);
            bool componentIsCommon = true;
            for (size_t entityIndex = 1; entityIndex < inspectorEntities.size(); ++entityIndex) {
                Component* peer = FindMatchingInspectorComponent(
                    *inspectorEntities[entityIndex], *orderedComponent, occurrence);
                if (!peer) {
                    componentIsCommon = false;
                    break;
                }
                peerComponents.push_back(peer);
            }
            if (!componentIsCommon)
                continue;
            BeginInspectorMultiEdit(*orderedComponent, std::move(peerComponents));

            auto* transformComp = dynamic_cast<TransformComponent*>(orderedComponent);
            if (transformComp && !selectedEntity->GetComponent<RectTransformComponent>()) {
                if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                    if (InspectorDragFloat3("Position", glm::value_ptr(transformComp->position), 0.1f) ||
                        InspectorDragFloat3("Rotation", glm::value_ptr(transformComp->rotation), 0.1f) ||
                        InspectorDragFloat3("Scale", glm::value_ptr(transformComp->scale), 0.1f)) {
                        for (Entity* target : inspectorEntities) {
                            SyncSelectedCameraFromTransform(target);
                            SyncPhysicsAfterTransformEdit(target);
                        }
                        m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
                        m_SceneDirty = true;
                    }
                }
                DrawInspectorDivider();
                DrawInspectorDivider();
            }

            auto* cameraComp = dynamic_cast<CameraComponent*>(orderedComponent);
            if (cameraComp) {
                const bool open = BeginInspectableComponent(
                    "Camera", *cameraComp, removeComponent, this, [&] {
                        if (ImGui::MenuItem("Clear Prerendered Background", nullptr, false,
                                            !cameraComp->prerenderedBackgroundPath.empty())) {
                            cameraComp->prerenderedBackgroundPath.clear();
                            m_PrerenderedBackgroundTextures.clear();
                            m_SceneDirty = true;
                        }
                    });
                if (!removeComponent && open) {
                    bool cameraChanged = false;
                    if (InspectorCheckbox("Primary", &cameraComp->primary)) {
                        if (cameraComp->primary)
                            m_Engine->GetScene().SetPrimaryCamera(*selectedEntity);
                        cameraChanged = true;
                    }
                    cameraChanged |= InspectorDragFloat("Field of View", &cameraComp->camera.fov, 0.5f, 15.0f, 120.0f);
                    cameraChanged |= InspectorDragFloat("Near Clip", &cameraComp->camera.nearClip, 0.01f, 0.01f, 10.0f);
                    cameraChanged |= InspectorDragFloat("Far Clip (Draw Dist)", &cameraComp->camera.farClip, 0.5f, 1.0f, 500.0f);

                    ImGui::Spacing();
                    ImGui::TextDisabled("Far Clip Presets:");
                    ImGui::SameLine();
                    if (ImGui::SmallButton("20m (PS1 Low)")) { cameraComp->camera.farClip = 20.0f; cameraChanged = true; }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("35m (PS1 Mid)")) { cameraComp->camera.farClip = 35.0f; cameraChanged = true; }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("60m (PS1 High)")) { cameraComp->camera.farClip = 60.0f; cameraChanged = true; }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("100m")) { cameraComp->camera.farClip = 100.0f; cameraChanged = true; }

                    char bgBuffer[512]{};
                    strncpy(bgBuffer, cameraComp->prerenderedBackgroundPath.c_str(), sizeof(bgBuffer) - 1);
                    if (InspectorInputText("Prerender BG", bgBuffer, sizeof(bgBuffer))) {
                        m_PrerenderedBackgroundTextures.clear();
                        m_SceneDirty = true;
                    }
                    if (InspectorInputInt("Shot Priority", &cameraComp->shotPriority))
                        m_SceneDirty = true;
                    if (cameraChanged)
                        SyncSelectedCameraFromTransform(selectedEntity);
                }
                EndInspectableComponent(*cameraComp, open);
                if (removeComponent) {
                    RemoveCurrentInspectorComponents(*selectedEntity, cameraComp);
                    ImGui::End();
                    return;
                }
            }

            auto* distanceCullComp = dynamic_cast<DistanceCullComponent*>(orderedComponent);
            if (distanceCullComp) {
                const bool open = BeginInspectableComponent("Distance Cull", *distanceCullComp, removeComponent, this);
                if (!removeComponent && open) {
                    bool changed = false;
                    changed |= InspectorCheckbox("Enabled", &distanceCullComp->enabled);
                    changed |= InspectorDragFloat("Cull Distance", &distanceCullComp->cullDistance, 0.5f, 1.0f, 200.0f);

                    ImGui::Spacing();
                    ImGui::TextDisabled("Distance Presets:");
                    ImGui::SameLine();
                    if (ImGui::SmallButton("15m (Close)")) { distanceCullComp->cullDistance = 15.0f; changed = true; }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("30m (Normal)")) { distanceCullComp->cullDistance = 30.0f; changed = true; }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("50m (Far)")) { distanceCullComp->cullDistance = 50.0f; changed = true; }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("80m (Max)")) { distanceCullComp->cullDistance = 80.0f; changed = true; }

                    ImGui::Spacing();
                    std::string targetLabel = "Self (This Entity / Camera)";
                    if (distanceCullComp->targetEntityId != 0) {
                        if (Entity* target = m_Engine->GetScene().FindEntity(distanceCullComp->targetEntityId)) {
                            if (auto* tag = target->GetComponent<TagComponent>())
                                targetLabel = tag->tag;
                            else
                                targetLabel = "Entity #" + std::to_string(distanceCullComp->targetEntityId);
                        } else {
                            targetLabel = "Missing (ID: " + std::to_string(distanceCullComp->targetEntityId) + ")";
                        }
                    }
                    if (ImGui::BeginCombo("Target Center", targetLabel.c_str())) {
                        if (ImGui::Selectable("Self (This Entity / Camera)", distanceCullComp->targetEntityId == 0)) {
                            distanceCullComp->targetEntityId = 0;
                            changed = true;
                        }
                        for (auto& entityPtr : m_Engine->GetScene().GetEntities()) {
                            if (!entityPtr || entityPtr.get() == selectedEntity)
                                continue;
                            std::string name = "Entity #" + std::to_string(entityPtr->GetID());
                            if (auto* tag = entityPtr->GetComponent<TagComponent>())
                                name = tag->tag;
                            const bool isPicked = (distanceCullComp->targetEntityId == entityPtr->GetID());
                            if (ImGui::Selectable(name.c_str(), isPicked)) {
                                distanceCullComp->targetEntityId = entityPtr->GetID();
                                changed = true;
                            }
                        }
                        ImGui::EndCombo();
                    }

                    changed |= InspectorCheckbox("Cull Static Meshes", &distanceCullComp->cullMeshRenderers);
                    changed |= InspectorCheckbox("Cull Skinned Meshes", &distanceCullComp->cullSkinnedMeshes);
                    changed |= InspectorCheckbox("Cull Lights", &distanceCullComp->cullLights);
                    changed |= InspectorCheckbox("Preview in Scene View", &distanceCullComp->previewInEditor);

                    if (changed)
                        m_SceneDirty = true;
                }
                EndInspectableComponent(*distanceCullComp, open);
                if (removeComponent) {
                    RemoveCurrentInspectorComponents(*selectedEntity, distanceCullComp);
                    ImGui::End();
                    return;
                }
            }

            auto* frustumCullComp = dynamic_cast<FrustumCullComponent*>(orderedComponent);
            if (frustumCullComp) {
                const bool open = BeginInspectableComponent("Frustum Cull", *frustumCullComp, removeComponent, this);
                if (!removeComponent && open) {
                    bool changed = false;
                    changed |= InspectorCheckbox("Enabled", &frustumCullComp->enabled);
                    changed |= InspectorDragFloat("Margin", &frustumCullComp->margin, 0.1f, 0.0f, 10.0f);

                    ImGui::Spacing();
                    ImGui::TextDisabled("Margin Presets:");
                    ImGui::SameLine();
                    if (ImGui::SmallButton("0.5m (Tight)")) { frustumCullComp->margin = 0.5f; changed = true; }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("1.5m (Standard)")) { frustumCullComp->margin = 1.5f; changed = true; }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("3.0m (Safe)")) { frustumCullComp->margin = 3.0f; changed = true; }

                    ImGui::Spacing();
                    changed |= InspectorCheckbox("Cull Static Meshes", &frustumCullComp->cullMeshRenderers);
                    changed |= InspectorCheckbox("Cull Skinned Meshes", &frustumCullComp->cullSkinnedMeshes);
                    changed |= InspectorCheckbox("Cull Lights", &frustumCullComp->cullLights);
                    changed |= InspectorCheckbox("Preview in Scene View", &frustumCullComp->previewInEditor);

                    if (changed)
                        m_SceneDirty = true;
                }
                EndInspectableComponent(*frustumCullComp, open);
                if (removeComponent) {
                    RemoveCurrentInspectorComponents(*selectedEntity, frustumCullComp);
                    ImGui::End();
                    return;
                }
            }

            auto* lightComp = dynamic_cast<LightComponent*>(orderedComponent);
            if (lightComp) {
                removeComponent = false;
                const bool open = BeginInspectableComponent("Light", *lightComp, removeComponent, this);
                if (!removeComponent && open) {
                    const char* typeNames[] = { "Directional", "Point", "Spot" };
                    int typeIdx = static_cast<int>(lightComp->type);
                    if (InspectorCombo("Type", &typeIdx, typeNames, IM_ARRAYSIZE(typeNames),
                                       InspectorValuesMixed(&lightComp->type))) {
                        lightComp->type = static_cast<LightType>(typeIdx);
                        PropagateInspectorValues(&lightComp->type);
                    }
                    if (InspectorColorEdit3("Color", glm::value_ptr(lightComp->color)))
                        { /* live */ }
                    if (InspectorDragFloat("Intensity", &lightComp->intensity, 0.05f, 0.0f, 10.0f))
                        { /* live */ }
                    if (lightComp->type != LightType::Directional) {
                        if (InspectorDragFloat("Range", &lightComp->range, 0.1f, 0.1f, 200.0f))
                            { /* live */ }
                    }
                    if (lightComp->type == LightType::Spot) {
                        if (InspectorDragFloat("Spot Angle", &lightComp->spotAngle, 0.5f, 1.0f, 89.0f))
                            lightComp->spotInnerAngle = std::min(lightComp->spotInnerAngle, lightComp->spotAngle);
                        if (InspectorDragFloat("Inner Angle", &lightComp->spotInnerAngle, 0.5f, 1.0f,
                                              lightComp->spotAngle))
                            lightComp->spotInnerAngle = std::min(lightComp->spotInnerAngle, lightComp->spotAngle);
                    }
                    ImGui::TextColored(EditorTheme::TextMuted,
                        "Rotation aims the light (local -Z forward).");
                }
                EndInspectableComponent(*lightComp, open);
                if (removeComponent) {
                    RemoveCurrentInspectorComponents(*selectedEntity, lightComp);
                    ImGui::End();
                    return;
                }
            }

            auto* postProcess = dynamic_cast<PostProcessVolumeComponent*>(orderedComponent);
            if (postProcess) {
                removeComponent = false;
                const bool open = BeginInspectableComponent("Post Process Volume", *postProcess,
                                                            removeComponent, this);
                if (!removeComponent && open) {
                    if (InspectorCheckbox("Is Global", &postProcess->isGlobal))
                        RecordUndoSnapshot();
                    if (InspectorDragInt("Priority", &postProcess->priority, 1.0f, -100, 100))
                        RecordUndoSnapshot();
                    ImGui::TextColored(EditorTheme::TextMuted,
                        "Highest enabled priority volume drives the scene post-process.");

                    if (ImGui::CollapsingHeader("HDRI Skybox", ImGuiTreeNodeFlags_DefaultOpen)) {
                        if (InspectorCheckbox("Enable Skybox", &postProcess->skyboxEnabled))
                            RecordUndoSnapshot();
                        ImGui::BeginDisabled(!postProcess->skyboxEnabled);
                        if (DrawAssetReferenceField(this, "HDRI Texture", AssetKind::Texture,
                                                    postProcess->skyboxTexturePath, "None (Texture)")) {
                            RecordUndoSnapshot();
                            postProcess->skyboxTexture = postProcess->skyboxTexturePath.empty()
                                ? nullptr
                                : AssetManager::Get().GetSkyboxTexture(postProcess->skyboxTexturePath);
                        }
                        if (InspectorDragFloat("Rotation Y", &postProcess->skyboxRotationDegrees,
                                             0.5f, -360.0f, 360.0f, "%.1f deg"))
                            RecordUndoSnapshot();
                        if (InspectorSliderFloat("Skybox Exposure", &postProcess->skyboxExposure, -4.0f, 4.0f))
                            RecordUndoSnapshot();
                        if (InspectorColorEdit3("Skybox Tint", glm::value_ptr(postProcess->skyboxTint)))
                            RecordUndoSnapshot();
                        ImGui::EndDisabled();
                    }

                    if (ImGui::CollapsingHeader("Fog", ImGuiTreeNodeFlags_DefaultOpen)) {
                        if (InspectorCheckbox("Enable Fog", &postProcess->fogEnabled))
                            RecordUndoSnapshot();
                        ImGui::BeginDisabled(!postProcess->fogEnabled);
                        if (InspectorColorEdit3("Fog Color", glm::value_ptr(postProcess->fogColor)))
                            RecordUndoSnapshot();
                        if (InspectorDragFloat("Fog Start", &postProcess->fogStart, 0.1f, 0.0f, 10000.0f))
                            RecordUndoSnapshot();
                        if (InspectorDragFloat("Fog End", &postProcess->fogEnd, 0.1f, 0.1f, 10000.0f)) {
                            postProcess->fogEnd = std::max(postProcess->fogEnd, postProcess->fogStart + 0.1f);
                            RecordUndoSnapshot();
                        }
                        ImGui::EndDisabled();
                    }

                    if (ImGui::CollapsingHeader("Color Grading", ImGuiTreeNodeFlags_DefaultOpen)) {
                        if (InspectorCheckbox("Enable Color Grading", &postProcess->colorGradingEnabled))
                            RecordUndoSnapshot();
                        ImGui::BeginDisabled(!postProcess->colorGradingEnabled);
                        if (InspectorSliderFloat("Exposure", &postProcess->exposure, -4.0f, 4.0f))
                            RecordUndoSnapshot();
                        if (InspectorSliderFloat("Contrast", &postProcess->contrast, 0.0f, 3.0f))
                            RecordUndoSnapshot();
                        if (InspectorSliderFloat("Saturation", &postProcess->saturation, 0.0f, 3.0f))
                            RecordUndoSnapshot();
                        if (InspectorColorEdit3("Color Filter", glm::value_ptr(postProcess->colorFilter)))
                            RecordUndoSnapshot();
                        ImGui::EndDisabled();
                    }

                    if (ImGui::CollapsingHeader("Vignette", ImGuiTreeNodeFlags_DefaultOpen)) {
                        if (InspectorCheckbox("Enable Vignette", &postProcess->vignetteEnabled))
                            RecordUndoSnapshot();
                        ImGui::BeginDisabled(!postProcess->vignetteEnabled);
                        if (InspectorColorEdit3("Vignette Color", glm::value_ptr(postProcess->vignetteColor)))
                            RecordUndoSnapshot();
                        if (InspectorSliderFloat("Intensity", &postProcess->vignetteIntensity, 0.0f, 1.0f))
                            RecordUndoSnapshot();
                        if (InspectorSliderFloat("Smoothness", &postProcess->vignetteSmoothness, 0.01f, 2.0f))
                            RecordUndoSnapshot();
                        ImGui::EndDisabled();
                    }
                }
                EndInspectableComponent(*postProcess, open);
                if (removeComponent) {
                    RemoveCurrentInspectorComponents(*selectedEntity, postProcess);
                    ImGui::End();
                    return;
                }
            }

            auto* skinnedComp = dynamic_cast<SkinnedMeshRendererComponent*>(orderedComponent);
            if (skinnedComp) {
                removeComponent = false;
                const bool open = BeginInspectableComponent(
                    "Skinned Mesh Renderer", *skinnedComp, removeComponent, this, [&] {
                        const bool hasModel = !skinnedComp->modelPath.empty();
                        if (ImGui::MenuItem("Reload Model", nullptr, false, hasModel)) {
                            RecordUndoSnapshot();
                            skinnedComp->SetModelFile(skinnedComp->modelPath);
                            ApplySkeletalModelFitScale(*selectedEntity, skinnedComp->modelPath);
                            if (auto* anim = selectedEntity->GetComponent<AnimatorComponent>()) {
                                if (anim->modelPath.empty())
                                    anim->modelPath = skinnedComp->modelPath;
                                anim->ReloadAssets();
                            }
                        }
                        if (ImGui::MenuItem("Fit Scale", nullptr, false, hasModel)) {
                            RecordUndoSnapshot();
                            ApplySkeletalModelFitScale(*selectedEntity, skinnedComp->modelPath);
                        }
                        if (ImGui::MenuItem("Save Material", nullptr, false,
                                            !skinnedComp->materialPath.empty())) {
                            Material mat;
                            mat.color = skinnedComp->color;
                            mat.texturePath = skinnedComp->texturePath;
                            mat.mainTextureTiling = skinnedComp->textureTiling;
                            mat.mainTextureOffset = skinnedComp->textureOffset;
                            std::string err;
                            if (Material::Save(AssetManager::Get().ToAbsolute(
                                                   skinnedComp->materialPath), mat, err)) {
                                AssetThumbnail::Get().DropMaterialThumbnail(
                                    skinnedComp->materialPath);
                            } else {
                                MIPSYNC_WARN("Material save failed: {}", err);
                            }
                        }
                    });
                if (!removeComponent && open) {
                    InspectorColorEdit4("Color", glm::value_ptr(skinnedComp->color));
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
                        if (indexCount == 0)
                            ImGui::TextColored(EditorTheme::Error, "Skinned mesh has 0 indices");
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
                    if (InspectorCombo("PS1 Mode", &ps1Mode, ps1Modes, IM_ARRAYSIZE(ps1Modes),
                                       InspectorValuesMixed(&skinnedComp->ps1ExportMode))) {
                        RecordUndoSnapshot();
                        skinnedComp->ps1ExportMode =
                            static_cast<SkinnedMeshRendererComponent::Ps1ExportMode>(std::clamp(ps1Mode, 0, 2));
                        PropagateInspectorValues(&skinnedComp->ps1ExportMode);
                        m_SceneDirty = true;
                    }
                    if (skinnedComp->ps1ExportMode == SkinnedMeshRendererComponent::Ps1ExportMode::Off) {
                        ImGui::TextColored(EditorTheme::TextMuted,
                                           "Skipped in PS1 builds. Use this for high-poly editor-only Mixamo models.");
                    } else {
                        ImGui::TextColored(EditorTheme::PsAccent,
                                           "PS1 uses rigid bone parts by default; no runtime skinning.");
                        bool ps1AnimChanged = false;
                        ps1AnimChanged |= InspectorDragInt("PS1 Anim FPS", &skinnedComp->ps1VertexAnimFps,
                                                         1.0f, 1, 30);
                        ps1AnimChanged |= InspectorDragInt("PS1 Max Frames", &skinnedComp->ps1VertexAnimMaxFrames,
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
                EndInspectableComponent(*skinnedComp, open);
                if (removeComponent) {
                    RemoveCurrentInspectorComponents(*selectedEntity, skinnedComp);
                    ImGui::End();
                    return;
                }
            }

            auto* audioSource = dynamic_cast<AudioSourceComponent*>(orderedComponent);
            if (audioSource) {
                removeComponent = false;
                const bool open = BeginInspectableComponent("Audio Source", *audioSource,
                                                            removeComponent, this);
                if (!removeComponent && open) {
                    if (DrawAssetReferenceField(this, "Audio Clip", AssetKind::Audio,
                                                audioSource->clipPath, "None (Audio Clip)"))
                        RecordUndoSnapshot();
                    if (InspectorCheckbox("Play On Awake", &audioSource->playOnAwake))
                        RecordUndoSnapshot();
                    if (InspectorCheckbox("Loop", &audioSource->loop))
                        RecordUndoSnapshot();
                    if (InspectorCheckbox("Mute", &audioSource->mute))
                        RecordUndoSnapshot();
                    if (InspectorSliderFloat("Volume", &audioSource->volume, 0.0f, 1.0f))
                        RecordUndoSnapshot();
                }
                EndInspectableComponent(*audioSource, open);
                if (removeComponent) {
                    RemoveCurrentInspectorComponents(*selectedEntity, audioSource);
                    ImGui::End();
                    return;
                }
            }

            auto* animatorComp = dynamic_cast<AnimatorComponent*>(orderedComponent);
            if (animatorComp) {
                removeComponent = false;
                auto* animatorSkinnedComp =
                    selectedEntity->GetComponent<SkinnedMeshRendererComponent>();
                const bool open = BeginInspectableComponent(
                    "Animator", *animatorComp, removeComponent, this, [&] {
                        if (ImGui::MenuItem("Reload Assets"))
                            animatorComp->ReloadAssets();

                        std::string modelForController = animatorComp->modelPath;
                        if (modelForController.empty() && animatorSkinnedComp)
                            modelForController = animatorSkinnedComp->modelPath;
                        if (ImGui::MenuItem("Create from Model Clips", nullptr, false,
                                            !modelForController.empty())) {
                            CreateAndAssignControllerFromModel(
                                this, *animatorComp, modelForController, animatorSkinnedComp);
                        }
                        if (ImGui::MenuItem("Open Controller Window", nullptr, false,
                                            !animatorComp->controllerPath.empty())) {
                            OpenAnimatorControllerWindow(animatorComp->controllerPath);
                        }
                    });
                if (!removeComponent && open) {
                    if (InspectorDragFloat("Playback Speed", &animatorComp->speed, 0.01f, 0.0f, 10.0f))
                        RecordUndoSnapshot();
                    if (InspectorDragFloat("Animation FPS", &animatorComp->animationFps, 0.5f, 1.0f,
                                         120.0f))
                        RecordUndoSnapshot();

                    if (DrawAssetReferenceField(this, "Controller", AssetKind::AnimatorController,
                                                animatorComp->controllerPath, "None (Controller)")) {
                        RecordUndoSnapshot();
                        animatorComp->ReloadAssets();
                        if (!animatorComp->controllerPath.empty())
                            OpenAnimatorControllerWindow(animatorComp->controllerPath);
                    }

                    if (animatorComp->controller && !animatorComp->controller->parameters.empty()) {
                        ImGui::Separator();
                        ImGui::TextColored(EditorTheme::TextSecondary,
                                           "Controller Parameters (for transitions)");
                        for (const auto& p : animatorComp->controller->parameters) {
                            if (p.type == AnimatorParamType::Float) {
                                float v = animatorComp->parameters.floats.count(p.name)
                                    ? animatorComp->parameters.floats[p.name]
                                    : p.defaultFloat;
                                if (InspectorDragFloat(p.name.c_str(), &v, 0.01f, -100.0f, 100.0f))
                                    animatorComp->parameters.floats[p.name] = v;
                            } else if (p.type == AnimatorParamType::Bool) {
                                bool v = animatorComp->parameters.bools.count(p.name)
                                    ? animatorComp->parameters.bools[p.name]
                                    : p.defaultBool;
                                if (InspectorCheckbox(p.name.c_str(), &v))
                                    animatorComp->parameters.bools[p.name] = v;
                            } else if (p.type == AnimatorParamType::Int) {
                                int v = animatorComp->parameters.ints.count(p.name)
                                    ? animatorComp->parameters.ints[p.name]
                                    : p.defaultInt;
                                if (InspectorDragInt(p.name.c_str(), &v))
                                    animatorComp->parameters.ints[p.name] = v;
                            }
                        }

                    }
                }
                EndInspectableComponent(*animatorComp, open);
                if (removeComponent) {
                    RemoveCurrentInspectorComponents(*selectedEntity, animatorComp);
                    ImGui::End();
                    return;
                }
            }

            auto* meshRendererComp = dynamic_cast<MeshRendererComponent*>(orderedComponent);
            if (meshRendererComp) {
                removeComponent = false;
                const bool open = BeginInspectableComponent(
                    "Mesh Renderer", *meshRendererComp, removeComponent, this, [&] {
                        if (ImGui::MenuItem("Use Primitive Cube", nullptr, false,
                                            meshRendererComp->meshPrimitive == "File")) {
                            RecordUndoSnapshot();
                            meshRendererComp->SetPrimitive("Cube", 1.0f);
                        }
                    });
                if (!removeComponent && open) {
                    const char* presetNames[] = {
                        "Prop", "Corridor", "Character", "Viewmodel", "Floor"
                    };
                    int preset = static_cast<int>(meshRendererComp->typePreset);
                    if (InspectorCombo("Type Preset", &preset, presetNames,
                                       IM_ARRAYSIZE(presetNames),
                                       InspectorValuesMixed(&meshRendererComp->typePreset))) {
                        RecordUndoSnapshot();
                        meshRendererComp->typePreset =
                            static_cast<MeshRendererComponent::TypePreset>(
                                std::clamp(preset, 0, 4));
                        PropagateInspectorValues(&meshRendererComp->typePreset);
                    }
                    if (meshRendererComp->meshPrimitive == "File") {
                        if (DrawAssetReferenceField(this, "Mesh", AssetKind::Model,
                                                    meshRendererComp->meshPath, "None (Mesh)")) {
                            RecordUndoSnapshot();
                            if (!meshRendererComp->meshPath.empty())
                                meshRendererComp->SetMeshFile(meshRendererComp->meshPath);
                        }
                    } else {
                        const char* meshTypes[] = { "Cube", "Sphere", "Plane", "Terrain" };
                        int currentMeshType = 0;
                        if (meshRendererComp->meshPrimitive == "Sphere") currentMeshType = 1;
                        else if (meshRendererComp->meshPrimitive == "Plane") currentMeshType = 2;
                        else if (meshRendererComp->meshPrimitive == "Terrain") currentMeshType = 3;
                        if (InspectorCombo("Mesh Type", &currentMeshType, meshTypes, IM_ARRAYSIZE(meshTypes))) {
                            meshRendererComp->SetPrimitive(meshTypes[currentMeshType], meshRendererComp->meshSize);
                            if (meshRendererComp->meshPrimitive == "Terrain") {
                                auto* terrain = selectedEntity->GetComponent<TerrainComponent>();
                                if (!terrain)
                                    terrain = &selectedEntity->AddComponent<TerrainComponent>();
                                terrain->size = meshRendererComp->meshSize;
                                terrain->RebuildMesh(*meshRendererComp);
                            }
                        }
                        if (InspectorDragFloat("Mesh Size", &meshRendererComp->meshSize, 0.1f, 0.01f, 100.0f)) {
                            meshRendererComp->RebuildMesh();
                            if (auto* terrain = selectedEntity->GetComponent<TerrainComponent>();
                                terrain && meshRendererComp->meshPrimitive == "Terrain") {
                                terrain->size = meshRendererComp->meshSize;
                                terrain->RebuildMesh(*meshRendererComp);
                            }
                        }
                    }

                    DrawMaterialSlot(*meshRendererComp);
                    InspectorCheckbox("Editor Only", &meshRendererComp->editorOnly);
                    switch (meshRendererComp->typePreset) {
                    case MeshRendererComponent::TypePreset::Corridor:
                        ImGui::TextDisabled("Near-field static geometry; close clipping and cache optimized.");
                        break;
                    case MeshRendererComponent::TypePreset::Character:
                        ImGui::TextDisabled("Dynamic world object; prioritized without static projection caching.");
                        break;
                    case MeshRendererComponent::TypePreset::Viewmodel:
                        ImGui::TextDisabled("First-person foreground pass with a reduced near clip.");
                        break;
                    case MeshRendererComponent::TypePreset::Floor:
                        ImGui::TextDisabled("Static floor; PS1 export tessellates large faces to reduce affine warping.");
                        break;
                    case MeshRendererComponent::TypePreset::Prop:
                    default:
                        ImGui::TextDisabled("Static world object; projection and lighting cache optimized.");
                        break;
                    }

                }
                EndInspectableComponent(*meshRendererComp, open);
                if (removeComponent) {
                    RemoveCurrentInspectorComponents(*selectedEntity, meshRendererComp);
                    ImGui::End();
                    return;
                }
            }

            auto* meshSubdividerComp =
                dynamic_cast<MeshSubdividerComponent*>(orderedComponent);
            if (meshSubdividerComp) {
                removeComponent = false;
                const bool open = BeginInspectableComponent(
                    "Mesh Subdivider", *meshSubdividerComp, removeComponent, this);
                if (!removeComponent && open) {
                    bool changed = false;
                    changed |= InspectorSliderInt(
                        "Max Levels", &meshSubdividerComp->maxLevels, 0, 4);
                    changed |= InspectorDragFloat(
                        "Max Edge Length", &meshSubdividerComp->maxEdgeLength,
                        0.05f, 0.0f, 1000.0f);
                    changed |= InspectorCheckbox("Preview Subdivision",
                                                 &meshSubdividerComp->preview);
                    meshSubdividerComp->maxLevels = std::clamp(
                        meshSubdividerComp->maxLevels, 0, 4);
                    meshSubdividerComp->maxEdgeLength = std::max(
                        0.0f, meshSubdividerComp->maxEdgeLength);
                    if (changed) {
                        meshSubdividerComp->InvalidatePreview();
                        m_SceneDirty = true;
                    }

                    const auto* renderer =
                        selectedEntity->GetComponent<MeshRendererComponent>();
                    const size_t sourceTris = renderer && renderer->mesh
                        ? renderer->mesh->GetIndexCount() / 3u : 0u;
                    const size_t previewTris = meshSubdividerComp->previewMesh
                        ? meshSubdividerComp->previewMesh->GetIndexCount() / 3u
                        : sourceTris;
                    ImGui::TextDisabled(
                        "PS1 render geometry only; colliders stay unchanged.");
                    ImGui::TextDisabled("%zu tris -> %zu preview tris",
                                        sourceTris, previewTris);
                }
                EndInspectableComponent(*meshSubdividerComp, open);
                if (removeComponent) {
                    RemoveCurrentInspectorComponents(*selectedEntity, meshSubdividerComp);
                    ImGui::End();
                    return;
                }
            }

            auto* terrainComp = dynamic_cast<TerrainComponent*>(orderedComponent);

            if (terrainComp) {
                removeComponent = false;
                const auto rebuildTerrain = [&] {
                    auto* mr = selectedEntity->GetComponent<MeshRendererComponent>();
                    if (!mr)
                        mr = &selectedEntity->AddComponent<MeshRendererComponent>();
                    terrainComp->RebuildMesh(*mr);
                    if (!mr->texture)
                        mr->texture = std::make_shared<Texture>(Texture::CreateCheckerboard(256, 32));
                    m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
                };
                const bool open = BeginInspectableComponent(
                    "Terrain", *terrainComp, removeComponent, this, [&] {
                        if (ImGui::MenuItem("Flatten Terrain")) {
                            RecordUndoSnapshot();
                            terrainComp->ResetProcedural();
                            rebuildTerrain();
                        }
                        if (ImGui::MenuItem("Clear Paint")) {
                            RecordUndoSnapshot();
                            terrainComp->ClearPaint();
                            rebuildTerrain();
                        }
                        if (ImGui::MenuItem("Fill Layer")) {
                            RecordUndoSnapshot();
                            terrainComp->EnsureData();
                            std::fill(terrainComp->paintColors.begin(),
                                      terrainComp->paintColors.end(), terrainComp->brushColor);
                            rebuildTerrain();
                        }
                    });
                if (!removeComponent && open) {
                    bool changed = false;
                    ImGui::TextDisabled("Terrain Data");
                    changed |= InspectorDragFloat("Size", &terrainComp->size, 0.25f, 0.1f, 512.0f);
                    const int oldSubdivisions = terrainComp->subdivisions;
                    changed |= InspectorSliderInt("Resolution", &terrainComp->subdivisions, 1, 128);

                    if (changed)
                        terrainComp->subdivisions = std::clamp(terrainComp->subdivisions, 1, 128);

                    if (oldSubdivisions != terrainComp->subdivisions)
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
                    brushSettingsChanged |= InspectorCheckbox("Enable Scene Brush", &terrainComp->brushEnabled);
                    ImGui::BeginDisabled(!terrainComp->brushEnabled);
                    brushSettingsChanged |= InspectorSliderFloat("Brush Size", &terrainComp->brushRadius, 0.05f, 64.0f, "%.2f");
                    brushSettingsChanged |= InspectorSliderFloat("Opacity", &terrainComp->brushStrength, 0.0f, 10.0f, "%.2f");
                    if (terrainComp->brushMode == TerrainComponent::BrushMode::Paint)
                        brushSettingsChanged |= InspectorColorEdit4("Terrain Layer Color", glm::value_ptr(terrainComp->brushColor));
                    ImGui::EndDisabled();

                    ImGui::TextDisabled("Scene View: left-drag to paint. Shift lowers in Raise/Lower mode.");
                    if (brushSettingsChanged)
                        m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());

                    if (changed)
                        rebuildTerrain();

                    ImGui::TextDisabled("%d x %d vertices, %d tris",
                        terrainComp->subdivisions + 1,
                        terrainComp->subdivisions + 1,
                        terrainComp->subdivisions * terrainComp->subdivisions * 2);
                }
                EndInspectableComponent(*terrainComp, open);
                if (removeComponent) {
                    RemoveCurrentInspectorComponents(*selectedEntity, terrainComp);
                    ImGui::End();
                    return;
                }
            }

            auto* collider = dynamic_cast<ColliderComponent*>(orderedComponent);
            if (collider) {
                removeComponent = false;
                auto* colliderMeshRenderer = selectedEntity->GetComponent<MeshRendererComponent>();
                const bool canFitCollider = colliderMeshRenderer && colliderMeshRenderer->mesh;
                const bool open = BeginInspectableComponent(
                    "Collider", *collider, removeComponent, this, [&] {
                        if (ImGui::MenuItem("Fit to Mesh", nullptr, false, canFitCollider)) {
                            RecordUndoSnapshot();
                            ColliderUtils::FitColliderToMesh(*collider,
                                                             *colliderMeshRenderer->mesh);
                            if (m_IsPlaying)
                                m_Engine->GetPhysicsWorld().RefreshBodies(m_Engine->GetScene());
                        }
                    });
                if (!removeComponent && open) {
                    const char* shapeNames[] = { "Box", "Sphere", "Capsule", "Mesh" };
                    int shapeIdx = static_cast<int>(collider->shape);
                    if (shapeIdx > 3) shapeIdx = 0;
                    if (InspectorCombo("Shape", &shapeIdx, shapeNames, IM_ARRAYSIZE(shapeNames),
                                       InspectorValuesMixed(&collider->shape))) {
                        collider->shape = static_cast<ColliderShape>(shapeIdx);
                        PropagateInspectorValues(&collider->shape);
                    }

                    InspectorDragFloat3("Center", glm::value_ptr(collider->center), 0.01f);
                    if (collider->shape == ColliderShape::Box || collider->shape == ColliderShape::Mesh) {
                        if (collider->shape == ColliderShape::Mesh) {
                            if (InspectorCheckbox("Convex", &collider->convex)) {
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
                        InspectorDragFloat3("Half Extents", glm::value_ptr(collider->halfExtents), 0.01f, 0.01f, 100.0f);
                    } else {
                        InspectorDragFloat("Radius", &collider->radius, 0.01f, 0.01f, 50.0f);
                        if (collider->shape == ColliderShape::Capsule)
                            InspectorDragFloat("Height", &collider->capsuleHeight, 0.01f, 0.01f, 50.0f);
                    }
                    InspectorCheckbox("Is Trigger", &collider->isTrigger);
                    if (InspectorCheckbox("Camera Shot Trigger", &collider->cameraShotTrigger)) {
                        if (collider->cameraShotTrigger)
                            collider->isTrigger = true;
                        ResetShotCameraState();
                        m_SceneDirty = true;
                    }
                }
                EndInspectableComponent(*collider, open);
                if (removeComponent) {
                    RemoveCurrentInspectorComponents(*selectedEntity, collider);
                    if (m_IsPlaying)
                        m_Engine->GetPhysicsWorld().RefreshBodies(m_Engine->GetScene());
                    ImGui::End();
                    return;
                }
            }

            auto* rigidbody = dynamic_cast<RigidbodyComponent*>(orderedComponent);
            if (rigidbody) {
                removeComponent = false;
                const bool open = BeginInspectableComponent("Rigidbody", *rigidbody, removeComponent, this);
                if (!removeComponent && open) {
                    const char* bodyNames[] = { "Static", "Kinematic", "Dynamic" };
                    int bodyIdx = static_cast<int>(rigidbody->bodyType);
                    if (InspectorCombo("Body Type", &bodyIdx, bodyNames, IM_ARRAYSIZE(bodyNames),
                                       InspectorValuesMixed(&rigidbody->bodyType))) {
                        rigidbody->bodyType = static_cast<RigidbodyType>(bodyIdx);
                        PropagateInspectorValues(&rigidbody->bodyType);
                    }
                    if (rigidbody->bodyType == RigidbodyType::Dynamic) {
                        InspectorDragFloat("Mass", &rigidbody->mass, 0.1f, 0.001f, 10000.0f);
                        InspectorCheckbox("Use Gravity", &rigidbody->useGravity);
                        InspectorDragFloat("Linear Drag", &rigidbody->linearDrag, 0.01f, 0.0f, 10.0f);
                        InspectorDragFloat("Bounciness", &rigidbody->bounciness, 0.01f, 0.0f, 1.0f);
                        InspectorCheckbox("Freeze Rotation", &rigidbody->freezeRotation);
                    } else if (rigidbody->bodyType == RigidbodyType::Kinematic) {
                        if (rigidbody->characterController)
                            ImGui::TextDisabled("Character controller (Jolt CharacterVirtual)");
                        else
                            ImGui::TextDisabled("Moved by transform / scripts");
                    }
                }
                EndInspectableComponent(*rigidbody, open);
                if (removeComponent) {
                    RemoveCurrentInspectorComponents(*selectedEntity, rigidbody);
                    if (m_IsPlaying)
                        m_Engine->GetPhysicsWorld().RefreshBodies(m_Engine->GetScene());
                    ImGui::End();
                    return;
                }
            }

            auto* rectTransform = dynamic_cast<RectTransformComponent*>(orderedComponent);
            if (rectTransform) {
                removeComponent = false;
                const bool open = BeginInspectableComponent("Rect Transform", *rectTransform, removeComponent, this);
                if (!removeComponent && open) {
                    InspectorDragFloat2("Anchor Min", glm::value_ptr(rectTransform->anchorMin), 0.01f, 0.0f, 1.0f);
                    InspectorDragFloat2("Anchor Max", glm::value_ptr(rectTransform->anchorMax), 0.01f, 0.0f, 1.0f);
                    InspectorDragFloat2("Pivot", glm::value_ptr(rectTransform->pivot), 0.01f, 0.0f, 1.0f);
                    InspectorDragFloat2("Anchored Position", glm::value_ptr(rectTransform->anchoredPosition), 1.0f);
                    InspectorDragFloat2("Size Delta", glm::value_ptr(rectTransform->sizeDelta), 1.0f);
                }
                EndInspectableComponent(*rectTransform, open);
                if (removeComponent) {
                    RemoveCurrentInspectorComponents(*selectedEntity, rectTransform);
                    ImGui::End();
                    return;
                }
            }

            auto* canvas = dynamic_cast<CanvasComponent*>(orderedComponent);
            if (canvas) {
                removeComponent = false;
                const bool open = BeginInspectableComponent("Canvas", *canvas, removeComponent, this);
                if (!removeComponent && open) {
                    const char* renderModes[] = { "Screen Space - Overlay", "Screen Space - Camera", "World Space" };
                    int renderMode = static_cast<int>(canvas->renderMode);
                    if (InspectorCombo("Render Mode", &renderMode, renderModes,
                                       IM_ARRAYSIZE(renderModes),
                                       InspectorValuesMixed(&canvas->renderMode))) {
                        canvas->renderMode = static_cast<UICanvasRenderMode>(renderMode);
                        PropagateInspectorValues(&canvas->renderMode);
                    }

                    const char* scaleModes[] = { "Constant Pixel Size", "Scale With Screen Size" };
                    int scaleMode = static_cast<int>(canvas->scaleMode);
                    if (InspectorCombo("UI Scale Mode", &scaleMode, scaleModes,
                                       IM_ARRAYSIZE(scaleModes),
                                       InspectorValuesMixed(&canvas->scaleMode))) {
                        canvas->scaleMode = static_cast<UICanvasScaleMode>(scaleMode);
                        PropagateInspectorValues(&canvas->scaleMode);
                    }

                    InspectorDragInt("Sort Order", &canvas->sortOrder, 1.0f, -100, 100);
                    InspectorDragFloat2("Reference Resolution", glm::value_ptr(canvas->referenceResolution), 1.0f, 1.0f, 8192.0f);
                    InspectorSliderFloat("Match Width Or Height", &canvas->matchWidthOrHeight, 0.0f, 1.0f);

                    if (canvas->renderMode == UICanvasRenderMode::ScreenSpaceCamera ||
                        canvas->renderMode == UICanvasRenderMode::WorldSpace) {
                        int camId = static_cast<int>(canvas->eventCameraEntityId);
                        if (InspectorInputInt("Event Camera Entity ID", &camId)) {
                            canvas->eventCameraEntityId = static_cast<uint32_t>(std::max(camId, 0));
                        }
                        ImGui::TextDisabled("0 = primary camera");
                    }
                    if (canvas->renderMode == UICanvasRenderMode::ScreenSpaceCamera)
                        InspectorDragFloat("Plane Distance", &canvas->planeDistance, 0.5f, 0.1f, 500.0f);
                }
                EndInspectableComponent(*canvas, open);
                if (removeComponent) {
                    RemoveCurrentInspectorComponents(*selectedEntity, canvas);
                    ImGui::End();
                    return;
                }
            }

            auto* uiImage = dynamic_cast<UIImageComponent*>(orderedComponent);
            if (uiImage) {
                removeComponent = false;
                const bool open = BeginInspectableComponent("Image", *uiImage, removeComponent, this);
                if (!removeComponent && open) {
                    InspectorColorEdit4("Color", glm::value_ptr(uiImage->color));
                    if (InspectorCheckbox("Preserve Aspect", &uiImage->preserveAspect))
                        RecordUndoSnapshot();
                    DrawUITextureSlot(*uiImage);
                }
                EndInspectableComponent(*uiImage, open);
                if (removeComponent) {
                    RemoveCurrentInspectorComponents(*selectedEntity, uiImage);
                    ImGui::End();
                    return;
                }
            }

            auto* uiText = dynamic_cast<UITextComponent*>(orderedComponent);
            if (uiText) {
                removeComponent = false;
                const bool open = BeginInspectableComponent("Text", *uiText, removeComponent, this);
                if (!removeComponent && open) {
                    char textBuffer[1024];
                    memset(textBuffer, 0, sizeof(textBuffer));
                    strncpy(textBuffer, uiText->text.c_str(), sizeof(textBuffer) - 1);
                    if (DrawInspectorPropertyRow("Text", [&] {
                            return ImGui::InputTextMultiline(
                                "##value", textBuffer, sizeof(textBuffer), ImVec2(-1, 60));
                        }))
                        uiText->text = textBuffer;
                    InspectorColorEdit4("Color", glm::value_ptr(uiText->color));
                    InspectorDragFloat("Font Size", &uiText->fontSize, 0.5f, 8.0f, 96.0f);
                    const char* alignments[] = { "Left", "Center", "Right" };
                    int align = static_cast<int>(uiText->alignment);
                    if (InspectorCombo("Alignment", &align, alignments, IM_ARRAYSIZE(alignments),
                                       InspectorValuesMixed(&uiText->alignment))) {
                        uiText->alignment = static_cast<UITextAlignment>(align);
                        PropagateInspectorValues(&uiText->alignment);
                    }
                }
                EndInspectableComponent(*uiText, open);
                if (removeComponent) {
                    RemoveCurrentInspectorComponents(*selectedEntity, uiText);
                    ImGui::End();
                    return;
                }
            }

            auto* uiButtonGroup = dynamic_cast<UIButtonGroupComponent*>(orderedComponent);
            if (uiButtonGroup) {
                removeComponent = false;
                const bool open = BeginInspectableComponent("Button Group", *uiButtonGroup, removeComponent, this);
                if (!removeComponent && open) {
                    InspectorDragInt("Selected Index", &uiButtonGroup->selectedIndex, 1.0f, 0, 999);
                    InspectorCheckbox("Wrap Navigation", &uiButtonGroup->wrapNavigation);
                    InspectorCheckbox("Keyboard Navigation", &uiButtonGroup->keyboardNavigation);
                    InspectorCheckbox("Gamepad Navigation", &uiButtonGroup->gamepadNavigation);
                    ImGui::SeparatorText("Confirm");
                    InspectorCheckbox("Keyboard Confirm", &uiButtonGroup->keyboardConfirm);
                    const char* keyNames[] = { "Enter", "Space", "Z", "X", "E", "F" };
                    int key = static_cast<int>(uiButtonGroup->confirmKey);
                    if (InspectorCombo("PC Key", &key, keyNames, IM_ARRAYSIZE(keyNames),
                                       InspectorValuesMixed(&uiButtonGroup->confirmKey))) {
                        uiButtonGroup->confirmKey = static_cast<UIConfirmKey>(key);
                        PropagateInspectorValues(&uiButtonGroup->confirmKey);
                    }
                    InspectorCheckbox("Gamepad Confirm", &uiButtonGroup->gamepadConfirm);
                    const char* buttonNames[] = { "South / Cross / A", "East / Circle / B",
                                                  "West / Square / X", "North / Triangle / Y", "Start" };
                    int button = static_cast<int>(uiButtonGroup->confirmButton);
                    if (InspectorCombo("Controller Button", &button, buttonNames,
                                       IM_ARRAYSIZE(buttonNames),
                                       InspectorValuesMixed(&uiButtonGroup->confirmButton))) {
                        uiButtonGroup->confirmButton = static_cast<UIConfirmGamepadButton>(button);
                        PropagateInspectorValues(&uiButtonGroup->confirmButton);
                    }
                    ImGui::SeparatorText("Cursor");
                    DrawUIButtonGroupCursorSlot(*uiButtonGroup);
                    InspectorDragFloat2("Cursor Offset", glm::value_ptr(uiButtonGroup->cursorOffset), 1.0f);
                    InspectorDragFloat2("Cursor Size", glm::value_ptr(uiButtonGroup->cursorSize), 1.0f, 1.0f, 512.0f);
                }
                EndInspectableComponent(*uiButtonGroup, open);
                if (removeComponent) {
                    RemoveCurrentInspectorComponents(*selectedEntity, uiButtonGroup);
                    ImGui::End();
                    return;
                }
            }

            auto* uiButton = dynamic_cast<UIButtonComponent*>(orderedComponent);
            if (uiButton) {
                removeComponent = false;
                bool hasTextChild = false;
                for (uint32_t childId : selectedEntity->GetChildIDs()) {
                    Entity* child = m_Engine->GetScene().FindEntity(childId);
                    if (child && child->HasComponent<UITextComponent>()) {
                        hasTextChild = true;
                        break;
                    }
                }
                const bool open = BeginInspectableComponent(
                    "Button", *uiButton, removeComponent, this, [&] {
                        if (ImGui::MenuItem("Create Child Text", nullptr, false, !hasTextChild)) {
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
                        if (ImGui::MenuItem("Add On Click Listener")) {
                            RecordUndoSnapshot();
                            uiButton->onClick.emplace_back();
                            m_SceneDirty = true;
                        }
                    });
                if (!removeComponent && open) {
                    bool interactable = uiButton->interactable;
                    if (InspectorCheckbox("Interactable", &interactable)) {
                        RecordUndoSnapshot();
                        uiButton->interactable = interactable;
                        m_SceneDirty = true;
                    }
                    DrawUIButtonBackgroundSlot(*uiButton);
                    if (InspectorCheckbox("Preserve Aspect", &uiButton->preserveAspect))
                        RecordUndoSnapshot();
                    InspectorColorEdit4("Normal", glm::value_ptr(uiButton->normalColor));
                    InspectorColorEdit4("Selected", glm::value_ptr(uiButton->selectedColor));
                    InspectorColorEdit4("Pressed", glm::value_ptr(uiButton->pressedColor));
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
                        const bool openTargetPicker =
                            DrawInspectorObjectReferenceButton(targetLabel.c_str());
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
                        if (openTargetPicker) {
                            s_InspectorObjectSearch[0] = '\0';
                            s_InspectorObjectCandidate = listener.targetEntityId;
                            ImGui::OpenPopup("##scene_object_picker");
                        }
                        uint32_t pickedTargetId = listener.targetEntityId;
                        if (DrawInspectorSceneObjectPickerPopup(
                                scene, "Entity", pickedTargetId)) {
                            RecordUndoSnapshot();
                            listener.targetEntityId = pickedTargetId;
                            listener.scriptPath.clear();
                            listener.methodName.clear();
                            m_SceneDirty = true;
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
                }
                EndInspectableComponent(*uiButton, open);
                if (removeComponent) {
                    RemoveCurrentInspectorComponents(*selectedEntity, uiButton);
                    ImGui::End();
                    return;
                }
            }

            auto* uiSpectrum = dynamic_cast<UIAudioSpectrumComponent*>(orderedComponent);
            if (uiSpectrum) {
                removeComponent = false;
                const bool open = BeginInspectableComponent("Audio Spectrum", *uiSpectrum, removeComponent, this);
                if (!removeComponent && open) {
                    int sourceId = static_cast<int>(uiSpectrum->sourceEntityId);
                    if (InspectorInputInt("Audio Source Entity ID", &sourceId))
                        uiSpectrum->sourceEntityId = static_cast<uint32_t>(std::max(sourceId, 0));
                    ImGui::TextDisabled("0 = first playing Audio Source");
                    InspectorColorEdit4("Bar Color", glm::value_ptr(uiSpectrum->color));
                    InspectorColorEdit4("Background", glm::value_ptr(uiSpectrum->backgroundColor));
                    InspectorSliderInt("Bars", &uiSpectrum->barCount, 4, 32);
                    InspectorDragFloat("Gap", &uiSpectrum->barGap, 0.25f, 0.0f, 32.0f);
                    InspectorDragFloat("Sensitivity", &uiSpectrum->sensitivity, 0.02f, 0.1f, 8.0f);
                    InspectorSliderFloat("Smoothing", &uiSpectrum->smoothing, 0.0f, 0.98f);
                }
                EndInspectableComponent(*uiSpectrum, open);
                if (removeComponent) {
                    RemoveCurrentInspectorComponents(*selectedEntity, uiSpectrum);
                    ImGui::End();
                    return;
                }
            }

            auto* mipsScript = dynamic_cast<MipsScriptComponent*>(orderedComponent);
            if (mipsScript) {
                removeComponent = false;
                const bool open = BeginInspectableComponent(
                    "Mips# Script", *mipsScript, removeComponent, this,
                    [&] { DrawMipsScriptIdeMenuItems(mipsScript->scriptPath); });
                if (!removeComponent && open) {
                    std::vector<MipsScriptComponent*> peerScripts;
                    for (Component* peer : s_InspectorMultiEdit.peers) {
                        if (auto* script = dynamic_cast<MipsScriptComponent*>(peer))
                            peerScripts.push_back(script);
                    }
                    const bool scriptPathMixed = std::any_of(
                        peerScripts.begin(), peerScripts.end(),
                        [&](const MipsScriptComponent* script) {
                            return script->scriptPath != mipsScript->scriptPath;
                        });
                    if (DrawAssetReferenceField(this, "Script", AssetKind::Script,
                                                mipsScript->scriptPath, "None (Mips# Script)")) {
                        RecordUndoSnapshot();
                        mipsScript->module.reset();
                        mipsScript->fieldValues.clear();
                        mipsScript->fieldAssetPaths.clear();
                        for (MipsScriptComponent* script : peerScripts) {
                            script->module.reset();
                            script->fieldValues.clear();
                            script->fieldAssetPaths.clear();
                        }
                    }
                    std::vector<std::string> compileErrors;
                    const bool compiled =
                        !scriptPathMixed &&
                        Mips::MipsRuntime::EnsureScriptReady(*mipsScript, compileErrors);
                    if (scriptPathMixed)
                        ImGui::TextDisabled("Select one common script to edit its fields.");
                    if (compiled) {
                        for (MipsScriptComponent* script : peerScripts) {
                            std::vector<std::string> peerErrors;
                            Mips::MipsRuntime::EnsureScriptReady(*script, peerErrors);
                        }
                    }
                    for (const auto& err : compileErrors)
                        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s", err.c_str());

                    if (compiled && mipsScript->module) {
                        if (m_IsPlaying)
                            ImGui::BeginDisabled();

                        for (size_t i = 0; i < mipsScript->module->fields.size(); ++i) {
                            const auto& field = mipsScript->module->fields[i];
                            const bool isVector3Start =
                                field.typeName == "Vector3.x" && i + 2 < mipsScript->module->fields.size() &&
                                mipsScript->module->fields[i + 1].typeName == "Vector3.y" &&
                                mipsScript->module->fields[i + 2].typeName == "Vector3.z";
                            if (isVector3Start) {
                                float value[3] = {
                                    static_cast<float>(mipsScript->fieldValues[i]),
                                    static_cast<float>(mipsScript->fieldValues[i + 1]),
                                    static_cast<float>(mipsScript->fieldValues[i + 2]),
                                };
                                const std::string label = field.name.size() > 2
                                    ? field.name.substr(0, field.name.size() - 2)
                                    : field.name;
                                bool mixedAxes[3]{};
                                for (int axis = 0; axis < 3; ++axis) {
                                    mixedAxes[axis] = std::any_of(
                                        peerScripts.begin(), peerScripts.end(),
                                        [&](const MipsScriptComponent* script) {
                                            return i + static_cast<size_t>(axis) >= script->fieldValues.size() ||
                                                   script->fieldValues[i + static_cast<size_t>(axis)] !=
                                                       mipsScript->fieldValues[i + static_cast<size_t>(axis)];
                                        });
                                }
                                if (InspectorDragFloat3(label.c_str(), value, 0.1f,
                                                        0.0f, 0.0f, "%.3f", mixedAxes)) {
                                    mipsScript->fieldValues[i] = static_cast<double>(value[0]);
                                    mipsScript->fieldValues[i + 1] = static_cast<double>(value[1]);
                                    mipsScript->fieldValues[i + 2] = static_cast<double>(value[2]);
                                    for (MipsScriptComponent* script : peerScripts) {
                                        if (script->fieldValues.size() <= i + 2)
                                            script->fieldValues.resize(i + 3);
                                        script->fieldValues[i] = static_cast<double>(value[0]);
                                        script->fieldValues[i + 1] = static_cast<double>(value[1]);
                                        script->fieldValues[i + 2] = static_cast<double>(value[2]);
                                    }
                                }
                                i += 2;
                                continue;
                            }
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
                                DrawInspectorPropertyRow(field.name.c_str(), [&] {
                                    const bool openPicker =
                                        DrawInspectorObjectReferenceButton(label.c_str());
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
                                    if (referenced &&
                                        ImGui::BeginPopupContextItem("ReferenceContext")) {
                                        if (ImGui::MenuItem("Clear")) {
                                            RecordUndoSnapshot();
                                            mipsScript->fieldValues[i] = 0.0;
                                            m_SceneDirty = true;
                                        }
                                        ImGui::EndPopup();
                                    }
                                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) &&
                                        field.typeName == "Camera") {
                                        ImGui::SetTooltip(
                                            "Drag a Scene object with a Camera component here.");
                                    }
                                    if (openPicker) {
                                        s_InspectorObjectSearch[0] = '\0';
                                        s_InspectorObjectCandidate = referencedId;
                                        ImGui::OpenPopup("##scene_object_picker");
                                    }
                                    uint32_t pickedId = referencedId;
                                    if (DrawInspectorSceneObjectPickerPopup(
                                            scene, field.typeName, pickedId)) {
                                        RecordUndoSnapshot();
                                        mipsScript->fieldValues[i] =
                                            static_cast<double>(pickedId);
                                        m_SceneDirty = true;
                                    }
                                    return false;
                                });
                                ImGui::PopID();
                                continue;
                            }
                            if (isBool) {
                                bool value = mipsScript->fieldValues[i] != 0.0;
                                const bool mixed = std::any_of(
                                    peerScripts.begin(), peerScripts.end(),
                                    [&](const MipsScriptComponent* script) {
                                        return i >= script->fieldValues.size() ||
                                               (script->fieldValues[i] != 0.0) != value;
                                    });
                                if (InspectorCheckbox(field.name.c_str(), &value, mixed)) {
                                    mipsScript->fieldValues[i] = value ? 1.0 : 0.0;
                                    for (MipsScriptComponent* script : peerScripts) {
                                        if (script->fieldValues.size() <= i)
                                            script->fieldValues.resize(i + 1);
                                        script->fieldValues[i] = value ? 1.0 : 0.0;
                                    }
                                }
                                continue;
                            }
                            if (!isFloat && !isInt)
                                continue;

                            bool valueChanged = false;
                            if (isInt) {
                                int value = static_cast<int>(std::lround(mipsScript->fieldValues[i]));
                                const bool mixed = std::any_of(
                                    peerScripts.begin(), peerScripts.end(),
                                    [&](const MipsScriptComponent* script) {
                                        return i >= script->fieldValues.size() ||
                                               static_cast<int>(std::lround(script->fieldValues[i])) != value;
                                    });
                                if (InspectorDragInt(field.name.c_str(), &value,
                                                     1.0f, 0, 0, mixed)) {
                                    mipsScript->fieldValues[i] = static_cast<double>(value);
                                    for (MipsScriptComponent* script : peerScripts) {
                                        if (script->fieldValues.size() <= i)
                                            script->fieldValues.resize(i + 1);
                                        script->fieldValues[i] = static_cast<double>(value);
                                    }
                                    valueChanged = true;
                                }
                            } else {
                                float value = static_cast<float>(mipsScript->fieldValues[i]);
                                const bool mixed = std::any_of(
                                    peerScripts.begin(), peerScripts.end(),
                                    [&](const MipsScriptComponent* script) {
                                        return i >= script->fieldValues.size() ||
                                               static_cast<float>(script->fieldValues[i]) != value;
                                    });
                                if (InspectorDragFloat(field.name.c_str(), &value, 0.1f,
                                                       0.0f, 0.0f, "%.3f", mixed)) {
                                    mipsScript->fieldValues[i] = static_cast<double>(value);
                                    for (MipsScriptComponent* script : peerScripts) {
                                        if (script->fieldValues.size() <= i)
                                            script->fieldValues.resize(i + 1);
                                        script->fieldValues[i] = static_cast<double>(value);
                                    }
                                    valueChanged = true;
                                }
                            }
                            if (valueChanged) {
                                if (m_IsPlaying) {
                                    m_Engine->GetMipsRuntime().ApplyFieldOverrides(
                                        selectedEntity, *mipsScript);
                                    for (MipsScriptComponent* script : peerScripts)
                                        m_Engine->GetMipsRuntime().ApplyFieldOverrides(
                                            script->entity, *script);
                                }
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
                EndInspectableComponent(*mipsScript, open);
                if (removeComponent) {
                    RemoveCurrentInspectorComponents(*selectedEntity, mipsScript);
                    break;
                }
            }
            EndInspectorMultiEdit();
            }

            ImGui::Spacing();
            auto allInspectorEntitiesHave = [&]<typename T>() {
                return std::all_of(inspectorEntities.begin(), inspectorEntities.end(),
                                   [](Entity* entity) { return entity->HasComponent<T>(); });
            };
            auto addMissingInspectorComponents = [&]<typename T>(auto&& initialize) {
                for (Entity* entity : inspectorEntities) {
                    if (entity->HasComponent<T>())
                        continue;
                    T& component = entity->AddComponent<T>();
                    initialize(*entity, component);
                }
                m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
                m_SceneDirty = true;
            };
            if (ImGui::Button("Add Component", ImVec2(-1, 0))) {
                ImGui::OpenPopup("AddComponentPopup");
            }

            if (ImGui::BeginPopup("AddComponentPopup")) {
                if (ImGui::BeginMenu("Mesh")) {
                  if (!allInspectorEntitiesHave.operator()<MeshRendererComponent>()) {
                    if (ImGui::MenuItem("Mesh Renderer")) {
                        RecordUndoSnapshot();
                        addMissingInspectorComponents.operator()<MeshRendererComponent>(
                            [](Entity&, MeshRendererComponent& mr) {
                                mr.SetPrimitive("Cube", 1.0f);
                                mr.texture = std::make_shared<Texture>(
                                    Texture::CreateCheckerboard(128, 16));
                            });
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!allInspectorEntitiesHave.operator()<SkinnedMeshRendererComponent>()) {
                    if (ImGui::MenuItem("Skinned Mesh Renderer")) {
                        RecordUndoSnapshot();
                        addMissingInspectorComponents.operator()<SkinnedMeshRendererComponent>(
                            [](Entity&, SkinnedMeshRendererComponent& sk) {
                                sk.texture = std::make_shared<Texture>(
                                    Texture::CreateCheckerboard(128, 16));
                            });
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!allInspectorEntitiesHave.operator()<TerrainComponent>()) {
                    if (ImGui::MenuItem("Terrain")) {
                        RecordUndoSnapshot();
                        addMissingInspectorComponents.operator()<TerrainComponent>(
                            [](Entity& entity, TerrainComponent& terrain) {
                                auto* mr = entity.GetComponent<MeshRendererComponent>();
                                if (!mr) {
                                    mr = &entity.AddComponent<MeshRendererComponent>();
                                    mr->texture = std::make_shared<Texture>(
                                        Texture::CreateCheckerboard(256, 32));
                                }
                                terrain.RebuildMesh(*mr);
                            });
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!allInspectorEntitiesHave.operator()<ProModelerComponent>()) {
                    if (ImGui::MenuItem("ProModeler Mesh")) {
                        RecordUndoSnapshot();
                        addMissingInspectorComponents.operator()<ProModelerComponent>(
                            [&](Entity& entity, ProModelerComponent& pb) {
                                auto* mr = entity.GetComponent<MeshRendererComponent>();
                                if (!mr) {
                                    mr = &entity.AddComponent<MeshRendererComponent>();
                                    mr->texture = std::make_shared<Texture>(
                                        Texture::CreateCheckerboard(128, 16));
                                }
                                pb.ResetBox();
                                pb.RebuildMesh(*mr);
                                AddProModelerMeshCollider(entity, *mr);
                            });
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!allInspectorEntitiesHave.operator()<MeshSubdividerComponent>()) {
                    const bool hasMesh = std::all_of(
                        inspectorEntities.begin(), inspectorEntities.end(),
                        [](Entity* entity) {
                            return entity->HasComponent<MeshRendererComponent>();
                        });
                    if (ImGui::MenuItem("Mesh Subdivider", nullptr, false, hasMesh)) {
                        RecordUndoSnapshot();
                        addMissingInspectorComponents.operator()<MeshSubdividerComponent>(
                            [](Entity&, MeshSubdividerComponent&) {});
                        ImGui::CloseCurrentPopup();
                    }
                }
                  ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Animation")) {
                  if (!allInspectorEntitiesHave.operator()<AnimatorComponent>()) {
                    if (ImGui::MenuItem("Animator")) {
                        RecordUndoSnapshot();
                        addMissingInspectorComponents.operator()<AnimatorComponent>(
                            [](Entity& entity, AnimatorComponent& anim) {
                                if (auto* sk = entity.GetComponent<SkinnedMeshRendererComponent>()) {
                                    anim.modelPath = sk->modelPath;
                                    anim.ReloadAssets();
                                }
                            });
                        ImGui::CloseCurrentPopup();
                    }
                }
                  ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Rendering")) {
                  if (!allInspectorEntitiesHave.operator()<CameraComponent>()) {
                    if (ImGui::MenuItem("Camera")) {
                        RecordUndoSnapshot();
                        addMissingInspectorComponents.operator()<CameraComponent>(
                            [&](Entity& entity, CameraComponent&) {
                                SyncSelectedCameraFromTransform(&entity);
                            });
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!allInspectorEntitiesHave.operator()<DistanceCullComponent>()) {
                    if (ImGui::MenuItem("Distance Cull")) {
                        RecordUndoSnapshot();
                        addMissingInspectorComponents.operator()<DistanceCullComponent>(
                            [](Entity&, DistanceCullComponent&) {});
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!allInspectorEntitiesHave.operator()<FrustumCullComponent>()) {
                    if (ImGui::MenuItem("Frustum Cull")) {
                        RecordUndoSnapshot();
                        addMissingInspectorComponents.operator()<FrustumCullComponent>(
                            [](Entity&, FrustumCullComponent&) {});
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!allInspectorEntitiesHave.operator()<PostProcessVolumeComponent>()) {
                    if (ImGui::MenuItem("Post Process Volume")) {
                        RecordUndoSnapshot();
                        addMissingInspectorComponents.operator()<PostProcessVolumeComponent>(
                            [](Entity&, PostProcessVolumeComponent&) {});
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!allInspectorEntitiesHave.operator()<LightComponent>()) {
                    if (ImGui::BeginMenu("Light")) {
                        if (ImGui::MenuItem("Directional")) {
                            RecordUndoSnapshot();
                            addMissingInspectorComponents.operator()<LightComponent>(
                                [](Entity&, LightComponent& light) {
                                    light.type = LightType::Directional;
                                });
                            ImGui::CloseCurrentPopup();
                        }
                        if (ImGui::MenuItem("Point")) {
                            RecordUndoSnapshot();
                            addMissingInspectorComponents.operator()<LightComponent>(
                                [](Entity&, LightComponent& light) {
                                    light.type = LightType::Point;
                                });
                            ImGui::CloseCurrentPopup();
                        }
                        if (ImGui::MenuItem("Spot")) {
                            RecordUndoSnapshot();
                            addMissingInspectorComponents.operator()<LightComponent>(
                                [](Entity&, LightComponent& light) {
                                    light.type = LightType::Spot;
                                });
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndMenu();
                    }
                }
                  ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Audio")) {
                  if (!allInspectorEntitiesHave.operator()<AudioSourceComponent>()) {
                    if (ImGui::MenuItem("Audio Source")) {
                        RecordUndoSnapshot();
                        addMissingInspectorComponents.operator()<AudioSourceComponent>(
                            [](Entity&, AudioSourceComponent&) {});
                        ImGui::CloseCurrentPopup();
                    }
                  }
                  ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Physics")) {
                  if (!allInspectorEntitiesHave.operator()<ColliderComponent>()) {
                    if (ImGui::BeginMenu("Collider")) {
                    auto addCollider = [&](ColliderShape shape) {
                        RecordUndoSnapshot();
                        addMissingInspectorComponents.operator()<ColliderComponent>(
                            [&](Entity& entity, ColliderComponent& col) {
                                col.shape = shape;
                                if (auto* mr = entity.GetComponent<MeshRendererComponent>();
                                    mr && mr->mesh)
                                    ColliderUtils::FitColliderToMesh(col, *mr->mesh);
                                else if (shape == ColliderShape::Capsule) {
                                    col.radius = 0.35f;
                                    col.capsuleHeight = 1.0f;
                                }
                            });
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
                if (!allInspectorEntitiesHave.operator()<RigidbodyComponent>()) {
                    if (ImGui::MenuItem("Rigidbody")) {
                        RecordUndoSnapshot();
                        addMissingInspectorComponents.operator()<RigidbodyComponent>(
                            [](Entity&, RigidbodyComponent&) {});
                        ImGui::CloseCurrentPopup();
                    }
                }
                  ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("UI")) {
                  if (!allInspectorEntitiesHave.operator()<CanvasComponent>()) {
                    if (ImGui::MenuItem("Canvas")) {
                        RecordUndoSnapshot();
                        addMissingInspectorComponents.operator()<CanvasComponent>(
                            [](Entity& entity, CanvasComponent&) {
                                if (!entity.HasComponent<RectTransformComponent>()) {
                                    auto& rect = entity.AddComponent<RectTransformComponent>();
                                    rect.anchorMin = { 0.0f, 0.0f };
                                    rect.anchorMax = { 1.0f, 1.0f };
                                    rect.sizeDelta = { 0.0f, 0.0f };
                                }
                            });
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!allInspectorEntitiesHave.operator()<RectTransformComponent>()) {
                    if (ImGui::MenuItem("Rect Transform")) {
                        RecordUndoSnapshot();
                        addMissingInspectorComponents.operator()<RectTransformComponent>(
                            [](Entity&, RectTransformComponent&) {});
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!allInspectorEntitiesHave.operator()<UIImageComponent>()) {
                    if (ImGui::MenuItem("Image")) {
                        RecordUndoSnapshot();
                        addMissingInspectorComponents.operator()<UIImageComponent>(
                            [](Entity&, UIImageComponent&) {});
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!allInspectorEntitiesHave.operator()<UITextComponent>()) {
                    if (ImGui::MenuItem("Text")) {
                        RecordUndoSnapshot();
                        addMissingInspectorComponents.operator()<UITextComponent>(
                            [](Entity&, UITextComponent&) {});
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!allInspectorEntitiesHave.operator()<UIButtonGroupComponent>()) {
                    if (ImGui::MenuItem("Button Group")) {
                        RecordUndoSnapshot();
                        Scene& scene = m_Engine->GetScene();
                        addMissingInspectorComponents.operator()<UIButtonGroupComponent>(
                            [&](Entity& entity, UIButtonGroupComponent&) {
                                Entity* canvas = entity.HasComponent<CanvasComponent>()
                                    ? &entity
                                    : FindCanvasAncestor(scene, &entity);
                                if (!canvas) {
                                    canvas = CreateUICanvas(nullptr);
                                    scene.SetParent(&entity, canvas, true);
                                }
                                if (!entity.HasComponent<RectTransformComponent>())
                                    entity.AddComponent<RectTransformComponent>();
                            });
                        m_Engine->GetMipsRuntime().SyncEditSnapshot(scene);
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!allInspectorEntitiesHave.operator()<UIButtonComponent>()) {
                    if (ImGui::MenuItem("Button")) {
                        RecordUndoSnapshot();
                        Scene& scene = m_Engine->GetScene();
                        addMissingInspectorComponents.operator()<UIButtonComponent>(
                            [&](Entity& entity, UIButtonComponent& button) {
                            Entity* group = FindButtonGroupAncestor(scene, &entity);
                            if (!group) {
                                Entity* canvas = FindCanvasAncestor(scene, &entity);
                                if (!canvas)
                                    canvas = CreateUICanvas(nullptr);
                                group = CreateUIButtonGroup(canvas);
                            }
                            if (group)
                                scene.SetParent(&entity, group, true);
                            if (!entity.HasComponent<RectTransformComponent>())
                                entity.AddComponent<RectTransformComponent>();
                            Entity* textEntity = scene.CreateEntity("Text");
                            scene.SetParent(textEntity, &entity);
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
                            });
                        m_Engine->GetMipsRuntime().SyncEditSnapshot(scene);
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!allInspectorEntitiesHave.operator()<UIAudioSpectrumComponent>()) {
                    if (ImGui::MenuItem("Audio Spectrum")) {
                        RecordUndoSnapshot();
                        addMissingInspectorComponents.operator()<UIAudioSpectrumComponent>(
                            [](Entity& entity, UIAudioSpectrumComponent&) {
                                if (!entity.HasComponent<RectTransformComponent>())
                                    entity.AddComponent<RectTransformComponent>();
                            });
                        ImGui::CloseCurrentPopup();
                    }
                }
                  ImGui::EndMenu();
                }
                DrawAddComponentScriptMenu(inspectorEntities);
                ImGui::EndPopup();
            }
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
    } else {
        m_Engine->GetScene().SetWorldPosition(*spawned, GetSceneViewSpawnPosition());
    }

    SelectSingleEntity(spawned->GetID());
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
            if (viewMin && viewSize && viewSize->x > 0.0f && viewSize->y > 0.0f) {
                const ImGuiIO& io = ImGui::GetIO();
                m_Engine->GetScene().SetWorldPosition(
                    *spawned,
                    PickPointOnPlane(m_SceneCamera.GetCamera(), io.MousePos.x, io.MousePos.y,
                                     viewMin->x, viewMin->y, viewSize->x, viewSize->y, 0.0f));
            } else {
                m_Engine->GetScene().SetWorldPosition(*spawned, GetSceneViewSpawnPosition());
            }
            SelectSingleEntity(spawned->GetID());
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
        } else {
            m_Engine->GetScene().SetWorldPosition(
                *audioEntity, GetSceneViewSpawnPosition());
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
                            SelectSingleEntity(target->GetID());
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
                        SelectSingleEntity(target->GetID());
                        m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
                        MIPSYNC_INFO("UI button background applied: {}", projectRel);
                    } else if (auto* uiImage = target->GetComponent<UIImageComponent>()) {
                        RecordUndoSnapshot();
                        uiImage->texturePath = projectRel;
                        uiImage->texture = AssetManager::Get().GetTexture(projectRel);
                        uiImage->preserveAspect = true;
                        FitRectTransformToTextureAspect(target, uiImage->texture.get());
                        SelectSingleEntity(target->GetID());
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
                            SelectSingleEntity(img->GetID());
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
    if (InspectorColorEdit4("Color", glm::value_ptr(mat.color)))
        changed = true;

    if (DrawAssetReferenceField(this, "Texture", AssetKind::Texture,
                                mat.texturePath, "None (Texture)")) {
        changed = true;
    }

    ImGui::Spacing();
    ImGui::TextColored(EditorTheme::TextSecondary, "Main Maps");
    if (InspectorDragFloat2("Tiling", glm::value_ptr(mat.mainTextureTiling), 0.01f, 0.001f, 100.0f))
        changed = true;
    if (InspectorDragFloat2("Offset", glm::value_ptr(mat.mainTextureOffset), 0.01f, -100.0f, 100.0f))
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
    std::string materialPath = mr.materialPath;
    if (DrawAssetReferenceField(this, "Material", AssetKind::Material,
                                materialPath, "None (Material)")) {
        RecordUndoSnapshot();
        if (materialPath.empty()) {
            AssetManager::Get().ClearMeshRendererMaterial(mr);
        } else {
            Material material;
            std::string error;
            if (Material::Load(AssetManager::Get().ToAbsolute(materialPath),
                               material, error)) {
                AssetManager::Get().ApplyMaterialToMeshRenderer(
                    mr, material, materialPath);
            } else {
                MIPSYNC_WARN("Material load failed: {}", error);
            }
        }
        m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
    }
}

void EditorApp::DrawMaterialSlot(SkinnedMeshRendererComponent& mr) {
    std::string materialPath = mr.materialPath;
    if (DrawAssetReferenceField(this, "Material", AssetKind::Material,
                                materialPath, "None (Material)")) {
        RecordUndoSnapshot();
        if (materialPath.empty()) {
            AssetManager::Get().ClearSkinnedMeshRendererMaterial(mr);
        } else {
            Material material;
            std::string error;
            if (Material::Load(AssetManager::Get().ToAbsolute(materialPath),
                               material, error)) {
                AssetManager::Get().ApplyMaterialToSkinnedMeshRenderer(
                    mr, material, materialPath);
            } else {
                MIPSYNC_WARN("Material load failed: {}", error);
            }
        }
        m_Engine->GetMipsRuntime().SyncEditSnapshot(m_Engine->GetScene());
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
                MIPSYNC_CONSOLE_INFO("> {}", command);
                const std::string output = m_CommandHost->ExecuteConsoleLine(command);
                if (!output.empty()) MIPSYNC_CONSOLE_INFO("{}", output);
            }
            ImGui::SetKeyboardFocusHere(-1);
        }
    }
    ImGui::End();
}

} // namespace MipsyncEngine
