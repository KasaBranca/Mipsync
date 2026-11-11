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
set(_formal_version_dirs)
foreach(_candidate IN LISTS _install_candidates)
    if(IS_DIRECTORY "${_candidate}")
        get_filename_component(_name "${_candidate}" NAME)
        if(_name MATCHES "^v([0-9]+)\\.[0-9]+\\.[0-9]+([abf][1-9][0-9]*)?$")
            if(CMAKE_MATCH_1 GREATER 0 AND CMAKE_MATCH_1 LESS 2000)
                list(APPEND _formal_version_dirs "${_candidate}")
            else()
                list(APPEND _version_dirs "${_candidate}")
            endif()
        endif()
    endif()
endforeach()
if(_formal_version_dirs)
    set(_version_dirs ${_formal_version_dirs})
endif()

if(NOT _version_dirs)
    message(STATUS "No versioned Mipsync install found; skipping installed-editor deployment")
    return()
endif()

list(SORT _version_dirs COMPARE NATURAL ORDER DESCENDING)
list(GET _version_dirs 0 _latest_install)

if(SKILLS_ONLY)
    include("${CMAKE_CURRENT_LIST_DIR}/DeployLatestInstalledSkills.cmake")
    return()
endif()

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

function(_deploy_verified_executable source destination)
    file(SHA256 "${source}" _source_hash)
    if(EXISTS "${destination}")
        file(SHA256 "${destination}" _destination_hash)
        if(_source_hash STREQUAL _destination_hash)
            return()
        endif()
    endif()
    # Stage before replacing anything. Windows can rename a running executable;
    # keep it alive under a backup name so unsaved editor sessions need not close.
    string(TIMESTAMP _stamp "%Y%m%d-%H%M%S" UTC)
    string(RANDOM LENGTH 8 ALPHABET 0123456789abcdef _suffix)
    set(_staged "${destination}.update-${_stamp}-${_suffix}")
    set(_backup "${destination}.previous-${_stamp}-${_suffix}")
    file(COPY_FILE "${source}" "${_staged}" RESULT _copy)
    if(NOT _copy STREQUAL "0")
        message(FATAL_ERROR "Could not stage ${destination}: ${_copy}")
    endif()
    file(SHA256 "${_staged}" _staged_hash)
    if(NOT _source_hash STREQUAL _staged_hash)
        message(FATAL_ERROR "Staged executable hash mismatch: ${destination}")
    endif()
    set(_had_previous FALSE)
    if(EXISTS "${destination}")
        file(RENAME "${destination}" "${_backup}" RESULT _rename NO_REPLACE)
        if(NOT _rename STREQUAL "0")
            message(FATAL_ERROR "Could not preserve installed executable ${destination}: ${_rename}")
        endif()
        set(_had_previous TRUE)
    endif()
    file(RENAME "${_staged}" "${destination}" RESULT _rename NO_REPLACE)
    if(NOT _rename STREQUAL "0")
        if(_had_previous)
            file(RENAME "${_backup}" "${destination}" RESULT _rollback NO_REPLACE)
        endif()
        message(FATAL_ERROR "Executable deployment failed: ${_rename}; rollback: ${_rollback}")
    endif()
    file(SHA256 "${destination}" _installed_hash)
    if(NOT _source_hash STREQUAL _installed_hash)
        message(FATAL_ERROR "Installed executable hash mismatch: ${destination}")
    endif()
    if(_had_previous)
        message(STATUS "Previous executable preserved: ${_backup}")
    endif()
endfunction()

_deploy_verified_executable("${_editor_exe}" "${_latest_install}/MipsyncEngine.exe")

# Keep the CLI compiler and command descriptors in step with the editor.
if(EXISTS "${BUILD_DIR}/mipsync.exe")
    _deploy_verified_executable("${BUILD_DIR}/mipsync.exe" "${_latest_install}/mipsync.exe")
endif()

foreach(_runtime_dir IN ITEMS resources templates ps1_runtime tools)
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

include("${CMAKE_CURRENT_LIST_DIR}/DeployLatestInstalledSkills.cmake")

message(STATUS "Deployed editor build to latest install: ${_latest_install}")
