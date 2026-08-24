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

mipsync_add_ps1_executable(mipsync
    main.c
    runtime/fixedp.c
    runtime/mipsync_gte.c
    runtime/host.c
    runtime/vm.c
    runtime/scene.c
    runtime/lookat.c
    runtime/render.c
    runtime/ui.c
    runtime/textures.c
    runtime/input.c
    runtime/physics.c
    runtime/audio.c
    generated/scripts_data.c
    generated/scene_data.c
    generated/mesh_data.c
    generated/textures_data.c
    generated/audio_data.c
)
target_include_directories(mipsync PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

psn00bsdk_add_cd_image(
    iso             # CMake target name
    mipsync         # Output basename (= mipsync.bin + mipsync.cue)
    iso.xml
    DEPENDS mipsync system.cnf
)
