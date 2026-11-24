# Tests for src/document (Linux, g++ or clang++).
#
# On its own:
#   cmake -S tests/document -B build-document && cmake --build build-document && ctest --test-dir build-document
#
# Or from a parent project: add_subdirectory(tests/document).
cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(document-tests LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    enable_testing()
endif()

get_filename_component(DOCUMENT_REPO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if(NOT TARGET document)
    add_subdirectory("${DOCUMENT_REPO_DIR}/src/document" "${CMAKE_CURRENT_BINARY_DIR}/document")
endif()

set(QOL_FILTER_JSON "${DOCUMENT_REPO_DIR}/../../qol-filter/Sullys-QoL-Filter.json" CACHE FILEPATH
    "Sully's QoL Filter in its git checkout (older versions are read with git from there).")

add_executable(document_test "${DOCUMENT_REPO_DIR}/tests/document_test.cpp")
target_link_libraries(document_test PRIVATE document)
if(NOT MSVC)
    target_compile_options(document_test PRIVATE -Wall -Wextra)
endif()

add_test(NAME document_test COMMAND document_test "${QOL_FILTER_JSON}")
