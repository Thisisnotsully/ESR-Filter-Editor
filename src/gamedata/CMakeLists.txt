# The mod's item data (standard C++20 only; builds and is tested on Linux).
add_library(gamedata STATIC
    affixes.cpp
    gamedata.cpp
    groups.cpp
    item_types.cpp
    items.cpp
    stats.cpp
    strings.cpp
    table.cpp)
target_include_directories(gamedata PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(gamedata PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(gamedata PRIVATE /W4 /permissive- /utf-8 /EHsc)
else()
    target_compile_options(gamedata PRIVATE -Wall -Wextra)
endif()
