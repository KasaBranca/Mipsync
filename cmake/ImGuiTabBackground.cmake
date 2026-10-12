# Build a corrected copy of the pinned ImGui source without modifying FetchContent.
# The native fill begins above the tab's border, exposing a bright strip and corners.
file(READ "${imgui_SOURCE_DIR}/imgui_widgets.cpp" mipsync_imgui_widgets)
set(mipsync_tab_fill_old
    "draw_list->AddRectFilled(bb.Min, ImVec2(bb.Max.x, y2), col, rounding, ImDrawFlags_RoundCornersTop);")
set(mipsync_tab_fill_new
    "draw_list->AddRectFilled(ImVec2(bb.Min.x + 0.5f, y1 + 0.5f), ImVec2(bb.Max.x - 0.5f, y2), col, rounding, ImDrawFlags_RoundCornersTop);")
string(FIND "${mipsync_imgui_widgets}" "${mipsync_tab_fill_old}" mipsync_tab_fill_position)
if(mipsync_tab_fill_position EQUAL -1)
    message(FATAL_ERROR "The pinned ImGui tab background changed; review its fill/border geometry.")
endif()
string(REPLACE "${mipsync_tab_fill_old}" "${mipsync_tab_fill_new}"
       mipsync_imgui_widgets "${mipsync_imgui_widgets}")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated/imgui")
include("${CMAKE_CURRENT_LIST_DIR}/ImGuiRaisedControls.cmake")
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/generated/imgui/imgui_widgets.cpp"
     CONTENT "${mipsync_imgui_widgets}" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${imgui_SOURCE_DIR}/imgui_widgets.cpp")
