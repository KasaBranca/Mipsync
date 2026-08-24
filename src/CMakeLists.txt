# Collect all source files
file(GLOB_RECURSE SOURCES
    "*.cpp"
    "*.h"
)

# Main executable
add_executable(MipsyncEngine ${SOURCES})

# Target properties
target_compile_features(MipsyncEngine PRIVATE cxx_std_20)
set_target_properties(MipsyncEngine PROPERTIES 
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/EngineVersion.h.in"
    "${CMAKE_BINARY_DIR}/generated/EngineVersion.h"
    @ONLY)

# Include directories
target_include_directories(MipsyncEngine PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/extern/glad/include
    ${CMAKE_BINARY_DIR}/generated
)

# Link libraries
target_link_libraries(MipsyncEngine PRIVATE
    glad
    glfw
    glm::glm
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    imgui_lib
    imguizmo_lib
    stb
    tinyobjloader
    ufbx
    Jolt
)

target_compile_definitions(MipsyncEngine PRIVATE GLFW_INCLUDE_NONE)

# Windows specific
if(WIN32)
    target_link_libraries(MipsyncEngine PRIVATE comdlg32 ole32 winmm mfplat mfreadwrite mfuuid)
    if(EXISTS "${CMAKE_SOURCE_DIR}/resources/icons/app_icon.ico")
        enable_language(RC)
        target_sources(MipsyncEngine PRIVATE "${CMAKE_SOURCE_DIR}/resources/MipsyncEngine.rc")
    endif()
endif()
if(WIN32 AND NOT MSVC)
    # MinGW standard libs
    target_link_libraries(MipsyncEngine PRIVATE opengl32 gdi32 user32 shell32)
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/resources/icons/app_icon.png")
    add_custom_command(TARGET MipsyncEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:MipsyncEngine>/resources/icons"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/resources/icons/app_icon.png"
            "$<TARGET_FILE_DIR:MipsyncEngine>/resources/icons/app_icon.png"
        COMMENT "Copying window icon next to executable"
    )
endif()

if(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/resources/fonts")
    add_custom_command(TARGET MipsyncEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/resources/fonts"
            "$<TARGET_FILE_DIR:MipsyncEngine>/resources/fonts"
        COMMENT "Copying UI fonts next to executable"
    )
endif()

if(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/resources/icons/project")
    add_custom_command(TARGET MipsyncEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/resources/icons/project"
            "$<TARGET_FILE_DIR:MipsyncEngine>/resources/icons/project"
        COMMENT "Copying project panel icons next to executable"
    )
endif()

if(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/templates/ps1")
    add_custom_command(TARGET MipsyncEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/templates/ps1"
            "$<TARGET_FILE_DIR:MipsyncEngine>/templates/ps1"
        COMMENT "Copying PS1 build templates next to executable"
    )
endif()
