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

bool SceneViewToolbarToggle(const char* label, bool& value) {
    if (value) {
        ImGui::PushStyleColor(ImGuiCol_Button, EditorTheme::Selection);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorTheme::AccentHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, EditorTheme::AccentActive);
        ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::SelectionText);
    }

    const bool pressed = ImGui::Button(label);
    if (value)
        ImGui::PopStyleColor(4);
    if (pressed)
        value = !value;
    return pressed;
}

} // namespace

void EditorApp::DrawGizmoToolbar() {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));

    DrawGizmoIconButton(GizmoIcon::Translate, ImGuizmo::TRANSLATE, m_GizmoOperation, m_GizmoOperation);
    DrawGizmoIconButton(GizmoIcon::Rotate, ImGuizmo::ROTATE, m_GizmoOperation, m_GizmoOperation);
    DrawGizmoIconButton(GizmoIcon::Scale, ImGuizmo::SCALE, m_GizmoOperation, m_GizmoOperation);

    ImGui::SameLine(0.0f, 10.0f);
    DrawRenderModeSelector();
    DrawSceneViewPsxToolbar();

    ImGui::PopStyleVar(2);
}

void EditorApp::DrawSceneViewPsxToolbar() {
    ImGui::SameLine(0.0f, 12.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("PSX");

    ImGui::SameLine();
    const bool allOn = m_SceneViewPsx.vertexJitter && m_SceneViewPsx.affineMapping &&
                       m_SceneViewPsx.colorDepthLimit && m_SceneViewPsx.dithering &&
                       m_SceneViewPsx.meshPreview;
    bool master = allOn;
    if (SceneViewToolbarToggle("All", master)) {
        m_SceneViewPsx.vertexJitter    = master;
        m_SceneViewPsx.affineMapping   = master;
        m_SceneViewPsx.colorDepthLimit = master;
        m_SceneViewPsx.dithering       = master;
        m_SceneViewPsx.meshPreview     = master;
    }

    ImGui::SameLine();
    SceneViewToolbarToggle("Jit", m_SceneViewPsx.vertexJitter);
    ImGui::SameLine();
    SceneViewToolbarToggle("Aff", m_SceneViewPsx.affineMapping);
    ImGui::SameLine();
    SceneViewToolbarToggle("15b", m_SceneViewPsx.colorDepthLimit);
    ImGui::SameLine();
    SceneViewToolbarToggle("Dith", m_SceneViewPsx.dithering);
    ImGui::SameLine();
    SceneViewToolbarToggle("Mesh", m_SceneViewPsx.meshPreview);
}

void EditorApp::ApplySceneViewPsxOverrides(PS1Settings& settings) const {
    settings.vertexJitter =
        m_SceneViewPsx.vertexJitter ? m_SceneViewPsxBaseline.vertexJitter : 0.0f;
    settings.affineMapping =
        m_SceneViewPsx.affineMapping && m_SceneViewPsxBaseline.affineMapping;
    settings.colorDepthLimit =
        m_SceneViewPsx.colorDepthLimit && m_SceneViewPsxBaseline.colorDepthLimit;
    settings.ditheringEnabled =
        m_SceneViewPsx.dithering && m_SceneViewPsxBaseline.ditheringEnabled;

    // Scene View: extend fog to match editor camera far (PS1 baseline fogEnd is ~40).
    const float sceneFar = m_SceneCamera.GetCamera().farClip;
    if (settings.fogEnabled && sceneFar > settings.fogEnd)
        settings.fogEnd = sceneFar * 0.9f;
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
    constexpr float kToolbarHeight = 28.0f;

    const int currentIndex = (int)m_SceneRenderMode;
    const char* currentLabel = (currentIndex >= 0 && currentIndex < kModeCount)
        ? kModeLabels[currentIndex] : kModeLabels[0];

    const float paddingY = (kToolbarHeight - ImGui::GetTextLineHeight()) * 0.5f;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, paddingY));
    ImGui::SetNextItemWidth(160.0f);
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

    glm::mat4 worldMat(1.0f);
    const bool useRectGizmo =
        EditorRectTransformGizmo::ShouldUseRectGizmo(scene, *selected) &&
        EditorRectTransformGizmo::BuildGizmoMatrix(scene, *selected, camera, layoutW, layoutH,
                                                   viewportAspect, worldMat);

    if (!useRectGizmo) {
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
        ImGuizmo::LOCAL,
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
            transformComp->rotation = rotation;
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
    Entity* selected = GetSelectedEntity();
    if (!selected)
        return;

    auto* cameraComp = selected->GetComponent<CameraComponent>();
    auto* transformComp = selected->GetComponent<TransformComponent>();
    if (!cameraComp || !transformComp)
        return;

    const float aspect = imageSize.x / imageSize.y;
    cameraComp->camera.SyncFromTransform(transformComp->position, transformComp->rotation);

    EditorCameraGizmo::DrawFrustum(
        cameraComp->camera,
        *transformComp,
        aspect,
        sceneCamera.GetViewMatrix(),
        sceneCamera.GetProjectionMatrix(),
        imageMin,
        imageSize,
        ImGui::GetWindowDrawList());
}

} // namespace MipsyncEngine
