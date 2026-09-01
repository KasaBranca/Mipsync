#include "EditorApp.h"

#include "EditorCanvasGizmo.h"
#include "EditorCameraGizmo.h"
#include "EditorColliderGizmo.h"
#include "EditorIcons.h"
#include "EditorLightGizmo.h"
#include "EditorRectTransformGizmo.h"
#include "EditorSceneFraming.h"
#include "EditorTheme.h"
#include "../core/Engine.h"
#include "../mips/MipsRuntime.h"
#include "../physics/PhysicsWorld.h"
#include "../scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>
#include <imgui.h>
#include <imgui_internal.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace MipsyncEngine {

namespace {

bool MatrixNearlyEqual(const glm::mat4& a, const glm::mat4& b, float epsilon = 1e-5f) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (std::abs(a[column][row] - b[column][row]) > epsilon)
                return false;
        }
    }
    return true;
}

float NearestEquivalentDegrees(float value, float reference) {
    while (value - reference > 180.0f)
        value -= 360.0f;
    while (value - reference < -180.0f)
        value += 360.0f;
    return value;
}

glm::vec3 ExtractYXZEulerDegrees(const glm::mat4& transform,
                                 const glm::vec3& referenceDegrees) {
    glm::mat4 rotation(1.0f);
    for (int column = 0; column < 3; ++column) {
        glm::vec3 axis(transform[column]);
        const float length = glm::length(axis);
        if (length > 1e-6f)
            axis /= length;
        rotation[column] = glm::vec4(axis, 0.0f);
    }

    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    yaw = std::atan2(rotation[2][0], rotation[2][2]);
    const float pitchCos = std::sqrt(rotation[0][1] * rotation[0][1] +
                                     rotation[1][1] * rotation[1][1]);
    pitch = std::atan2(-rotation[2][1], pitchCos);
    const float sinYaw = std::sin(yaw);
    const float cosYaw = std::cos(yaw);
    roll = std::atan2(
        sinYaw * rotation[1][2] - cosYaw * rotation[1][0],
        cosYaw * rotation[0][0] - sinYaw * rotation[0][2]);

    glm::vec3 result = glm::degrees(glm::vec3(pitch, yaw, roll));
    result.x = NearestEquivalentDegrees(result.x, referenceDegrees.x);
    result.y = NearestEquivalentDegrees(result.y, referenceDegrees.y);
    result.z = NearestEquivalentDegrees(result.z, referenceDegrees.z);
    return result;
}

} // namespace

void EditorApp::DrawGizmoToolbar() {
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
    // Scene View deliberately has zero window padding so the framebuffer can
    // touch its edges. Give only the toolbar chrome its own left inset.
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3, 2));

    DrawGizmoIconButton(GizmoIcon::Translate, ImGuizmo::TRANSLATE, m_GizmoOperation, m_GizmoOperation);
    DrawGizmoIconButton(GizmoIcon::Rotate, ImGuizmo::ROTATE, m_GizmoOperation, m_GizmoOperation);
    DrawGizmoIconButton(GizmoIcon::Scale, ImGuizmo::SCALE, m_GizmoOperation, m_GizmoOperation);

    ImGui::SameLine(0.0f, 6.0f);
    const bool worldMode = m_GizmoMode == ImGuizmo::WORLD;
    if (ImGui::Button(worldMode ? "World##GizmoSpace" : "Local##GizmoSpace",
                      ImVec2(48.0f, 0.0f))) {
        m_GizmoMode = worldMode ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Transform orientation: %s (click to switch)",
                          worldMode ? "World" : "Local");

    ImGui::SameLine(0.0f, 10.0f);
    DrawRenderModeSelector();

    ImGui::PopStyleVar(2);
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
}

void EditorApp::DrawRenderModeSelector() {
    static const char* kModeLabels[] = {
        "Shaded",
        "Wireframe",
        "Shaded Wireframe",
        "Unlit",
        "Normals",
    };
    constexpr int kModeCount = (int)(sizeof(kModeLabels) / sizeof(kModeLabels[0]));
    constexpr float kToolbarHeight = 22.0f;

    const int currentIndex = (int)m_SceneRenderMode;
    const char* currentLabel = (currentIndex >= 0 && currentIndex < kModeCount)
        ? kModeLabels[currentIndex] : kModeLabels[0];

    const float paddingY = (kToolbarHeight - ImGui::GetTextLineHeight()) * 0.5f;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, paddingY));
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::BeginCombo("##SceneRenderMode", currentLabel)) {
        for (int i = 0; i < kModeCount; ++i) {
            const bool selected = (i == currentIndex);
            if (ImGui::Selectable(kModeLabels[i], selected))
                m_SceneRenderMode = static_cast<RenderMode>(i);
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::PopStyleVar();
}

void EditorApp::DrawGizmoControls(const ImVec2& imageMin, const ImVec2& imageSize, const Camera& camera) {
    ImGuizmo::SetAlternativeWindow(ImGui::GetCurrentWindow());
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(imageMin.x, imageMin.y, imageSize.x, imageSize.y);

    if (DrawProModelerSubobjectGizmo(imageMin, imageSize, camera))
        return;

    Entity* selected = GetSelectedEntity();
    if (!selected)
        return;

    ImGuizmo::SetOrthographic(false);

    Scene& scene = m_Engine->GetScene();
    glm::mat4 view = camera.GetViewMatrix();
    glm::mat4 proj = camera.GetProjectionMatrix();

    const float viewportAspect = imageSize.y > 0.0f ? (imageSize.x / imageSize.y) : 1.0f;
    const int layoutW = m_GameViewSettings.renderWidth;
    const int layoutH = m_GameViewSettings.renderHeight;

    std::vector<Entity*> transformTargets;
    for (uint32_t id : GetSelectedHierarchyRootIDs(selected->GetID())) {
        if (Entity* target = scene.FindEntity(id)) {
            if (target->GetComponent<TransformComponent>())
                transformTargets.push_back(target);
        }
    }
    if (transformTargets.empty())
        transformTargets.push_back(selected);
    const bool multiTransform = transformTargets.size() > 1;

    glm::mat4 worldMat(1.0f);
    const bool useRectGizmo =
        !multiTransform &&
        EditorRectTransformGizmo::ShouldUseRectGizmo(scene, *selected) &&
        EditorRectTransformGizmo::BuildGizmoMatrix(scene, *selected, camera, layoutW, layoutH,
                                                   viewportAspect, worldMat);

    if (multiTransform) {
        glm::vec3 pivot(0.0f);
        for (Entity* target : transformTargets)
            pivot += glm::vec3(scene.GetWorldMatrix(*target)[3]);
        pivot /= static_cast<float>(transformTargets.size());
        worldMat = scene.GetWorldMatrix(*selected);
        for (int column = 0; column < 3; ++column) {
            glm::vec3 axis(worldMat[column]);
            const float axisLength = glm::length(axis);
            worldMat[column] = glm::vec4(
                axisLength > 1e-6f ? axis / axisLength
                                   : glm::vec3(column == 0, column == 1, column == 2),
                0.0f);
        }
        worldMat[3] = glm::vec4(pivot, 1.0f);
    } else if (!useRectGizmo) {
        auto* transformComp = selected->GetComponent<TransformComponent>();
        if (!transformComp)
            return;

        const glm::mat4 parentWorld = scene.GetParentWorldMatrix(*selected);
        worldMat = parentWorld * transformComp->GetTransform();
    }

    float matrix[16];
    std::memcpy(matrix, glm::value_ptr(worldMat), sizeof(matrix));

    static bool wasGizmoUsing = false;
    const bool gizmoUsing = ImGuizmo::IsUsing();
    const bool gizmoStarted = gizmoUsing && !wasGizmoUsing;
    wasGizmoUsing = gizmoUsing;

    const ImGuizmo::OPERATION gizmoOp = static_cast<ImGuizmo::OPERATION>(m_GizmoOperation);
    const glm::mat4 originalWorldMat = worldMat;

    const bool changed = ImGuizmo::Manipulate(
        glm::value_ptr(view),
        glm::value_ptr(proj),
        gizmoOp,
        m_GizmoMode,
        matrix
    );

    if (useRectGizmo) {
        if (changed || gizmoUsing) {
            EditorRectTransformGizmo::ApplyGizmoDrag(scene, *selected, camera, layoutW, layoutH,
                                                     viewportAspect, gizmoOp, originalWorldMat,
                                                     glm::make_mat4(matrix),
                                                     gizmoUsing, gizmoStarted);
            m_Engine->GetMipsRuntime().SyncEditSnapshot(scene);
        }
        return;
    }

    if (multiTransform && (changed || gizmoUsing)) {
        const glm::mat4 newGroupWorld = glm::make_mat4(matrix);
        const glm::mat4 deltaWorld = newGroupWorld * glm::inverse(originalWorldMat);
        if (!changed && MatrixNearlyEqual(deltaWorld, glm::mat4(1.0f)))
            return;

        for (Entity* target : transformTargets) {
            auto* transformComp = target->GetComponent<TransformComponent>();
            if (!transformComp)
                continue;
            const glm::mat4 oldWorld = scene.GetWorldMatrix(*target);
            const glm::mat4 parentWorld = scene.GetParentWorldMatrix(*target);
            const glm::mat4 newLocal = glm::inverse(parentWorld) * deltaWorld * oldWorld;
            glm::vec3 translation, rotation, scale;
            ImGuizmo::DecomposeMatrixToComponents(
                glm::value_ptr(newLocal),
                glm::value_ptr(translation),
                glm::value_ptr(rotation),
                glm::value_ptr(scale));
            transformComp->position = glm::vec3(newLocal[3]);
            transformComp->rotation = ExtractYXZEulerDegrees(
                newLocal, transformComp->rotation);
            transformComp->scale = scale;
            SyncSelectedCameraFromTransform(target);
            if (changed)
                SyncPhysicsAfterTransformEdit(target);
        }
        m_Engine->GetMipsRuntime().SyncEditSnapshot(scene);
        return;
    }

    if (changed || gizmoUsing) {
        auto* transformComp = selected->GetComponent<TransformComponent>();
        if (!transformComp)
            return;

        const glm::mat4 parentWorld = scene.GetParentWorldMatrix(*selected);
        const glm::mat4 newWorld = glm::make_mat4(matrix);
        const glm::mat4 newLocal = glm::inverse(parentWorld) * newWorld;
        const glm::mat4 currentLocal = transformComp->GetTransform();
        if (!changed && MatrixNearlyEqual(newLocal, currentLocal))
            return;

        glm::vec3 translation, rotation, scale;
        ImGuizmo::DecomposeMatrixToComponents(
            glm::value_ptr(newLocal),
            glm::value_ptr(translation),
            glm::value_ptr(rotation),
            glm::value_ptr(scale));

        if (gizmoOp == ImGuizmo::TRANSLATE) {
            transformComp->position = glm::vec3(newLocal[3]);
        } else if (gizmoOp == ImGuizmo::ROTATE) {
            // TransformComponent composes rotations as Y * X * Z. ImGuizmo's
            // generic decomposition uses a different Euler convention, which
            // could turn a character by 90 degrees around X/Z while dragging
            // the yaw ring. Decode the matrix using the engine's convention.
            transformComp->rotation = ExtractYXZEulerDegrees(
                newLocal, transformComp->rotation);
        } else if (gizmoOp == ImGuizmo::SCALE) {
            transformComp->scale = scale;
        } else {
            transformComp->position = translation;
            transformComp->rotation = rotation;
            transformComp->scale = scale;
        }
        SyncSelectedCameraFromTransform(selected);
        m_Engine->GetMipsRuntime().SyncEditSnapshot(scene);
        if (changed)
            SyncPhysicsAfterTransformEdit(selected);
    }
}

void EditorApp::SyncSelectedCameraFromTransform(Entity* entity) {
    if (!entity)
        return;

    auto* cameraComp = entity->GetComponent<CameraComponent>();
    auto* transformComp = entity->GetComponent<TransformComponent>();
    if (!cameraComp || !transformComp)
        return;

    cameraComp->camera.SyncFromTransform(transformComp->position, transformComp->rotation);
}

void EditorApp::SyncPhysicsAfterTransformEdit(Entity* entity) {
    if (!m_Engine || !entity || !m_IsPlaying)
        return;
    PhysicsWorld& physics = m_Engine->GetPhysicsWorld();
    if (!physics.IsActive())
        return;
    physics.SyncEntityFromScene(m_Engine->GetScene(), *entity);
}

void EditorApp::DrawLightGizmos(const ImVec2& imageMin, const ImVec2& imageSize, const Camera& sceneCamera) {
    EditorLightGizmo::Draw(m_Engine->GetScene(), sceneCamera, imageMin, imageSize);
}

void EditorApp::DrawColliderGizmos(const ImVec2& imageMin, const ImVec2& imageSize, const Camera& sceneCamera) {
    EditorColliderGizmo::DrawSceneColliders(
        m_Engine->GetScene(),
        sceneCamera.GetViewMatrix(),
        sceneCamera.GetProjectionMatrix(),
        imageMin,
        imageSize,
        ImGui::GetWindowDrawList(),
        m_SelectedEntityIDs);
}

void EditorApp::DrawMeshSubdivisionPreview(const ImVec2& imageMin, const ImVec2& imageSize,
                                           const Camera& sceneCamera) {
    if (!m_Engine || m_SelectedEntityIDs.empty())
        return;

    Scene& scene = m_Engine->GetScene();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const glm::mat4& view = sceneCamera.GetViewMatrix();
    const glm::mat4& projection = sceneCamera.GetProjectionMatrix();
    constexpr ImU32 kSubdivisionColor = IM_COL32(35, 210, 255, 225);

    for (uint32_t entityId : m_SelectedEntityIDs) {
        Entity* entity = scene.FindEntity(entityId);
        if (!entity)
            continue;

        auto* subdivider = entity->GetComponent<MeshSubdividerComponent>();
        if (!subdivider || !subdivider->enabled || !subdivider->preview ||
            subdivider->maxLevels <= 0 || !subdivider->previewMesh)
            continue;

        const std::vector<Vertex>& vertices = subdivider->previewMesh->GetVertices();
        const std::vector<uint32_t>& indices = subdivider->previewMesh->GetIndices();
        if (vertices.empty() || indices.size() < 3)
            continue;

        const glm::mat4 world = scene.GetWorldMatrix(*entity);
        std::unordered_set<uint64_t> drawnEdges;
        drawnEdges.reserve(indices.size());

        const auto drawEdge = [&](uint32_t first, uint32_t second) {
            if (first >= vertices.size() || second >= vertices.size() || first == second)
                return;
            const uint32_t lo = std::min(first, second);
            const uint32_t hi = std::max(first, second);
            const uint64_t key = (static_cast<uint64_t>(lo) << 32u) | hi;
            if (!drawnEdges.insert(key).second)
                return;

            const glm::vec3 a = glm::vec3(world * glm::vec4(vertices[first].position, 1.0f));
            const glm::vec3 b = glm::vec3(world * glm::vec4(vertices[second].position, 1.0f));
            EditorCameraGizmo::DrawWorldLine(
                drawList, a, b, view, projection, imageMin, imageSize,
                kSubdivisionColor, 1.15f);
        };

        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            drawEdge(indices[i], indices[i + 1]);
            drawEdge(indices[i + 1], indices[i + 2]);
            drawEdge(indices[i + 2], indices[i]);
        }
    }
}

void EditorApp::FrameEntityInSceneView(Entity& entity) {
    MipsyncEngine::FrameEntityInSceneView(m_Engine->GetScene(), entity, m_SceneCamera, m_LastSceneViewAspect,
                                          m_GameViewSettings.renderWidth, m_GameViewSettings.renderHeight);
}

void EditorApp::DrawCanvasGizmo(const ImVec2& imageMin, const ImVec2& imageSize, const Camera& sceneCamera) {
    EditorCanvasGizmo::DrawSceneCanvases(
        m_Engine->GetScene(),
        sceneCamera,
        sceneCamera.GetViewMatrix(),
        sceneCamera.GetProjectionMatrix(),
        imageMin,
        imageSize,
        ImGui::GetWindowDrawList(),
        m_SelectedEntityID,
        m_GameViewSettings.renderWidth,
        m_GameViewSettings.renderHeight);
}

void EditorApp::DrawCameraFrustumGizmo(const ImVec2& imageMin, const ImVec2& imageSize, const Camera& sceneCamera) {
    if (!m_Engine)
        return;

    Scene& scene = m_Engine->GetScene();
    Entity* selected = GetSelectedEntity();
    const float aspect = (m_GameViewSettings.renderHeight > 0)
        ? ((float)m_GameViewSettings.renderWidth / (float)m_GameViewSettings.renderHeight)
        : (imageSize.x / imageSize.y);

    // Draw unselected cameras in scene (subtle wireframe)
    for (auto& entity : scene.GetEntities()) {
        if (!entity || entity.get() == selected)
            continue;
        auto* cameraComp = entity->GetComponent<CameraComponent>();
        auto* transformComp = entity->GetComponent<TransformComponent>();
        if (!cameraComp || !cameraComp->enabled || !transformComp)
            continue;

        cameraComp->camera.SyncFromTransform(transformComp->position, transformComp->rotation);
        EditorCameraGizmo::DrawFrustum(
            cameraComp->camera,
            *transformComp,
            aspect,
            sceneCamera.GetViewMatrix(),
            sceneCamera.GetProjectionMatrix(),
            imageMin,
            imageSize,
            ImGui::GetWindowDrawList(),
            false);
    }

    // Draw selected camera (highlighted with Far Clip plane and distance label)
    if (selected) {
        auto* cameraComp = selected->GetComponent<CameraComponent>();
        auto* transformComp = selected->GetComponent<TransformComponent>();
        if (cameraComp && transformComp) {
            cameraComp->camera.SyncFromTransform(transformComp->position, transformComp->rotation);
            EditorCameraGizmo::DrawFrustum(
                cameraComp->camera,
                *transformComp,
                aspect,
                sceneCamera.GetViewMatrix(),
                sceneCamera.GetProjectionMatrix(),
                imageMin,
                imageSize,
                ImGui::GetWindowDrawList(),
                true);
        }
    }

    // Draw Distance Cull gizmos for active entities in scene
    for (auto& entity : scene.GetEntities()) {
        if (!entity || !entity->IsActive())
            continue;
        auto* cullComp = entity->GetComponent<DistanceCullComponent>();
        if (!cullComp || !cullComp->enabled || cullComp->cullDistance <= 0.0f)
            continue;

        glm::vec3 center(0.0f);
        if (cullComp->targetEntityId != 0) {
            if (const Entity* target = scene.FindEntity(cullComp->targetEntityId)) {
                center = glm::vec3(scene.GetWorldMatrix(*target)[3]);
            } else {
                center = glm::vec3(scene.GetWorldMatrix(*entity)[3]);
            }
        } else {
            center = glm::vec3(scene.GetWorldMatrix(*entity)[3]);
        }

        const bool isSelected = (entity.get() == selected);
        EditorCameraGizmo::DrawDistanceCullGizmo(
            center,
            cullComp->cullDistance,
            sceneCamera.GetViewMatrix(),
            sceneCamera.GetProjectionMatrix(),
            imageMin,
            imageSize,
            ImGui::GetWindowDrawList(),
            isSelected);
    }
}

} // namespace MipsyncEngine
