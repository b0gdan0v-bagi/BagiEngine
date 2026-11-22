# The body of the ctest entry <Exe>CookVerify (be_add_game_cook_test in BagiEngine.cmake):
#   cmake -DEXE=<game executable> -DOUT=<folder> -P BagiCookVerify.cmake
# Runs `<EXE> --cook=<OUT> --cook-verify` and fails when it does. A missing executable (the target was not built) prints the line the
# test's SKIP_REGULAR_EXPRESSION matches and succeeds.
cmake_minimum_required(VERSION 3.31)

if(NOT EXE OR NOT OUT)
    message(FATAL_ERROR "usage: cmake -DEXE=<executable> -DOUT=<folder> -P BagiCookVerify.cmake")
endif()

if(NOT EXISTS "${EXE}")
    message("BagiCookVerify: skipped, ${EXE} is not built")
    return()
endif()

execute_process(COMMAND "${EXE}" "--cook=${OUT}" --cook-verify RESULT_VARIABLE code)
if(NOT code EQUAL 0)
    message(FATAL_ERROR "BagiCookVerify: '${EXE} --cook=${OUT} --cook-verify' exited with ${code}")
endif()
