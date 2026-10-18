include("${CMAKE_SOURCE_DIR}/cmake/CompileInspectorScriptIcon.cmake")
compile_inspector_script_icon(
    "${CMAKE_SOURCE_DIR}/resources/icons/project/script.svg"
    "${CMAKE_BINARY_DIR}/generated/InspectorScriptIcon.h")

add_library(MipsyncAssetFiles STATIC core/AssetFiles.cpp)
add_library(MipsyncSkinnedSurfaceReduction STATIC
    mips/SkinnedSurfaceReduction.cpp
    ${CMAKE_SOURCE_DIR}/extern/meshoptimizer/allocator.cpp
    ${CMAKE_SOURCE_DIR}/extern/meshoptimizer/indexgenerator.cpp
    ${CMAKE_SOURCE_DIR}/extern/meshoptimizer/simplifier.cpp)
target_compile_features(MipsyncSkinnedSurfaceReduction PUBLIC cxx_std_20)
target_include_directories(MipsyncSkinnedSurfaceReduction PRIVATE
    ${CMAKE_SOURCE_DIR}/extern/meshoptimizer)
add_executable(MipsyncSkinnedSurfaceReductionTests
    ${CMAKE_SOURCE_DIR}/tests/SkinnedSurfaceReductionTests.cpp)
target_include_directories(MipsyncSkinnedSurfaceReductionTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(MipsyncSkinnedSurfaceReductionTests PRIVATE MipsyncSkinnedSurfaceReduction)
add_test(NAME MipsyncSkinnedSurfaceReduction COMMAND MipsyncSkinnedSurfaceReductionTests)
target_compile_features(MipsyncAssetFiles PUBLIC cxx_std_20)
target_link_libraries(MipsyncAssetFiles PUBLIC nlohmann_json::nlohmann_json)
if(WIN32)
    target_link_libraries(MipsyncAssetFiles PUBLIC bcrypt)
endif()

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
    audio/MemoryAudioVoice.cpp
    audio/AudioSystem.cpp
    audio/MidiFile.cpp
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
    core/PlayLoadingProcess.cpp
    core/Engine.cpp
    ps1/PSync.cpp
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
    editor/EditorSlider.cpp
    editor/EditorGameView.cpp
    editor/EditorDropdown.cpp
    editor/EditorMenu.cpp
    editor/EditorComponentPicker.cpp
    editor/EditorPreferences.cpp
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
    ps1/BootLogo.cpp
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
    scene/Transform.cpp
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
    mips/PS1Export.cpp
    project/Project.cpp
    core/Log.cpp
    core/RuntimePaths.cpp
)

add_executable(MipsyncCLI cli/main.cpp ${MIPSYNC_COMMAND_SOURCES})
set_target_properties(MipsyncCLI PROPERTIES OUTPUT_NAME mipsync)

# Keep source-built programs usable outside the compiler's PATH.
if(MINGW)
    get_filename_component(_MIPSYNC_COMPILER_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(_MIPSYNC_DLL libstdc++-6.dll libwinpthread-1.dll
            libgcc_s_seh-1.dll libgcc_s_dw2-1.dll libgcc_s_sjlj-1.dll)
        if(EXISTS "${_MIPSYNC_COMPILER_DIR}/${_MIPSYNC_DLL}")
            foreach(_MIPSYNC_TARGET MipsyncEngine MipsyncCLI)
                add_custom_command(TARGET ${_MIPSYNC_TARGET} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_MIPSYNC_COMPILER_DIR}/${_MIPSYNC_DLL}"
                        "$<TARGET_FILE_DIR:${_MIPSYNC_TARGET}>/${_MIPSYNC_DLL}")
            endforeach()
        endif()
    endforeach()
endif()
target_compile_features(MipsyncCLI PRIVATE cxx_std_20)
target_include_directories(MipsyncCLI PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
)
target_link_libraries(MipsyncCLI PRIVATE
    MipsyncAssetFiles
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)
if(WIN32)
    target_link_libraries(MipsyncCLI PRIVATE shell32)
endif()
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
    MipsyncAssetFiles
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)
enable_testing()
add_executable(MipsyncInspectorTests
    ${CMAKE_SOURCE_DIR}/tests/MipsInspectorTests.cpp
    mips/Lexer.cpp mips/Parser.cpp mips/Compiler.cpp mips/Bytecode.cpp mips/PS1Export.cpp)
target_compile_features(MipsyncInspectorTests PRIVATE cxx_std_20)
target_include_directories(MipsyncInspectorTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(MipsyncInspectorTests PRIVATE imgui_lib nlohmann_json::nlohmann_json spdlog::spdlog glm::glm)
add_test(NAME MipsyncInspector COMMAND MipsyncInspectorTests)

add_executable(MipsyncComponentClipboardTests
    ${CMAKE_SOURCE_DIR}/tests/EditorComponentClipboardTests.cpp renderer/Camera.cpp scene/Transform.cpp)
target_compile_features(MipsyncComponentClipboardTests PRIVATE cxx_std_20)
target_include_directories(MipsyncComponentClipboardTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(MipsyncComponentClipboardTests PRIVATE glm::glm)
add_test(NAME MipsyncComponentClipboard COMMAND MipsyncComponentClipboardTests)
add_executable(MipsyncPlayerPackageTests
    ${CMAKE_SOURCE_DIR}/tests/PlayerPackageTests.cpp
    ${MIPSYNC_COMMAND_SOURCES}
    build/PlayerBuild.cpp build/BuildManifest.cpp audio/AudioDecoder.cpp
    audio/MemoryAudioVoice.cpp assets/Material.cpp)
target_compile_features(MipsyncPlayerPackageTests PRIVATE cxx_std_20)
target_include_directories(MipsyncPlayerPackageTests PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(MipsyncPlayerPackageTests PRIVATE MipsyncAssetFiles nlohmann_json::nlohmann_json spdlog::spdlog glm::glm)
if(WIN32)
    target_link_libraries(MipsyncPlayerPackageTests PRIVATE shell32 ole32 shlwapi mfplat mfreadwrite mfuuid winmm)
endif()
add_test(NAME MipsyncPlayerPackage COMMAND MipsyncPlayerPackageTests $<TARGET_FILE:MipsyncEngine>)
add_test(NAME MipsyncCommandPlatform COMMAND MipsyncCommandTests)
add_executable(MipsyncPs1LightingTests
    ${CMAKE_SOURCE_DIR}/tests/Ps1LightingTests.cpp
    ${CMAKE_SOURCE_DIR}/templates/ps1/starter/runtime/lighting.c)
target_include_directories(MipsyncPs1LightingTests PRIVATE
    ${CMAKE_SOURCE_DIR}/templates/ps1/starter/runtime)
add_test(NAME MipsyncPs1Lighting COMMAND MipsyncPs1LightingTests)
add_executable(MipsyncUnlitRendererTests
    ${CMAKE_SOURCE_DIR}/tests/UnlitRendererTests.cpp
    renderer/Renderer.cpp renderer/Shader.cpp renderer/Mesh.cpp
    renderer/Camera.cpp renderer/Texture.cpp renderer/Framebuffer.cpp
    animation/SkinnedMesh.cpp scene/Transform.cpp core/Log.cpp ps1/PSync.cpp)
target_include_directories(MipsyncUnlitRendererTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/generated)
target_compile_definitions(MipsyncUnlitRendererTests PRIVATE GLFW_INCLUDE_NONE)
target_link_libraries(MipsyncUnlitRendererTests PRIVATE
    glad glfw glm::glm spdlog::spdlog stb ufbx tinyobjloader
    MipsyncAssetFiles nlohmann_json::nlohmann_json)
add_test(NAME MipsyncUnlitRenderer COMMAND MipsyncUnlitRendererTests)
add_executable(MipsyncPs1ColorGradingTests
    ${CMAKE_SOURCE_DIR}/tests/Ps1ColorGradingTests.cpp
    ${CMAKE_SOURCE_DIR}/templates/ps1/starter/runtime/color_grading.c)
target_compile_features(MipsyncPs1ColorGradingTests PRIVATE cxx_std_20)
target_include_directories(MipsyncPs1ColorGradingTests PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/stubs/ps1_gpu
    ${CMAKE_SOURCE_DIR}/templates/ps1/starter/runtime ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(MipsyncPs1ColorGradingTests PRIVATE glm::glm)
add_test(NAME MipsyncPs1ColorGrading COMMAND MipsyncPs1ColorGradingTests)
add_executable(MipsyncPs1GpuTransferTests
    ${CMAKE_SOURCE_DIR}/tests/Ps1GpuTransferTests.cpp
    ${CMAKE_SOURCE_DIR}/templates/ps1/starter/runtime/gpu_transfer.c)
target_include_directories(MipsyncPs1GpuTransferTests PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/stubs/ps1_gpu
    ${CMAKE_SOURCE_DIR}/templates/ps1/starter/runtime)
add_test(NAME MipsyncPs1GpuTransfer COMMAND MipsyncPs1GpuTransferTests)
add_executable(MipsyncPs1AnimationPhaseTests
    ${CMAKE_SOURCE_DIR}/tests/Ps1AnimationPhaseTests.cpp
    ${CMAKE_SOURCE_DIR}/templates/ps1/starter/runtime/scene.c
    ${CMAKE_SOURCE_DIR}/templates/ps1/starter/runtime/fixedp.c)
target_include_directories(MipsyncPs1AnimationPhaseTests PRIVATE
    ${CMAKE_SOURCE_DIR}/templates/ps1/starter/runtime)
add_test(NAME MipsyncPs1AnimationPhase COMMAND MipsyncPs1AnimationPhaseTests)
add_executable(MipsyncPs1DestroyTests
    ${CMAKE_SOURCE_DIR}/tests/Ps1DestroyTests.cpp
    ${CMAKE_SOURCE_DIR}/templates/ps1/starter/runtime/scene.c
    ${CMAKE_SOURCE_DIR}/templates/ps1/starter/runtime/physics.c
    ${CMAKE_SOURCE_DIR}/templates/ps1/starter/runtime/fixedp.c)
target_include_directories(MipsyncPs1DestroyTests PRIVATE
    ${CMAKE_SOURCE_DIR}/templates/ps1/starter/runtime)
add_test(NAME MipsyncPs1Destroy COMMAND MipsyncPs1DestroyTests)
add_executable(MipsyncPs1ObjectApiTests
    ${CMAKE_SOURCE_DIR}/tests/Ps1ObjectApiTests.cpp
    mips/Lexer.cpp mips/Parser.cpp mips/Compiler.cpp mips/Bytecode.cpp mips/PS1Export.cpp
    ${CMAKE_SOURCE_DIR}/templates/ps1/starter/runtime/vm.c
    ${CMAKE_SOURCE_DIR}/templates/ps1/starter/runtime/host.c
    ${CMAKE_SOURCE_DIR}/templates/ps1/starter/runtime/scene.c
    ${CMAKE_SOURCE_DIR}/templates/ps1/starter/runtime/physics.c
    ${CMAKE_SOURCE_DIR}/templates/ps1/starter/runtime/fixedp.c)
target_compile_features(MipsyncPs1ObjectApiTests PRIVATE cxx_std_20)
target_include_directories(MipsyncPs1ObjectApiTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/stubs/ps1_host
    ${CMAKE_SOURCE_DIR}/templates/ps1/starter/runtime)
target_link_libraries(MipsyncPs1ObjectApiTests PRIVATE nlohmann_json::nlohmann_json spdlog::spdlog glm::glm)
add_test(NAME MipsyncPs1ObjectApi COMMAND MipsyncPs1ObjectApiTests ${CMAKE_SOURCE_DIR}/tests/mips/ObjectReferences.mips)
add_executable(MipsyncPs1ViewmodelProjectionTests
    ${CMAKE_SOURCE_DIR}/tests/Ps1ViewmodelProjectionTests.c)
add_test(NAME MipsyncPs1ViewmodelProjection COMMAND MipsyncPs1ViewmodelProjectionTests)
add_executable(MipsyncPs1ClipMathTests
    ${CMAKE_SOURCE_DIR}/tests/Ps1ClipMathTests.c)
add_test(NAME MipsyncPs1ClipMath COMMAND MipsyncPs1ClipMathTests)
add_executable(MipsyncPs1TextureResolutionTests
    ${CMAKE_SOURCE_DIR}/tests/Ps1TextureResolutionTests.cpp
    mips/PS1TextureExport.cpp assets/Material.cpp core/Log.cpp)
target_include_directories(MipsyncPs1TextureResolutionTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(MipsyncPs1TextureResolutionTests PRIVATE
    glm::glm spdlog::spdlog stb MipsyncAssetFiles nlohmann_json::nlohmann_json)
add_test(NAME MipsyncPs1TextureResolution COMMAND MipsyncPs1TextureResolutionTests)
add_executable(MipsyncPs1BootBrandGenerator
    ${CMAKE_SOURCE_DIR}/tools/generate-ps1-boot-brand.cpp)
target_include_directories(MipsyncPs1BootBrandGenerator PRIVATE ${imgui_SOURCE_DIR})
target_link_libraries(MipsyncPs1BootBrandGenerator PRIVATE stb)
set(MIPSYNC_BOOT_BRAND_IMAGE "${CMAKE_SOURCE_DIR}/templates/ps1/starter/runtime/boot_brand_image.h")
add_custom_command(OUTPUT "${MIPSYNC_BOOT_BRAND_IMAGE}"
    COMMAND MipsyncPs1BootBrandGenerator
        "${CMAKE_SOURCE_DIR}/resources/icons/app_icon.png"
        "${CMAKE_SOURCE_DIR}/resources/fonts/Inter-SemiBold.ttf"
        "-"
        "-" "${MIPSYNC_BOOT_BRAND_IMAGE}"
    DEPENDS MipsyncPs1BootBrandGenerator
        "${CMAKE_SOURCE_DIR}/resources/icons/app_icon.png"
        "${CMAKE_SOURCE_DIR}/resources/fonts/Inter-SemiBold.ttf"
    VERBATIM)
add_custom_target(MipsyncPs1BootBrandAssets DEPENDS "${MIPSYNC_BOOT_BRAND_IMAGE}")
add_dependencies(MipsyncEngine MipsyncPs1BootBrandAssets)
add_executable(MipsyncPs1BootLogoTests ${CMAKE_SOURCE_DIR}/tests/Ps1BootLogoTests.cpp ps1/BootLogo.cpp)
target_include_directories(MipsyncPs1BootLogoTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(MipsyncPs1BootLogoTests PRIVATE
    MIPSYNC_BOOT_LICENSE="${CMAKE_SOURCE_DIR}/templates/ps1/licensej.dat")
add_test(NAME MipsyncPs1BootLogo COMMAND MipsyncPs1BootLogoTests)
add_executable(MipsyncPs1BootBrandTests ${CMAKE_SOURCE_DIR}/tests/Ps1BootBrandTests.c)
target_include_directories(MipsyncPs1BootBrandTests PRIVATE ${CMAKE_SOURCE_DIR}/templates/ps1/starter/runtime)
add_dependencies(MipsyncPs1BootBrandTests MipsyncPs1BootBrandAssets)
add_test(NAME MipsyncPs1BootBrand COMMAND MipsyncPs1BootBrandTests)
find_path(MIPSYNC_TEST_PAD_SDK psxpad.h
    HINTS "${CMAKE_SOURCE_DIR}/third_party/.cache/psn00bsdk-build/sdk/PSn00bSDK-0.24-win32/include/libpsn00b")
if(MIPSYNC_TEST_PAD_SDK)
    add_executable(MipsyncPs1PadBackendTests ${CMAKE_SOURCE_DIR}/tests/Ps1PadBackendTests.c
        ${CMAKE_SOURCE_DIR}/templates/ps1/starter/runtime/fixedp.c)
    target_include_directories(MipsyncPs1PadBackendTests PRIVATE
        ${CMAKE_SOURCE_DIR}/tests/stubs/ps1_input
        ${CMAKE_SOURCE_DIR}/templates/ps1/starter/runtime)
    target_compile_definitions(MipsyncPs1PadBackendTests PRIVATE
        MIPSYNC_TEST_PAD_HEADER="${MIPSYNC_TEST_PAD_SDK}/psxpad.h")
    add_test(NAME MipsyncPs1PadBackend COMMAND MipsyncPs1PadBackendTests)
endif()
add_executable(MipsyncMidiTests
    ${CMAKE_SOURCE_DIR}/tests/MidiTests.cpp audio/MidiFile.cpp)
target_link_libraries(MipsyncMidiTests PRIVATE MipsyncAssetFiles)
target_include_directories(MipsyncMidiTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(MipsyncMidiTests PRIVATE cxx_std_20)
add_test(NAME MipsyncMidi COMMAND MipsyncMidiTests)

add_executable(MipsyncPSyncTests ${CMAKE_SOURCE_DIR}/tests/PSyncTests.cpp ps1/PSync.cpp
    ${CMAKE_SOURCE_DIR}/tests/PSyncFrameTimingTests.c
    ${CMAKE_SOURCE_DIR}/templates/ps1/starter/runtime/fixedp.c)
target_compile_features(MipsyncPSyncTests PRIVATE cxx_std_20)
target_include_directories(MipsyncPSyncTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(MipsyncPSyncTests PRIVATE glm::glm)
add_test(NAME MipsyncPSync COMMAND MipsyncPSyncTests)
add_executable(MipsyncPSyncInputTests ${CMAKE_SOURCE_DIR}/tests/PSyncInputTests.cpp)
target_compile_features(MipsyncPSyncInputTests PRIVATE cxx_std_20)
target_include_directories(MipsyncPSyncInputTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(MipsyncPSyncInputTests PRIVATE glm::glm glfw)
add_test(NAME MipsyncPSyncInput COMMAND MipsyncPSyncInputTests)
add_test(NAME MipsyncPSyncVM COMMAND MipsyncEngine --test-mips-runtime
    "${CMAKE_SOURCE_DIR}/tests/mips/ArraysCoroutines.mips")
add_executable(MipsyncComponentPickerTests
    ${CMAKE_SOURCE_DIR}/tests/EditorComponentPickerTests.cpp editor/EditorComponentPicker.cpp)
target_compile_features(MipsyncComponentPickerTests PRIVATE cxx_std_20)
target_include_directories(MipsyncComponentPickerTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(MipsyncComponentPickerTests PRIVATE imgui_lib)
add_test(NAME MipsyncComponentPicker COMMAND MipsyncComponentPickerTests)
add_executable(MipsyncPreferencesTests
    ${CMAKE_SOURCE_DIR}/tests/EditorPreferencesTests.cpp
    editor/EditorPreferences.cpp editor/EditorDropdown.cpp editor/EditorMenu.cpp
    editor/EditorSlider.cpp editor/EditorTheme.cpp)
target_compile_features(MipsyncPreferencesTests PRIVATE cxx_std_20)
target_include_directories(MipsyncPreferencesTests PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/extern/glad/include)
target_link_libraries(MipsyncPreferencesTests PRIVATE imgui_lib nlohmann_json::nlohmann_json glm::glm)
if(WIN32)
    target_link_libraries(MipsyncPreferencesTests PRIVATE user32 gdi32)
endif()
add_test(NAME MipsyncPreferences COMMAND MipsyncPreferencesTests)
add_executable(MipsyncMeshPartsTests ${CMAKE_SOURCE_DIR}/tests/MeshPartsTests.cpp
    renderer/Mesh.cpp core/Log.cpp)
target_compile_features(MipsyncMeshPartsTests PRIVATE cxx_std_20)
target_include_directories(MipsyncMeshPartsTests PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(MipsyncMeshPartsTests PRIVATE
    glad glm::glm spdlog::spdlog ufbx tinyobjloader MipsyncAssetFiles nlohmann_json::nlohmann_json)
add_test(NAME MipsyncMeshParts COMMAND MipsyncMeshPartsTests)
if(WIN32)
    add_executable(MipsyncPreferencesNativeTests
        ${CMAKE_SOURCE_DIR}/tests/EditorPreferencesNativeTests.cpp
        editor/EditorPreferences.cpp editor/EditorDropdown.cpp editor/EditorMenu.cpp
        editor/EditorSlider.cpp editor/EditorTheme.cpp)
    target_compile_features(MipsyncPreferencesNativeTests PRIVATE cxx_std_20)
    target_include_directories(MipsyncPreferencesNativeTests PRIVATE
        ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/extern/glad/include)
    target_link_libraries(MipsyncPreferencesNativeTests PRIVATE
        imgui_lib glad nlohmann_json::nlohmann_json glm::glm user32 gdi32)
    add_test(NAME MipsyncPreferencesNative COMMAND MipsyncPreferencesNativeTests)
    set_tests_properties(MipsyncPreferencesNative PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()
add_test(NAME MipsyncRuntimeRegression
    COMMAND $<TARGET_FILE:MipsyncEngine>
            --test-mips-runtime "${CMAKE_SOURCE_DIR}/tests/mips/ArraysCoroutines.mips")
# Both entries execute the same round-trip tests with shared legacy temp paths.
set_tests_properties(MipsyncPSyncVM MipsyncRuntimeRegression PROPERTIES
    RESOURCE_LOCK MipsyncRuntimeFixtures)
add_test(NAME MipsyncSilentHillControllerValidation
    COMMAND $<TARGET_FILE:MipsyncEngine>
            --validate-mips "${CMAKE_SOURCE_DIR}/tests/mips/SilentHillController.mips")
add_test(NAME MipsyncProModelerBevel
    COMMAND $<TARGET_FILE:MipsyncEngine> --test-promodeler-bevel)
add_test(NAME MipsyncMeshCollider
    COMMAND $<TARGET_FILE:MipsyncEngine> --test-mesh-collider)

if(WIN32)
    find_program(MIPSYNC_TEST_POWERSHELL NAMES pwsh powershell REQUIRED)
    add_test(NAME MipsyncPs1ModelPartsAndSubdivision
        COMMAND "${MIPSYNC_TEST_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/tests/Ps1ModelPartsAndSubdivision.ps1"
            -EngineExe $<TARGET_FILE:MipsyncEngine>
            -WorkDir "${CMAKE_BINARY_DIR}/tests/ps1-model-parts-and-subdivision")
    add_test(NAME MipsyncUnicodeCommandLine
        COMMAND "${MIPSYNC_TEST_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/tests/UnicodeCommandLine.ps1"
            -EngineExe $<TARGET_FILE:MipsyncEngine>
            -WorkDir "${CMAKE_BINARY_DIR}/Testing/unicode-command-line")
    add_test(NAME MipsyncPs1SampleExport
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/tests/Ps1SampleExport.ps1"
            -EngineExe "$<TARGET_FILE:MipsyncEngine>"
            -WorkDir "${CMAKE_BINARY_DIR}/Testing/ps1-sample-export")
    add_test(NAME MipsyncPs1MidiExport
        COMMAND "${MIPSYNC_TEST_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/tests/Ps1MidiExport.ps1"
            -EngineExe $<TARGET_FILE:MipsyncEngine>
            -WorkDir "${CMAKE_BINARY_DIR}/Testing/ps1-midi-export")
    add_test(NAME MipsyncPs1StartupSceneOrder
        COMMAND "${MIPSYNC_TEST_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/tests/Ps1StartupScene.ps1"
            -EngineExe $<TARGET_FILE:MipsyncEngine>
            -WorkDir "${CMAKE_BINARY_DIR}/Testing/ps1-startup-scene")
    add_test(NAME MipsyncPs1VisualExport
        COMMAND "${MIPSYNC_TEST_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/tests/Ps1VisualExport.ps1"
            -EngineExe $<TARGET_FILE:MipsyncEngine>
            -WorkDir "${CMAKE_BINARY_DIR}/Testing/ps1-visual-export")
endif()

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
    MipsyncSkinnedSurfaceReduction
    glad
    glfw
    glm::glm
    spdlog::spdlog
    MipsyncAssetFiles
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
    get_property(_MIPSYNC_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    set(_MIPSYNC_TEMPLATE_STAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    if(_MIPSYNC_MULTI_CONFIG)
        string(APPEND _MIPSYNC_TEMPLATE_STAGE_DIR "/$<CONFIG>")
    endif()
    set(_MIPSYNC_TEMPLATE_COPY_COMMAND copy_directory)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.26)
        set(_MIPSYNC_TEMPLATE_COPY_COMMAND copy_directory_if_different)
    endif()
    add_custom_target(MipsyncPs1TemplateAssets
        COMMAND ${CMAKE_COMMAND} -E ${_MIPSYNC_TEMPLATE_COPY_COMMAND}
            "${CMAKE_SOURCE_DIR}/templates/ps1"
            "${_MIPSYNC_TEMPLATE_STAGE_DIR}/templates/ps1"
        COMMENT "Synchronizing PS1 build templates"
    )
    add_dependencies(MipsyncPs1TemplateAssets MipsyncPs1BootBrandAssets)
    add_dependencies(MipsyncEngine MipsyncPs1TemplateAssets)
    if(WIN32 AND MIPSYNC_DEPLOY_LATEST_INSTALL)
        add_custom_command(TARGET MipsyncPs1TemplateAssets POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                -DBUILD_DIR=${_MIPSYNC_TEMPLATE_STAGE_DIR}
                -DPS1_TEMPLATES_ONLY=ON
                -P "${CMAKE_SOURCE_DIR}/cmake/DeployLatestInstalledEditor.cmake")
    endif()
endif()

# Bundle PCSX-Redux + OpenBIOS (built into pcsx-redux.exe) next to the engine
# so a fresh Mipsync download can "Build & Run" PS1 targets with zero setup.
# Populate third_party/ps1_runtime via scripts/fetch_ps1_runtime.ps1 first.
if(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/third_party/ps1_runtime/emulator")
    set(_MIPSYNC_RUNTIME_COPY_COMMAND copy_directory)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.26)
        set(_MIPSYNC_RUNTIME_COPY_COMMAND copy_directory_if_different)
    endif()
    add_custom_command(TARGET MipsyncEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E ${_MIPSYNC_RUNTIME_COPY_COMMAND}
            "${CMAKE_SOURCE_DIR}/third_party/ps1_runtime"
            "$<TARGET_FILE_DIR:MipsyncEngine>/ps1_runtime"
        COMMENT "Bundling PCSX-Redux PS1 runtime next to executable"
    )
else()
    message(STATUS "third_party/ps1_runtime not found; run scripts/fetch_ps1_runtime.ps1 to bundle PCSX-Redux + OpenBIOS.")
endif()

# Bundle Ninja next to the editor so Build PS1 works without a separate install.
set(_MIPSYNC_NINJA "${CMAKE_SOURCE_DIR}/third_party/.cache/ninja/ninja.exe")
if(NOT EXISTS "${_MIPSYNC_NINJA}")
    set(_MIPSYNC_NINJA "${CMAKE_SOURCE_DIR}/tools/ninja.exe")
endif()
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
