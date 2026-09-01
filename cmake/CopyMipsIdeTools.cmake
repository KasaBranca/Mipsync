if(NOT DEFINED SRC_DIR OR NOT DEFINED DST_DIR)
    message(FATAL_ERROR "CopyMipsIdeTools.cmake requires SRC_DIR and DST_DIR")
endif()

set(TOOLS_DST "${DST_DIR}/tools")
file(MAKE_DIRECTORY "${TOOLS_DST}")

set(VSCODE_MIPS_SRC "${SRC_DIR}/tools/vscode-mips")
set(VSCODE_MIPS_DST "${TOOLS_DST}/vscode-mips")
if(IS_DIRECTORY "${VSCODE_MIPS_SRC}")
    file(REMOVE_RECURSE "${VSCODE_MIPS_DST}")
    file(COPY "${VSCODE_MIPS_SRC}/"
        DESTINATION "${VSCODE_MIPS_DST}"
        PATTERN "node_modules" EXCLUDE
        PATTERN "package-lock.json" EXCLUDE
        PATTERN "*.vsix" EXCLUDE
        PATTERN "npm-debug.log" EXCLUDE)
    if(WIN32 AND EXISTS "${SRC_DIR}/scripts/package_mips_vscode_extension.ps1")
        find_program(MIPSYNC_POWERSHELL_EXECUTABLE NAMES pwsh powershell)
        if(MIPSYNC_POWERSHELL_EXECUTABLE)
            execute_process(
                COMMAND "${MIPSYNC_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
                    -File "${SRC_DIR}/scripts/package_mips_vscode_extension.ps1"
                    -RepoRoot "${SRC_DIR}"
                    -OutputDirectory "${SRC_DIR}/build/vsix"
                RESULT_VARIABLE MIPS_VSIX_RESULT
                OUTPUT_QUIET)
            if(NOT MIPS_VSIX_RESULT EQUAL 0)
                message(WARNING "Failed to package the Mips# VS Code extension")
            endif()
        endif()
    endif()
    file(GLOB MIPS_VSIX_FILES "${SRC_DIR}/build/vsix/mipsync-mipssharp-vscode-*.vsix")
    if(MIPS_VSIX_FILES)
        file(COPY ${MIPS_VSIX_FILES} DESTINATION "${VSCODE_MIPS_DST}")
    endif()
endif()

set(MIPS_LSP_SRC "${SRC_DIR}/tools/mips-language-server")
set(MIPS_LSP_DST "${TOOLS_DST}/mips-language-server")
if(IS_DIRECTORY "${MIPS_LSP_SRC}")
    file(REMOVE_RECURSE "${MIPS_LSP_DST}")
    file(COPY "${MIPS_LSP_SRC}/" DESTINATION "${MIPS_LSP_DST}")
endif()
