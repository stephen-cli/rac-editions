# The installer core: a C11 library that makes an edition's image from a release payload and the player's discs,
# byte for byte as the edition's tools/install.py does, and rac-installer, its command line and the builder's
# sidecar (--serve). Endian-neutral, so the same code runs on PCs and on the PS3.
cmake_minimum_required(VERSION 3.20)
project(rac_installer C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Warnings are errors, on both compilers, so an endian or narrowing slip shows wherever it is built.
function(strict_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /utf-8)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
        # fseeko and ftello with 64-bit offsets on 32-bit systems
        target_compile_definitions(${target} PRIVATE _FILE_OFFSET_BITS=64 _POSIX_C_SOURCE=200809L)
    endif()
endfunction()

add_library(rac_core STATIC
    src/build.c
    src/disc.c
    src/elf.c
    src/files.c
    src/fill.c
    src/hash_cache.c
    src/installer.c
    src/iso.c
    src/job.c
    src/serve.c
    src/sha256.c
)
target_include_directories(rac_core PUBLIC src)
strict_warnings(rac_core)

# JSON on its own, for the libraries beside the core that read it (moby_mesh.c)
add_library(rac_json STATIC src/json.c)
target_include_directories(rac_json PUBLIC src)
strict_warnings(rac_json)
target_link_libraries(rac_core PUBLIC rac_json)

# Threads, locks and the job pool, on their own so the tools below can run on the pool without the whole core.
# Win32 threads on Windows; pthreads (-pthread) elsewhere.
find_package(Threads REQUIRED)
add_library(rac_threads STATIC src/pool.c src/thread.c)
target_include_directories(rac_threads PUBLIC src)
target_link_libraries(rac_threads PUBLIC Threads::Threads)
strict_warnings(rac_threads)

target_link_libraries(rac_core PUBLIC rac_threads)
if(NOT MSVC)
    target_link_libraries(rac_core PUBLIC m)
endif()

add_executable(rac-installer src/main.c)
target_link_libraries(rac-installer PRIVATE rac_core)
strict_warnings(rac-installer)

enable_testing()
add_executable(core_tests tests/core_tests.c)
target_link_libraries(core_tests PRIVATE rac_core)
strict_warnings(core_tests)
add_test(NAME core_tests COMMAND core_tests)
# Runs one piece of the core on real inputs, to check it against the same step of tools/install.py.
add_executable(probe tests/probe.c)
target_link_libraries(probe PRIVATE rac_core)
strict_warnings(probe)

# The game's WAD compression (wad.c, wad.h) and the HelpDesk audio maths (vag.c, vag.h), with rac-wad and rac-vag,
# their command lines, which the Python tools call until they come over to C.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/wad.c)
    add_library(wad STATIC wad.c)
    target_include_directories(wad PUBLIC .)
    strict_warnings(wad)
    target_link_libraries(rac_core PUBLIC wad)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/rac-wad.c)
        add_executable(rac-wad tools/rac-wad.c)
        target_link_libraries(rac-wad PRIVATE wad rac_threads)
        strict_warnings(rac-wad)
    endif()
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/moby.c)
    # moby classes read and written in either game's layout (wrench's mobyconvert, ported)
    add_library(moby STATIC moby.c)
    target_include_directories(moby PUBLIC .)
    strict_warnings(moby)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/moby_mesh.c)
        # wrench's mesh export and packet builder, its float maths reproduced bit for bit: no fused multiply-adds
        target_sources(moby PRIVATE moby_mesh.c)
        target_link_libraries(moby PRIVATE rac_json)
        if(NOT MSVC)
            set_source_files_properties(moby_mesh.c PROPERTIES COMPILE_OPTIONS -ffp-contract=off)
        endif()
    endif()
    if(NOT MSVC)
        target_link_libraries(moby PUBLIC m)
    endif()
    target_link_libraries(rac_core PUBLIC moby)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/rac-moby.c)
        add_executable(rac-moby tools/rac-moby.c)
        target_link_libraries(rac-moby PRIVATE moby)
        strict_warnings(rac-moby)
    endif()
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/vag.c)
    add_library(vag STATIC vag.c)
    target_include_directories(vag PUBLIC .)
    strict_warnings(vag)
    if(NOT MSVC)
        target_link_libraries(vag PUBLIC m)
    endif()
    target_link_libraries(rac_core PUBLIC vag)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/helpdesk.c)
        # the HelpDesk lines' patch step, which makes them on the job pool
        target_sources(rac_core PRIVATE helpdesk.c)
    endif()
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/levels.c)
        # the levels' patch step: one pass over the 19 levels' data, with Kalebo III's sign made from the PAL disc
        # (its float maths reproduced bit for bit, as moby_mesh.c's: no fused multiply-adds)
        target_sources(rac_core PRIVATE levels.c sign.c)
        if(NOT MSVC)
            set_source_files_properties(sign.c PROPERTIES COMPILE_OPTIONS -ffp-contract=off)
        endif()
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/rac-levels.c)
            add_executable(rac-levels tools/rac-levels.c)
            target_link_libraries(rac-levels PRIVATE rac_core)
            strict_warnings(rac-levels)
        endif()
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/rac-sign.c)
            add_executable(rac-sign tools/rac-sign.c)
            target_link_libraries(rac-sign PRIVATE rac_core)
            strict_warnings(rac-sign)
        endif()
    endif()
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/rac-vag.c)
        add_executable(rac-vag tools/rac-vag.c)
        target_link_libraries(rac-vag PRIVATE vag rac_threads)
        strict_warnings(rac-vag)
    endif()
endif()
