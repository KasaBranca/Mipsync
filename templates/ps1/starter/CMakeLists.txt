cmake_minimum_required(VERSION 3.21)

project(
    mipsync_ps1_starter
    LANGUAGES    C ASM
    VERSION      0.1.0
    DESCRIPTION  "Mipsync Engine PS1 starter (PSn00bSDK)"
)

# Produces mipsync.exe (PS-EXE), which the Mipsync build script renames to
# PSX.EXE before bundling into the disc image / running in the emulator.
# Runtime sources live under runtime/; user-script bytecode is stamped into
# generated/scripts_data.c every time the host engine runs `Build PS1`.
function(mipsync_add_ps1_executable name)
    add_executable(${name} ${ARGN})
    set_target_properties(${name} PROPERTIES
        PSN00BSDK_TARGET_TYPE EXECUTABLE_GPREL
    )
    # libpsxgte is intentionally excluded. Mipsync ships an original minimal
    # GTE/COP2 compatibility layer under runtime/mipsync_gte.* instead.
    set(mipsync_psn00b_libraries ${PSN00BSDK_EXECUTABLE_LINK_LIBRARIES})
    # Current PSn00bSDK packages expose this list as absolute archive paths,
    # while older packages may expose logical library names. Filtering the
    # complete entry handles both forms; REMOVE_ITEM "psxgte" does not handle
    # an entry such as .../libpsxgte_exe_gprel.a.
    list(FILTER mipsync_psn00b_libraries EXCLUDE REGEX "(^|[/\\\\])lib?psxgte([_.-]|$)|^psxgte$")
    target_link_libraries(${name} PRIVATE ${mipsync_psn00b_libraries})
    target_link_options(${name} PRIVATE
        -T$<SHELL_PATH:${PSN00BSDK_LDSCRIPTS}/exe.ld>
    )

    # PSn00bSDK 0.24 generates the symbol map with an unquoted shell `>`.
    # That splits build paths containing spaces. Generate the PS-EXE normally,
    # then capture nm output through a CMake script instead of shell redirection.
    add_custom_command(
        TARGET ${name} POST_BUILD
        COMMAND ${ELF2X} -q
            $<SHELL_PATH:$<TARGET_FILE:${name}>>
            $<SHELL_PATH:${CMAKE_CURRENT_BINARY_DIR}/${name}${PSN00BSDK_EXECUTABLE_SUFFIX}>
        COMMAND ${CMAKE_COMMAND}
            "-DNM=${TOOLCHAIN_NM}"
            "-DELF=$<TARGET_FILE:${name}>"
            "-DMAP=${CMAKE_CURRENT_BINARY_DIR}/${name}${PSN00BSDK_SYMBOL_MAP_SUFFIX}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/write_symbol_map.cmake"
        BYPRODUCTS
            ${CMAKE_CURRENT_BINARY_DIR}/${name}${PSN00BSDK_EXECUTABLE_SUFFIX}
            ${CMAKE_CURRENT_BINARY_DIR}/${name}${PSN00BSDK_SYMBOL_MAP_SUFFIX}
        VERBATIM
    )
endfunction()

function(mipsync_add_scene_executable name source_dir)
    set(scene_sources
    main.c
    runtime/fixedp.c
    runtime/boot_brand.c
    runtime/mipsync_gte.c
    runtime/host.c
    runtime/vm.c
    runtime/scene.c
    runtime/scene_loader.c
    runtime/lookat.c
    runtime/lighting.c
    runtime/color_grading.c
    runtime/gpu_transfer.c
    runtime/gpu_readback.c
    runtime/render.c
    runtime/ui.c
    runtime/textures.c
    runtime/input.c
    runtime/physics.c
    runtime/audio.c
    runtime/midi.c
    generated/scripts_data.c
    generated/scene_data.c
    generated/mesh_data.c
    generated/textures_data.c
    generated/audio_data.c
    )
    list(TRANSFORM scene_sources PREPEND "${source_dir}/")
    mipsync_add_ps1_executable(${name} ${scene_sources})
    # Optimize the generated scene and runtime together; visibility and fixed
    # point helpers otherwise spend a substantial part of each PS1 frame in
    # cross-file calls. Keep compiler and linker options paired for LTO.
    target_compile_options(${name} PRIVATE -O3 -flto)
    target_link_options(${name} PRIVATE -O3 -flto)
    target_include_directories(${name} PRIVATE "${source_dir}")
endfunction()

mipsync_add_scene_executable(mipsync "${CMAKE_CURRENT_SOURCE_DIR}")
set(MIPSYNC_SCENE_TARGETS mipsync)
include("${CMAKE_CURRENT_SOURCE_DIR}/generated/scene_targets.cmake" OPTIONAL)

# PSn00bSDK relocates iso.xml into the binary directory without rewriting
# <license file>. Keep unmodified regional system-area data beside that XML.
foreach(license IN ITEMS licensej.dat licensea.dat licensee.dat)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${license}")
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${license}"
                       "${CMAKE_CURRENT_BINARY_DIR}/${license}" COPYONLY)
    endif()
endforeach()

psn00bsdk_add_cd_image(
    iso             # CMake target name
    mipsync         # Output basename (= mipsync.bin + mipsync.cue)
    iso.xml
    DEPENDS ${MIPSYNC_SCENE_TARGETS} system.cnf
)
