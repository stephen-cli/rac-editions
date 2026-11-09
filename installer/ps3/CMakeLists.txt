# The PS3 builder's preview on a computer (src/platform_pc.c): the same screens and the same installer core in a
# window, for working on the screens. Needs SDL2, SDL2_ttf and SDL2_image (2.x) where CMake finds them, e.g.
#
#   cmake -S installer/ps3 -B build/ps3-preview -DCMAKE_PREFIX_PATH="<SDL2>;<SDL2_ttf>;<SDL2_image>"
#
# The PS3 build itself is the Makefile, in the toolchain container.
cmake_minimum_required(VERSION 3.20)
project(rac_ps3_preview C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_subdirectory(../core core)

find_package(SDL2 REQUIRED CONFIG)
find_package(SDL2_ttf REQUIRED CONFIG)
find_package(SDL2_image REQUIRED CONFIG)

add_executable(ps3-preview
    src/client.c
    src/draw_sdl.c
    src/main.c
    src/net_pc.c
    src/platform_pc.c
    src/screens.c
    src/script.c
    src/session.c
    src/ui.c
    src/update.c
    src/words.c
)
target_link_libraries(ps3-preview PRIVATE rac_core SDL2::SDL2 SDL2::SDL2main SDL2_ttf::SDL2_ttf SDL2_image::SDL2_image)
target_compile_definitions(ps3-preview PRIVATE RAC_PREVIEW)
strict_warnings(ps3-preview)

# data/ICON0.PNG, the XMB icon, drawn: ps3-icon data/fonts data/ICON0.PNG
add_executable(ps3-icon tools/icon.c)
target_link_libraries(ps3-icon PRIVATE SDL2::SDL2 SDL2::SDL2main SDL2_ttf::SDL2_ttf SDL2_image::SDL2_image)
target_compile_definitions(ps3-icon PRIVATE _USE_MATH_DEFINES)
strict_warnings(ps3-icon)
