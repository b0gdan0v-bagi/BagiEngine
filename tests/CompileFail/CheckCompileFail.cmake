# Runs from ctest (cmake -P): builds the object target of each LogFormat.cpp case. Case 0 must build, the others must fail with a compiler
# error, so the check cannot pass for a reason other than the format string. Inputs: BUILD_DIR, CASE_TARGETS (the cases as a list of
# <target>:<expected>, expected is "ok" or "error"), optional CONFIG (a multi-config generator builds Debug without it). The error
# pattern covers clang/gcc ("error:") and MSVC ("error C2027:").
set(CONFIG_ARGS)
if(CONFIG)
    set(CONFIG_ARGS --config ${CONFIG})
endif()
foreach(CASE IN LISTS CASE_TARGETS)
    string(REPLACE ":" ";" PARTS "${CASE}")
    list(GET PARTS 0 TARGET_NAME)
    list(GET PARTS 1 EXPECTED)
    execute_process(COMMAND ${CMAKE_COMMAND} --build ${BUILD_DIR} --target ${TARGET_NAME} ${CONFIG_ARGS}
        RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE OUTPUT)
    if(EXPECTED STREQUAL "ok" AND NOT RESULT EQUAL 0)
        message(FATAL_ERROR "${TARGET_NAME} must compile but did not:\n${OUTPUT}")
    elseif(EXPECTED STREQUAL "error" AND (RESULT EQUAL 0 OR NOT OUTPUT MATCHES "error( C[0-9]+)?:"))
        message(FATAL_ERROR "${TARGET_NAME} must fail to compile with an error but did not (exit ${RESULT}):\n${OUTPUT}")
    endif()
endforeach()
