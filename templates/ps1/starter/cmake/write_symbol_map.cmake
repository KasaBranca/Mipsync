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
