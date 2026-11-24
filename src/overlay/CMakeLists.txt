# The editor's in-game window (see README.md here).
#
#   overlay_core  the toolkit and the rules: no Windows headers, builds anywhere (the Linux tests use it)
#   overlay       the Win32 window host, GDI/GDI+ canvas, controller and input block (Windows only)
#
# The root CMakeLists.txt adds this folder and links `overlay` into the plugin DLL.

add_library(overlay_core STATIC
    color_text.cpp
    demo_view.cpp
    focus_nav.cpp
    form.cpp
    help.cpp
    osk.cpp
    pickers.cpp
    text_edit.cpp
    text_fold.cpp
    ui_text.cpp
    utf.cpp
    widgets.cpp)
target_include_directories(overlay_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(overlay_core PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(overlay_core PRIVATE /W4 /permissive- /utf-8 /EHsc)
else()
    target_compile_options(overlay_core PRIVATE -Wall -Wextra -Werror)
endif()

if(WIN32)
    add_library(overlay STATIC
        gamepad.cpp
        gdi_canvas.cpp
        input_block.cpp
        overlay_host.cpp)
    target_link_libraries(overlay PUBLIC overlay_core user32 gdi32 gdiplus msimg32 hid)
    if(MSVC)
        target_compile_options(overlay PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
endif()
