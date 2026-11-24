# Tests for src/gamedata against the real mod files. Standalone:
#   cmake -S tests/gamedata -B build-gamedata -DESR_MOD_DATA=/path/to/data && cmake --build build-gamedata && ctest --test-dir build-gamedata
# ESR_MOD_DATA holds excel/*.txt and strings/*.json (copied from
# <mod>.mpq/data/global/excel and data/local/lng/strings).
cmake_minimum_required(VERSION 3.20)
project(gamedata_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(ESR_MOD_DATA "/home/claude/ref/moddata" CACHE PATH "Folder with excel/ and strings/ of the mod")

if(NOT TARGET gamedata)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../src/gamedata" "${CMAKE_CURRENT_BINARY_DIR}/gamedata")
endif()

add_executable(gamedata_test "${CMAKE_CURRENT_SOURCE_DIR}/../gamedata_test.cpp")
target_link_libraries(gamedata_test PRIVATE gamedata)
if(NOT MSVC)
    target_compile_options(gamedata_test PRIVATE -Wall -Wextra)
endif()

enable_testing()
add_test(NAME gamedata COMMAND gamedata_test "${ESR_MOD_DATA}/excel" "${ESR_MOD_DATA}/strings")
