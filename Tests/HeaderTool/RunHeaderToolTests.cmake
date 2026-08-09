set(VALID_HEADER "${OUTPUT_DIR}/ValidReflectedType.generated.h")
set(VALID_SOURCE "${OUTPUT_DIR}/ValidReflectedType.gen.cpp")

execute_process(
    COMMAND "${PHT}"
        --input "${FIXTURE_DIR}/ValidReflectedType.h"
        --header-output "${VALID_HEADER}"
        --source-output "${VALID_SOURCE}"
        --include "ValidReflectedType.h"
        --file-id "ValidReflectedType_h"
    RESULT_VARIABLE VALID_RESULT
    OUTPUT_VARIABLE VALID_OUTPUT
    ERROR_VARIABLE VALID_ERROR
)
if(NOT VALID_RESULT EQUAL 0)
    message(FATAL_ERROR "valid fixture failed:\n${VALID_OUTPUT}\n${VALID_ERROR}")
endif()

file(READ "${VALID_HEADER}" GENERATED_HEADER)
file(READ "${VALID_SOURCE}" GENERATED_SOURCE)
foreach(EXPECTED
    "PICO_DECLARE_CLASS(PValidReflectedType, Pico::PObject)"
    "PICO_ADD_PROPERTY_METADATA(Properties, Score"
    "PICO_ADD_FUNCTION(Functions, GetScore"
    "EFunctionFlags::Pure"
    "EPropertyFlags::Replicated")
    string(FIND "${GENERATED_HEADER}${GENERATED_SOURCE}" "${EXPECTED}" FOUND_AT)
    if(FOUND_AT EQUAL -1)
        message(FATAL_ERROR "generated output is missing: ${EXPECTED}")
    endif()
endforeach()

file(TIMESTAMP "${VALID_SOURCE}" FIRST_TIMESTAMP "%s")
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
execute_process(
    COMMAND "${PHT}"
        --input "${FIXTURE_DIR}/ValidReflectedType.h"
        --header-output "${VALID_HEADER}"
        --source-output "${VALID_SOURCE}"
        --include "ValidReflectedType.h"
        --file-id "ValidReflectedType_h"
    RESULT_VARIABLE SECOND_RESULT
)
file(TIMESTAMP "${VALID_SOURCE}" SECOND_TIMESTAMP "%s")
if(NOT SECOND_RESULT EQUAL 0 OR NOT FIRST_TIMESTAMP STREQUAL SECOND_TIMESTAMP)
    message(FATAL_ERROR "unchanged generation rewrote its output")
endif()

execute_process(
    COMMAND "${PHT}"
        --input "${FIXTURE_DIR}/InvalidReflectedType.h"
        --header-output "${OUTPUT_DIR}/Invalid.generated.h"
        --source-output "${OUTPUT_DIR}/Invalid.gen.cpp"
        --include "InvalidReflectedType.h"
        --file-id "InvalidReflectedType_h"
    RESULT_VARIABLE INVALID_RESULT
    ERROR_VARIABLE INVALID_ERROR
)
if(INVALID_RESULT EQUAL 0)
    message(FATAL_ERROR "invalid fixture unexpectedly succeeded")
endif()
if(NOT INVALID_ERROR MATCHES "InvalidReflectedType.h\\([0-9]+,[0-9]+\\): error PHT1003")
    message(FATAL_ERROR "invalid fixture did not produce a file/line diagnostic:\n${INVALID_ERROR}")
endif()
