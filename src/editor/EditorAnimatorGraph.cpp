#include "EditorAnimatorGraph.h"
#include "EditorAnimatorControllerInspector.h"
#include "EditorApp.h"
#include "EditorTheme.h"
#include "EditorMenu.h"
#include "EditorToolbar.h"
#include "AssetBrowserPanel.h"
#include "../animation/AnimationTypes.h"
#include "../animation/AnimatorControllerIO.h"
#include "../animation/SkeletalModel.h"
#include "../assets/AssetManager.h"
#include "../core/Engine.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstring>
#include <cstdio>

namespace MipsyncEngine {

namespace {

constexpr ImVec2 kNodeSize{ 184.0f, 58.0f };
constexpr ImVec2 kSpecialNodeSize{ 144.0f, 34.0f };
constexpr float kGridStep = 24.0f;
constexpr int kLinkFromAnyState = -2;

struct GraphViewState {
    std::string path;
    ImVec2 pan{ 80.0f, 60.0f };
    float zoom = 1.0f;
    int selectedState = -1;
    int selectedTransition = -1;
    int linkDragFrom = -1;
    bool linkingActive = false;
    bool linkClickMode = false;
    int draggingNode = -1;
    glm::vec2 dragGrabOffset{ 0.0f, 0.0f };
    glm::vec2 anyStatePosition{ 40.0f, 56.0f };
    bool draggingAnyState = false;
    bool draggingEntry = false;
    bool didAutoLayout = false;
    bool didInitialFrame = false;
    glm::vec2 contextSpawn{ 40.0f, 40.0f };
};

constexpr float kPortHitRadius = 10.0f;
constexpr float kPortDrawRadius = 6.0f;

std::string ModelPathFromDragPayload(const ImGuiPayload* payload) {
    if (!payload || !payload->Data)
        return {};
    std::vector<std::string> paths;
    if (payload->IsDataType(DragDrop::kAssetMove))
        paths = DragDrop::ParseAssetMovePayload(payload->Data, payload->DataSize);
    else if (payload->IsDataType(DragDrop::kAssetModel)) {
        const std::string path(static_cast<const char*>(payload->Data),
                               payload->DataSize > 0 ? payload->DataSize - 1 : 0);
        if (!path.empty())
            paths.push_back(path);
    } else if (payload->IsDataType(DragDrop::kAssetAnimClip)) {
        std::string modelPath;
        std::string clipName;
        if (DragDrop::ParseAnimClipPayload(payload->Data, payload->DataSize, modelPath, clipName))
            return modelPath;
        return {};
    }
    if (paths.size() != 1)
        return {};
    if (AssetBrowserPanel::ClassifyAssetByPath(paths[0]) != AssetKind::Model)
        return {};
    return paths[0];
}

bool AnimClipFromDragPayload(const ImGuiPayload* payload, std::string& outModelPath,
                             std::string& outClipName, int& outClipStackIndex) {
    if (!payload || !payload->IsDataType(DragDrop::kAssetAnimClip))
        return false;
    return DragDrop::ParseAnimClipPayload(payload->Data, payload->DataSize, outModelPath,
                                          outClipName, &outClipStackIndex);
}

GraphViewState g_Graph;

int FindStateIndex(const AnimatorControllerAsset& asset, const std::string& name) {
    for (size_t i = 0; i < asset.states.size(); ++i) {
        if (asset.states[i].name == name)
            return static_cast<int>(i);
    }
    return -1;
}

bool StatesNeedAutoLayout(const AnimatorControllerAsset& asset) {
    if (asset.states.empty())
        return false;
    for (const auto& st : asset.states) {
        if (st.graphPosition.x != 0.0f || st.graphPosition.y != 0.0f)
            return false;
    }
    return true;
}

glm::vec2 EntryPosition(const AnimatorControllerAsset& asset) {
    if (asset.entryGraphPosition) return *asset.entryGraphPosition;
    const int index = FindStateIndex(asset, asset.defaultState);
    return index >= 0 ? asset.states[static_cast<size_t>(index)].graphPosition +
        glm::vec2(-250.0f, (kNodeSize.y - kSpecialNodeSize.y) * 0.5f) : glm::vec2(-210.0f, 0.0f);
}

void FrameGraphToStates(const AnimatorControllerAsset& asset, ImVec2 canvasSize, ImVec2& pan,
                        float& zoom) {
    if (canvasSize.x < 8.0f || canvasSize.y < 8.0f)
        return;

    float minX = FLT_MAX;
    float minY = FLT_MAX;
    float maxX = -FLT_MAX;
    float maxY = -FLT_MAX;
    for (const auto& st : asset.states) {
        minX = std::min(minX, st.graphPosition.x);
        minY = std::min(minY, st.graphPosition.y);
        maxX = std::max(maxX, st.graphPosition.x + kNodeSize.x);
        maxY = std::max(maxY, st.graphPosition.y + kNodeSize.y);
    }

    // Include both movable special nodes when framing the complete graph.
    minX = std::min(minX, asset.anyStateGraphPosition.x);
    minY = std::min(minY, asset.anyStateGraphPosition.y);
    maxX = std::max(maxX, asset.anyStateGraphPosition.x + kSpecialNodeSize.x);
    maxY = std::max(maxY, asset.anyStateGraphPosition.y + kSpecialNodeSize.y);
    {
        const glm::vec2 entry = EntryPosition(asset);
        minX = std::min(minX, entry.x);
        minY = std::min(minY, entry.y);
        maxX = std::max(maxX, entry.x + kSpecialNodeSize.x);
        maxY = std::max(maxY, entry.y + kSpecialNodeSize.y);
    }

    const float pad = 48.0f;
    const float bw = std::max(1.0f, maxX - minX);
    const float bh = std::max(1.0f, maxY - minY);
    zoom = std::clamp(std::min((canvasSize.x - pad) / bw, (canvasSize.y - pad) / bh), 0.35f, 2.5f);

    const float cx = (minX + maxX) * 0.5f;
    const float cy = (minY + maxY) * 0.5f;
    pan.x = canvasSize.x * 0.5f - cx * zoom;
    pan.y = canvasSize.y * 0.5f - cy * zoom;
}

void FrameGraphToState(const AnimatorControllerAsset& asset, int stateIndex, ImVec2 canvasSize,
                       ImVec2& pan, float& zoom) {
    if (stateIndex < 0 || static_cast<size_t>(stateIndex) >= asset.states.size())
        return;
    zoom = std::clamp(zoom, 0.75f, 1.5f);
    const auto& state = asset.states[static_cast<size_t>(stateIndex)];
    const float cx = state.graphPosition.x + kNodeSize.x * 0.5f;
    const float cy = state.graphPosition.y + kNodeSize.y * 0.5f;
    pan.x = canvasSize.x * 0.5f - cx * zoom;
    pan.y = canvasSize.y * 0.5f - cy * zoom;
}

ImVec2 GraphToScreen(ImVec2 canvasMin, ImVec2 pan, float zoom, glm::vec2 graphPos) {
    return ImVec2(canvasMin.x + pan.x + graphPos.x * zoom,
                  canvasMin.y + pan.y + graphPos.y * zoom);
}

ImVec2 AddImVec2(ImVec2 a, ImVec2 b) {
    return ImVec2(a.x + b.x, a.y + b.y);
}

glm::vec2 ScreenToGraph(ImVec2 canvasMin, ImVec2 pan, float zoom, ImVec2 screen) {
    return glm::vec2((screen.x - canvasMin.x - pan.x) / zoom,
                     (screen.y - canvasMin.y - pan.y) / zoom);
}

void DrawGrid(ImDrawList* dl, ImVec2 canvasMin, ImVec2 canvasMax, ImVec2 pan, float zoom) {
    const float step = kGridStep * zoom;
    if (step < 8.0f)
        return;
    const ImU32 minor = ImGui::ColorConvertFloat4ToU32(
        UiTokens::WithAlpha(EditorTheme::TextMuted, 0.045f));
    const ImU32 major = ImGui::ColorConvertFloat4ToU32(
        UiTokens::WithAlpha(EditorTheme::TextMuted, 0.11f));
    for (int i = static_cast<int>(std::floor(-pan.x / step));
         canvasMin.x + pan.x + i * step < canvasMax.x; ++i) {
        const float x = canvasMin.x + pan.x + i * step;
        dl->AddLine(ImVec2(x, canvasMin.y), ImVec2(x, canvasMax.y), i % 4 == 0 ? major : minor);
    }
    for (int i = static_cast<int>(std::floor(-pan.y / step));
         canvasMin.y + pan.y + i * step < canvasMax.y; ++i) {
        const float y = canvasMin.y + pan.y + i * step;
        dl->AddLine(ImVec2(canvasMin.x, y), ImVec2(canvasMax.x, y), i % 4 == 0 ? major : minor);
    }
}

ImVec2 BezierControlPoints(ImVec2 fromPort, ImVec2 toPort, ImVec2& cp1, ImVec2& cp2,
                           float bend = 0.0f) {
    const float dx = toPort.x - fromPort.x;
    const float dy = toPort.y - fromPort.y;
    const float length = std::max(std::hypot(dx, dy), 1.0f);
    const ImVec2 normal(-dy / length, dx / length);
    cp1 = ImVec2(fromPort.x + dx * 0.34f + normal.x * bend,
                 fromPort.y + dy * 0.34f + normal.y * bend);
    cp2 = ImVec2(fromPort.x + dx * 0.66f + normal.x * bend,
                 fromPort.y + dy * 0.66f + normal.y * bend);
    return ImVec2((fromPort.x + toPort.x) * 0.5f, (fromPort.y + toPort.y) * 0.5f);
}

ImVec2 CubicPoint(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, float t) {
    const float u = 1.0f - t;
    const float a = u * u * u;
    const float b = 3.0f * u * u * t;
    const float c = 3.0f * u * t * t;
    const float d = t * t * t;
    return ImVec2(a * p0.x + b * p1.x + c * p2.x + d * p3.x,
                  a * p0.y + b * p1.y + c * p2.y + d * p3.y);
}

void DrawGraphText(ImDrawList* dl, ImVec2 pos, float width, const char* text,
                   const ImVec4& color, float zoom, float fontScale = 1.0f) {
    const float fontSize = std::max(9.0f, ImGui::GetFontSize() * zoom * fontScale);
    std::string fitted = text;
    ImFont* font = ImGui::GetFont();
    if (font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, fitted.c_str()).x > width) {
        while (!fitted.empty() &&
               font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, (fitted + "...").c_str()).x > width) {
            size_t end = fitted.size() - 1;
            while (end > 0 && (static_cast<unsigned char>(fitted[end]) & 0xC0) == 0x80) --end;
            fitted.resize(end);
        }
        fitted += "...";
    }
    const ImVec4 clip(pos.x, pos.y, pos.x + std::max(0.0f, width), pos.y + fontSize + 2.0f);
    dl->AddText(font, fontSize, pos, ImGui::ColorConvertFloat4ToU32(color), fitted.c_str(),
                nullptr, 0.0f, &clip);
}

void DrawRaisedGraphNode(ImDrawList* dl, ImVec2 min, ImVec2 size, const ImVec4& face,
                         ImU32 border, float radius, float zoom, bool selected = false) {
    const ImVec2 max(min.x + size.x, min.y + size.y);
    const float shadowOffset = 2.0f * zoom;
    dl->AddRectFilled(ImVec2(min.x - zoom, min.y + shadowOffset),
                      ImVec2(max.x + zoom, max.y + shadowOffset + zoom),
                      IM_COL32(0, 0, 0, 22), radius + zoom);
    dl->AddRectFilled(ImVec2(min.x, min.y + shadowOffset),
                      ImVec2(max.x, max.y + shadowOffset),
                      IM_COL32(0, 0, 0, 65), radius);

    const ImDrawListFlags savedFlags = dl->Flags;
    dl->Flags |= ImDrawListFlags_AntiAliasedLines;
    dl->Flags &= ~ImDrawListFlags_AntiAliasedLinesUseTex;
    if (selected) {
        const ImVec4 accent = ImGui::ColorConvertU32ToFloat4(border);
        for (int layer = 3; layer >= 1; --layer) {
            const float spread = layer * zoom;
            dl->AddRect(ImVec2(min.x - spread, min.y - spread),
                        ImVec2(max.x + spread, max.y + spread),
                        ImGui::ColorConvertFloat4ToU32(
                            UiTokens::WithAlpha(accent, (4 - layer) * 0.025f)),
                        radius + spread, 0, std::max(1.0f, zoom * 1.5f));
        }
    }

    auto shade = [&](float amount) {
        return ImVec4(std::clamp(face.x + amount, 0.0f, 1.0f),
                      std::clamp(face.y + amount, 0.0f, 1.0f),
                      std::clamp(face.z + amount, 0.0f, 1.0f), face.w);
    };
    const int firstVertex = dl->VtxBuffer.Size;
    dl->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(face), radius);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(
        dl, firstVertex, dl->VtxBuffer.Size, min, ImVec2(min.x, max.y),
        ImGui::ColorConvertFloat4ToU32(shade(0.065f)),
        ImGui::ColorConvertFloat4ToU32(shade(-0.045f)));

    const float inset = std::max(zoom, 0.5f);
    const float stroke = std::max(0.65f, zoom);
    dl->AddRect(ImVec2(min.x + inset, min.y + inset),
                ImVec2(max.x - inset, max.y - inset), IM_COL32(255, 255, 255, 14),
                std::max(0.0f, radius - inset), 0, stroke);
    const float innerRadius = std::max(0.0f, radius - inset);
    const int shineStart = dl->VtxBuffer.Size;
    dl->PathArcToFast(ImVec2(min.x + inset + innerRadius, min.y + inset + innerRadius),
                      innerRadius, 6, 9);
    dl->PathArcToFast(ImVec2(max.x - inset - innerRadius, min.y + inset + innerRadius),
                      innerRadius, 9, 12);
    dl->PathStroke(IM_COL32(255, 255, 255, 62), 0, stroke);
    // Follow both top corners, then fade the light down the curved sides.
    for (int vertex = shineStart; vertex < dl->VtxBuffer.Size; ++vertex) {
        ImDrawVert& v = dl->VtxBuffer[vertex];
        const float fade = 1.0f - std::clamp(
            (v.pos.y - min.y - inset) / std::max(innerRadius, 1.0f), 0.0f, 1.0f);
        const ImU32 alpha = static_cast<ImU32>(((v.col >> IM_COL32_A_SHIFT) & 255u) * fade);
        v.col = (v.col & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
    }
    dl->AddLine(ImVec2(min.x + radius + inset, max.y - inset),
                ImVec2(max.x - radius - inset, max.y - inset),
                IM_COL32(0, 0, 0, 58), stroke);
    dl->AddRect(min, max, border, radius, 0, selected ? 2.0f : 1.0f);
    dl->Flags = savedFlags;
}

void DrawSpecialNode(ImDrawList* dl, ImVec2 min, ImVec2 size, const char* label,
                     const ImVec4& accent, bool hovered, float zoom) {
    const float radius = size.y * 0.5f;
    DrawRaisedGraphNode(dl, min, size,
        hovered ? EditorTheme::SurfaceHover : EditorTheme::PanelFace,
        ImGui::ColorConvertFloat4ToU32(UiTokens::WithAlpha(accent, hovered ? 1.0f : 0.6f)),
        radius, zoom);
    dl->AddCircleFilled(ImVec2(min.x + 16.0f * zoom, min.y + size.y * 0.5f),
                         3.0f * zoom, ImGui::ColorConvertFloat4ToU32(accent));
    DrawGraphText(dl, ImVec2(min.x + 28.0f * zoom,
                            min.y + (size.y - std::max(9.0f, ImGui::GetFontSize() * zoom)) * 0.5f),
                   size.x - 40.0f * zoom, label, EditorTheme::TextPrimary, zoom);
}

void DrawTransitionArrow(ImDrawList* dl, ImVec2 fromPort, ImVec2 toPort, ImU32 color, bool selected,
                         const char* label = nullptr, float bend = 0.0f) {
    ImVec2 cp1, cp2;
    BezierControlPoints(fromPort, toPort, cp1, cp2, bend);

    const float thickness = selected ? 2.5f : 1.5f;
    dl->AddBezierCubic(fromPort, cp1, cp2, toPort, color, thickness);

    const ImVec2 tip = CubicPoint(fromPort, cp1, cp2, toPort, 0.57f);
    const ImVec2 beforeTip = CubicPoint(fromPort, cp1, cp2, toPort, 0.53f);
    const ImVec2 dir(tip.x - beforeTip.x, tip.y - beforeTip.y);
    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    if (len > 1e-3f) {
        const ImVec2 n(dir.x / len, dir.y / len);
        const ImVec2 p(-n.y, n.x);
        const float s = selected ? 8.0f : 7.0f;
        dl->AddTriangleFilled(tip,
                              ImVec2(tip.x - n.x * s + p.x * s * 0.5f, tip.y - n.y * s + p.y * s * 0.5f),
                              ImVec2(tip.x - n.x * s - p.x * s * 0.5f, tip.y - n.y * s - p.y * s * 0.5f),
                              color);
    }

    if (label && label[0]) {
        ImVec2 mid = CubicPoint(fromPort, cp1, cp2, toPort, 0.5f);
        mid.y -= ImGui::GetTextLineHeight() + 8.0f;
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        const ImVec2 pad(6.0f, 3.0f);
        const ImVec2 boxMin(mid.x - textSize.x * 0.5f - pad.x, mid.y - textSize.y * 0.5f - pad.y);
        const ImVec2 boxMax(mid.x + textSize.x * 0.5f + pad.x, mid.y + textSize.y * 0.5f + pad.y);
        dl->AddRectFilled(boxMin, boxMax,
                          ImGui::ColorConvertFloat4ToU32(EditorTheme::PanelFace), 4.0f);
        dl->AddRect(boxMin, boxMax, ImGui::ColorConvertFloat4ToU32(EditorTheme::Border), 4.0f);
        dl->AddText(ImVec2(mid.x - textSize.x * 0.5f, mid.y - textSize.y * 0.5f),
                    ImGui::ColorConvertFloat4ToU32(EditorTheme::TextSecondary), label);
    }
}

glm::vec2 SnapGraphPosition(glm::vec2 pos) {
    return glm::vec2(std::round(pos.x / kGridStep) * kGridStep,
                     std::round(pos.y / kGridStep) * kGridStep);
}

std::string SummarizeTransitionLabel(const AnimatorTransitionDef& tr) {
    if (tr.conditions.empty())
        return tr.hasExitTime ? "Exit" : "";
    std::string out;
    for (size_t i = 0; i < tr.conditions.size() && i < 2; ++i) {
        const auto& c = tr.conditions[i];
        if (i)
            out += " && ";
        out += c.parameter;
        switch (c.mode) {
        case AnimatorConditionMode::Greater: out += " > "; break;
        case AnimatorConditionMode::Less: out += " < "; break;
        case AnimatorConditionMode::Equals: out += " == "; break;
        case AnimatorConditionMode::NotEquals: out += " != "; break;
        case AnimatorConditionMode::IfTrue: out += " true"; break;
        case AnimatorConditionMode::IfFalse: out += " false"; break;
        }
        if (c.mode == AnimatorConditionMode::Greater || c.mode == AnimatorConditionMode::Less ||
            c.mode == AnimatorConditionMode::Equals || c.mode == AnimatorConditionMode::NotEquals)
        {
            char threshold[32];
            std::snprintf(threshold, sizeof(threshold), "%.3g", c.threshold);
            out += threshold;
        }
    }
    if (tr.conditions.size() > 2)
        out += "...";
    return out;
}

void DrawAnimatorParameterCombo(const AnimatorControllerAsset& asset, std::string& parameter,
                                bool& changed) {
    const char* preview = parameter.empty() ? "<parameter>" : parameter.c_str();
    if (EditorDropdown::BeginCombo("##Parameter", preview)) {
        for (const auto& p : asset.parameters) {
            if (EditorDropdown::Selectable(p.name.c_str(), p.name == parameter)) {
                parameter = p.name;
                changed = true;
            }
        }
        if (asset.parameters.empty())
            ImGui::TextColored(EditorTheme::TextMuted, "Add parameters in the left column.");
        EditorDropdown::EndCombo();
    }
}

const AnimatorParameterDef* FindAnimatorParameter(const AnimatorControllerAsset& asset,
                                                  const std::string& name) {
    for (const auto& parameter : asset.parameters)
        if (parameter.name == name)
            return &parameter;
    return nullptr;
}

void DuplicateAnimatorState(AnimatorControllerAsset& asset, int stateIndex, int& selectedState) {
    if (stateIndex < 0 || static_cast<size_t>(stateIndex) >= asset.states.size())
        return;
    AnimatorStateDef copy = asset.states[static_cast<size_t>(stateIndex)];
    copy.name = MakeUniqueAnimatorStateName(asset, copy.name);
    copy.graphPosition += glm::vec2(kGridStep, kGridStep);
    asset.states.push_back(copy);
    selectedState = static_cast<int>(asset.states.size()) - 1;
}

int HitTestAnyState(const GraphViewState& graph, ImVec2 canvasMin, ImVec2 pan, float zoom,
                    ImVec2 mouseScreen) {
    const ImVec2 nodeScreen = GraphToScreen(canvasMin, pan, zoom, graph.anyStatePosition);
    const ImVec2 nodeSize(kSpecialNodeSize.x * zoom, kSpecialNodeSize.y * zoom);
    if (mouseScreen.x >= nodeScreen.x && mouseScreen.x <= nodeScreen.x + nodeSize.x &&
        mouseScreen.y >= nodeScreen.y && mouseScreen.y <= nodeScreen.y + nodeSize.y)
        return 0;
    return -1;
}

bool HitTestEntry(const AnimatorControllerAsset& asset, ImVec2 canvasMin, ImVec2 pan, float zoom,
                   ImVec2 mouseScreen) {
    const ImVec2 node = GraphToScreen(canvasMin, pan, zoom, EntryPosition(asset));
    return ImRect(node, ImVec2(node.x + kSpecialNodeSize.x * zoom,
                               node.y + kSpecialNodeSize.y * zoom)).Contains(mouseScreen);
}

float DistancePointToSegment(ImVec2 p, ImVec2 a, ImVec2 b) {
    const float abx = b.x - a.x;
    const float aby = b.y - a.y;
    const float abLenSq = abx * abx + aby * aby;
    if (abLenSq < 1e-6f)
        return std::hypot(p.x - a.x, p.y - a.y);
    float t = ((p.x - a.x) * abx + (p.y - a.y) * aby) / abLenSq;
    t = std::clamp(t, 0.0f, 1.0f);
    const float qx = a.x + t * abx;
    const float qy = a.y + t * aby;
    return std::hypot(p.x - qx, p.y - qy);
}

float DistancePointToBezier(ImVec2 p, ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3) {
    float best = 1e9f;
    ImVec2 prev = p0;
    constexpr int kSamples = 24;
    for (int i = 1; i <= kSamples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSamples);
        const float u = 1.0f - t;
        const float uu = u * u;
        const float uuu = uu * u;
        const float tt = t * t;
        const float ttt = tt * t;
        const ImVec2 pt(
            uuu * p0.x + 3.0f * uu * t * p1.x + 3.0f * u * tt * p2.x + ttt * p3.x,
            uuu * p0.y + 3.0f * uu * t * p1.y + 3.0f * u * tt * p2.y + ttt * p3.y);
        best = std::min(best, DistancePointToSegment(p, prev, pt));
        prev = pt;
    }
    return best;
}

ImVec2 NodeOutputPortScreen(ImVec2 nodeScreen, ImVec2 nodeSize) {
    return ImVec2(nodeScreen.x + nodeSize.x, nodeScreen.y + nodeSize.y * 0.5f);
}

int HitTestAnyStatePort(const GraphViewState& graph, ImVec2 canvasMin, ImVec2 pan, float zoom,
                        ImVec2 mouseScreen) {
    const ImVec2 nodeScreen = GraphToScreen(canvasMin, pan, zoom, graph.anyStatePosition);
    const ImVec2 nodeSize(kSpecialNodeSize.x * zoom, kSpecialNodeSize.y * zoom);
    const ImVec2 port = NodeOutputPortScreen(nodeScreen, nodeSize);
    const float hitR = kPortHitRadius * zoom;
    return std::hypot(mouseScreen.x - port.x, mouseScreen.y - port.y) <= hitR ? 0 : -1;
}

ImVec2 RoundedNodeEdgeToward(ImVec2 rectMin, ImVec2 rectSize, ImVec2 target,
                             float radius, ImVec2 offset = ImVec2(0, 0)) {
    const ImVec2 center(rectMin.x + rectSize.x * 0.5f, rectMin.y + rectSize.y * 0.5f);
    const ImVec2 origin(center.x + offset.x, center.y + offset.y);
    ImVec2 direction(target.x - origin.x, target.y - origin.y);
    if (std::hypot(direction.x, direction.y) < 1e-4f) direction = ImVec2(1, 0);
    radius = std::clamp(radius, 0.0f, std::max(0.0f, std::min(rectSize.x, rectSize.y) * 0.5f - 1.0f));
    float far = FLT_MAX;
    if (std::abs(direction.x) > 1e-5f)
        far = std::min(far, ((direction.x > 0 ? rectMin.x + rectSize.x : rectMin.x) - origin.x) / direction.x);
    if (std::abs(direction.y) > 1e-5f)
        far = std::min(far, ((direction.y > 0 ? rectMin.y + rectSize.y : rectMin.y) - origin.y) / direction.y);
    float near = 0.0f;
    // Intersect the actual rounded outline, also for offset parallel links and capsule nodes.
    for (int iteration = 0; iteration < 24; ++iteration) {
        const float t = (near + far) * 0.5f;
        const float x = std::abs(origin.x + direction.x * t - center.x) - (rectSize.x * 0.5f - radius);
        const float y = std::abs(origin.y + direction.y * t - center.y) - (rectSize.y * 0.5f - radius);
        const float distance = std::hypot(std::max(x, 0.0f), std::max(y, 0.0f)) + std::min(std::max(x, y), 0.0f);
        if (distance <= radius) near = t; else far = t;
    }
    return ImVec2(origin.x + direction.x * near, origin.y + direction.y * near);
}

bool TransitionEndpoints(const AnimatorControllerAsset& asset, const GraphViewState& graph,
                         ImVec2 canvasMin, ImVec2 pan, float zoom,
                         const AnimatorTransitionDef& transition,
                         ImVec2& outFrom, ImVec2& outTo) {
    const int toIndex = FindStateIndex(asset, transition.toState);
    if (toIndex < 0)
        return false;

    const ImVec2 toMin = GraphToScreen(canvasMin, pan, zoom,
        asset.states[static_cast<size_t>(toIndex)].graphPosition);
    const ImVec2 toSize(kNodeSize.x * zoom, kNodeSize.y * zoom);
    const ImVec2 toCenter(toMin.x + toSize.x * 0.5f, toMin.y + toSize.y * 0.5f);

    ImVec2 fromMin{};
    ImVec2 fromSize{};
    if (transition.fromState == kAnimatorAnyStateName) {
        fromMin = GraphToScreen(canvasMin, pan, zoom, graph.anyStatePosition);
        fromSize = ImVec2(kSpecialNodeSize.x * zoom, kSpecialNodeSize.y * zoom);
    } else {
        const int fromIndex = FindStateIndex(asset, transition.fromState);
        if (fromIndex < 0)
            return false;
        fromMin = GraphToScreen(canvasMin, pan, zoom,
            asset.states[static_cast<size_t>(fromIndex)].graphPosition);
        fromSize = ImVec2(kNodeSize.x * zoom, kNodeSize.y * zoom);
        if (fromIndex == toIndex) {
            outFrom = ImVec2(fromMin.x + fromSize.x, fromMin.y + fromSize.y * 0.68f);
            outTo = ImVec2(fromMin.x + fromSize.x, fromMin.y + fromSize.y * 0.32f);
            return true;
        }
    }

    const ImVec2 fromCenter(fromMin.x + fromSize.x * 0.5f, fromMin.y + fromSize.y * 0.5f);
    const bool hasReverse = std::any_of(
        asset.transitions.begin(), asset.transitions.end(),
        [&](const AnimatorTransitionDef& other) {
            return other.fromState == transition.toState &&
                   other.toState == transition.fromState;
        });
    ImVec2 offset(0, 0);
    if (hasReverse) {
        const float dx = toCenter.x - fromCenter.x;
        const float dy = toCenter.y - fromCenter.y;
        const float length = std::max(std::hypot(dx, dy), 1.0f);
        offset = ImVec2(-dy / length * 5.0f * zoom, dx / length * 5.0f * zoom);
    }
    outFrom = RoundedNodeEdgeToward(fromMin, fromSize, AddImVec2(toCenter, offset),
        transition.fromState == kAnimatorAnyStateName ? fromSize.y * 0.5f : 4.0f * zoom, offset);
    outTo = RoundedNodeEdgeToward(toMin, toSize, AddImVec2(fromCenter, offset), 4.0f * zoom, offset);
    return true;
}

float TransitionBend(const AnimatorControllerAsset& asset,
                     const AnimatorTransitionDef& transition) {
    if (transition.fromState == transition.toState)
        return 42.0f;
    const bool hasReverse = std::any_of(
        asset.transitions.begin(), asset.transitions.end(),
        [&](const AnimatorTransitionDef& other) {
            return other.fromState == transition.toState &&
                   other.toState == transition.fromState;
        });
    if (!hasReverse)
        return 0.0f;
    // Reciprocal links are separated by offsetting their endpoints, keeping
    // both paths straight and parallel instead of eye-shaped.
    return 0.0f;
}

int HitTestTransition(const AnimatorControllerAsset& asset, const GraphViewState& graph,
                      ImVec2 canvasMin, ImVec2 pan, float zoom, ImVec2 mouseScreen,
                      float maxDistance) {
    int best = -1;
    float bestDist = maxDistance;
    for (size_t ti = 0; ti < asset.transitions.size(); ++ti) {
        const auto& tr = asset.transitions[ti];
        ImVec2 fromPort{};
        ImVec2 toPort{};
        if (!TransitionEndpoints(asset, graph, canvasMin, pan, zoom, tr, fromPort, toPort))
            continue;

        ImVec2 cp1, cp2;
        BezierControlPoints(fromPort, toPort, cp1, cp2, TransitionBend(asset, tr));
        const float dist = DistancePointToBezier(mouseScreen, fromPort, cp1, cp2, toPort);
        if (dist < bestDist) {
            bestDist = dist;
            best = static_cast<int>(ti);
        }
    }
    return best;
}

int HitTestState(const AnimatorControllerAsset& asset, ImVec2 canvasMin, ImVec2 pan, float zoom,
                 ImVec2 mouseScreen) {
    for (int i = static_cast<int>(asset.states.size()) - 1; i >= 0; --i) {
        const glm::vec2 gp = asset.states[static_cast<size_t>(i)].graphPosition;
        const ImVec2 ns = GraphToScreen(canvasMin, pan, zoom, gp);
        const ImVec2 nodeSize(kNodeSize.x * zoom, kNodeSize.y * zoom);
        const float portStrip = (kPortHitRadius + 4.0f) * zoom;
        const ImVec2 bodyMax(ns.x + nodeSize.x - portStrip, ns.y + nodeSize.y);
        if (mouseScreen.x >= ns.x && mouseScreen.x <= bodyMax.x && mouseScreen.y >= ns.y &&
            mouseScreen.y <= bodyMax.y) {
            return i;
        }
    }
    return -1;
}

int HitTestOutputPort(const AnimatorControllerAsset& asset, ImVec2 canvasMin, ImVec2 pan,
                      float zoom, ImVec2 mouseScreen) {
    const float hitR = kPortHitRadius * zoom;
    int best = -1;
    float bestDist = hitR;
    for (int i = 0; i < static_cast<int>(asset.states.size()); ++i) {
        const glm::vec2 gp = asset.states[static_cast<size_t>(i)].graphPosition;
        const ImVec2 nodeScreen = GraphToScreen(canvasMin, pan, zoom, gp);
        const ImVec2 nodeSize(kNodeSize.x * zoom, kNodeSize.y * zoom);
        const ImVec2 port = NodeOutputPortScreen(nodeScreen, nodeSize);
        const float dist = std::hypot(mouseScreen.x - port.x, mouseScreen.y - port.y);
        if (dist <= hitR && dist < bestDist) {
            bestDist = dist;
            best = i;
        }
    }
    return best;
}

bool HitTestNodeOutputZone(ImVec2 nodeScreen, ImVec2 nodeSize, ImVec2 mouseScreen) {
    const float zoneLeft = nodeScreen.x + nodeSize.x * 0.65f;
    return mouseScreen.x >= zoneLeft && mouseScreen.x <= nodeScreen.x + nodeSize.x + kPortHitRadius &&
           mouseScreen.y >= nodeScreen.y && mouseScreen.y <= nodeScreen.y + nodeSize.y;
}

/// State or Any State handle used to start a transition drag.
int HitTestLinkStart(const AnimatorControllerAsset& asset, const GraphViewState& graph,
                     ImVec2 canvasMin, ImVec2 pan, float zoom, ImVec2 mouseScreen) {
    if (HitTestAnyStatePort(graph, canvasMin, pan, zoom, mouseScreen) >= 0)
        return kLinkFromAnyState;
    return HitTestOutputPort(asset, canvasMin, pan, zoom, mouseScreen);
}

int HitTestStateForLinkTarget(const AnimatorControllerAsset& asset, ImVec2 canvasMin, ImVec2 pan,
                              float zoom, ImVec2 mouseScreen) {
    for (int i = static_cast<int>(asset.states.size()) - 1; i >= 0; --i) {
        const glm::vec2 gp = asset.states[static_cast<size_t>(i)].graphPosition;
        const ImVec2 ns = GraphToScreen(canvasMin, pan, zoom, gp);
        const ImVec2 nodeSize(kNodeSize.x * zoom, kNodeSize.y * zoom);
        const ImVec2 ne(ns.x + nodeSize.x, ns.y + nodeSize.y);
        if (mouseScreen.x >= ns.x && mouseScreen.x <= ne.x && mouseScreen.y >= ns.y &&
            mouseScreen.y <= ne.y) {
            return i;
        }
    }
    return -1;
}

void DrawNodePort(ImDrawList* dl, ImVec2 center, float radius, ImU32 fill, ImU32 border,
                  bool hovered) {
    const float r = radius + (hovered ? 2.0f : 0.0f);
    dl->AddCircleFilled(center, r, fill);
    dl->AddCircle(center, r, border, 0, hovered ? 2.0f : 1.5f);
}

void RemoveAnimatorState(AnimatorControllerAsset& asset, int stateIndex, int& selectedState,
                         int& selectedTransition) {
    if (stateIndex < 0 || static_cast<size_t>(stateIndex) >= asset.states.size())
        return;

    const std::string removed = asset.states[static_cast<size_t>(stateIndex)].name;
    asset.states.erase(asset.states.begin() + stateIndex);

    for (auto& tr : asset.transitions) {
        if (tr.fromState == removed)
            tr.fromState.clear();
        if (tr.toState == removed)
            tr.toState.clear();
    }
    asset.transitions.erase(
        std::remove_if(asset.transitions.begin(), asset.transitions.end(),
                       [](const AnimatorTransitionDef& tr) {
                           return tr.fromState.empty() || tr.toState.empty();
                       }),
        asset.transitions.end());

    if (asset.defaultState == removed)
        asset.defaultState = asset.states.empty() ? std::string{} : asset.states[0].name;

    selectedState = -1;
    selectedTransition = -1;
}

bool AnimatorControllerWindowFocused() {
    ImGuiWindow* window = ImGui::FindWindowByName(kAnimatorControllerWindowTitle);
    return window && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
}

bool GraphDeleteButton(const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Text, UiTokens::Hex(0xFF9A86));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UiTokens::WithAlpha(EditorTheme::Danger, 0.22f));
    const bool pressed = ImGui::Button(label, ImVec2(-1.0f, EditorTheme::ButtonHeight));
    ImGui::PopStyleColor(2);
    return pressed;
}

void DetailsSection(const char* label) {
    ImGui::Dummy(ImVec2(0.0f, 5.0f));
    ImGui::SeparatorText(label);
}

void DetailsLabel(const char* label) {
    ImGui::TextColored(EditorTheme::TextSecondary, "%s", label);
}

// A weight diagram derived from the runtime's linear crossfade, in seconds.
void DrawBlendDiagram(const AnimatorTransitionDef& tr) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::InvisibleButton("##BlendDiagram", ImVec2(width, 90.0f));
    auto* draw = ImGui::GetWindowDrawList();
    const auto color = [](const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); };
    draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + 90.0f),
                         color(EditorTheme::WorkArea), 4.0f);
    const float left = origin.x + 10.0f, right = origin.x + width - 10.0f;
    const float top = origin.y + 27.0f, bottom = origin.y + 64.0f;
    const float range = std::max(0.5f, tr.duration * 1.5f);
    const float end = left + (right - left) * std::clamp(tr.duration / range, 0.0f, 1.0f);
    for (int i = 0; i <= 4; ++i) {
        const float x = left + (right - left) * i / 4.0f;
        draw->AddLine(ImVec2(x, top), ImVec2(x, bottom), color(EditorTheme::Border));
    }
    draw->AddLine(ImVec2(left, bottom), ImVec2(right, bottom), color(EditorTheme::BorderStrong));
    draw->AddText(ImVec2(left, origin.y + 7.0f), color(EditorTheme::TextMuted), "Blend weights");
    draw->AddLine(ImVec2(left, top), ImVec2(end, bottom), color(EditorTheme::TextSecondary), 2.0f);
    draw->AddLine(ImVec2(left, bottom), ImVec2(end, top), color(EditorTheme::AccentHover), 2.0f);
    draw->AddLine(ImVec2(end, top), ImVec2(right, top), color(EditorTheme::AccentHover), 2.0f);
    char seconds[32];
    std::snprintf(seconds, sizeof(seconds), "%.2f s", tr.duration);
    draw->AddText(ImVec2(left, bottom + 7.0f), color(EditorTheme::TextMuted), "0");
    draw->AddText(ImVec2(std::clamp(end - ImGui::CalcTextSize(seconds).x * 0.5f,
                                   left + 16.0f, right - ImGui::CalcTextSize(seconds).x), bottom + 7.0f),
                  color(EditorTheme::TextSecondary), seconds);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Source weight (gray) fades out as destination weight (blue) fades in.\n"
                          "Duration is in seconds; zero switches immediately.");
}

void DrawStateProperties(AnimatorControllerAsset& asset, int stateIndex,
                         const std::shared_ptr<SkeletalModelAsset>& model,
                         const std::vector<std::string>& availableClips, bool& changed,
                         bool& requestDelete) {
    if (stateIndex < 0 || static_cast<size_t>(stateIndex) >= asset.states.size())
        return;

    auto& st = asset.states[static_cast<size_t>(stateIndex)];
    ImGui::TextColored(EditorTheme::AccentHover, "STATE");
    ImGui::TextWrapped("%s", st.name.c_str());
    DetailsSection("Animation");
    ImGui::PushItemWidth(-1.0f);
    DetailsLabel("Name");
    char nameBuf[128] = {};
    std::strncpy(nameBuf, st.name.c_str(), sizeof(nameBuf) - 1);
    if (ImGui::InputText("##StateName", nameBuf, sizeof(nameBuf))) {
        const std::string oldName = st.name;
        st.name = nameBuf;
        if (oldName != st.name) {
            for (auto& tr : asset.transitions) {
                if (tr.fromState == oldName)
                    tr.fromState = st.name;
                if (tr.toState == oldName)
                    tr.toState = st.name;
            }
            if (asset.defaultState == oldName)
                asset.defaultState = st.name;
        }
        changed = true;
    }
    DetailsLabel("Animation Clip");
    DrawAnimatorClipField("##StateClip", st.clipName, st.clipStackIndex, &st.clipSourceModelPath,
                          asset.sourceModelPath, model, availableClips, changed);
    DetailsSection("Playback");
    DetailsLabel("Playback Speed");
    if (ImGui::DragFloat("##StateSpeed", &st.speed, 0.01f, 0.0f, 10.0f, "%.2fx"))
        changed = true;
    DetailsLabel("Start Position");
    if (EditorTheme::SliderFloat("##StateOffset", &st.startOffset, 0.0f, 1.0f, "%.3f",
                         ImGuiSliderFlags_AlwaysClamp))
        changed = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Normalized clip position used when this state begins (0 to 1).");
    if (EditorTheme::Checkbox("Loop", &st.loop))
        changed = true;
    DetailsSection("State Actions");
    ImGui::BeginDisabled(asset.defaultState == st.name);
    if (ImGui::Button(asset.defaultState == st.name ? "Default State" : "Set as Default State",
                      ImVec2(-1.0f, 0.0f))) {
        asset.defaultState = st.name;
        changed = true;
    }
    ImGui::EndDisabled();
    if (GraphDeleteButton("Delete State")) {
        requestDelete = true;
    }
    ImGui::PopItemWidth();
}

void DrawTransitionProperties(AnimatorControllerAsset& asset, int trIndex, bool& changed,
                              bool& requestDelete) {
    if (trIndex < 0 || static_cast<size_t>(trIndex) >= asset.transitions.size())
        return;

    auto& tr = asset.transitions[static_cast<size_t>(trIndex)];
    ImGui::TextColored(EditorTheme::AccentHover, "TRANSITION");
    ImGui::TextWrapped("%s  ->  %s", tr.fromState.c_str(), tr.toState.c_str());
    DetailsSection("Route");
    ImGui::PushItemWidth(-1.0f);
    DetailsLabel("Source");
    if (EditorDropdown::BeginCombo("##From", tr.fromState.c_str())) {
        if (EditorDropdown::Selectable("Any State", tr.fromState == kAnimatorAnyStateName)) {
            tr.fromState = kAnimatorAnyStateName;
            changed = true;
        }
        for (const auto& st : asset.states) {
            if (EditorDropdown::Selectable(st.name.c_str(), st.name == tr.fromState)) {
                tr.fromState = st.name;
                changed = true;
            }
        }
        EditorDropdown::EndCombo();
    }
    DetailsLabel("Destination");
    if (EditorDropdown::BeginCombo("##To", tr.toState.c_str())) {
        for (const auto& st : asset.states) {
            if (EditorDropdown::Selectable(st.name.c_str(), st.name == tr.toState)) {
                tr.toState = st.name;
                changed = true;
            }
        }
        EditorDropdown::EndCombo();
    }
    DetailsSection("Timing");
    DrawBlendDiagram(tr);
    DetailsLabel("Blend Duration");
    if (ImGui::DragFloat("##Duration", &tr.duration, 0.01f, 0.0f, 5.0f, "%.3f s",
                         ImGuiSliderFlags_AlwaysClamp))
        changed = true;
    if (EditorTheme::Checkbox("Wait for Exit Time", &tr.hasExitTime))
        changed = true;
    if (tr.hasExitTime) {
        DetailsLabel("Exit Position");
        if (EditorTheme::SliderFloat("##ExitTime", &tr.exitTime, 0.0f, 1.0f, "%.3f",
                               ImGuiSliderFlags_AlwaysClamp))
            changed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Normalized source clip position (0 = start, 1 = end).");
    }

    DetailsSection("Conditions");
    if (tr.conditions.empty())
        ImGui::TextWrapped("No parameter conditions. Timing alone controls this transition.");
    else
        ImGui::TextColored(EditorTheme::TextMuted, "All conditions must pass");

    int condRemove = -1;
    for (size_t ci = 0; ci < tr.conditions.size(); ++ci) {
        auto& c = tr.conditions[ci];
        ImGui::PushID(static_cast<int>(ci));
        const float rowWidth = ImGui::GetContentRegionAvail().x;
        ImGui::SetNextItemWidth(rowWidth - 36.0f);
        const std::string previousParameter = c.parameter;
        DrawAnimatorParameterCombo(asset, c.parameter, changed);
        ImGui::SameLine();
        if (ImGui::Button("-##RemoveCondition", ImVec2(28.0f, ImGui::GetFrameHeight())))
            condRemove = static_cast<int>(ci);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove this condition");
        const AnimatorParameterDef* parameter = FindAnimatorParameter(asset, c.parameter);
        if (c.parameter != previousParameter && parameter) {
            c.mode = (parameter->type == AnimatorParamType::Bool ||
                      parameter->type == AnimatorParamType::Trigger)
                         ? AnimatorConditionMode::IfTrue
                         : AnimatorConditionMode::Greater;
        }

        if (parameter && (parameter->type == AnimatorParamType::Bool ||
                          parameter->type == AnimatorParamType::Trigger)) {
            bool positive = c.mode != AnimatorConditionMode::IfFalse;
            const char* modes = parameter->type == AnimatorParamType::Trigger
                ? "Triggered\0Not Triggered\0"
                : "True\0False\0";
            int boolMode = positive ? 0 : 1;
            if (EditorDropdown::Combo("##Condition", &boolMode, modes)) {
                c.mode = boolMode == 0 ? AnimatorConditionMode::IfTrue
                                       : AnimatorConditionMode::IfFalse;
                changed = true;
            }
        } else {
            int numericMode = 0;
            if (c.mode == AnimatorConditionMode::Less) numericMode = 1;
            else if (c.mode == AnimatorConditionMode::Equals) numericMode = 2;
            else if (c.mode == AnimatorConditionMode::NotEquals) numericMode = 3;
            const bool integer = parameter && parameter->type == AnimatorParamType::Int;
            const char* modes = integer ? "Greater\0Less\0Equals\0Not Equal\0"
                                        : "Greater\0Less\0";
            if (!integer)
                numericMode = std::min(numericMode, 1);
            ImGui::SetNextItemWidth((rowWidth - 8.0f) * 0.55f);
            if (EditorDropdown::Combo("##Condition", &numericMode, modes)) {
                static constexpr AnimatorConditionMode kModes[] = {
                    AnimatorConditionMode::Greater, AnimatorConditionMode::Less,
                    AnimatorConditionMode::Equals, AnimatorConditionMode::NotEquals,
                };
                c.mode = kModes[numericMode];
                changed = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth((rowWidth - 8.0f) * 0.45f);
            if (integer) {
                int threshold = static_cast<int>(c.threshold);
                if (ImGui::DragInt("##Threshold", &threshold)) {
                    c.threshold = static_cast<float>(threshold);
                    changed = true;
                }
            } else if (ImGui::DragFloat("##Threshold", &c.threshold, 0.01f)) {
                changed = true;
            }
        }
        if (!parameter)
            ImGui::TextColored(EditorTheme::Danger, "Parameter no longer exists");
        ImGui::Dummy(ImVec2(0.0f, 3.0f));
        ImGui::Separator();
        ImGui::PopID();
    }
    if (condRemove >= 0) {
        tr.conditions.erase(tr.conditions.begin() + condRemove);
        changed = true;
    }
    ImGui::BeginDisabled(asset.parameters.empty());
    if (ImGui::Button("+ Add Condition", ImVec2(-1.0f, 0.0f))) {
        const auto& p = asset.parameters.front();
        const bool flag = p.type == AnimatorParamType::Bool || p.type == AnimatorParamType::Trigger;
        tr.conditions.push_back({p.name, flag ? AnimatorConditionMode::IfTrue
                                             : AnimatorConditionMode::Greater, 0.0f});
        changed = true;
    }
    ImGui::EndDisabled();
    if (asset.parameters.empty()) ImGui::TextWrapped("Add a parameter in the left panel first.");
    DetailsSection("Transition Actions");
    if (GraphDeleteButton("Delete Transition")) {
        requestDelete = true;
    }
    ImGui::PopItemWidth();
}

} // namespace

void ClearAnimatorGraphSelection() {
    g_Graph.selectedState = -1;
    g_Graph.selectedTransition = -1;
    g_Graph.draggingNode = -1;
    g_Graph.draggingAnyState = false;
    g_Graph.draggingEntry = false;
    g_Graph.linkingActive = false;
    g_Graph.linkClickMode = false;
    g_Graph.linkDragFrom = -1;
}

bool DrawAnimatorGraphSelectionInspector(EditorApp* editor, const std::string& projectRelPath) {
    if (projectRelPath.empty() || g_Graph.path != projectRelPath ||
        (g_Graph.selectedState < 0 && g_Graph.selectedTransition < 0))
        return false;

    if (editor && !editor->RecordAssetUndoSnapshot(AssetManager::Get().ToAbsolute(projectRelPath))) return false;
    std::string error;
    auto asset = AssetManager::Get().GetAnimatorController(projectRelPath, error);
    if (!asset || g_Graph.selectedState >= static_cast<int>(asset->states.size()) ||
        g_Graph.selectedTransition >= static_cast<int>(asset->transitions.size())) {
        ClearAnimatorGraphSelection();
        return false;
    }
    std::shared_ptr<SkeletalModelAsset> sourceModel;
    std::vector<std::string> availableClips;
    if (!asset->sourceModelPath.empty()) {
        sourceModel = AssetManager::Get().GetSkeletalModel(asset->sourceModelPath);
        availableClips = LoadAnimationClipNames(asset->sourceModelPath);
    }
    bool changed = false;
    bool requestDeleteState = false;
    bool requestDeleteTransition = false;
    ImGui::PushID(projectRelPath.c_str());
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 7.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, 5.0f));
    const std::string filename = projectRelPath.substr(projectRelPath.find_last_of("/\\") + 1);
    ImGui::TextColored(EditorTheme::TextMuted, "%s", filename.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", projectRelPath.c_str());
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    if (g_Graph.selectedState >= 0) {
        DrawStateProperties(*asset, g_Graph.selectedState, sourceModel, availableClips, changed,
                            requestDeleteState);
        ImGui::Dummy(ImVec2(0.0f, 5.0f));
        ImGui::SeparatorText("Outgoing Transitions");
        const std::string selectedName =
            asset->states[static_cast<size_t>(g_Graph.selectedState)].name;
        bool hasOutgoing = false;
        for (size_t i = 0; i < asset->transitions.size(); ++i) {
            const auto& transition = asset->transitions[i];
            if (transition.fromState != selectedName)
                continue;
            hasOutgoing = true;
            const std::string label = "-> " + transition.toState;
            ImGui::PushID(static_cast<int>(i));
            if (EditorDropdown::Selectable(label.c_str(), false, 0, ImVec2(0.0f, 26.0f))) {
                g_Graph.selectedTransition = static_cast<int>(i);
                g_Graph.selectedState = -1;
            }
            ImGui::PopID();
        }
        if (!hasOutgoing)
            ImGui::TextColored(EditorTheme::TextMuted, "None");
        if (ImGui::Button("Make Transition", ImVec2(-1.0f, 0.0f))) {
            g_Graph.linkDragFrom = FindStateIndex(*asset, selectedName);
            g_Graph.linkingActive = true;
            g_Graph.linkClickMode = true;
        }
    } else if (g_Graph.selectedTransition >= 0) {
        DrawTransitionProperties(*asset, g_Graph.selectedTransition, changed,
                                 requestDeleteTransition);
    }
    ImGui::PopStyleVar(2);
    ImGui::PopID();

    if (requestDeleteState) {
        RemoveAnimatorState(*asset, g_Graph.selectedState, g_Graph.selectedState,
                            g_Graph.selectedTransition);
        ClearAnimatorGraphSelection();
        changed = true;
    } else if (requestDeleteTransition) {
        asset->transitions.erase(asset->transitions.begin() + g_Graph.selectedTransition);
        ClearAnimatorGraphSelection();
        changed = true;
    }
    if (changed) {
        if (editor) editor->RecordUndoSnapshot();
        CommitAnimatorController(projectRelPath, *asset,
                                 editor ? &editor->GetEngine()->GetScene() : nullptr);
    }
    return true;
}

void AutoLayoutAnimatorStates(AnimatorControllerAsset& asset) {
    const float spacingX = 220.0f;
    const float spacingY = 90.0f;
    for (size_t i = 0; i < asset.states.size(); ++i) {
        asset.states[i].graphPosition = glm::vec2((static_cast<float>(i % 4)) * spacingX,
                                                  (static_cast<float>(i / 4)) * spacingY);
    }
}

bool DrawAnimatorControllerGraphEditor(EditorApp* editor, const std::string& projectRelPath,
                                       std::shared_ptr<AnimatorControllerAsset>& asset,
                                       std::vector<std::string>& availableClips,
                                       bool& outChanged) {
    if (!asset)
        return false;
    if (editor && !editor->RecordAssetUndoSnapshot(AssetManager::Get().ToAbsolute(projectRelPath))) return false;

    std::shared_ptr<SkeletalModelAsset> sourceModel;
    if (!asset->sourceModelPath.empty())
        sourceModel = AssetManager::Get().GetSkeletalModel(asset->sourceModelPath);

    if (g_Graph.path != projectRelPath) {
        g_Graph = GraphViewState{};
        g_Graph.path = projectRelPath;
    }
    g_Graph.anyStatePosition = asset->anyStateGraphPosition;

    if (!g_Graph.didAutoLayout && StatesNeedAutoLayout(*asset)) {
        AutoLayoutAnimatorStates(*asset);
        g_Graph.didAutoLayout = true;
        g_Graph.didInitialFrame = false;
        outChanged = true;
    }

    EditorToolbar toolbar;

    const std::string addClipPopupId = "AddClipGraph##" + projectRelPath;
    if (!availableClips.empty()) {
        if (toolbar.Button("+ Animation")) {
            ImGui::OpenPopup(addClipPopupId.c_str());
        }
    }
    if (toolbar.Button("+ State")) {
        AnimatorStateDef st{};
        st.name = MakeUniqueAnimatorStateName(*asset, "New State");
        st.clipName = availableClips.empty()
            ? (asset->states.empty() ? "Take 001" : asset->states[0].clipName)
            : availableClips[0];
        st.graphPosition = glm::vec2(40.0f + static_cast<float>(asset->states.size()) * 40.0f,
                                     40.0f + static_cast<float>(asset->states.size()) * 20.0f);
        asset->states.push_back(st);
        if (asset->defaultState.empty())
            asset->defaultState = st.name;
        g_Graph.selectedState = static_cast<int>(asset->states.size()) - 1;
        outChanged = true;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 7.0f));
    if (!availableClips.empty() && ImGui::BeginPopup(addClipPopupId.c_str())) {
        if (sourceModel && !sourceModel->animationNames.empty()) {
            for (size_t i = 0; i < sourceModel->animationNames.size(); ++i) {
                const std::string& clip = sourceModel->animationNames[i];
                const int stackIdx = static_cast<int>(sourceModel->animationStackIndices[i]);
                const bool used = StateUsesAnimStack(*asset, asset->sourceModelPath, stackIdx);
                const std::string row = clip + "  [stack " + std::to_string(stackIdx) + "]";
                if (used)
                    ImGui::BeginDisabled();
                if (EditorDropdown::Selectable(row.c_str(), false) && !used) {
                    AddAnimatorStateForClip(*asset, clip, glm::vec2{ -1.0f, -1.0f }, stackIdx);
                    g_Graph.selectedState = static_cast<int>(asset->states.size()) - 1;
                    outChanged = true;
                    ImGui::CloseCurrentPopup();
                }
                if (used)
                    ImGui::EndDisabled();
            }
        } else {
            for (const std::string& clip : availableClips) {
                const bool used = AnimatorStateUsesClip(*asset, clip);
                if (used)
                    ImGui::BeginDisabled();
                if (EditorDropdown::Selectable(clip.c_str(), false) && !used) {
                    AddAnimatorStateForClip(*asset, clip);
                    g_Graph.selectedState = static_cast<int>(asset->states.size()) - 1;
                    outChanged = true;
                    ImGui::CloseCurrentPopup();
                }
                if (used)
                    ImGui::EndDisabled();
            }
        }
        EditorMenu::EndPopup();
    }
    ImGui::PopStyleVar();
    if (toolbar.Button("Arrange")) {
        AutoLayoutAnimatorStates(*asset);
        g_Graph.didInitialFrame = false;
        outChanged = true;
    }
    if (toolbar.Button("Frame All")) {
        g_Graph.didInitialFrame = false;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fit all states in view (A)");
    toolbar.End();
    int pendingStateRemove = -1;
    int pendingTransitionRemove = -1;

    const float graphWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const float graphHeight = std::max(80.0f, ImGui::GetContentRegionAvail().y);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorTheme::WorkArea);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::BeginChild("AnimatorGraphCanvas", ImVec2(graphWidth, graphHeight), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    const float footerHeight = ImGui::GetTextLineHeight() + 12.0f;
    canvasSize.y = std::max(1.0f, canvasSize.y - footerHeight);
    const ImVec2 canvasMax(canvasMin.x + canvasSize.x, canvasMin.y + canvasSize.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(canvasMin, canvasMax, true);

    if (!g_Graph.didInitialFrame) {
        FrameGraphToStates(*asset, canvasSize, g_Graph.pan, g_Graph.zoom);
        g_Graph.didInitialFrame = true;
    }

    dl->AddRectFilled(canvasMin, canvasMax, ImGui::ColorConvertFloat4ToU32(EditorTheme::WorkArea));
    DrawGrid(dl, canvasMin, canvasMax, g_Graph.pan, g_Graph.zoom);

    ImGui::SetCursorScreenPos(canvasMin);
    ImGui::InvisibleButton("##graph_bg", canvasSize, ImGuiButtonFlags_MouseButtonMiddle);
    const bool canvasHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const int hoveredLinkStart =
        HitTestLinkStart(*asset, g_Graph, canvasMin, g_Graph.pan, g_Graph.zoom, mouse);
    const int hoveredOutputPort = hoveredLinkStart;
    const int hoveredNode = HitTestState(*asset, canvasMin, g_Graph.pan, g_Graph.zoom, mouse);
    const int linkTargetNode =
        g_Graph.linkingActive
            ? HitTestStateForLinkTarget(*asset, canvasMin, g_Graph.pan, g_Graph.zoom, mouse)
            : hoveredNode;

    const bool altPan = canvasHovered && ImGui::GetIO().KeyAlt &&
                        ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    if (canvasHovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || altPan)) {
        g_Graph.pan.x += ImGui::GetIO().MouseDelta.x;
        g_Graph.pan.y += ImGui::GetIO().MouseDelta.y;
    }
    if (canvasHovered && ImGui::GetIO().MouseWheel != 0.0f) {
        const float zOld = g_Graph.zoom;
        g_Graph.zoom = std::clamp(g_Graph.zoom * (1.0f + ImGui::GetIO().MouseWheel * 0.1f), 0.35f,
                                  2.5f);
        const ImVec2 mouseRel(mouse.x - canvasMin.x, mouse.y - canvasMin.y);
        g_Graph.pan.x = mouseRel.x - (mouseRel.x - g_Graph.pan.x) * (g_Graph.zoom / zOld);
        g_Graph.pan.y = mouseRel.y - (mouseRel.y - g_Graph.pan.y) * (g_Graph.zoom / zOld);
    }

    if (g_Graph.draggingEntry && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const glm::vec2 gp = ScreenToGraph(canvasMin, g_Graph.pan, g_Graph.zoom, mouse);
        const glm::vec2 position = gp - g_Graph.dragGrabOffset;
        if (position != EntryPosition(*asset)) {
            asset->entryGraphPosition = position;
            outChanged = true;
        }
    } else if (g_Graph.draggingAnyState && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const glm::vec2 gp = ScreenToGraph(canvasMin, g_Graph.pan, g_Graph.zoom, mouse);
        const glm::vec2 position = gp - g_Graph.dragGrabOffset;
        if (position != g_Graph.anyStatePosition) {
            g_Graph.anyStatePosition = position;
            asset->anyStateGraphPosition = position;
            outChanged = true;
        }
    } else if (g_Graph.draggingNode >= 0 && !g_Graph.linkingActive &&
               static_cast<size_t>(g_Graph.draggingNode) < asset->states.size() &&
               ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        auto& st = asset->states[static_cast<size_t>(g_Graph.draggingNode)];
        const glm::vec2 gp = ScreenToGraph(canvasMin, g_Graph.pan, g_Graph.zoom, mouse);
        st.graphPosition = gp - g_Graph.dragGrabOffset;
        outChanged = true;
    } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        g_Graph.draggingNode = -1;
        g_Graph.draggingAnyState = false;
        g_Graph.draggingEntry = false;
    }

    if (g_Graph.linkingActive && g_Graph.linkClickMode && canvasHovered &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const bool fromAny = g_Graph.linkDragFrom == kLinkFromAnyState;
        const bool fromState = g_Graph.linkDragFrom >= 0 &&
                               static_cast<size_t>(g_Graph.linkDragFrom) < asset->states.size();
        if (linkTargetNode >= 0 && (fromAny || linkTargetNode != g_Graph.linkDragFrom)) {
            AnimatorTransitionDef tr{};
            tr.fromState = fromAny ? std::string(kAnimatorAnyStateName)
                                   : asset->states[static_cast<size_t>(g_Graph.linkDragFrom)].name;
            tr.toState = asset->states[static_cast<size_t>(linkTargetNode)].name;
            tr.duration = 0.25f;
            asset->transitions.push_back(tr);
            g_Graph.selectedTransition = static_cast<int>(asset->transitions.size()) - 1;
            g_Graph.selectedState = -1;
            outChanged = true;
            if (editor)
                editor->RecordUndoSnapshot();
        }
        g_Graph.linkingActive = false;
        g_Graph.linkClickMode = false;
        g_Graph.linkDragFrom = -1;
    } else if (g_Graph.linkingActive && !g_Graph.linkClickMode &&
               ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        const bool fromAny = g_Graph.linkDragFrom == kLinkFromAnyState;
        const bool fromState = g_Graph.linkDragFrom >= 0 &&
                               static_cast<size_t>(g_Graph.linkDragFrom) < asset->states.size();
        if (linkTargetNode >= 0 && (fromAny || linkTargetNode != g_Graph.linkDragFrom)) {
            AnimatorTransitionDef tr{};
            tr.fromState = fromAny ? std::string(kAnimatorAnyStateName)
                                   : asset->states[static_cast<size_t>(g_Graph.linkDragFrom)].name;
            tr.toState = asset->states[static_cast<size_t>(linkTargetNode)].name;
            tr.duration = 0.25f;
            asset->transitions.push_back(tr);
            g_Graph.selectedTransition = static_cast<int>(asset->transitions.size()) - 1;
            g_Graph.selectedState = -1;
            outChanged = true;
            if (editor)
                editor->RecordUndoSnapshot();
        }
        g_Graph.linkingActive = false;
        g_Graph.linkClickMode = false;
        g_Graph.linkDragFrom = -1;
    } else if (!g_Graph.linkClickMode && !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
               g_Graph.linkingActive) {
        g_Graph.linkingActive = false;
        g_Graph.linkClickMode = false;
        g_Graph.linkDragFrom = -1;
    }

    if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        const int transitionHit = HitTestTransition(
            *asset, g_Graph, canvasMin, g_Graph.pan, g_Graph.zoom, mouse, 10.0f);
        if (HitTestEntry(*asset, canvasMin, g_Graph.pan, g_Graph.zoom, mouse)) {
            g_Graph.selectedState = -1;
            g_Graph.selectedTransition = -1;
            ImGui::OpenPopup("AnimatorEntryContext");
        } else if (transitionHit >= 0) {
            g_Graph.selectedTransition = transitionHit;
            g_Graph.selectedState = -1;
            ImGui::OpenPopup("AnimatorTransitionContext");
        } else if (hoveredNode >= 0) {
            g_Graph.selectedState = hoveredNode;
            g_Graph.selectedTransition = -1;
            ImGui::OpenPopup("AnimatorNodeContext");
        } else if (HitTestAnyState(g_Graph, canvasMin, g_Graph.pan, g_Graph.zoom, mouse) >= 0) {
            g_Graph.selectedState = -1;
            g_Graph.selectedTransition = -1;
            ImGui::OpenPopup("AnimatorAnyStateContext");
        } else {
            g_Graph.selectedState = -1;
            g_Graph.selectedTransition = -1;
            g_Graph.contextSpawn =
                ScreenToGraph(canvasMin, g_Graph.pan, g_Graph.zoom, mouse);
            ImGui::OpenPopup("AnimatorCanvasContext");
        }
    }

    if (canvasHovered && !ImGui::GetIO().KeyAlt &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !g_Graph.linkingActive) {
        if (HitTestEntry(*asset, canvasMin, g_Graph.pan, g_Graph.zoom, mouse)) {
            g_Graph.draggingEntry = true;
            g_Graph.draggingAnyState = false;
            g_Graph.draggingNode = -1;
            const glm::vec2 gp = ScreenToGraph(canvasMin, g_Graph.pan, g_Graph.zoom, mouse);
            g_Graph.dragGrabOffset = gp - EntryPosition(*asset);
            g_Graph.selectedState = -1;
            g_Graph.selectedTransition = -1;
        } else if (hoveredLinkStart == kLinkFromAnyState) {
            g_Graph.linkDragFrom = kLinkFromAnyState;
            g_Graph.linkingActive = true;
            g_Graph.linkClickMode = false;
            g_Graph.selectedState = -1;
            g_Graph.selectedTransition = -1;
            g_Graph.draggingNode = -1;
            g_Graph.draggingAnyState = false;
        } else if (hoveredLinkStart >= 0) {
            g_Graph.linkDragFrom = hoveredLinkStart;
            g_Graph.linkingActive = true;
            g_Graph.linkClickMode = false;
            g_Graph.selectedState = hoveredLinkStart;
            g_Graph.selectedTransition = -1;
            g_Graph.draggingNode = -1;
            g_Graph.draggingAnyState = false;
        } else if (HitTestAnyState(g_Graph, canvasMin, g_Graph.pan, g_Graph.zoom, mouse) >= 0 &&
                   HitTestAnyStatePort(g_Graph, canvasMin, g_Graph.pan, g_Graph.zoom, mouse) < 0) {
            g_Graph.draggingAnyState = true;
            g_Graph.draggingEntry = false;
            g_Graph.draggingNode = -1;
            const glm::vec2 gp = ScreenToGraph(canvasMin, g_Graph.pan, g_Graph.zoom, mouse);
            g_Graph.dragGrabOffset = gp - g_Graph.anyStatePosition;
            g_Graph.selectedState = -1;
            g_Graph.selectedTransition = -1;
        } else if (hoveredNode >= 0) {
            g_Graph.selectedState = hoveredNode;
            g_Graph.selectedTransition = -1;
            g_Graph.draggingNode = hoveredNode;
            const glm::vec2 gp = ScreenToGraph(canvasMin, g_Graph.pan, g_Graph.zoom, mouse);
            g_Graph.dragGrabOffset =
                gp - asset->states[static_cast<size_t>(hoveredNode)].graphPosition;
        } else {
            const int trHit =
                HitTestTransition(*asset, g_Graph, canvasMin, g_Graph.pan, g_Graph.zoom, mouse, 10.0f);
            if (trHit >= 0) {
                g_Graph.selectedTransition = trHit;
                g_Graph.selectedState = -1;
            } else {
                g_Graph.selectedState = -1;
                g_Graph.selectedTransition = -1;
            }
        }
    }

    const ImU32 lineColor = ImGui::ColorConvertFloat4ToU32(EditorTheme::TextMuted);
    const ImU32 lineSelColor = ImGui::ColorConvertFloat4ToU32(EditorTheme::Selection);
    const int hoveredTransition = canvasHovered ? HitTestTransition(
        *asset, g_Graph, canvasMin, g_Graph.pan, g_Graph.zoom, mouse, 7.0f) : -1;

    for (size_t ti = 0; ti < asset->transitions.size(); ++ti) {
        const auto& tr = asset->transitions[ti];
        ImVec2 fromPort{};
        ImVec2 toPort{};
        if (!TransitionEndpoints(*asset, g_Graph, canvasMin, g_Graph.pan, g_Graph.zoom,
                                 tr, fromPort, toPort))
            continue;

        const bool sel = static_cast<int>(ti) == g_Graph.selectedTransition;
        const std::string label = SummarizeTransitionLabel(tr);
        const bool hovered = hoveredTransition == static_cast<int>(ti);
        DrawTransitionArrow(dl, fromPort, toPort, sel || hovered ? lineSelColor : lineColor, sel,
                            (sel || hovered) && !label.empty() ? label.c_str() : nullptr,
                            TransitionBend(*asset, tr));
    }

    if (g_Graph.linkingActive) {
        ImVec2 fromPort = mouse;
        ImVec2 toPort = mouse;
        float bend = 0.0f;
        AnimatorTransitionDef preview;
        if (g_Graph.linkDragFrom == kLinkFromAnyState) {
            const ImVec2 nodeScreen =
                GraphToScreen(canvasMin, g_Graph.pan, g_Graph.zoom, g_Graph.anyStatePosition);
            const ImVec2 nodeSize(kSpecialNodeSize.x * g_Graph.zoom, kSpecialNodeSize.y * g_Graph.zoom);
            fromPort = RoundedNodeEdgeToward(nodeScreen, nodeSize, mouse, nodeSize.y * 0.5f);
            preview.fromState = kAnimatorAnyStateName;
        } else if (g_Graph.linkDragFrom >= 0 &&
                   static_cast<size_t>(g_Graph.linkDragFrom) < asset->states.size()) {
            const auto& fromSt = asset->states[static_cast<size_t>(g_Graph.linkDragFrom)];
            const ImVec2 nodeScreen =
                GraphToScreen(canvasMin, g_Graph.pan, g_Graph.zoom, fromSt.graphPosition);
            const ImVec2 nodeSize(kNodeSize.x * g_Graph.zoom, kNodeSize.y * g_Graph.zoom);
            fromPort = RoundedNodeEdgeToward(nodeScreen, nodeSize, mouse, 4.0f * g_Graph.zoom);
            preview.fromState = fromSt.name;
        }
        if (linkTargetNode >= 0 && !preview.fromState.empty()) {
            preview.toState = asset->states[static_cast<size_t>(linkTargetNode)].name;
            TransitionEndpoints(*asset, g_Graph, canvasMin, g_Graph.pan, g_Graph.zoom,
                preview, fromPort, toPort);
            bend = TransitionBend(*asset, preview);
        }
        DrawTransitionArrow(dl, fromPort, toPort,
            ImGui::ColorConvertFloat4ToU32(EditorTheme::PsAccent), true, nullptr, bend);
    }

    // Independently positioned Entry and Any State nodes.
    {
        const ImVec2 anyScreen = GraphToScreen(canvasMin, g_Graph.pan, g_Graph.zoom, g_Graph.anyStatePosition);
        const ImVec2 anySize(kSpecialNodeSize.x * g_Graph.zoom, kSpecialNodeSize.y * g_Graph.zoom);
        const bool anyHovered = HitTestAnyState(g_Graph, canvasMin, g_Graph.pan, g_Graph.zoom, mouse) >= 0;
        DrawSpecialNode(dl, anyScreen, anySize, "Any State", UiTokens::Component,
                        anyHovered, g_Graph.zoom);
        const ImVec2 anyPort = NodeOutputPortScreen(anyScreen, anySize);
        if (!g_Graph.linkingActive && hoveredLinkStart == kLinkFromAnyState)
            DrawNodePort(dl, anyPort, kPortDrawRadius * g_Graph.zoom,
                         ImGui::ColorConvertFloat4ToU32(EditorTheme::BtnFaceLight),
                         ImGui::ColorConvertFloat4ToU32(UiTokens::Component), false);
    }

    {
        const glm::vec2 entryPos = EntryPosition(*asset);
        const ImVec2 entryScreen = GraphToScreen(canvasMin, g_Graph.pan, g_Graph.zoom, entryPos);
        const ImVec2 entrySize(kSpecialNodeSize.x * g_Graph.zoom, kSpecialNodeSize.y * g_Graph.zoom);
        DrawSpecialNode(dl, entryScreen, entrySize, "Entry", EditorTheme::Success,
                        HitTestEntry(*asset, canvasMin, g_Graph.pan, g_Graph.zoom, mouse), g_Graph.zoom);

        const int defaultIdx = FindStateIndex(*asset, asset->defaultState);
        if (defaultIdx >= 0) {
            const auto& defSt = asset->states[static_cast<size_t>(defaultIdx)];
            const ImVec2 defScreen = GraphToScreen(canvasMin, g_Graph.pan, g_Graph.zoom, defSt.graphPosition);
            const ImVec2 defSize(kNodeSize.x * g_Graph.zoom, kNodeSize.y * g_Graph.zoom);
            const ImVec2 entryCenter(entryScreen.x + entrySize.x * 0.5f, entryScreen.y + entrySize.y * 0.5f);
            const ImVec2 defCenter(defScreen.x + defSize.x * 0.5f, defScreen.y + defSize.y * 0.5f);
            const ImVec2 entryPort = RoundedNodeEdgeToward(entryScreen, entrySize, defCenter, entrySize.y * 0.5f);
            const ImVec2 defIn = RoundedNodeEdgeToward(defScreen, defSize, entryCenter, 4.0f * g_Graph.zoom);
            DrawTransitionArrow(dl, entryPort, defIn,
                                ImGui::ColorConvertFloat4ToU32(EditorTheme::Success), false);
        }
    }

    for (size_t i = 0; i < asset->states.size(); ++i) {
        auto& st = asset->states[i];
        const ImVec2 nodeScreen = GraphToScreen(canvasMin, g_Graph.pan, g_Graph.zoom, st.graphPosition);
        const ImVec2 nodeSize(kNodeSize.x * g_Graph.zoom, kNodeSize.y * g_Graph.zoom);

        const bool isDefault = st.name == asset->defaultState;
        const bool isSelected = static_cast<int>(i) == g_Graph.selectedState;
        const bool nodeHovered = static_cast<int>(i) == hoveredNode;
        const bool linkTarget =
            g_Graph.linkingActive && static_cast<int>(i) == linkTargetNode && linkTargetNode >= 0 &&
            linkTargetNode != g_Graph.linkDragFrom;
        const ImVec4 fill =
            isSelected ? UiTokens::Hex(0x24465D)
                       : (nodeHovered ? EditorTheme::SurfaceHover : EditorTheme::BtnFace);
        const ImU32 border = ImGui::ColorConvertFloat4ToU32(
            linkTarget ? EditorTheme::PsAccent
                       : (isSelected ? EditorTheme::AccentHover
                                     : (nodeHovered ? EditorTheme::Accent : EditorTheme::BorderStrong)));

        const ImVec2 nodeMax(nodeScreen.x + nodeSize.x, nodeScreen.y + nodeSize.y);
        DrawRaisedGraphNode(dl, nodeScreen, nodeSize, fill, border,
                            4.0f * g_Graph.zoom, g_Graph.zoom, isSelected);
        const bool showSubtitle = g_Graph.zoom >= 0.55f;
        const bool showStartTag = isDefault && g_Graph.zoom >= 0.70f;
        const float titleY = showSubtitle ? 10.0f * g_Graph.zoom
            : (nodeSize.y - std::max(9.0f, ImGui::GetFontSize() * g_Graph.zoom)) * 0.5f;
        DrawGraphText(dl, ImVec2(nodeScreen.x + 12.0f * g_Graph.zoom,
                                 nodeScreen.y + titleY),
                       nodeSize.x - (showStartTag ? 66.0f : 24.0f) * g_Graph.zoom,
                       st.name.c_str(), EditorTheme::TextPrimary, g_Graph.zoom);
        if (showSubtitle) DrawGraphText(dl, ImVec2(nodeScreen.x + 12.0f * g_Graph.zoom,
                                 nodeScreen.y + 33.0f * g_Graph.zoom),
                       nodeSize.x - 24.0f * g_Graph.zoom,
                       st.clipName.empty() ? "No animation clip" : st.clipName.c_str(),
                       EditorTheme::TextSecondary, g_Graph.zoom, 0.85f);
        if (showStartTag)
            DrawGraphText(dl, ImVec2(nodeMax.x - 44.0f * g_Graph.zoom,
                                     nodeScreen.y + 12.0f * g_Graph.zoom),
                           38.0f * g_Graph.zoom, "START", EditorTheme::AccentHover, g_Graph.zoom, 0.7f);
        if (nodeHovered && canvasHovered && !g_Graph.linkingActive)
            ImGui::SetTooltip("%s\n%s\n%s · %.2fx", st.name.c_str(),
                              st.clipName.empty() ? "No animation clip" : st.clipName.c_str(),
                              st.loop ? "Loop" : "Once", st.speed);

        const ImVec2 outPort = NodeOutputPortScreen(nodeScreen, nodeSize);
        const bool portHovered = static_cast<int>(i) == hoveredLinkStart;
        const bool portActive = g_Graph.linkingActive && static_cast<int>(i) == g_Graph.linkDragFrom;
        const float portRadius = 4.0f * g_Graph.zoom;
        const ImU32 portFill = ImGui::ColorConvertFloat4ToU32(
            portActive ? EditorTheme::PsAccent
                       : (portHovered ? EditorTheme::AccentHover : EditorTheme::BtnFaceLight));
        const ImU32 portBorder = ImGui::ColorConvertFloat4ToU32(
            portHovered || portActive ? EditorTheme::Accent
                                      : EditorTheme::Border);
        if (!g_Graph.linkingActive && portHovered)
            DrawNodePort(dl, outPort, portRadius, portFill, portBorder, portHovered);
        if (portHovered && !g_Graph.linkingActive)
            ImGui::SetTooltip("Drag to create a transition");
    }

    if (asset->states.empty()) {
        const char* hint = "Drop a model or animation clip to create states";
        const ImVec2 textSize = ImGui::CalcTextSize(hint);
        const ImVec2 hintPos((canvasMin.x + canvasMax.x - textSize.x) * 0.5f,
                             (canvasMin.y + canvasMax.y - textSize.y) * 0.5f);
        dl->AddText(hintPos, ImGui::ColorConvertFloat4ToU32(EditorTheme::TextMuted), hint);
    }

    dl->PopClipRect();
    const ImVec2 footerMax(canvasMax.x, canvasMax.y + footerHeight);
    dl->PushClipRect(ImVec2(canvasMin.x, canvasMax.y), footerMax, true);
    dl->AddRectFilled(ImVec2(canvasMin.x, canvasMax.y), footerMax,
                      ImGui::ColorConvertFloat4ToU32(EditorTheme::PanelFace));
    dl->AddLine(ImVec2(canvasMin.x, canvasMax.y + 0.5f), ImVec2(canvasMax.x, canvasMax.y + 0.5f),
                ImGui::ColorConvertFloat4ToU32(EditorTheme::Border));
    const std::string stats = std::to_string(asset->states.size()) + " states   /   " +
                              std::to_string(asset->transitions.size()) + " transitions";
    dl->AddText(ImVec2(canvasMin.x + 10.0f, canvasMax.y + 6.0f),
                ImGui::ColorConvertFloat4ToU32(EditorTheme::TextMuted), stats.c_str());
    const std::string zoomLabel = std::to_string(static_cast<int>(std::round(g_Graph.zoom * 100))) + "%";
    dl->AddText(ImVec2(canvasMax.x - ImGui::CalcTextSize(zoomLabel.c_str()).x - 10.0f, canvasMax.y + 6.0f),
                ImGui::ColorConvertFloat4ToU32(EditorTheme::TextSecondary), zoomLabel.c_str());
    dl->PopClipRect();
    if (ImGui::IsMouseHoveringRect(ImVec2(canvasMin.x, canvasMax.y), footerMax))
        ImGui::SetTooltip("Middle mouse / Alt+drag: pan\nWheel: zoom\nA: frame all · F: frame selected\nDelete: remove · Ctrl+D: duplicate");

    if (ImGui::GetDragDropPayload() != nullptr) {
        const ImRect canvasRect(canvasMin, canvasMax);
        const ImGuiID dropId = ImGui::GetID("##AnimGraphDropTarget");
        if (ImGui::BeginDragDropTargetCustom(canvasRect, dropId)) {
            const ImGuiPayload* payload = ImGui::GetDragDropPayload();
            if (payload && payload->IsDataType(DragDrop::kAssetAnimClip)) {
                if (const ImGuiPayload* accepted =
                        ImGui::AcceptDragDropPayload(DragDrop::kAssetAnimClip)) {
                    std::string modelPath;
                    std::string clipName;
                    int clipStack = -1;
                    if (DragDrop::ParseAnimClipPayload(accepted->Data, accepted->DataSize, modelPath,
                                                       clipName, &clipStack)) {
                        const glm::vec2 origin =
                            ScreenToGraph(canvasMin, g_Graph.pan, g_Graph.zoom, mouse);
                        asset->sourceModelPath = modelPath;
                        availableClips = LoadAnimationClipNames(modelPath);
                        sourceModel = AssetManager::Get().GetSkeletalModel(modelPath);
                        AddAnimatorStateForClip(*asset, clipName, origin, clipStack);
                        g_Graph.selectedState = static_cast<int>(asset->states.size()) - 1;
                        g_Graph.selectedTransition = -1;
                        outChanged = true;
                        if (editor)
                            editor->RecordUndoSnapshot();
                    }
                }
            } else {
                const ImGuiPayload* p = ImGui::AcceptDragDropPayload(DragDrop::kAssetMove);
                if (!p)
                    p = ImGui::AcceptDragDropPayload(DragDrop::kAssetModel);
                const std::string modelPath = ModelPathFromDragPayload(p);
                if (!modelPath.empty()) {
                    const glm::vec2 origin =
                        ScreenToGraph(canvasMin, g_Graph.pan, g_Graph.zoom, mouse);
                    ApplyAnimatorModelToController(*asset, modelPath, availableClips, origin,
                                                   outChanged);
                    if (editor)
                        editor->RecordUndoSnapshot();
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
    if (EditorMenu::BeginPopup("AnimatorCanvasContext")) {
        const glm::vec2 spawn = g_Graph.contextSpawn;
        if (EditorMenu::BeginMenu("Create State")) {
            if (EditorMenu::MenuItem("Empty")) {
                AnimatorStateDef st{};
                st.name = MakeUniqueAnimatorStateName(*asset, "New State");
                st.graphPosition = SnapGraphPosition(spawn);
                asset->states.push_back(st);
                if (asset->defaultState.empty())
                    asset->defaultState = st.name;
                g_Graph.selectedState = static_cast<int>(asset->states.size()) - 1;
                outChanged = true;
            }
            if (!availableClips.empty() && EditorMenu::BeginMenu("From Animation Clip")) {
                for (const std::string& clip : availableClips) {
                    if (AnimatorStateUsesClip(*asset, clip))
                        continue;
                    if (EditorMenu::MenuItem(clip.c_str())) {
                        AddAnimatorStateForClip(*asset, clip, SnapGraphPosition(spawn));
                        g_Graph.selectedState = static_cast<int>(asset->states.size()) - 1;
                        outChanged = true;
                    }
                }
                EditorMenu::EndMenu();
            }
            EditorMenu::EndMenu();
        }
        ImGui::Separator();
        if (EditorMenu::MenuItem("Frame All", "A"))
            g_Graph.didInitialFrame = false;
        if (EditorMenu::MenuItem("Auto Layout"))
            AutoLayoutAnimatorStates(*asset);
        EditorMenu::EndPopup();
    }

    if (EditorMenu::BeginPopup("AnimatorNodeContext")) {
        if (g_Graph.selectedState >= 0 &&
            g_Graph.selectedState < static_cast<int>(asset->states.size())) {
            const auto& st = asset->states[static_cast<size_t>(g_Graph.selectedState)];
            if (EditorMenu::MenuItem("Set as Layer Default State")) {
                asset->defaultState = st.name;
                outChanged = true;
            }
            if (EditorMenu::MenuItem("Duplicate State", "Ctrl+D")) {
                DuplicateAnimatorState(*asset, g_Graph.selectedState, g_Graph.selectedState);
                outChanged = true;
                if (editor)
                    editor->RecordUndoSnapshot();
            }
            if (EditorMenu::MenuItem("Make Transition")) {
                g_Graph.linkDragFrom = g_Graph.selectedState;
                g_Graph.linkingActive = true;
                g_Graph.linkClickMode = true;
                g_Graph.selectedTransition = -1;
            }
        }
        if (EditorMenu::MenuItem("Delete State", "Del"))
            pendingStateRemove = g_Graph.selectedState;
        EditorMenu::EndPopup();
    }

    if (EditorMenu::BeginPopup("AnimatorEntryContext")) {
        if (EditorMenu::MenuItem("Reset Entry Position")) {
            asset->entryGraphPosition.reset();
            outChanged = true;
        }
        EditorMenu::EndPopup();
    }

    if (EditorMenu::BeginPopup("AnimatorAnyStateContext")) {
        if (EditorMenu::MenuItem("Make Transition")) {
            g_Graph.linkDragFrom = kLinkFromAnyState;
            g_Graph.linkingActive = true;
            g_Graph.linkClickMode = true;
        }
        if (EditorMenu::MenuItem("Frame Any State")) {
            g_Graph.pan = ImVec2(canvasSize.x * 0.5f - (g_Graph.anyStatePosition.x + kSpecialNodeSize.x * 0.5f) * g_Graph.zoom,
                                 canvasSize.y * 0.5f - (g_Graph.anyStatePosition.y + kSpecialNodeSize.y * 0.5f) * g_Graph.zoom);
        }
        EditorMenu::EndPopup();
    }

    if (EditorMenu::BeginPopup("AnimatorTransitionContext")) {
        if (EditorMenu::MenuItem("Delete Transition", "Del"))
            pendingTransitionRemove = g_Graph.selectedTransition;
        EditorMenu::EndPopup();
    }
    ImGui::PopStyleVar(2);

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    if (AnimatorControllerWindowFocused() && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) && g_Graph.linkingActive) {
            g_Graph.linkingActive = false;
            g_Graph.linkClickMode = false;
            g_Graph.linkDragFrom = -1;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
            if (g_Graph.selectedTransition >= 0)
                pendingTransitionRemove = g_Graph.selectedTransition;
            else if (g_Graph.selectedState >= 0)
                pendingStateRemove = g_Graph.selectedState;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_A))
            FrameGraphToStates(*asset, canvasSize, g_Graph.pan, g_Graph.zoom);
        if (ImGui::IsKeyPressed(ImGuiKey_F)) {
            if (g_Graph.selectedState >= 0)
                FrameGraphToState(*asset, g_Graph.selectedState, canvasSize,
                                  g_Graph.pan, g_Graph.zoom);
            else
                FrameGraphToStates(*asset, canvasSize, g_Graph.pan, g_Graph.zoom);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_D) &&
            (ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper) && g_Graph.selectedState >= 0) {
            DuplicateAnimatorState(*asset, g_Graph.selectedState, g_Graph.selectedState);
            outChanged = true;
            if (editor)
                editor->RecordUndoSnapshot();
        }
    }

    if (pendingTransitionRemove >= 0 &&
        static_cast<size_t>(pendingTransitionRemove) < asset->transitions.size()) {
        asset->transitions.erase(asset->transitions.begin() + pendingTransitionRemove);
        g_Graph.selectedTransition = -1;
        outChanged = true;
        if (editor)
            editor->RecordUndoSnapshot();
    }
    if (pendingStateRemove >= 0) {
        RemoveAnimatorState(*asset, pendingStateRemove, g_Graph.selectedState,
                            g_Graph.selectedTransition);
        outChanged = true;
        if (editor)
            editor->RecordUndoSnapshot();
    }

    return true;
}

} // namespace MipsyncEngine
