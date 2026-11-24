# A player's changes to a filter, the merge into the copy the game loads, and the game's selection (standard C++).
add_library(edit STATIC
    changes.cpp
    session.cpp
    new_filter.cpp)
target_include_directories(edit PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(edit PUBLIC document)
target_compile_features(edit PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(edit PRIVATE /W4 /permissive- /utf-8 /EHsc)
else()
    target_compile_options(edit PRIVATE -Wall -Wextra -Werror)
endif()
