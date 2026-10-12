#include "EditorTheme.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cstring>
#include <cstdlib>

namespace MipsyncEngine {
namespace {

bool Slider(const char* label, ImGuiDataType type, void* value, const void* min,
            const void* max, const char* format, ImGuiSliderFlags flags) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    ImGuiContext& context = *ImGui::GetCurrentContext();
    const float width = std::max(1.0f, ImGui::CalcItemWidth());
    const float height = ImGui::GetFrameHeight();
    const float scale = ImGui::GetFontSize() / 14.0f;
    const float gap = std::min(6.0f * scale, width * 0.08f);
    const float numberWidth = std::min((type == ImGuiDataType_Float ? 64.0f : 54.0f) * scale,
                                       width * 0.48f);
    const float trackWidth = std::max(1.0f, width - numberWidth - gap);
    const float radius = std::min(4.5f * scale, trackWidth * 0.2f);
    const bool mixed = std::strcmp(format, "-") == 0 ||
                       (context.CurrentItemFlags & ImGuiItemFlags_MixedValue) != 0;
    const char* behaviorFormat = mixed ? (type == ImGuiDataType_Float ? "%.3f" : "%d") : format;
    bool changed = false;

    ImGui::BeginGroup();
    ImGui::PushID(label);
    const ImGuiID trackId = window->GetID("##track");
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const ImRect track(position, ImVec2(position.x + trackWidth, position.y + height));
    ImGui::ItemSize(track, ImGui::GetStyle().FramePadding.y);
    if (ImGui::ItemAdd(track, trackId, &track)) {
        const bool hovered = ImGui::ItemHoverable(track, trackId, context.LastItemData.ItemFlags);
        const bool clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left, 0, trackId);
        if (clicked || context.NavActivateId == trackId) {
            if (clicked) ImGui::SetKeyOwner(ImGuiKey_MouseLeft, trackId);
            std::memcpy(&context.ActiveIdValueOnActivation, value, ImGui::DataTypeGetInfo(type)->Size);
            ImGui::SetActiveID(trackId, window);
            ImGui::SetFocusID(trackId, window);
            ImGui::FocusWindow(window);
        }
        if (context.ActiveId == trackId)
            context.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);
        ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, radius * 2.0f);
        ImRect grab;
        changed = ImGui::SliderBehavior(track, trackId, type, value, min, max, behaviorFormat,
                                         flags | ImGuiSliderFlags_NoInput, &grab);
        ImGui::PopStyleVar();
        if (changed) ImGui::MarkItemEdited(trackId);

        const float halfGrab = (grab.Max.x - grab.Min.x) * 0.5f;
        const float centerY = position.y + height * 0.5f;
        window->DrawList->AddLine(ImVec2(track.Min.x + 2.0f + halfGrab, centerY),
                                   ImVec2(track.Max.x - 2.0f - halfGrab, centerY),
                                   ImGui::GetColorU32(EditorTheme::BorderStrong), std::max(1.0f, scale));
        if (!mixed || changed)
            window->DrawList->AddCircleFilled(ImVec2((grab.Min.x + grab.Max.x) * 0.5f, centerY), radius,
                ImGui::GetColorU32(context.ActiveId == trackId ? EditorTheme::Accent
                                  : hovered ? EditorTheme::TextPrimary : EditorTheme::TextMuted));
        ImGui::RenderNavCursor(track, trackId);
    }

    ImGui::SameLine(0.0f, gap);
    ImGui::SetNextItemWidth(numberWidth);
    ImGui::BeginDisabled((flags & ImGuiSliderFlags_NoInput) != 0);
    bool numberChanged = false;
    if (mixed) {
        char text[64] = "-";
        ImGui::PushItemFlag(ImGuiItemFlags_NoMarkEdited, true);
        const bool edited = ImGui::InputText("##number", text, sizeof(text),
            ImGuiInputTextFlags_AutoSelectAll | (type == ImGuiDataType_Float
                ? ImGuiInputTextFlags_CharsScientific : ImGuiInputTextFlags_CharsDecimal));
        ImGui::PopItemFlag();
        char* end = nullptr;
        std::strtod(text, &end);
        if (edited && end != text) {
            ImGui::DataTypeApplyFromText(text, type, value, behaviorFormat, nullptr);
            // Entering the primary object's existing value still resolves a mixed selection.
            numberChanged = true;
        }
    } else {
        numberChanged = ImGui::InputScalar("##number", type, value, nullptr, nullptr, format);
    }
    if (numberChanged) {
        ImGui::DataTypeClamp(type, value, min, max);
        ImGui::MarkItemEdited(context.LastItemData.ID);
        changed = true;
    }
    ImGui::EndDisabled();
    const char* labelEnd = ImGui::FindRenderedTextEnd(label);
    if (labelEnd != label) {
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label, labelEnd);
    }
    ImGui::PopID();
    ImGui::EndGroup();
    return changed;
}

} // namespace

bool EditorTheme::SliderFloat(const char* label, float* value, float min, float max,
                              const char* format, ImGuiSliderFlags flags) {
    return Slider(label, ImGuiDataType_Float, value, &min, &max, format ? format : "%.3f", flags);
}

bool EditorTheme::SliderInt(const char* label, int* value, int min, int max,
                            const char* format, ImGuiSliderFlags flags) {
    return Slider(label, ImGuiDataType_S32, value, &min, &max, format ? format : "%d", flags);
}

} // namespace MipsyncEngine
