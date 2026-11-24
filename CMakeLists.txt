# ESR Filter Editor, a D2RLoader plugin.
#
#   cmake -S . -B build -G "Visual Studio 18 2026" -A x64
#   cmake --build build --config Release
#
# Copy the DLL into your mod while building:
#   cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DD2R_MOD_DIR="F:/Diablo II Resurrected/mods/<YourMod>"
#
cmake_minimum_required(VERSION 3.29)
project(d2rl-loot-filter-editor LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(D2RL_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../d2rloader_plugins/third_party/PluginSDK" CACHE PATH
    "The D2RLoader PluginSDK folder (the one with include/D2RLPlugin).")
set(LOOT_FILTER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../d2rloader_plugins/plugins/loot-filter" CACHE PATH
    "The loot filter source folder (the one with editor_bridge_api.h).")
set(D2R_MOD_DIR "" CACHE PATH
    "Mod folder to copy the DLL into. Leave empty to skip deployment.")

if(NOT EXISTS "${D2RL_SDK_DIR}/include/D2RLPlugin/api.h")
    message(FATAL_ERROR "D2RL_SDK_DIR is wrong: ${D2RL_SDK_DIR}/include/D2RLPlugin/api.h not found.")
endif()
if(NOT EXISTS "${LOOT_FILTER_SRC_DIR}/editor_bridge_api.h")
    message(FATAL_ERROR "LOOT_FILTER_SRC_DIR is wrong: ${LOOT_FILTER_SRC_DIR}/editor_bridge_api.h not found.")
endif()

# The SDK, as the loot filter repo does it: no SDK tests, and the SDK samples
# stay out of the default build.
set(BUILD_TESTING OFF)
set(D2RLPLUGIN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${D2RL_SDK_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/d2rl-sdk")

# The editor's parts (see docs/ARCHITECTURE.md). document and gamedata are plain C++;
# overlay's window host is Windows only.
add_subdirectory(src/document)
add_subdirectory(src/gamedata)
add_subdirectory(src/overlay)

set(target d2rl-sully-loot-filter-editor)
add_library(${target} SHARED
    src/plugin.cpp
    src/plugin.rc)

target_include_directories(${target} PRIVATE "${LOOT_FILTER_SRC_DIR}")
target_link_libraries(${target} PRIVATE D2RLPlugin::D2RLPlugin overlay document gamedata)

set_target_properties(${target} PROPERTIES
    PREFIX ""
    OUTPUT_NAME "${target}")

if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()

if(D2R_MOD_DIR)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "${D2R_MOD_DIR}/d2rloader/plugins"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:${target}>"
                "${D2R_MOD_DIR}/d2rloader/plugins/"
        COMMENT "Deploying ${target} to ${D2R_MOD_DIR}")
endif()
