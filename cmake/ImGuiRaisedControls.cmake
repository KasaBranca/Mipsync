# Use the same editor face renderer for ImGui's existing control behaviors.
# Patching a generated copy keeps keyboard/input semantics and FetchContent intact.
function(mipsync_replace_control before after)
    string(FIND "${mipsync_imgui_widgets}" "${before}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "The pinned ImGui control changed; review raised-face integration.")
    endif()
    string(REPLACE "${before}" "${after}" patched "${mipsync_imgui_widgets}")
    set(mipsync_imgui_widgets "${patched}" PARENT_SCOPE)
endfunction()

mipsync_replace_control("#include \"imgui_internal.h\""
    "#include \"imgui_internal.h\"\n#include \"editor/EditorSurface.h\"")
mipsync_replace_control(
    "RenderFrame(bb.Min, bb.Max, col, true, style.FrameRounding);"
    "MipsyncEngine::EditorSurface::DrawRaisedFace(window->DrawList, bb.Min, bb.Max, col, style.FrameRounding, GetColorU32(ImGuiCol_Border), style.FrameBorderSize, held && hovered);")
mipsync_replace_control(
    "RenderFrame(frame_bb.Min, frame_bb.Max, bg_col, true, style.FrameRounding);"
    "MipsyncEngine::EditorSurface::DrawRaisedFace(window->DrawList, frame_bb.Min, frame_bb.Max, bg_col, style.FrameRounding, GetColorU32(ImGuiCol_Border), style.FrameBorderSize, held && hovered);")
mipsync_replace_control(
    "RenderNavCursor(frame_bb, id, nav_render_cursor_flags);"
    "if (g.IO.ConfigFlags & (ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad))\n                RenderNavCursor(frame_bb, id, nav_render_cursor_flags);")
set(combo_before [=[    if (!(flags & ImGuiComboFlags_NoPreview))
        window->DrawList->AddRectFilled(bb.Min, ImVec2(value_x2, bb.Max.y), frame_col, style.FrameRounding, (flags & ImGuiComboFlags_NoArrowButton) ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersLeft);
    if (!(flags & ImGuiComboFlags_NoArrowButton))
    {
        ImU32 bg_col = GetColorU32((popup_open || hovered) ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
        ImU32 text_col = GetColorU32(ImGuiCol_Text);
        window->DrawList->AddRectFilled(ImVec2(value_x2, bb.Min.y), bb.Max, bg_col, style.FrameRounding, (w <= arrow_size) ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersRight);
        if (value_x2 + arrow_size - style.FramePadding.x <= bb.Max.x)
            RenderArrow(window->DrawList, ImVec2(value_x2 + style.FramePadding.y, bb.Min.y + style.FramePadding.y), text_col, ImGuiDir_Down, 1.0f);
    }
    RenderFrameBorder(bb.Min, bb.Max, style.FrameRounding);]=])
set(combo_after [=[    MipsyncEngine::EditorSurface::DrawRaisedFace(window->DrawList, bb.Min, bb.Max, frame_col,
        style.FrameRounding, GetColorU32(ImGuiCol_Border), style.FrameBorderSize, held && hovered);
    if (!(flags & ImGuiComboFlags_NoArrowButton))
    {
        const ImU32 text_col = GetColorU32(ImGuiCol_Text);
        if (value_x2 > bb.Min.x && style.FrameBorderSize > 0)
            window->DrawList->AddLine(ImVec2(value_x2, bb.Min.y + 1), ImVec2(value_x2, bb.Max.y - 1), GetColorU32(ImGuiCol_Border));
        if (value_x2 + arrow_size - style.FramePadding.x <= bb.Max.x)
            RenderArrow(window->DrawList, ImVec2(value_x2 + style.FramePadding.y, bb.Min.y + style.FramePadding.y), text_col, ImGuiDir_Down, 1.0f);
    }]=])
mipsync_replace_control("${combo_before}" "${combo_after}")
