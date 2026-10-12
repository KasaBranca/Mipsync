#include "EditorIcons.h"
#include "EditorTheme.h"
#include "../scene/Scene.h"
#include "InspectorScriptIcon.h"
#include <cmath>

namespace MipsyncEngine {

void DrawComponentIcon(const Component& component, ImDrawList* dl, ImVec2 center, float size) {
    // Normalized coordinates keep the glyph centered at every editor UI scale.
    auto p = [&](float x, float y) { return ImVec2(center.x + x * size, center.y + y * size); };
    const float stroke = size / 12.0f;
    const ImU32 blue = IM_COL32(122, 190, 231, 255);
    const ImU32 green = IM_COL32(123, 210, 155, 255);
    const ImU32 gold = IM_COL32(236, 198, 99, 255);
    const ImU32 white = ImGui::GetColorU32(EditorTheme::TextPrimary);
    auto line = [&](float x, float y, float u, float v, ImU32 color) {
        dl->AddLine(p(x, y), p(u, v), color, stroke);
    };
    auto box = [&](ImU32 color) { dl->AddRect(p(-.4f, -.32f), p(.4f, .32f), color, size * .06f, 0, stroke); };
    auto cube = [&](ImU32 color) {
        const ImVec2 top = p(0, -.43f), left = p(-.39f, -.21f), right = p(.39f, -.21f);
        const ImVec2 middle = p(0, 0), bottom = p(0, .43f);
        dl->AddQuad(top, right, middle, left, color, stroke);
        dl->AddQuad(left, middle, bottom, p(-.39f, .21f), color, stroke);
        dl->AddQuad(middle, right, p(.39f, .21f), bottom, color, stroke);
    };
    if (dynamic_cast<const TransformComponent*>(&component)) {
        line(-.1f, .13f, .38f, .13f, IM_COL32(237, 113, 107, 255));
        line(-.1f, .13f, -.1f, -.4f, green);
        line(-.1f, .13f, -.38f, .4f, blue);
        dl->AddTriangleFilled(p(.45f, .13f), p(.25f, .03f), p(.25f, .23f), IM_COL32(237, 113, 107, 255));
        dl->AddTriangleFilled(p(-.1f, -.47f), p(-.2f, -.26f), p(0, -.26f), green);
    } else if (dynamic_cast<const MipsScriptComponent*>(&component)) {
        DrawProjectScriptSvgIcon(dl, p(-.5f, -.5f), p(.5f, .5f),
                                blue, IM_COL32(30, 58, 76, 255));
    } else if (dynamic_cast<const AudioSourceComponent*>(&component)) {
        dl->AddRectFilled(p(-.43f, -.14f), p(-.23f, .14f), gold);
        dl->AddQuadFilled(p(-.25f, -.14f), p(.02f, -.35f), p(.02f, .35f), p(-.25f, .14f), gold);
        for (float radius : {.24f, .4f}) {
            dl->PathArcTo(p(.04f, 0), size * radius, -.8f, .8f, 12);
            dl->PathStroke(gold, 0, stroke);
        }
    } else if (dynamic_cast<const LightComponent*>(&component)) {
        dl->AddCircle(p(0, 0), size * .19f, gold, 16, stroke);
        for (int i = 0; i < 8; ++i) {
            const float a = i * 3.14159265f / 4;
            line(std::cos(a) * .29f, std::sin(a) * .29f,
                 std::cos(a) * .44f, std::sin(a) * .44f, gold);
        }
    } else if (dynamic_cast<const CameraComponent*>(&component)) {
        dl->AddRect(p(-.43f, -.29f), p(.17f, .29f), blue, size * .07f, 0, stroke);
        dl->AddQuad(p(.17f, -.14f), p(.43f, -.3f), p(.43f, .3f), p(.17f, .14f), blue, stroke);
    } else if (dynamic_cast<const RigidbodyComponent*>(&component)) {
        // Falling mass, landing surface and gravity arrow remain readable at
        // the Inspector's small header size without resembling a mesh cube.
        dl->AddCircleFilled(p(-.18f, -.12f), size * .18f, gold, 16);
        line(-.18f, -.45f, -.18f, -.35f, gold);
        line(-.42f, .34f, .06f, .34f, gold);
        line(.27f, -.38f, .27f, .19f, gold);
        dl->AddTriangleFilled(p(.27f, .43f), p(.11f, .16f), p(.43f, .16f), gold);
    } else if (dynamic_cast<const SkinnedMeshRendererComponent*>(&component)) {
        // A mesh silhouette encloses a contrasting shoulder/spine rig.
        const ImVec2 skin[] = {p(-.4f, -.12f), p(-.12f, -.25f), p(.12f, -.25f),
                              p(.4f, -.12f), p(.26f, .39f), p(-.26f, .39f)};
        dl->AddConvexPolyFilled(skin, 6, IM_COL32(122, 190, 231, 40));
        dl->AddPolyline(skin, 6, blue, ImDrawFlags_Closed, stroke);
        dl->AddCircleFilled(p(0, -.4f), size * .075f, blue, 12);
        line(-.4f, -.12f, 0, .08f, IM_COL32(122, 190, 231, 160));
        line(.4f, -.12f, 0, .08f, IM_COL32(122, 190, 231, 160));
        line(-.22f, -.09f, 0, -.17f, gold);
        line(.22f, -.09f, 0, -.17f, gold);
        line(0, -.17f, 0, .31f, gold);
        for (float y : {-.17f, .08f, .31f})
            dl->AddCircleFilled(p(0, y), size * .055f, gold, 12);
    } else if (dynamic_cast<const MeshRendererComponent*>(&component)) {
        // Shaded faces and triangle edges distinguish rendered geometry from
        // the plain wire volume used for box colliders and modeling tools.
        const ImVec2 top = p(0, -.43f), left = p(-.39f, -.21f), right = p(.39f, -.21f);
        const ImVec2 middle = p(0, 0), bottom = p(0, .43f);
        dl->AddQuadFilled(top, right, middle, left, IM_COL32(122, 190, 231, 88));
        dl->AddQuadFilled(left, middle, bottom, p(-.39f, .21f), IM_COL32(122, 190, 231, 40));
        dl->AddQuadFilled(middle, right, p(.39f, .21f), bottom, IM_COL32(122, 190, 231, 64));
        const ImVec2 outline[] = {top, right, p(.39f, .21f), bottom, p(-.39f, .21f), left};
        dl->AddPolyline(outline, 6, blue, ImDrawFlags_Closed, stroke);
        dl->AddLine(left, middle, blue, stroke);
        dl->AddLine(right, middle, blue, stroke);
        dl->AddLine(middle, bottom, blue, stroke);
        dl->AddLine(top, middle, blue, stroke);
        dl->AddLine(left, bottom, blue, stroke);
        dl->AddLine(right, bottom, blue, stroke);
    } else if (const auto* collider = dynamic_cast<const ColliderComponent*>(&component)) {
        switch (collider->shape) {
        case ColliderShape::Box:
            cube(green);
            break;
        case ColliderShape::Sphere:
            dl->AddCircle(center, size * .4f, green, 24, stroke);
            for (int i = 0; i < 24; ++i) {
                const float angle = i * 6.2831853f / 24;
                dl->PathLineTo(p(std::cos(angle) * .15f, std::sin(angle) * .4f));
            }
            dl->PathStroke(green, ImDrawFlags_Closed, stroke);
            break;
        case ColliderShape::Capsule:
            dl->AddRect(p(-.27f, -.43f), p(.27f, .43f), green, size * .27f, 0, stroke);
            line(-.25f, 0, .25f, 0, green);
            break;
        case ColliderShape::Mesh: {
            const ImVec2 vertices[] = {
                p(-.4f, .22f), p(-.24f, -.32f), p(.2f, -.4f),
                p(.42f, .08f), p(.12f, .4f)
            };
            dl->AddPolyline(vertices, 5, green, ImDrawFlags_Closed, stroke);
            for (const ImVec2& vertex : vertices)
                dl->AddLine(vertex, center, green, stroke);
            break;
        }
        }
    } else if (dynamic_cast<const AnimatorComponent*>(&component)) {
        line(-.25f, -.25f, .25f, 0, blue); line(.25f, 0, -.25f, .25f, blue);
        for (ImVec2 pos : {p(-.25f, -.25f), p(.25f, 0), p(-.25f, .25f)})
            dl->AddRectFilled(ImVec2(pos.x - size * .12f, pos.y - size * .1f),
                              ImVec2(pos.x + size * .12f, pos.y + size * .1f), blue, size * .03f);
    } else if (dynamic_cast<const PostProcessVolumeComponent*>(&component)) {
        for (int i = 0; i < 3; ++i) {
            const float y = (i - 1) * .28f, x = i == 1 ? -.16f : .16f;
            line(-.42f, y, .42f, y, blue);
            dl->AddCircleFilled(p(x, y), size * .09f, white, 12);
        }
    } else if (dynamic_cast<const UIAudioSpectrumComponent*>(&component)) {
        for (int i = 0; i < 5; ++i) {
            const float x = (i - 2) * .17f, height = (i % 3 + 1) * .2f;
            dl->AddRectFilled(p(x - .05f, .35f - height), p(x + .05f, .35f), blue);
        }
    } else if (dynamic_cast<const UITextComponent*>(&component)) {
        line(-.35f, -.34f, .35f, -.34f, blue); line(0, -.34f, 0, .35f, blue);
        line(-.17f, .35f, .17f, .35f, blue);
    } else if (dynamic_cast<const UIImageComponent*>(&component)) {
        box(blue);
        dl->AddCircleFilled(p(.19f, -.14f), size * .07f, gold, 12);
        line(-.33f, .24f, -.12f, -.05f, green); line(-.12f, -.05f, .11f, .24f, green);
        line(.06f, .18f, .21f, .02f, green); line(.21f, .02f, .34f, .24f, green);
    } else if (dynamic_cast<const CanvasComponent*>(&component) ||
               dynamic_cast<const RectTransformComponent*>(&component)) {
        box(blue);
        for (float x : {-.4f, .4f}) for (float y : {-.32f, .32f})
            dl->AddRectFilled(p(x - .055f, y - .055f), p(x + .055f, y + .055f), blue);
    } else if (dynamic_cast<const UIButtonComponent*>(&component)) {
        box(blue); line(-.18f, 0, .18f, 0, white);
    } else if (dynamic_cast<const UIButtonGroupComponent*>(&component)) {
        for (float y : {-.24f, .24f})
            dl->AddRect(p(-.4f, y - .14f), p(.4f, y + .14f), blue, size * .04f, 0, stroke);
    } else if (dynamic_cast<const DistanceCullComponent*>(&component) ||
               dynamic_cast<const FrustumCullComponent*>(&component)) {
        dl->AddTriangle(p(-.4f, -.35f), p(.4f, -.35f), p(0, .4f), blue, stroke);
        line(-.24f, -.1f, .24f, -.1f, green);
    } else if (dynamic_cast<const TerrainComponent*>(&component)) {
        line(-.44f, .33f, -.13f, -.35f, green); line(-.13f, -.35f, .17f, .18f, green);
        line(.03f, -.06f, .24f, -.27f, green); line(.24f, -.27f, .44f, .33f, green);
        line(-.44f, .33f, .44f, .33f, green);
    } else if (dynamic_cast<const MeshSubdividerComponent*>(&component)) {
        box(blue); line(0, -.32f, 0, .32f, blue); line(-.4f, 0, .4f, 0, blue);
    } else {
        // Generic modeling components retain the simple volume glyph.
        cube(blue);
    }
}

static void DrawArrowHead(ImDrawList* dl, ImVec2 tip, ImVec2 dir, float length, ImU32 color) {
    float len = sqrtf(dir.x * dir.x + dir.y * dir.y);
    if (len < 0.001f) return;
    dir.x /= len;
    dir.y /= len;

    ImVec2 side{ -dir.y, dir.x };
    ImVec2 base{ tip.x - dir.x * length, tip.y - dir.y * length };
    dl->AddTriangleFilled(tip, { base.x + side.x * length * 0.45f, base.y + side.y * length * 0.45f },
                          { base.x - side.x * length * 0.45f, base.y - side.y * length * 0.45f }, color);
}

void DrawToolbarIcon(ToolbarIcon icon, ImDrawList* dl, ImVec2 center, float size, ImU32 color) {
    auto p = [&](float x, float y) { return ImVec2(center.x + x * size, center.y + y * size); };
    const float stroke = std::max(1.0f, size / 12.0f);
    auto line = [&](float x, float y, float u, float v) { dl->AddLine(p(x,y),p(u,v),color,stroke); };
    switch (icon) {
    case ToolbarIcon::PSync:
        for (float start : {-.7f, 2.44159265f}) {
            const float end = start + 2.1f;
            dl->PathArcTo(center, size * .34f, start, end, 12);
            dl->PathStroke(color, 0, stroke);
            DrawArrowHead(dl, p(std::cos(end)*.34f,std::sin(end)*.34f),
                          {-std::sin(end),std::cos(end)},size*.2f,color);
        }
        break;
    case ToolbarIcon::Statistics:
        for (int i=0;i<3;++i) {
            const float x=-.32f+i*.25f, top=.13f-i*.22f;
            dl->AddRectFilled(p(x-.075f,top),p(x+.075f,.3f),color);
        }
        line(-.43f,.4f,.43f,.4f);
        break;
    case ToolbarIcon::Gizmos:
        line(-.08f,.1f,.38f,.1f); line(-.08f,.1f,-.08f,-.38f);
        line(-.08f,.1f,-.36f,.38f);
        DrawArrowHead(dl,p(.43f,.1f),{1,0},size*.2f,color);
        DrawArrowHead(dl,p(-.08f,-.43f),{0,-1},size*.2f,color);
        DrawArrowHead(dl,p(-.4f,.42f),{-1,1},size*.16f,color);
        break;
    case ToolbarIcon::Local: {
        const ImVec2 vertices[]={p(0,-.42f),p(.37f,-.2f),p(.37f,.2f),
                                p(0,.42f),p(-.37f,.2f),p(-.37f,-.2f)};
        dl->AddPolyline(vertices,6,color,ImDrawFlags_Closed,stroke);
        line(-.37f,-.2f,0,.02f); line(.37f,-.2f,0,.02f);line(0,.02f,0,.42f);
        break;
    }
    case ToolbarIcon::World:
        dl->AddCircle(center,size*.4f,color,24,stroke);
        for(int i=0;i<24;++i) {
            const float angle=i*6.2831853f/24;
            dl->PathLineTo(p(std::cos(angle)*.17f,std::sin(angle)*.4f));
        }
        dl->PathStroke(color,ImDrawFlags_Closed,stroke);
        line(-.4f,0,.4f,0);
        break;
    }
}

void DrawGizmoIcon(GizmoIcon icon, ImDrawList* drawList, ImVec2 center, float size, ImU32 color) {
    const float s = size * 0.5f;
    const float thick = 1.8f;

    switch (icon) {
    case GizmoIcon::Translate: {
        // Unity-style 4-way move arrows
        drawList->AddLine({ center.x - s, center.y }, { center.x + s, center.y }, color, thick);
        drawList->AddLine({ center.x, center.y - s }, { center.x, center.y + s }, color, thick);
        DrawArrowHead(drawList, { center.x + s, center.y }, { 1, 0 }, s * 0.35f, color);
        DrawArrowHead(drawList, { center.x - s, center.y }, { -1, 0 }, s * 0.35f, color);
        DrawArrowHead(drawList, { center.x, center.y - s }, { 0, -1 }, s * 0.35f, color);
        DrawArrowHead(drawList, { center.x, center.y + s }, { 0, 1 }, s * 0.35f, color);
        break;
    }
    case GizmoIcon::Rotate: {
        // Circular rotate arrow
        const float r = s * 0.82f;
        drawList->PathClear();
        drawList->PathArcTo(center, r, -0.2f, 5.2f, 24);
        drawList->PathStroke(color, 0, thick);

        float ax = center.x + r * cosf(5.2f);
        float ay = center.y + r * sinf(5.2f);
        float tx = -sinf(5.2f);
        float ty = cosf(5.2f);
        DrawArrowHead(drawList, { ax, ay }, { tx, ty }, s * 0.38f, color);
        break;
    }
    case GizmoIcon::Scale: {
        // Box with corner scale handles
        ImVec2 tl{ center.x - s * 0.65f, center.y - s * 0.65f };
        ImVec2 br{ center.x + s * 0.65f, center.y + s * 0.65f };
        drawList->AddRect(tl, br, color, 0.0f, 0, thick);

        auto corner = [&](float cx, float cy, float dx, float dy) {
            drawList->AddLine({ cx, cy }, { cx + dx * s * 0.55f, cy + dy * s * 0.55f }, color, thick);
            DrawArrowHead(drawList, { cx + dx * s * 0.55f, cy + dy * s * 0.55f }, { dx, dy }, s * 0.28f, color);
        };
        corner(tl.x, tl.y, -1, -1);
        corner(br.x, tl.y, 1, -1);
        corner(tl.x, br.y, -1, 1);
        corner(br.x, br.y, 1, 1);
        break;
    }
    }
}

bool DrawGizmoIconButton(GizmoIcon icon, ImGuizmo::OPERATION operation, ImGuizmo::OPERATION current, ImGuizmo::OPERATION& selected) {
    const ImVec2 buttonSize{ 28.0f, ImGui::GetFrameHeight() };
    const bool active = current == operation;

    ImGui::PushID(static_cast<int>(operation));

    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, EditorTheme::BtnFace);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorTheme::SurfaceHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, EditorTheme::BtnDarkShadow);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorTheme::SurfaceHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, EditorTheme::BtnDarkShadow);
    }

    const bool pressed = ImGui::Button("##gizmo", buttonSize);

    ImVec2 rectMin = ImGui::GetItemRectMin();
    ImVec2 rectMax = ImGui::GetItemRectMax();
    ImVec2 center{ (rectMin.x + rectMax.x) * 0.5f, (rectMin.y + rectMax.y) * 0.5f };

    ImU32 iconColor = active ? ImGui::ColorConvertFloat4ToU32(EditorTheme::Accent)
                             : ImGui::ColorConvertFloat4ToU32(EditorTheme::TextPrimary);
    DrawGizmoIcon(icon, ImGui::GetWindowDrawList(), center, 11.0f, iconColor);

    ImGui::PopStyleColor(3);
    ImGui::PopID();

    if (pressed)
        selected = operation;

    return pressed;
}

} // namespace MipsyncEngine
