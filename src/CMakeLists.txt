# Engine sources are explicit so temporary/generated .cpp files cannot become
# part of a release merely by appearing under src/.
set(MIPSYNC_ENGINE_SOURCES
    animation/AnimatorControllerIO.cpp
    animation/AnimatorRuntime.cpp
    animation/SkeletalModel.cpp
    animation/SkinnedMesh.cpp
    assets/AssetManager.cpp
    assets/AssetThumbnail.cpp
    assets/Material.cpp
    audio/AudioDecoder.cpp
    audio/AudioSystem.cpp
    audio/Ps1VagEncoder.cpp
    bootstrap/AgentIntegration.cpp
    bootstrap/ProjectBootstrap.cpp
    build/BuildManifest.cpp
    build/PlayerBuild.cpp
    command/CommandExecutor.cpp
    command/CommandLine.cpp
    command/CommandRegistry.cpp
    command/CommandTypes.cpp
    command/CoreCommands.cpp
    command/EditorCommandHost.cpp
    command/IpcTransport.cpp
    command/ResultRenderer.cpp
    command/SymbolRegistry.cpp
    core/BootSplash.cpp
    core/Engine.cpp
    core/Input.cpp
    core/Log.cpp
    core/OsFileDrop.cpp
    core/RuntimePaths.cpp
    core/Time.cpp
    core/Win32AppIcon.cpp
    core/Window.cpp
    editor/AssetBrowserPanel.cpp
    editor/EditorAnimationWindow.cpp
    editor/EditorAnimatorControllerInspector.cpp
    editor/EditorAnimatorDebug.cpp
    editor/EditorAnimatorGraph.cpp
    editor/EditorApp.cpp
    editor/EditorApp.SceneView.cpp
    editor/EditorAssetIcons.cpp
    editor/EditorBuildSettings.cpp
    editor/EditorCameraGizmo.cpp
    editor/EditorCanvasGizmo.cpp
    editor/EditorColliderGizmo.cpp
    editor/EditorIcons.cpp
    editor/EditorLightGizmo.cpp
    editor/EditorRectTransformGizmo.cpp
    editor/EditorSceneCamera.cpp
    editor/EditorSceneFraming.cpp
    editor/EditorTheme.cpp
    editor/EditorUISnap.cpp
    editor/EditorUndoStack.cpp
    editor/GameViewSettings.cpp
    editor/MipsEditorIntegration.cpp
    editor/Raycast.cpp
    game/GameSave.cpp
    hub/HubApp.cpp
    main.cpp
    mips/AstPrinter.cpp
    mips/Bytecode.cpp
    mips/Compiler.cpp
    mips/Lexer.cpp
    mips/MipsPhysicsEvents.cpp
    mips/MipsRuntime.cpp
    mips/MipsSceneSnapshot.cpp
    mips/MipsScriptLoader.cpp
    mips/MipsTest.cpp
    mips/Parser.cpp
    mips/PS1Export.cpp
    mips/PS1SceneExport.cpp
    mips/PS1TextureExport.cpp
    mips/Token.cpp
    mips/VM.cpp
    physics/ColliderUtils.cpp
    physics/PhysicsWorld.cpp
    project/Project.cpp
    ps1/Ps1Build.cpp
    ps1/Ps1EditorPrefs.cpp
    ps1/Ps1Runner.cpp
    ps1/Ps1Toolchain.cpp
    renderer/Camera.cpp
    renderer/Framebuffer.cpp
    renderer/Mesh.cpp
    renderer/Renderer.cpp
    renderer/Shader.cpp
    renderer/Texture.cpp
    rhi/D3D12Context.cpp
    rhi/Gfx.cpp
    rhi/GfxImGui.cpp
    scene/Scene.cpp
    scene/SceneIO.cpp
    scene/SceneUI.cpp
    ui/RectTransform.cpp
    ui/UICanvasLayout.cpp
    ui/UILayout.cpp
    ui/UIRenderer.cpp
)

add_executable(MipsyncEngine ${MIPSYNC_ENGINE_SOURCES})

set(MIPSYNC_COMMAND_SOURCES
    bootstrap/AgentIntegration.cpp
    command/CommandTypes.cpp
    command/CommandRegistry.cpp
    command/SymbolRegistry.cpp
    command/CommandExecutor.cpp
    command/CommandLine.cpp
    command/ResultRenderer.cpp
    command/IpcTransport.cpp
    command/CoreCommands.cpp
    mips/Token.cpp
    mips/Lexer.cpp
    mips/Parser.cpp
    mips/Bytecode.cpp
    mips/Compiler.cpp
    mips/MipsScriptLoader.cpp
    project/Project.cpp
    core/Log.cpp
    core/RuntimePaths.cpp
)

add_executable(MipsyncCLI cli/main.cpp ${MIPSYNC_COMMAND_SOURCES})
set_target_properties(MipsyncCLI PROPERTIES OUTPUT_NAME mipsync)
target_compile_features(MipsyncCLI PRIVATE cxx_std_20)
target_include_directories(MipsyncCLI PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
)
target_link_libraries(MipsyncCLI PRIVATE
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)
option(MIPSYNC_DEPLOY_LATEST_INSTALL
       "Copy local builds into the newest per-user Mipsync install" ON)
if(WIN32 AND MIPSYNC_DEPLOY_LATEST_INSTALL)
    add_custom_command(TARGET MipsyncCLI POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DCLI_EXE="$<TARGET_FILE:MipsyncCLI>"
            -P "${CMAKE_SOURCE_DIR}/cmake/DeployLatestInstalledCli.cmake"
        COMMENT "Deploying MipsyncCLI to newest installed version"
    )
endif()

add_executable(MipsyncCommandTests
    ${CMAKE_SOURCE_DIR}/tests/CommandPlatformTests.cpp
    ${MIPSYNC_COMMAND_SOURCES}
)
target_compile_features(MipsyncCommandTests PRIVATE cxx_std_20)
target_include_directories(MipsyncCommandTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
)
target_link_libraries(MipsyncCommandTests PRIVATE
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)
enable_testing()
add_test(NAME MipsyncCommandPlatform COMMAND MipsyncCommandTests)
add_test(NAME MipsyncRuntimeRegression
    COMMAND $<TARGET_FILE:MipsyncEngine>
            --test-mips-runtime "${CMAKE_SOURCE_DIR}/tests/mips/ArraysCoroutines.mips")
add_test(NAME MipsyncSilentHillControllerValidation
    COMMAND $<TARGET_FILE:MipsyncEngine>
            --validate-mips "${CMAKE_SOURCE_DIR}/tests/mips/SilentHillController.mips")
add_test(NAME MipsyncProModelerBevel
    COMMAND $<TARGET_FILE:MipsyncEngine> --test-promodeler-bevel)
add_test(NAME MipsyncMeshCollider
    COMMAND $<TARGET_FILE:MipsyncEngine> --test-mesh-collider)

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
    target_link_libraries(MipsyncEngine PRIVATE comdlg32 ole32 oleaut32 shlwapi uuid winmm mfplat mfreadwrite mfuuid)
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

# License notices for source adapted from third-party UI systems must ship
# beside binary distributions as well as remain in the source files.
if(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/resources/licenses")
    add_custom_command(TARGET MipsyncEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/resources/licenses"
            "$<TARGET_FILE_DIR:MipsyncEngine>/resources/licenses"
        COMMENT "Copying third-party license notices next to executable"
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

if(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/tools/vscode-mips")
    add_custom_command(TARGET MipsyncEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DSRC_DIR="${CMAKE_SOURCE_DIR}"
            -DDST_DIR="$<TARGET_FILE_DIR:MipsyncEngine>"
            -P "${CMAKE_SOURCE_DIR}/cmake/CopyMipsIdeTools.cmake"
        COMMENT "Copying Mips# IDE integration tools next to executable"
    )
endif()

# Ship reusable Agent Skills next to the editor so coding agents can install
# the version that matches the bundled CLI protocol and command surface.
if(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/skills")
    add_custom_command(TARGET MipsyncEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/skills"
            "$<TARGET_FILE_DIR:MipsyncEngine>/skills"
        COMMENT "Copying Mipsync Agent Skills next to executable"
    )
endif()

# Copy Hub artifacts next to the engine so double-clicking the engine
# launches the newest locally-built Hub (and updater) without manual copying.
add_custom_command(TARGET MipsyncEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DSRC_DIR="${CMAKE_SOURCE_DIR}"
        -DDST_DIR="$<TARGET_FILE_DIR:MipsyncEngine>"
        -P "${CMAKE_SOURCE_DIR}/cmake/CopyHubArtifacts.cmake"
    COMMENT "Copying Hub artifacts next to engine (if present)"
)

if(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/templates/ps1")
    add_custom_command(TARGET MipsyncEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/templates/ps1"
            "$<TARGET_FILE_DIR:MipsyncEngine>/templates/ps1"
        COMMENT "Copying PS1 build templates next to executable"
    )
endif()

# Bundle PCSX-Redux + OpenBIOS (built into pcsx-redux.exe) next to the engine
# so a fresh Mipsync download can "Build & Run" PS1 targets with zero setup.
# Populate third_party/ps1_runtime via scripts/fetch_ps1_runtime.ps1 first.
if(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/third_party/ps1_runtime/emulator")
    add_custom_command(TARGET MipsyncEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/third_party/ps1_runtime"
            "$<TARGET_FILE_DIR:MipsyncEngine>/ps1_runtime"
        COMMENT "Bundling PCSX-Redux PS1 runtime next to executable"
    )
else()
    message(STATUS "third_party/ps1_runtime not found; run scripts/fetch_ps1_runtime.ps1 to bundle PCSX-Redux + OpenBIOS.")
endif()

# Bundle Ninja next to the editor so Build PS1 works without a separate install.
set(_MIPSYNC_NINJA "${CMAKE_SOURCE_DIR}/third_party/.cache/ninja/ninja.exe")
if(EXISTS "${_MIPSYNC_NINJA}")
    add_custom_command(TARGET MipsyncEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:MipsyncEngine>/tools"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_MIPSYNC_NINJA}"
            "$<TARGET_FILE_DIR:MipsyncEngine>/tools/ninja.exe"
        COMMENT "Copying Ninja next to executable for PS1 builds"
    )
endif()

# Keep the newest per-user installed editor in sync with local builds. The
# deployment script preserves install-only DLLs, Hub files, and user settings.
if(WIN32 AND MIPSYNC_DEPLOY_LATEST_INSTALL)
    add_custom_command(TARGET MipsyncEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DBUILD_DIR="$<TARGET_FILE_DIR:MipsyncEngine>"
            -P "${CMAKE_SOURCE_DIR}/cmake/DeployLatestInstalledEditor.cmake"
        COMMENT "Deploying editor build to newest installed version"
    )
endif()
