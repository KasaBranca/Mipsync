#pragma once
#include "../mips/InspectorAttributes.h"
#include <imgui.h>

namespace MipsyncEngine {
// The group covers both label and value, so tooltips work across the whole row.
class MipsInspectorFieldScope {
public:
    MipsInspectorFieldScope(size_t index, const Mips::InspectorAttributes& attributes) : m_Attributes(attributes) {
        ImGui::PushID(static_cast<int>(index));
        const float scale=ImGui::GetFontSize()/14.0f;
        if(attributes.space>0) ImGui::Dummy({0,static_cast<float>(attributes.space)*scale});
        if(!attributes.header.empty()) {
            ImGui::Dummy({0,6*scale}); ImGui::Separator(); ImGui::Dummy({0,3*scale});
            ImGui::TextUnformatted(attributes.header.c_str()); ImGui::Dummy({0,3*scale});
        }
        ImGui::BeginGroup();
        ImGui::BeginDisabled(attributes.readOnly);
    }
    ~MipsInspectorFieldScope() {
        ImGui::EndDisabled(); ImGui::EndGroup();
        if(!m_Attributes.tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s",m_Attributes.tooltip.c_str());
        ImGui::PopID();
    }
    MipsInspectorFieldScope(const MipsInspectorFieldScope&)=delete;
private:
    const Mips::InspectorAttributes& m_Attributes;
};
} // namespace MipsyncEngine
