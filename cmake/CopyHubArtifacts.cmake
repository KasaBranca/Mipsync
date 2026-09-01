cmake_minimum_required(VERSION 3.20)

# Copies Hub artifacts next to the engine executable if present.
# This is intentionally best-effort: missing files should not fail the engine build.

if(NOT DEFINED SRC_DIR OR NOT DEFINED DST_DIR)
  message(FATAL_ERROR "CopyHubArtifacts.cmake requires -DSRC_DIR and -DDST_DIR")
endif()

file(MAKE_DIRECTORY "${DST_DIR}")

set(_candidates
  "${SRC_DIR}/hub-tauri/src-tauri/target/release/MipsyncHub.exe"
  "${SRC_DIR}/hub-tauri/src-tauri/target/release/MipsyncHubUpdater.exe"
)

foreach(_src IN LISTS _candidates)
  if(EXISTS "${_src}")
    get_filename_component(_name "${_src}" NAME)
    set(_dst "${DST_DIR}/${_name}")
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_src}" "${_dst}"
      RESULT_VARIABLE _rc
      OUTPUT_QUIET
      ERROR_QUIET
    )
    if(NOT _rc EQUAL 0)
      message(WARNING "Failed to copy ${_src} -> ${_dst} (rc=${_rc})")
    endif()
  endif()
endforeach()
