# The filter file as an editable document. Standard C++20 only.
add_library(document STATIC
    json.cpp
    syntax.cpp
    document.cpp)

target_include_directories(document PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(document PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(document PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(document PRIVATE -Wall -Wextra)
endif()
