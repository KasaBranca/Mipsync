cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "DeployLatestInstalledEditor.cmake requires -DBUILD_DIR")
endif()

if(NOT DEFINED INSTALLS_ROOT)
    if(NOT DEFINED ENV{APPDATA} OR "$ENV{APPDATA}" STREQUAL "")
        message(WARNING "APPDATA is unavailable; skipping installed-editor deployment")
        return()
    endif()
    file(TO_CMAKE_PATH "$ENV{APPDATA}/MipsyncEngine/Installs" INSTALLS_ROOT)
endif()

if(NOT IS_DIRECTORY "${INSTALLS_ROOT}")
    message(STATUS "No Mipsync installs directory found; skipping installed-editor deployment")
    return()
endif()

file(GLOB _install_candidates LIST_DIRECTORIES true "${INSTALLS_ROOT}/v*")
set(_version_dirs)
foreach(_candidate IN LISTS _install_candidates)
    if(IS_DIRECTORY "${_candidate}")
        get_filename_component(_name "${_candidate}" NAME)
        if(_name MATCHES "^v[0-9]+(\\.[0-9]+)*$")
            list(APPEND _version_dirs "${_candidate}")
        endif()
    endif()
endforeach()

if(NOT _version_dirs)
    message(STATUS "No versioned Mipsync install found; skipping installed-editor deployment")
    return()
endif()

list(SORT _version_dirs COMPARE NATURAL ORDER DESCENDING)
list(GET _version_dirs 0 _latest_install)

# Template-only builds must reach the installed PS1 worker even when no native
# source changed and the editor's POST_BUILD command therefore does not run.
if(PS1_TEMPLATES_ONLY)
    set(_copy_templates_command copy_directory)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.26)
        set(_copy_templates_command copy_directory_if_different)
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E ${_copy_templates_command}
                "${BUILD_DIR}/templates/ps1" "${_latest_install}/templates/ps1"
        RESULT_VARIABLE _copy_templates_result)
    if(NOT _copy_templates_result EQUAL 0)
        message(FATAL_ERROR "Failed to deploy PS1 templates to ${_latest_install}")
    endif()
    message(STATUS "Deployed PS1 templates to latest install: ${_latest_install}")
    return()
endif()

set(_editor_exe "${BUILD_DIR}/MipsyncEngine.exe")
if(NOT EXISTS "${_editor_exe}")
    message(FATAL_ERROR "Built editor executable not found: ${_editor_exe}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_editor_exe}" "${_latest_install}/MipsyncEngine.exe"
    RESULT_VARIABLE _copy_exe_result
)
if(NOT _copy_exe_result EQUAL 0)
    message(FATAL_ERROR
        "Failed to deploy MipsyncEngine.exe to ${_latest_install}. Close the installed editor and rebuild.")
endif()

# Keep the CLI compiler and command descriptors in step with the editor.
if(EXISTS "${BUILD_DIR}/mipsync.exe")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${BUILD_DIR}/mipsync.exe" "${_latest_install}/mipsync.exe"
        RESULT_VARIABLE _copy_cli_result
    )
    if(NOT _copy_cli_result EQUAL 0)
        message(FATAL_ERROR "Failed to deploy mipsync.exe to ${_latest_install}")
    endif()
endif()

foreach(_runtime_dir IN ITEMS resources templates ps1_runtime tools skills)
    if(IS_DIRECTORY "${BUILD_DIR}/${_runtime_dir}")
        set(_copy_directory_command copy_directory)
        if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.26)
            set(_copy_directory_command copy_directory_if_different)
        endif()
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E ${_copy_directory_command}
                    "${BUILD_DIR}/${_runtime_dir}"
                    "${_latest_install}/${_runtime_dir}"
            RESULT_VARIABLE _copy_dir_result
        )
        if(NOT _copy_dir_result EQUAL 0)
            message(FATAL_ERROR
                "Failed to deploy ${_runtime_dir} to ${_latest_install}")
        endif()
    endif()
endforeach()

message(STATUS "Deployed editor build to latest install: ${_latest_install}")
