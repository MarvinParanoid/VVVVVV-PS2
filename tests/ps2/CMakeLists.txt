# P1 validation programs. Standalone: they do not use any VVVVVV code or assets.
# PS2:  cmake -DCMAKE_TOOLCHAIN_FILE=$PS2DEV/share/ps2dev.cmake -DPS2_SDL_VARIANT=stock|opt ...
#       stock: SDL 2.32.10 from the ps2dev image (ps2sdk-ports), unmodified
#       opt:   SDL 2.32.10 + patches/sdl2 (scripts/build-sdl2-ps2.sh); pass SDL2_DIR
# Host: cmake -DCMAKE_PREFIX_PATH=<host SDL2 2.32.10 prefix> ...  (reference run)
cmake_minimum_required(VERSION 3.13)
project(vvvvvv_ps2_tests C)

set(PS2_SDL_VARIANT "stock" CACHE STRING "stock or opt")

find_package(SDL2 REQUIRED CONFIG)
message(STATUS "SDL2 from ${SDL2_DIR} (variant ${PS2_SDL_VARIANT})")

add_library(harness STATIC common/harness.c common/gsread.c)
target_compile_options(harness PRIVATE -Wall -Wextra)

set(TESTS sdl_smoke rt_soft rt_micro sw_exact rt_scene)
foreach(t ${TESTS})
    add_executable(${t} ${t}/main.c)
    target_compile_options(${t} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    if(PS2_SDL_VARIANT STREQUAL "opt")
        target_compile_definitions(${t} PRIVATE PS2_SDL_OPT=1)
    endif()
    target_link_libraries(${t} PRIVATE harness SDL2::SDL2main SDL2::SDL2-static m)
    if(PS2)
        # libdebug: ps2_screenshot() for GS framebuffer readback (common/gsread.c)
        target_link_libraries(${t} PRIVATE -ldebug)
        set_target_properties(${t} PROPERTIES OUTPUT_NAME "${t}_${PS2_SDL_VARIANT}" SUFFIX ".elf")
        set(out "${t}_${PS2_SDL_VARIANT}")
        target_link_options(${t} PRIVATE "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${out}.map")
        add_custom_command(TARGET ${t} POST_BUILD
            COMMAND mips64r5900el-ps2-elf-size -A $<TARGET_FILE:${t}> > ${out}.size.txt
            COMMAND mips64r5900el-ps2-elf-size $<TARGET_FILE:${t}> >> ${out}.size.txt
            VERBATIM)
    endif()
endforeach()

# P2b research benchmark: needs the VVVVVV PS2 build (PhysicsFS with the PS2
# platform) and FAudio's stb_vorbis from the patched VVVVVV source tree.
set(VVVVVV_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../build/vvvvvv-src")
set(VVVVVV_BUILD "${CMAKE_CURRENT_SOURCE_DIR}/../../build/vvvvvv-ps2")
if(PS2 AND EXISTS "${VVVVVV_BUILD}/libphysfs-static.a")
    add_executable(music_bench music_bench/main.c)
    target_compile_options(music_bench PRIVATE -O2 -Wall -Wno-unused-function -Wno-unused-variable)
    target_include_directories(music_bench PRIVATE "${VVVVVV_SRC}/third_party/physfs/src" "${VVVVVV_SRC}/third_party/FAudio/src")
    target_link_libraries(music_bench PRIVATE harness "${VVVVVV_BUILD}/libphysfs-static.a" atomic m)
    set_target_properties(music_bench PROPERTIES SUFFIX ".elf")
endif()

# G1 GS renderer feasibility proof of concept (docs/ps2-gs-renderer-
# feasibility.md): the same VVVVVV-like display lists through the software
# renderer (gs_poc_sw, writes references) and as raw GS packets (gs_poc_gs).
# Research only; nothing here is linked into the game.
if(PS2 AND PS2_SDL_VARIANT STREQUAL "opt")
    add_executable(gs_poc_sw gs_poc/sw_main.c gs_poc/scene.c)
    target_compile_options(gs_poc_sw PRIVATE -O2 -Wall -Wextra -Wno-unused-parameter)
    target_link_libraries(gs_poc_sw PRIVATE harness SDL2::SDL2main SDL2::SDL2-static m)
    set_target_properties(gs_poc_sw PROPERTIES SUFFIX ".elf")
    add_executable(gs_poc_gs gs_poc/gs_main.c gs_poc/scene.c)
    target_compile_options(gs_poc_gs PRIVATE -O2 -Wall -Wextra -Wno-unused-parameter)
    target_link_libraries(gs_poc_gs PRIVATE harness -lps2_drivers -lpatches -ldebug -lgraph -ldma m)
    set_target_properties(gs_poc_gs PROPERTIES SUFFIX ".elf")
endif()

# P3 first-boot check for real hardware (USB mass storage via wLaunchELF).
# No SDL: same IOP bring-up as SDL's PS2 main(), output via libdebug.
if(PS2)
    add_executable(fs_smoke fs_smoke/main.c)
    target_compile_options(fs_smoke PRIVATE -O2 -Wall -Wextra)
    target_link_libraries(fs_smoke PRIVATE -lps2_drivers -lpatches -ldebug)
    set_target_properties(fs_smoke PROPERTIES SUFFIX ".elf")
endif()
