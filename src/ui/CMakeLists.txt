# The editor's views and the logic under them (rule descriptions). No Windows here: builds and is tested on Linux.
add_library(ui STATIC
    add_rule_view.cpp
    browse_view.cpp
    rule_draft.cpp
    rule_text.cpp
    rule_view.cpp)
target_include_directories(ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ui PUBLIC overlay_core document gamedata edit)
target_compile_features(ui PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(ui PRIVATE /W4 /permissive- /utf-8 /EHsc)
else()
    target_compile_options(ui PRIVATE -Wall -Wextra -Werror)
endif()
