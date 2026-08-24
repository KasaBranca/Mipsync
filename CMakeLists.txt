cmake_minimum_required(VERSION 3.20)
project(MipsyncEngine VERSION 2026.8.6 LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可
# FetchContent: all dependencies
# 髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# GLFW
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        7b6aead9fb88b3623e3b3725ebb42670cbe4c579
    GIT_SHALLOW    TRUE
)

# GLM
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        0af55ccecd98d4e5a8d1fad7de25ba429d60e863
    GIT_SHALLOW    TRUE
)

# spdlog
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        6fa36017cfd5731d617e1a934f0e5ea9c4445b13
    GIT_SHALLOW    TRUE
)

# nlohmann/json
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03
    GIT_SHALLOW    TRUE
)

# Dear ImGui (docking branch)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        5098ce161db7f3919630b2b575347fc3b2ce5c62
)

# ImGuizmo
FetchContent_Declare(imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG        a712ea83e937cc6f11e22c3b2c82920857ae13df
)

FetchContent_MakeAvailable(glfw glm spdlog json imgui)

FetchContent_GetProperties(imguizmo)
if(NOT imguizmo_POPULATED)
    FetchContent_Populate(imguizmo)
endif()

# 髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可
# GLAD (generated, bundled in project)
# 髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可
add_library(glad STATIC
    ${CMAKE_SOURCE_DIR}/extern/glad/src/glad.c
)
target_include_directories(glad PUBLIC ${CMAKE_SOURCE_DIR}/extern/glad/include)

# 髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可
# Dear ImGui library (manual build)
# 髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC glfw)
target_compile_definitions(imgui_lib PUBLIC IMGUI_IMPL_OPENGL_LOADER_GLAD)

# 髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可
# ImGuizmo library
# 髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可
add_library(imguizmo_lib STATIC
    ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp
)
target_include_directories(imguizmo_lib PUBLIC ${imguizmo_SOURCE_DIR}/src)
target_link_libraries(imguizmo_lib PUBLIC imgui_lib)

# 髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可
# stb (bundled)
# 髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可
add_library(stb STATIC
    ${CMAKE_SOURCE_DIR}/extern/stb/stb_image_impl.c
    ${CMAKE_SOURCE_DIR}/extern/stb/stb_image_write_impl.c
)
target_include_directories(stb PUBLIC ${CMAKE_SOURCE_DIR}/extern/stb)
# Make stbi_load() route through _wfopen so UTF-8 paths (Japanese filenames, etc.) work on Windows.
target_compile_definitions(stb PRIVATE STBI_WINDOWS_UTF8)

# 髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可
# tinyobjloader (header-only, bundled)
# 髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可
add_library(tinyobjloader INTERFACE)
target_include_directories(tinyobjloader INTERFACE ${CMAKE_SOURCE_DIR}/extern/tinyobjloader)

# 髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可
# ufbx (FBX loader, single-file C library)
# 髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可
FetchContent_Declare(ufbx
    GIT_REPOSITORY https://github.com/ufbx/ufbx.git
    GIT_TAG        2aef1971276442f9f6ce14c42b37deb8da9f4d3d
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(ufbx)
if(NOT ufbx_POPULATED)
    FetchContent_Populate(ufbx)
    add_library(ufbx STATIC ${ufbx_SOURCE_DIR}/ufbx.c)
    target_include_directories(ufbx PUBLIC ${ufbx_SOURCE_DIR})
    if(MSVC)
        target_compile_options(ufbx PRIVATE /W0)
    else()
        target_compile_options(ufbx PRIVATE -w)
    endif()
endif()

# 髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可
# Jolt Physics
# 髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(JPH_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG        a63aa3b8e24cf95f3fab2613f9a3015b164ef62c
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(JoltPhysics)
if(NOT joltphysics_POPULATED)
    FetchContent_Populate(JoltPhysics)
    add_subdirectory(${joltphysics_SOURCE_DIR}/Build ${joltphysics_BINARY_DIR}/jolt-build)
endif()

# 髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可
# Main executable
# 髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可髫ｨ貂可
add_subdirectory(src)














