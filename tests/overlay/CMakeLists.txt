# Linux-buildable tests of the overlay's pure logic (no Windows headers):
#
#   cmake -S tests/overlay -B build-overlay-tests && cmake --build build-overlay-tests && ctest --test-dir build-overlay-tests
#
# Also works as a subdirectory of the root project (add_subdirectory(tests/overlay)).
cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(overlay_tests LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    enable_testing()
endif()

set(OVERLAY_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../src/overlay")
if(NOT TARGET overlay_core)
    add_subdirectory("${OVERLAY_SRC_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/overlay")
endif()

add_executable(pad_logic_test pad_logic_test.cpp)
target_include_directories(pad_logic_test PRIVATE "${OVERLAY_SRC_DIR}")
add_executable(overlay_logic_test overlay_logic_test.cpp)
target_link_libraries(overlay_logic_test PRIVATE overlay_core)

foreach(test pad_logic_test overlay_logic_test)
    target_compile_features(${test} PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(${test} PRIVATE /W4 /permissive- /utf-8 /EHsc)
    else()
        target_compile_options(${test} PRIVATE -Wall -Wextra -Werror)
    endif()
    add_test(NAME ${test} COMMAND ${test})
endforeach()
