#include "EditorAnimatorGraph.h"
#include "EditorAnimatorControllerInspector.h"
#include "EditorApp.h"
#include "EditorTheme.h"
#include "AssetBrowserPanel.h"
#include "../animation/AnimationTypes.h"
#include "../animation/AnimatorControllerIO.h"
#include "../animation/SkeletalModel.h"
#include "../assets/AssetManager.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstring>

namespace MipsyncEngine {

namespace {

constexpr ImVec2 kNodeSize{ 180.0f, 62.0f };
constexpr ImVec2 kSpecialNodeSize{ 180.0f, 62.0f };
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
    bool didAutoLayout = false;
    bool didInitialFrame = false;
    glm::vec2 contextSpawn{ 40.0f, 40.0f };
};

constexpr float kPortHitRadius = 10.0f;
constexpr float kPortDrawRadius = 6.0f;
constexpr float kInspectorWidth = 220.0f;

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

void FrameGraphToStates(const AnimatorControllerAsset& asset, ImVec2 canvasSize, ImVec2& pan,
                        float& zoom) {
    if (asset.states.empty() || canvasSize.x < 8.0f || canvasSize.y < 8.0f)
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

    // Unity's frame-all includes the special nodes as well. Entry is derived
    // from the default state, while Any State has its own editor position.
    minX = std::min(minX, g_Graph.anyStatePosition.x);
    minY = std::min(minY, g_Graph.anyStatePosition.y);
    maxX = std::max(maxX, g_Graph.anyStatePosition.x + kSpecialNodeSize.x);
    maxY = std::max(maxY, g_Graph.anyStatePosition.y + kSpecialNodeSize.y);
    const int defaultIndex = FindStateIndex(asset, asset.defaultState);
    if (defaultIndex >= 0) {
        const glm::vec2 entry =
            asset.states[static_cast<size_t>(defaultIndex)].graphPosition - glm::vec2(250.0f, 0.0f);
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
    const ImU32 gridColor = ImGui::ColorConvertFloat4ToU32(UiTokens::WithAlpha(EditorTheme::PanelAlt, 0.65f));
    const float step = kGridStep * zoom;
    if (step < 8.0f)
        return;

    const float offX = std::fmod(pan.x, step);
    const float offY = std::fmod(pan.y, step);
    for (float x = canvasMin.x + offX; x < canvasMax.x; x += step)
        dl->AddLine(ImVec2(x, canvasMin.y), ImVec2(x, canvasMax.y), gridColor);
    for (float y = canvasMin.y + offY; y < canvasMax.y; y += step)
        dl->AddLine(ImVec2(canvasMin.x, y), ImVec2(canvasMax.x, y), gridColor);
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

void DrawTransitionArrow(ImDrawList* dl, ImVec2 fromPort, ImVec2 toPort, ImU32 color, bool selected,
                         const char* label = nullptr, float bend = 0.0f) {
    ImVec2 cp1, cp2;
    BezierControlPoints(fromPort, toPort, cp1, cp2, bend);

    const float thickness = selected ? 3.0f : 2.0f;
    dl->AddBezierCubic(fromPort, cp1, cp2, toPort,
                       ImGui::ColorConvertFloat4ToU32(UiTokens::Rgba(0x000000, 0xA0)),
                       thickness + 2.0f);
    dl->AddBezierCubic(fromPort, cp1, cp2, toPort, color, thickness);

    const ImVec2 dir(toPort.x - cp2.x, toPort.y - cp2.y);
    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    if (len > 1e-3f) {
        const ImVec2 n(dir.x / len, dir.y / len);
        const ImVec2 p(-n.y, n.x);
        const float s = 8.0f;
        dl->AddTriangleFilled(toPort,
                              ImVec2(toPort.x - n.x * s + p.x * 0.4f, toPort.y - n.y * s + p.y * 0.4f),
                              ImVec2(toPort.x - n.x * s - p.x * 0.4f, toPort.y - n.y * s - p.y * 0.4f),
                              color);
    }

    if (label && label[0]) {
        const ImVec2 mid = CubicPoint(fromPort, cp1, cp2, toPort, 0.5f);
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
            out += std::to_string(c.threshold);
    }
    if (tr.conditions.size() > 2)
        out += "...";
    return out;
}

void DrawAnimatorParameterCombo(const AnimatorControllerAsset& asset, std::string& parameter,
                                bool& changed) {
    const char* preview = parameter.empty() ? "<parameter>" : parameter.c_str();
    if (ImGui::BeginCombo("Parameter", preview)) {
        for (const auto& p : asset.parameters) {
            if (ImGui::Selectable(p.name.c_str(), p.name == parameter)) {
                parameter = p.name;
                changed = true;
            }
        }
        if (asset.parameters.empty())
            ImGui::TextColored(EditorTheme::TextMuted, "Add parameters in the left column.");
        ImGui::EndCombo();
    }
    if (parameter.empty()) {
        char paramBuf[128] = {};
        if (ImGui::InputText("Custom", paramBuf, sizeof(paramBuf))) {
            parameter = paramBuf;
            changed = true;
        }
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

ImVec2 TransitionFromPort(const AnimatorControllerAsset& asset, const GraphViewState& graph,
                          ImVec2 canvasMin, ImVec2 pan, float zoom, const AnimatorTransitionDef& tr) {
    if (tr.fromState == kAnimatorAnyStateName) {
        const ImVec2 nodeScreen = GraphToScreen(canvasMin, pan, zoom, graph.anyStatePosition);
        const ImVec2 nodeSize(kSpecialNodeSize.x * zoom, kSpecialNodeSize.y * zoom);
        return NodeOutputPortScreen(nodeScreen, nodeSize);
    }
    const int fromIdx = FindStateIndex(asset, tr.fromState);
    if (fromIdx < 0)
        return {};
    const ImVec2 fromNodeScreen =
        GraphToScreen(canvasMin, pan, zoom, asset.states[static_cast<size_t>(fromIdx)].graphPosition);
    const ImVec2 fromNodeSize(kNodeSize.x * zoom, kNodeSize.y * zoom);
    return NodeOutputPortScreen(fromNodeScreen, fromNodeSize);
}

ImVec2 RectEdgeToward(ImVec2 rectMin, ImVec2 rectSize, ImVec2 target) {
    const ImVec2 center(rectMin.x + rectSize.x * 0.5f, rectMin.y + rectSize.y * 0.5f);
    const float dx = target.x - center.x;
    const float dy = target.y - center.y;
    const float nx = std::abs(dx) / std::max(rectSize.x * 0.5f, 1.0f);
    const float ny = std::abs(dy) / std::max(rectSize.y * 0.5f, 1.0f);
    if (nx >= ny)
        return ImVec2(center.x + (dx >= 0.0f ? rectSize.x * 0.5f : -rectSize.x * 0.5f),
                      center.y + (std::abs(dx) > 1e-4f ? dy / std::abs(dx) * rectSize.x * 0.5f : 0.0f));
    return ImVec2(center.x + (std::abs(dy) > 1e-4f ? dx / std::abs(dy) * rectSize.y * 0.5f : 0.0f),
                  center.y + (dy >= 0.0f ? rectSize.y * 0.5f : -rectSize.y * 0.5f));
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
    outFrom = RectEdgeToward(fromMin, fromSize, toCenter);
    outTo = RectEdgeToward(toMin, toSize, fromCenter);

    const bool hasReverse = std::any_of(
        asset.transitions.begin(), asset.transitions.end(),
        [&](const AnimatorTransitionDef& other) {
            return other.fromState == transition.toState &&
                   other.toState == transition.fromState;
        });
    if (hasReverse) {
        const float dx = outTo.x - outFrom.x;
        const float dy = outTo.y - outFrom.y;
        const float length = std::max(std::hypot(dx, dy), 1.0f);
        const ImVec2 offset(-dy / length * 5.0f, dx / length * 5.0f);
        outFrom.x += offset.x;
        outFrom.y += offset.y;
        outTo.x += offset.x;
        outTo.y += offset.y;
    }
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

bool HitTestDefaultEntryArrow(ImVec2 nodeScreen, ImVec2 nodeSize, float zoom, ImVec2 mouseScreen) {
    const ImVec2 center(nodeScreen.x - 9.0f * zoom, nodeScreen.y + nodeSize.y * 0.5f);
    return std::hypot(mouseScreen.x - center.x, mouseScreen.y - center.y) <= 14.0f * zoom;
}

bool HitTestNodeOutputZone(ImVec2 nodeScreen, ImVec2 nodeSize, ImVec2 mouseScreen) {
    const float zoneLeft = nodeScreen.x + nodeSize.x * 0.65f;
    return mouseScreen.x >= zoneLeft && mouseScreen.x <= nodeScreen.x + nodeSize.x + kPortHitRadius &&
           mouseScreen.y >= nodeScreen.y && mouseScreen.y <= nodeScreen.y + nodeSize.y;
}

/// Best state index to start a transition drag (port, default entry arrow, or right-edge zone).
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

void DrawStateProperties(AnimatorControllerAsset& asset, int stateIndex,
                         const std::shared_ptr<SkeletalModelAsset>& model,
                         const std::vector<std::string>& availableClips, bool& changed,
                         bool& requestDelete) {
    if (stateIndex < 0 || static_cast<size_t>(stateIndex) >= asset.states.size())
        return;

    auto& st = asset.states[static_cast<size_t>(stateIndex)];
    ImGui::TextColored(EditorTheme::TextSecondary, "State");
    char nameBuf[128] = {};
    std::strncpy(nameBuf, st.name.c_str(), sizeof(nameBuf) - 1);
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
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
    DrawAnimatorClipField("Clip", st.clipName, st.clipStackIndex, &st.clipSourceModelPath,
                          asset.sourceModelPath, model, availableClips, changed);
    if (ImGui::DragFloat("Speed", &st.speed, 0.01f, 0.0f, 10.0f))
        changed = true;
    if (EditorTheme::Checkbox("Loop", &st.loop))
        changed = true;
    if (ImGui::Button("Set as Default State")) {
        asset.defaultState = st.name;
        changed = true;
    }
    if (EditorTheme::AeroButton("Delete State", ImVec2(-1.0f, EditorTheme::ButtonHeight),
                                AeroButtonKind::Danger)) {
        requestDelete = true;
    }
}

void DrawTransitionProperties(AnimatorControllerAsset& asset, int trIndex, bool& changed,
                              bool& requestDelete) {
    if (trIndex < 0 || static_cast<size_t>(trIndex) >= asset.transitions.size())
        return;

    auto& tr = asset.transitions[static_cast<size_t>(trIndex)];
    ImGui::TextColored(EditorTheme::TextSecondary, "Transition");

    int fromIdx = FindStateIndex(asset, tr.fromState);
    int toIdx = FindStateIndex(asset, tr.toState);
    if (ImGui::BeginCombo("From", tr.fromState.c_str())) {
        for (const auto& st : asset.states) {
            if (ImGui::Selectable(st.name.c_str(), st.name == tr.fromState)) {
                tr.fromState = st.name;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::BeginCombo("To", tr.toState.c_str())) {
        for (const auto& st : asset.states) {
            if (ImGui::Selectable(st.name.c_str(), st.name == tr.toState)) {
                tr.toState = st.name;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    (void)fromIdx;
    (void)toIdx;

    if (ImGui::DragFloat("Duration", &tr.duration, 0.01f, 0.0f, 5.0f))
        changed = true;
    if (EditorTheme::Checkbox("Has Exit Time", &tr.hasExitTime))
        changed = true;
    if (tr.hasExitTime && ImGui::DragFloat("Exit Time", &tr.exitTime, 0.01f, 0.0f, 1.0f))
        changed = true;

    ImGui::Spacing();
    ImGui::TextColored(EditorTheme::TextSecondary, "Conditions");
    if (tr.conditions.empty())
        ImGui::TextColored(EditorTheme::TextMuted, "None — always transitions (Unity: no conditions).");

    int condRemove = -1;
    for (size_t ci = 0; ci < tr.conditions.size(); ++ci) {
        auto& c = tr.conditions[ci];
        ImGui::PushID(static_cast<int>(ci));
        const std::string previousParameter = c.parameter;
        DrawAnimatorParameterCombo(asset, c.parameter, changed);
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
            if (ImGui::Combo("Condition", &boolMode, modes)) {
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
            if (ImGui::Combo("Condition", &numericMode, modes)) {
                static constexpr AnimatorConditionMode kModes[] = {
                    AnimatorConditionMode::Greater, AnimatorConditionMode::Less,
                    AnimatorConditionMode::Equals, AnimatorConditionMode::NotEquals,
                };
                c.mode = kModes[numericMode];
                changed = true;
            }
            if (integer) {
                int threshold = static_cast<int>(c.threshold);
                if (ImGui::DragInt("Threshold", &threshold)) {
                    c.threshold = static_cast<float>(threshold);
                    changed = true;
                }
            } else if (ImGui::DragFloat("Threshold", &c.threshold, 0.01f)) {
                changed = true;
            }
        }
        if (ImGui::SmallButton("Remove##cond"))
            condRemove = static_cast<int>(ci);
        ImGui::PopID();
    }
    if (condRemove >= 0) {
        tr.conditions.erase(tr.conditions.begin() + condRemove);
        changed = true;
    }
    if (!tr.conditions.empty() && ImGui::SmallButton("Clear All Conditions")) {
        tr.conditions.clear();
        changed = true;
    }
    if (ImGui::SmallButton("+ Condition")) {
        tr.conditions.push_back({ "Speed", AnimatorConditionMode::Greater, 0.1f });
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Speed > 0.1")) {
        tr.conditions.push_back({ "Speed", AnimatorConditionMode::Greater, 0.1f });
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Speed < 0.1")) {
        tr.conditions.push_back({ "Speed", AnimatorConditionMode::Less, 0.1f });
        changed = true;
    }
    if (EditorTheme::AeroButton("Delete Transition", ImVec2(-1.0f, EditorTheme::ButtonHeight),
                                AeroButtonKind::Danger)) {
        requestDelete = true;
    }
}

} // namespace

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

    std::shared_ptr<SkeletalModelAsset> sourceModel;
    if (!asset->sourceModelPath.empty())
        sourceModel = AssetManager::Get().GetSkeletalModel(asset->sourceModelPath);

    if (g_Graph.path != projectRelPath) {
        g_Graph = GraphViewState{};
        g_Graph.path = projectRelPath;
    }

    if (!g_Graph.didAutoLayout && StatesNeedAutoLayout(*asset)) {
        AutoLayoutAnimatorStates(*asset);
        g_Graph.didAutoLayout = true;
        g_Graph.didInitialFrame = false;
        outChanged = true;
    }

    ImGui::TextColored(EditorTheme::TextMuted,
                       "MMB / Alt+LMB pan · wheel zoom · A frame all · F frame selected · Del delete");
    ImGui::SameLine();
    ImGui::TextColored(EditorTheme::TextMuted, "(%zu states)", asset->states.size());
    ImGui::Spacing();

    const std::string addClipPopupId = "AddClipGraph##" + projectRelPath;
    if (!availableClips.empty()) {
        if (ImGui::Button("+ Add Animation")) {
            ImGui::OpenPopup(addClipPopupId.c_str());
        }
        ImGui::SameLine();
    }
    if (ImGui::Button("+ Empty State")) {
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
    if (!availableClips.empty() && ImGui::BeginPopup(addClipPopupId.c_str())) {
        if (sourceModel && !sourceModel->animationNames.empty()) {
            for (size_t i = 0; i < sourceModel->animationNames.size(); ++i) {
                const std::string& clip = sourceModel->animationNames[i];
                const int stackIdx = static_cast<int>(sourceModel->animationStackIndices[i]);
                const bool used = StateUsesAnimStack(*asset, asset->sourceModelPath, stackIdx);
                const std::string row = clip + "  [stack " + std::to_string(stackIdx) + "]";
                if (used)
                    ImGui::BeginDisabled();
                if (ImGui::Selectable(row.c_str(), false) && !used) {
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
                if (ImGui::Selectable(clip.c_str(), false) && !used) {
                    AddAnimatorStateForClip(*asset, clip);
                    g_Graph.selectedState = static_cast<int>(asset->states.size()) - 1;
                    outChanged = true;
                    ImGui::CloseCurrentPopup();
                }
                if (used)
                    ImGui::EndDisabled();
            }
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Auto Layout")) {
        AutoLayoutAnimatorStates(*asset);
        g_Graph.didInitialFrame = false;
        outChanged = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Frame All")) {
        g_Graph.didInitialFrame = false;
    }
    int pendingStateRemove = -1;
    int pendingTransitionRemove = -1;
    bool requestDeleteState = false;
    bool requestDeleteTransition = false;

    const float rowH = std::max(320.0f, ImGui::GetContentRegionAvail().y);
    const bool showSelectionInspector = g_Graph.selectedState >= 0 ||
                                        g_Graph.selectedTransition >= 0;
    const float inspectorSpace = showSelectionInspector ? kInspectorWidth + 8.0f : 0.0f;
    const float graphWidth = std::max(200.0f, ImGui::GetContentRegionAvail().x - inspectorSpace);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorTheme::WorkArea);
    ImGui::BeginChild("AnimatorGraphCanvas", ImVec2(graphWidth, rowH), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
    const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    const ImVec2 canvasMax(canvasMin.x + canvasSize.x, canvasMin.y + canvasSize.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(canvasMin, canvasMax, true);

    if (!asset->states.empty() && !g_Graph.didInitialFrame) {
        FrameGraphToStates(*asset, canvasSize, g_Graph.pan, g_Graph.zoom);
        g_Graph.didInitialFrame = true;
    }

    const ImU32 canvasTop = ImGui::ColorConvertFloat4ToU32(UiTokens::BgSecondary);
    const ImU32 canvasBottom = ImGui::ColorConvertFloat4ToU32(EditorTheme::WorkArea);
    dl->AddRectFilledMultiColor(canvasMin, canvasMax, canvasTop, canvasTop,
                                canvasBottom, canvasBottom);
    const ImU32 edgeLight = ImGui::ColorConvertFloat4ToU32(EditorTheme::BtnHighlight);
    const ImU32 edgeDark = ImGui::ColorConvertFloat4ToU32(EditorTheme::BtnDarkShadow);
    dl->AddLine(ImVec2(canvasMin.x + 1.0f, canvasMin.y + 1.0f),
                ImVec2(canvasMax.x - 1.0f, canvasMin.y + 1.0f), edgeLight);
    dl->AddLine(ImVec2(canvasMin.x + 1.0f, canvasMin.y + 1.0f),
                ImVec2(canvasMin.x + 1.0f, canvasMax.y - 1.0f), edgeLight);
    dl->AddLine(ImVec2(canvasMin.x + 1.0f, canvasMax.y - 1.0f),
                ImVec2(canvasMax.x - 1.0f, canvasMax.y - 1.0f), edgeDark, 2.0f);
    dl->AddLine(ImVec2(canvasMax.x - 1.0f, canvasMin.y + 1.0f),
                ImVec2(canvasMax.x - 1.0f, canvasMax.y - 1.0f), edgeDark, 2.0f);
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

    if (g_Graph.draggingAnyState && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const glm::vec2 gp = ScreenToGraph(canvasMin, g_Graph.pan, g_Graph.zoom, mouse);
        g_Graph.anyStatePosition = gp - g_Graph.dragGrabOffset;
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
        if (transitionHit >= 0) {
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
        if (hoveredLinkStart == kLinkFromAnyState) {
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
                   !HitTestAnyStatePort(g_Graph, canvasMin, g_Graph.pan, g_Graph.zoom, mouse)) {
            g_Graph.draggingAnyState = true;
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

    const ImU32 lineColor =
        ImGui::ColorConvertFloat4ToU32(UiTokens::WithAlpha(EditorTheme::Accent, 0.85f));
    const ImU32 lineSelColor = ImGui::ColorConvertFloat4ToU32(EditorTheme::Selection);

    for (size_t ti = 0; ti < asset->transitions.size(); ++ti) {
        const auto& tr = asset->transitions[ti];
        ImVec2 fromPort{};
        ImVec2 toPort{};
        if (!TransitionEndpoints(*asset, g_Graph, canvasMin, g_Graph.pan, g_Graph.zoom,
                                 tr, fromPort, toPort))
            continue;

        const bool sel = static_cast<int>(ti) == g_Graph.selectedTransition;
        const std::string label = SummarizeTransitionLabel(tr);
        DrawTransitionArrow(dl, fromPort, toPort, sel ? lineSelColor : lineColor, sel,
                            label.empty() ? nullptr : label.c_str(),
                            TransitionBend(*asset, tr));
    }

    if (g_Graph.linkingActive) {
        ImVec2 fromPort = mouse;
        if (g_Graph.linkDragFrom == kLinkFromAnyState) {
            const ImVec2 nodeScreen =
                GraphToScreen(canvasMin, g_Graph.pan, g_Graph.zoom, g_Graph.anyStatePosition);
            const ImVec2 nodeSize(kSpecialNodeSize.x * g_Graph.zoom, kSpecialNodeSize.y * g_Graph.zoom);
            fromPort = NodeOutputPortScreen(nodeScreen, nodeSize);
        } else if (g_Graph.linkDragFrom >= 0 &&
                   static_cast<size_t>(g_Graph.linkDragFrom) < asset->states.size()) {
            const auto& fromSt = asset->states[static_cast<size_t>(g_Graph.linkDragFrom)];
            const ImVec2 nodeScreen =
                GraphToScreen(canvasMin, g_Graph.pan, g_Graph.zoom, fromSt.graphPosition);
            const ImVec2 nodeSize(kNodeSize.x * g_Graph.zoom, kNodeSize.y * g_Graph.zoom);
            fromPort = NodeOutputPortScreen(nodeScreen, nodeSize);
        }
        dl->AddBezierCubic(fromPort, ImVec2(fromPort.x + 60.0f, fromPort.y),
                           ImVec2(mouse.x - 60.0f, mouse.y), mouse,
                           ImGui::ColorConvertFloat4ToU32(EditorTheme::PsAccent), 2.5f);
    }

    // Entry (visual) + Any State nodes
    {
        const ImVec2 anyScreen = GraphToScreen(canvasMin, g_Graph.pan, g_Graph.zoom, g_Graph.anyStatePosition);
        const ImVec2 anySize(kSpecialNodeSize.x * g_Graph.zoom, kSpecialNodeSize.y * g_Graph.zoom);
        const ImVec2 anyMax(anyScreen.x + anySize.x, anyScreen.y + anySize.y);
        const bool anyHovered = HitTestAnyState(g_Graph, canvasMin, g_Graph.pan, g_Graph.zoom, mouse) >= 0;
        dl->AddRectFilled(ImVec2(anyScreen.x + 3.0f, anyScreen.y + 4.0f),
                          ImVec2(anyMax.x + 3.0f, anyMax.y + 4.0f),
                          ImGui::ColorConvertFloat4ToU32(UiTokens::Rgba(0x000000, 0x70)), 6.0f);
        dl->AddRectFilled(anyScreen, anyMax,
                          ImGui::ColorConvertFloat4ToU32(anyHovered ? EditorTheme::Selection
                                                                  : EditorTheme::BtnFace),
                          6.0f);
        dl->AddRect(anyScreen, anyMax,
                    ImGui::ColorConvertFloat4ToU32(EditorTheme::PsAccent), 6.0f, 0, 2.0f);
        dl->AddText(ImVec2(anyScreen.x + 10.0f, anyScreen.y + anySize.y * 0.5f - 7.0f),
                    ImGui::ColorConvertFloat4ToU32(EditorTheme::TextPrimary), "Any State");
        const ImVec2 anyPort = NodeOutputPortScreen(anyScreen, anySize);
        DrawNodePort(dl, anyPort, kPortDrawRadius * g_Graph.zoom,
                     ImGui::ColorConvertFloat4ToU32(EditorTheme::BtnFaceLight),
                     ImGui::ColorConvertFloat4ToU32(EditorTheme::PsAccent),
                     g_Graph.linkDragFrom == kLinkFromAnyState);
    }

    const int defaultIdx = FindStateIndex(*asset, asset->defaultState);
    if (defaultIdx >= 0) {
        const auto& defSt = asset->states[static_cast<size_t>(defaultIdx)];
        const glm::vec2 entryPos = defSt.graphPosition - glm::vec2(250.0f, 0.0f);
        const ImVec2 entryScreen = GraphToScreen(canvasMin, g_Graph.pan, g_Graph.zoom, entryPos);
        const ImVec2 entrySize(kSpecialNodeSize.x * g_Graph.zoom, kSpecialNodeSize.y * g_Graph.zoom);
        const ImVec2 entryMax(entryScreen.x + entrySize.x, entryScreen.y + entrySize.y);
        dl->AddRectFilled(ImVec2(entryScreen.x + 3.0f, entryScreen.y + 4.0f),
                          ImVec2(entryMax.x + 3.0f, entryMax.y + 4.0f),
                          ImGui::ColorConvertFloat4ToU32(UiTokens::Rgba(0x000000, 0x70)), 6.0f);
        dl->AddRectFilled(entryScreen, entryMax,
                          ImGui::ColorConvertFloat4ToU32(EditorTheme::BtnFace), 6.0f);
        dl->AddRect(entryScreen, entryMax,
                    ImGui::ColorConvertFloat4ToU32(EditorTheme::Success), 6.0f, 0, 1.5f);
        dl->AddText(ImVec2(entryScreen.x + 28.0f, entryScreen.y + entrySize.y * 0.5f - 7.0f),
                    ImGui::ColorConvertFloat4ToU32(EditorTheme::TextPrimary), "Entry");

        const ImVec2 defScreen = GraphToScreen(canvasMin, g_Graph.pan, g_Graph.zoom, defSt.graphPosition);
        const ImVec2 defSize(kNodeSize.x * g_Graph.zoom, kNodeSize.y * g_Graph.zoom);
        const ImVec2 entryPort(entryMax.x, entryScreen.y + entrySize.y * 0.5f);
        const ImVec2 defIn(defScreen.x, defScreen.y + defSize.y * 0.5f);
        DrawTransitionArrow(dl, entryPort, defIn,
                            ImGui::ColorConvertFloat4ToU32(EditorTheme::Success), false);
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
        const ImU32 fill = ImGui::ColorConvertFloat4ToU32(
            isSelected ? EditorTheme::Selection : (isDefault ? EditorTheme::Success : EditorTheme::BtnFace));
        const ImU32 border = ImGui::ColorConvertFloat4ToU32(
            linkTarget ? EditorTheme::PsAccent
                       : (isSelected ? EditorTheme::AccentHover
                                     : (nodeHovered ? EditorTheme::Accent : EditorTheme::Border)));

        const ImVec2 nodeMax(nodeScreen.x + nodeSize.x, nodeScreen.y + nodeSize.y);
        dl->AddRectFilled(ImVec2(nodeScreen.x + 3.0f, nodeScreen.y + 4.0f),
                          ImVec2(nodeMax.x + 3.0f, nodeMax.y + 4.0f),
                          ImGui::ColorConvertFloat4ToU32(UiTokens::Rgba(0x000000, 0x78)), 6.0f);
        dl->AddRectFilled(nodeScreen, nodeMax, fill, 6.0f);
        dl->AddRect(nodeScreen, nodeMax, border, 6.0f, 0, isSelected ? 2.5f : 1.5f);
        dl->AddLine(ImVec2(nodeScreen.x + 6.0f, nodeScreen.y + 2.0f),
                    ImVec2(nodeMax.x - 6.0f, nodeScreen.y + 2.0f),
                    ImGui::ColorConvertFloat4ToU32(
                        isSelected ? UiTokens::WithAlpha(EditorTheme::SelectionText, 0.45f)
                                   : UiTokens::WithAlpha(EditorTheme::BtnHighlight, 0.65f)));
        dl->AddLine(ImVec2(nodeScreen.x + 6.0f, nodeMax.y - 2.0f),
                    ImVec2(nodeMax.x - 6.0f, nodeMax.y - 2.0f), edgeDark, 1.5f);

        const ImU32 textColor = ImGui::ColorConvertFloat4ToU32(
            isSelected ? EditorTheme::SelectionText : EditorTheme::TextPrimary);
        const ImU32 subColor = ImGui::ColorConvertFloat4ToU32(EditorTheme::TextMuted);
        dl->AddText(ImVec2(nodeScreen.x + 8.0f, nodeScreen.y + 8.0f), textColor, st.name.c_str());
        dl->AddText(ImVec2(nodeScreen.x + 8.0f, nodeScreen.y + 28.0f), subColor, st.clipName.c_str());

        const bool entryHovered =
            isDefault && static_cast<int>(i) == hoveredLinkStart && !g_Graph.linkingActive;
        if (isDefault) {
            const ImVec2 ePos(nodeScreen.x - 14.0f * g_Graph.zoom, nodeScreen.y + nodeSize.y * 0.5f - 6.0f);
            const ImU32 entryColor = ImGui::ColorConvertFloat4ToU32(
                entryHovered ? EditorTheme::AccentHover : EditorTheme::Success);
            dl->AddTriangleFilled(
                ePos, ImVec2(ePos.x + 10.0f * g_Graph.zoom, ePos.y - 6.0f * g_Graph.zoom),
                ImVec2(ePos.x + 10.0f * g_Graph.zoom, ePos.y + 6.0f * g_Graph.zoom), entryColor);
        }

        const ImVec2 outPort = NodeOutputPortScreen(nodeScreen, nodeSize);
        const bool portHovered = static_cast<int>(i) == hoveredLinkStart;
        const bool portActive = g_Graph.linkingActive && static_cast<int>(i) == g_Graph.linkDragFrom;
        const float portRadius = kPortDrawRadius * (isDefault ? 1.35f : 1.0f) * g_Graph.zoom;
        const ImU32 portFill = ImGui::ColorConvertFloat4ToU32(
            portActive ? EditorTheme::PsAccent
                       : (portHovered ? EditorTheme::AccentHover : EditorTheme::BtnFaceLight));
        const ImU32 portBorder = ImGui::ColorConvertFloat4ToU32(
            portHovered || portActive ? EditorTheme::Accent
                                      : (isDefault ? EditorTheme::Success : EditorTheme::Border));
        DrawNodePort(dl, outPort, portRadius, portFill, portBorder, portHovered || portActive);
    }

    if (asset->states.empty() && canvasHovered) {
        const char* hint = "FBX model をドロップしてアニメーションを追加";
        const ImVec2 textSize = ImGui::CalcTextSize(hint);
        const ImVec2 hintPos((canvasMin.x + canvasMax.x - textSize.x) * 0.5f,
                             (canvasMin.y + canvasMax.y - textSize.y) * 0.5f);
        dl->AddText(hintPos, ImGui::ColorConvertFloat4ToU32(EditorTheme::TextMuted), hint);
    }

    dl->PopClipRect();

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

    if (ImGui::BeginPopup("AnimatorCanvasContext")) {
        const glm::vec2 spawn = g_Graph.contextSpawn;
        if (ImGui::BeginMenu("Create State")) {
            if (ImGui::MenuItem("Empty")) {
                AnimatorStateDef st{};
                st.name = MakeUniqueAnimatorStateName(*asset, "New State");
                st.graphPosition = SnapGraphPosition(spawn);
                asset->states.push_back(st);
                if (asset->defaultState.empty())
                    asset->defaultState = st.name;
                g_Graph.selectedState = static_cast<int>(asset->states.size()) - 1;
                outChanged = true;
            }
            if (!availableClips.empty() && ImGui::BeginMenu("From Animation Clip")) {
                for (const std::string& clip : availableClips) {
                    if (AnimatorStateUsesClip(*asset, clip))
                        continue;
                    if (ImGui::MenuItem(clip.c_str())) {
                        AddAnimatorStateForClip(*asset, clip, SnapGraphPosition(spawn));
                        g_Graph.selectedState = static_cast<int>(asset->states.size()) - 1;
                        outChanged = true;
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Frame All", "A"))
            g_Graph.didInitialFrame = false;
        if (ImGui::MenuItem("Auto Layout"))
            AutoLayoutAnimatorStates(*asset);
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("AnimatorNodeContext")) {
        if (g_Graph.selectedState >= 0 &&
            g_Graph.selectedState < static_cast<int>(asset->states.size())) {
            const auto& st = asset->states[static_cast<size_t>(g_Graph.selectedState)];
            if (ImGui::MenuItem("Set as Layer Default State")) {
                asset->defaultState = st.name;
                outChanged = true;
            }
            if (ImGui::MenuItem("Duplicate State", "Ctrl+D")) {
                DuplicateAnimatorState(*asset, g_Graph.selectedState, g_Graph.selectedState);
                outChanged = true;
                if (editor)
                    editor->RecordUndoSnapshot();
            }
            if (ImGui::MenuItem("Make Transition")) {
                g_Graph.linkDragFrom = g_Graph.selectedState;
                g_Graph.linkingActive = true;
                g_Graph.linkClickMode = true;
                g_Graph.selectedTransition = -1;
            }
        }
        if (ImGui::MenuItem("Delete State", "Del"))
            pendingStateRemove = g_Graph.selectedState;
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("AnimatorAnyStateContext")) {
        if (ImGui::MenuItem("Make Transition")) {
            g_Graph.linkDragFrom = kLinkFromAnyState;
            g_Graph.linkingActive = true;
            g_Graph.linkClickMode = true;
        }
        if (ImGui::MenuItem("Frame Any State"))
            g_Graph.pan = ImVec2(60.0f, 60.0f);
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("AnimatorTransitionContext")) {
        if (ImGui::MenuItem("Delete Transition", "Del"))
            pendingTransitionRemove = g_Graph.selectedTransition;
        ImGui::EndPopup();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    if (showSelectionInspector) {
    ImGui::SameLine();
    ImGui::BeginChild("AnimatorGraphInspector", ImVec2(kInspectorWidth, rowH), true);
    if (g_Graph.selectedState >= 0) {
        DrawStateProperties(*asset, g_Graph.selectedState, sourceModel, availableClips, outChanged,
                            requestDeleteState);
        if (requestDeleteState)
            pendingStateRemove = g_Graph.selectedState;
        ImGui::Separator();
        const std::string selectedName =
            asset->states[static_cast<size_t>(g_Graph.selectedState)].name;
        ImGui::TextColored(EditorTheme::TextSecondary, "Outgoing Transitions");
        bool hasOutgoing = false;
        for (size_t i = 0; i < asset->transitions.size(); ++i) {
            const auto& transition = asset->transitions[i];
            if (transition.fromState != selectedName)
                continue;
            hasOutgoing = true;
            const std::string label = "-> " + transition.toState;
            if (ImGui::Selectable(label.c_str())) {
                g_Graph.selectedTransition = static_cast<int>(i);
                g_Graph.selectedState = -1;
            }
        }
        if (!hasOutgoing)
            ImGui::TextColored(EditorTheme::TextMuted, "None");
        if (ImGui::Button("Make Transition", ImVec2(-1.0f, 0.0f))) {
            g_Graph.linkDragFrom = g_Graph.selectedState;
            g_Graph.linkingActive = true;
            g_Graph.linkClickMode = true;
        }
    } else if (g_Graph.selectedTransition >= 0) {
        DrawTransitionProperties(*asset, g_Graph.selectedTransition, outChanged,
                                 requestDeleteTransition);
        if (requestDeleteTransition)
            pendingTransitionRemove = g_Graph.selectedTransition;
    } else {
        ImGui::TextColored(EditorTheme::TextMuted,
                           "Select a state or transition to edit its properties.");
        ImGui::Spacing();
        ImGui::TextWrapped("Right-click empty space to create a state. Right-click a state and "
                           "choose Make Transition, then click the destination state.");
    }
    ImGui::EndChild();
    }

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
