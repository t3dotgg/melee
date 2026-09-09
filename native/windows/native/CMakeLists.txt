cmake_minimum_required(VERSION 3.24)
project(melee_native_windows LANGUAGES CXX)

if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The native Windows shell requires a 64-bit toolchain")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(melee_native_core STATIC
    src/native_game.cpp
    src/native_game_memory.cpp
    src/native_archive.cpp
    src/native_timing.cpp
    src/native_input.cpp
    src/native_render.cpp
    src/native_filesystem.cpp
    src/native_asset_store.cpp
    src/native_disc.cpp
    src/native_snapshot.cpp
    src/native_fighter.cpp
    src/native_scene.cpp
    src/native_audio.cpp
    src/native_collision.cpp
    src/native_match.cpp
    src/native_windows_input.cpp
    src/native_heap.cpp
    src/native_renderer_backend.cpp
    src/native_d3d12.cpp
    src/native_texture.cpp
    src/native_window.cpp
)
target_include_directories(melee_native_core PUBLIC include)
if (WIN32)
    target_link_libraries(melee_native_core PUBLIC user32)
endif()
target_compile_options(melee_native_core PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<CXX_COMPILER_ID:MSVC>:/permissive->
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wextra>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wpedantic>
)

add_executable(melee_native_shell src/main.cpp)
target_link_libraries(melee_native_shell PRIVATE melee_native_core)

add_executable(melee_native_tests tests/native_tests.cpp)
target_link_libraries(melee_native_tests PRIVATE melee_native_core)

add_executable(melee_native_archive_tests tests/archive_tests.cpp)
target_link_libraries(melee_native_archive_tests PRIVATE melee_native_core)

add_executable(melee_native_timing_tests tests/timing_tests.cpp)
target_link_libraries(melee_native_timing_tests PRIVATE melee_native_core)
add_executable(melee_native_filesystem_tests tests/filesystem_tests.cpp)
target_link_libraries(melee_native_filesystem_tests PRIVATE melee_native_core)

enable_testing()
add_test(NAME native_memory_and_game COMMAND melee_native_tests)
add_test(NAME native_archive COMMAND melee_native_archive_tests)
add_test(NAME native_timing COMMAND melee_native_timing_tests)

add_executable(melee_native_input_tests tests/input_tests.cpp)
target_link_libraries(melee_native_input_tests PRIVATE melee_native_core)
add_test(NAME native_input_mapping COMMAND melee_native_input_tests)

add_executable(melee_native_render_tests tests/render_tests.cpp)
target_link_libraries(melee_native_render_tests PRIVATE melee_native_core)
add_test(NAME native_render COMMAND melee_native_render_tests)
add_test(NAME native_filesystem_and_assets COMMAND melee_native_filesystem_tests)

add_executable(melee_native_disc_tests tests/disc_tests.cpp)
target_link_libraries(melee_native_disc_tests PRIVATE melee_native_core)
add_test(NAME native_disc COMMAND melee_native_disc_tests)

add_executable(melee_native_snapshot_tests tests/snapshot_tests.cpp)
target_link_libraries(melee_native_snapshot_tests PRIVATE melee_native_core)
add_test(NAME native_snapshot COMMAND melee_native_snapshot_tests)

add_executable(melee_native_fighter_tests tests/fighter_tests.cpp)
target_link_libraries(melee_native_fighter_tests PRIVATE melee_native_core)
add_test(NAME native_fighter COMMAND melee_native_fighter_tests)
add_executable(melee_native_scene_tests tests/scene_tests.cpp)
target_link_libraries(melee_native_scene_tests PRIVATE melee_native_core)
add_test(NAME native_scene COMMAND melee_native_scene_tests)

add_executable(melee_native_audio_tests tests/audio_tests.cpp)
target_link_libraries(melee_native_audio_tests PRIVATE melee_native_core)
add_test(NAME native_audio COMMAND melee_native_audio_tests)

add_executable(melee_native_collision_tests tests/collision_tests.cpp)
target_link_libraries(melee_native_collision_tests PRIVATE melee_native_core)
add_test(NAME native_collision COMMAND melee_native_collision_tests)

add_executable(melee_native_match_tests tests/match_tests.cpp)
target_link_libraries(melee_native_match_tests PRIVATE melee_native_core)
add_test(NAME native_match COMMAND melee_native_match_tests)

add_executable(melee_native_windows_input_tests tests/windows_input_tests.cpp)
target_link_libraries(melee_native_windows_input_tests PRIVATE melee_native_core)
add_test(NAME native_windows_input COMMAND melee_native_windows_input_tests)
add_executable(melee_native_heap_tests tests/heap_tests.cpp)
target_link_libraries(melee_native_heap_tests PRIVATE melee_native_core)
add_test(NAME native_heap COMMAND melee_native_heap_tests)

add_executable(melee_native_renderer_backend_tests tests/renderer_backend_tests.cpp)
target_link_libraries(melee_native_renderer_backend_tests PRIVATE melee_native_core)
add_test(NAME native_renderer_backend COMMAND melee_native_renderer_backend_tests)

add_executable(melee_native_d3d12_tests tests/d3d12_tests.cpp)
target_link_libraries(melee_native_d3d12_tests PRIVATE melee_native_core)
add_test(NAME native_d3d12 COMMAND melee_native_d3d12_tests)

add_executable(melee_native_texture_tests tests/texture_tests.cpp)
target_link_libraries(melee_native_texture_tests PRIVATE melee_native_core)
add_test(NAME native_texture COMMAND melee_native_texture_tests)

add_executable(melee_native_window_tests tests/window_tests.cpp)
target_link_libraries(melee_native_window_tests PRIVATE melee_native_core)
add_test(NAME native_window COMMAND melee_native_window_tests)

