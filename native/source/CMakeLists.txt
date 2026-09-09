cmake_minimum_required(VERSION 3.20)
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "Native game architecture")
project(melee-source LANGUAGES C OBJC)

if(NOT APPLE OR NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    message(FATAL_ERROR "This build requires macOS ARM64")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This build requires eight-byte host pointers")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(MELEE_SANITIZERS "Check memory access and undefined behavior" ON)
option(MELEE_BUILD_NATIVE_GAME
    "Build the complete direct-source ARM64 game executable (experimental)" OFF)
get_filename_component(MELEE_ROOT "../.." ABSOLUTE)

add_library(melee_host_options INTERFACE)
target_compile_definitions(melee_host_options INTERFACE MELEE_NATIVE TARGET_PC)
target_include_directories(melee_host_options INTERFACE
    "${MELEE_ROOT}/src" "${MELEE_ROOT}/extern/dolphin/include" "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(melee_host_options INTERFACE
    -include math.h -fno-strict-aliasing -ffp-contract=off -fno-common
    -Werror=pointer-to-int-cast -Werror=int-to-pointer-cast
    -Werror=implicit-function-declaration -Werror=int-conversion)
if(MELEE_SANITIZERS)
    target_compile_options(melee_host_options INTERFACE -fsanitize=address,undefined -g)
    target_link_options(melee_host_options INTERFACE -fsanitize=address,undefined)
endif()

enable_testing()
add_executable(test_heap tests/test_heap.c platform/heap.c platform/report.c
    "${MELEE_ROOT}/src/sysdolphin/baselib/random.c")
target_link_libraries(test_heap PRIVATE melee_host_options)
add_test(NAME heap COMMAND test_heap)

add_executable(test_memory tests/test_memory.c)
foreach(module class objalloc id list object hash)
    target_sources(test_memory PRIVATE "${MELEE_ROOT}/src/sysdolphin/baselib/${module}.c")
endforeach()
target_link_libraries(test_memory PRIVATE melee_host_options)
add_test(NAME memory COMMAND test_memory)

add_executable(test_gobjplink tests/test_gobjplink.c)
target_link_libraries(test_gobjplink PRIVATE melee_host_options)
add_test(NAME gobjplink COMMAND test_gobjplink)

add_executable(test_particle_tag tests/test_particle_tag.c platform/report.c)
target_link_libraries(test_particle_tag PRIVATE melee_host_options)
add_test(NAME particle_tag COMMAND test_particle_tag)

add_executable(test_math tests/test_math.c platform/math.c)
target_link_libraries(test_math PRIVATE melee_host_options)
add_test(NAME math COMMAND test_math)

add_executable(test_scene_sort tests/test_scene_sort.c)
target_link_libraries(test_scene_sort PRIVATE melee_host_options)
target_link_options(test_scene_sort PRIVATE -Wl,-dead_strip)
add_test(NAME scene_sort COMMAND test_scene_sort)

add_executable(test_scene_bytecode tests/test_scene_bytecode.c
    "${MELEE_ROOT}/src/sysdolphin/baselib/bytecode.c")
target_link_libraries(test_scene_bytecode PRIVATE melee_host_options)
add_test(NAME scene_bytecode COMMAND test_scene_bytecode)

add_executable(test_fighter_dynamic tests/test_fighter_dynamic.c)
target_link_libraries(test_fighter_dynamic PRIVATE melee_host_options)
add_test(NAME fighter_dynamic COMMAND test_fighter_dynamic)

add_executable(test_commands tests/test_commands.c
    "${MELEE_ROOT}/src/melee/lb/lbcommand.c"
    "${MELEE_ROOT}/src/melee/it/itanimlist.c"
    "${MELEE_ROOT}/src/melee/lb/lb_013B.c"
    "${MELEE_ROOT}/src/melee/gr/grmaterial.c")
target_link_libraries(test_commands PRIVATE melee_host_options)
target_link_options(test_commands PRIVATE -Wl,-dead_strip)
add_test(NAME commands COMMAND test_commands)

add_executable(test_command_fighter tests/test_command_fighter.c)
target_link_libraries(test_command_fighter PRIVATE melee_host_options)
target_link_options(test_command_fighter PRIVATE -Wl,-dead_strip)
add_test(NAME command_fighter COMMAND test_command_fighter)

add_executable(test_fighter_attributes tests/test_fighter_attributes.c
    assets/fighter_attributes.c assets/archive.c)
target_link_libraries(test_fighter_attributes PRIVATE melee_host_options)
add_test(NAME fighter_attributes COMMAND test_fighter_attributes)

add_executable(test_fighter_common tests/test_fighter_common.c
    assets/fighter_common.c assets/archive.c assets/descriptors.c
    platform/report.c)
target_link_libraries(test_fighter_common PRIVATE melee_host_options)
add_test(NAME fighter_common COMMAND test_fighter_common)

add_executable(test_fighter_parts tests/test_fighter_parts.c
    assets/fighter_parts.c assets/archive.c assets/descriptors.c
    platform/report.c)
target_link_libraries(test_fighter_parts PRIVATE melee_host_options)
add_test(NAME fighter_parts COMMAND test_fighter_parts)

add_executable(test_fighter_articles tests/test_fighter_articles.c
    assets/fighter_articles.c assets/fighter_parts.c assets/items.c
    assets/items_special.c assets/archive.c assets/descriptors.c
    platform/report.c)
target_link_libraries(test_fighter_articles PRIVATE melee_host_options)
add_test(NAME fighter_articles COMMAND test_fighter_articles)

add_executable(test_fighter_archive tests/test_fighter_archive.c
    assets/fighters.c assets/fighter_attributes.c assets/fighter_common.c
    assets/fighter_parts.c assets/fighter_articles.c assets/items.c
    assets/items_special.c assets/archive.c assets/descriptors.c
    platform/report.c)
target_link_libraries(test_fighter_archive PRIVATE melee_host_options)
add_test(NAME fighter_archive COMMAND test_fighter_archive)

add_executable(test_fighter_motions tests/test_fighter_motions.c
    assets/archive.c assets/descriptors.c platform/report.c)
target_link_libraries(test_fighter_motions PRIVATE melee_host_options)
target_link_options(test_fighter_motions PRIVATE -Wl,-dead_strip)
add_test(NAME fighter_motions COMMAND test_fighter_motions)

add_executable(test_item_spawn tests/test_item_spawn.c
    "${MELEE_ROOT}/src/melee/it/itspawn.c")
target_link_libraries(test_item_spawn PRIVATE melee_host_options)
target_link_options(test_item_spawn PRIVATE -Wl,-dead_strip)
add_test(NAME item_spawn COMMAND test_item_spawn)

add_executable(test_effects tests/test_effects.c)
target_sources(test_effects PRIVATE "${MELEE_ROOT}/src/melee/ef/eflib.c")
target_link_libraries(test_effects PRIVATE melee_host_options)
target_link_options(test_effects PRIVATE -Wl,-dead_strip)
add_test(NAME effects COMMAND test_effects)

add_executable(test_archive tests/test_archive.c assets/archive.c assets/descriptors.c platform/report.c)
target_link_libraries(test_archive PRIVATE melee_host_options)
add_test(NAME archive COMMAND test_archive)

add_executable(test_effect_archive tests/test_effect_archive.c assets/effects.c
    assets/archive.c assets/descriptors.c platform/report.c)
target_link_libraries(test_effect_archive PRIVATE melee_host_options)
add_test(NAME effect_archive COMMAND test_effect_archive)

add_executable(test_items tests/test_items.c assets/items.c assets/items_special.c
    assets/archive.c assets/descriptors.c platform/report.c)
target_link_libraries(test_items PRIVATE melee_host_options)
add_test(NAME items COMMAND test_items)

add_executable(test_stage tests/test_stage.c assets/stage.c assets/archive.c
    assets/descriptors.c platform/report.c)
target_link_libraries(test_stage PRIVATE melee_host_options)
add_test(NAME stage COMMAND test_stage)

add_executable(test_archive_legacy tests/test_archive_legacy.c
    "${MELEE_ROOT}/src/sysdolphin/baselib/archive.c" platform/report.c)
target_link_libraries(test_archive_legacy PRIVATE melee_host_options)
add_test(NAME archive_legacy COMMAND test_archive_legacy)

add_executable(test_scheduler tests/test_scheduler.c platform/os.c
    platform/scheduler.c platform/vi.c platform/heap.c platform/report.c
    platform/card.c)
target_sources(test_scheduler PRIVATE platform/pad.c)
target_link_libraries(test_scheduler PRIVATE melee_host_options)
add_test(NAME scheduler COMMAND test_scheduler "${CMAKE_BINARY_DIR}")

add_executable(test_pad tests/test_pad.c platform/pad.c)
target_link_libraries(test_pad PRIVATE melee_host_options)
add_test(NAME pad COMMAND test_pad)

add_executable(test_audio tests/test_audio.c platform/audio.c)
target_link_libraries(test_audio PRIVATE melee_host_options)
add_test(NAME audio COMMAND test_audio)

add_executable(test_card tests/test_card.c platform/card.c)
target_link_libraries(test_card PRIVATE melee_host_options)
add_test(NAME card COMMAND test_card "${CMAKE_BINARY_DIR}")

add_executable(test_hsd_card tests/test_hsd_card.c platform/card.c
    "${MELEE_ROOT}/src/melee/lb/lbcardnew.c")
foreach(module hsd_3A94 hsd_3B27 hsd_3B2B hsd_3B2E hsd_4D11)
    target_sources(test_hsd_card PRIVATE
        "${MELEE_ROOT}/src/sysdolphin/baselib/${module}.c")
endforeach()
target_link_libraries(test_hsd_card PRIVATE melee_host_options)
add_test(NAME hsd_card COMMAND test_hsd_card)

add_executable(test_synth_stream tests/test_synth_stream.c platform/audio.c
    platform/report.c)
target_link_libraries(test_synth_stream PRIVATE melee_host_options)
target_link_options(test_synth_stream PRIVATE -Wl,-dead_strip)
add_test(NAME synth_stream_loop COMMAND test_synth_stream loop)
add_test(NAME synth_stream_finite COMMAND test_synth_stream finite)

add_executable(test_gx tests/test_gx.c platform/gx.c)
target_link_libraries(test_gx PRIVATE melee_host_options)
add_test(NAME gx COMMAND test_gx)

# The focused tests above are the default build. The complete game target is
# opt-in because it is still missing host services needed for a playable
# window, controller input, audio, and save data. When enabled, CMake compiles
# every game, engine, and native support source into one direct ARM64 binary.
if(MELEE_BUILD_NATIVE_GAME)
    file(GLOB_RECURSE MELEE_NATIVE_GAME_SOURCES CONFIGURE_DEPENDS
        "${MELEE_ROOT}/src/melee/*.c"
        "${MELEE_ROOT}/src/sysdolphin/*.c")
    list(APPEND MELEE_NATIVE_GAME_SOURCES
        "${MELEE_ROOT}/src/MSL/float.c")
    file(GLOB MELEE_NATIVE_SUPPORT_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/*.c")
    list(APPEND MELEE_NATIVE_SUPPORT_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/display.m")

    add_executable(native_melee EXCLUDE_FROM_ALL
        native_main.c
        ${MELEE_NATIVE_GAME_SOURCES}
        ${MELEE_NATIVE_SUPPORT_SOURCES})
    target_link_libraries(native_melee PRIVATE melee_host_options)
    target_compile_definitions(native_melee PRIVATE MELEE_NATIVE_DISPLAY)
    target_link_libraries(native_melee PRIVATE
        "-framework Cocoa"
        "-framework QuartzCore"
        "-framework AudioToolbox")
    target_sources(native_melee PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/audio_output.m")
    set_target_properties(native_melee PROPERTIES OUTPUT_NAME melee-native)
endif()
