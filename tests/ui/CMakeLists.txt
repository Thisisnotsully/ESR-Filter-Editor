# Tests for src/ui against the real mod files and Sully's QoL Filter. Standalone:
#   cmake -S tests/ui -B build-ui -DESR_MOD_DATA=/path/to/data -DESR_FILTER=/path/to/Sullys-QoL-Filter.json
#   cmake --build build-ui && ctest --test-dir build-ui --output-on-failure
cmake_minimum_required(VERSION 3.20)
project(ui_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(ESR_MOD_DATA "/home/claude/ref/moddata" CACHE PATH "Folder with excel/ and strings/ of the mod")
set(ESR_FILTER "/home/claude/qol-filter/Sullys-QoL-Filter.json" CACHE FILEPATH "A filter to describe")

set(SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
foreach(part document gamedata edit overlay ui)
    if(NOT TARGET ${part})
        add_subdirectory("${SRC}/${part}" "${CMAKE_CURRENT_BINARY_DIR}/${part}")
    endif()
endforeach()

add_executable(ui_test ui_test.cpp)
target_link_libraries(ui_test PRIVATE ui)
target_compile_options(ui_test PRIVATE -Wall -Wextra)
target_compile_definitions(ui_test PRIVATE ESRFE_STARTER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../data/new-filter")
enable_testing()
add_test(NAME ui COMMAND ui_test "${ESR_MOD_DATA}/excel" "${ESR_MOD_DATA}/strings" "${ESR_FILTER}")
