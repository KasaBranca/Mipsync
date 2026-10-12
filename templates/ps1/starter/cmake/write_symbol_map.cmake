if(NOT DEFINED NM OR NM STREQUAL "")
    message(FATAL_ERROR "NM executable was not provided")
endif()
if(NOT DEFINED ELF OR ELF STREQUAL "")
    message(FATAL_ERROR "ELF input was not provided")
endif()
if(NOT DEFINED MAP OR MAP STREQUAL "")
    message(FATAL_ERROR "MAP output was not provided")
endif()

execute_process(
    COMMAND "${NM}" -f posix -l -n "${ELF}"
    OUTPUT_FILE "${MAP}"
    ERROR_VARIABLE nm_error
    RESULT_VARIABLE nm_result
)

if(NOT nm_result EQUAL 0)
    message(FATAL_ERROR "Failed to generate symbol map: ${nm_error}")
endif()

# A PS-EXE is loaded at 0x80010000 and the hardware has only 2 MiB of main
# RAM. elf2x can still emit an executable whose .bss extends beyond RAM; such
# an image appears to build successfully and then boots to a black screen.
# Reject it here and reserve the final 64 KiB for the runtime stack.
file(READ "${MAP}" symbol_map)
string(REGEX MATCH "(^|\n)_end[ \t]+[^ \t]+[ \t]+([0-9A-Fa-f]+)" end_match "${symbol_map}")
if(end_match STREQUAL "")
    message(FATAL_ERROR "Cannot verify PS1 RAM budget: _end is missing from ${MAP}")
endif()
set(end_hex "${CMAKE_MATCH_2}")
string(LENGTH "${end_hex}" end_hex_length)
if(end_hex_length LESS 8)
    message(FATAL_ERROR "Cannot verify PS1 RAM budget: invalid _end address ${end_hex}")
endif()
math(EXPR end_hex_offset "${end_hex_length} - 8")
# nm may print a sign-extended 64-bit address for the 32-bit MIPS target.
string(SUBSTRING "${end_hex}" ${end_hex_offset} 8 end_hex)
math(EXPR end_address "0x${end_hex}")
math(EXPR ram_safe_end "0x801f0000")
if(end_address GREATER ram_safe_end)
    math(EXPR overflow_bytes "${end_address} - ${ram_safe_end}")
    message(FATAL_ERROR
        "PS1 RAM budget exceeded by ${overflow_bytes} bytes (_end=0x${end_hex}, "
        "safe limit=0x801f0000). Reduce baked animation frames, mesh data, "
        "textures, scripts, or scene complexity.")
endif()
