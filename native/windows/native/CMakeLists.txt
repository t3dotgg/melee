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
    src/native_dat_archive.cpp
    src/native_dat_scene.cpp
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
    src/native_swapchain.cpp
    src/native_animation.cpp
    src/native_render_geometry.cpp
    src/native_material.cpp
    src/native_material_asset.cpp
)
target_include_directories(melee_native_core PUBLIC include)
if (WIN32)
    target_link_libraries(melee_native_core PUBLIC user32 d3d12 dxgi)
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

add_executable(melee_native_dat_archive_tests tests/dat_archive_tests.cpp)
target_link_libraries(melee_native_dat_archive_tests PRIVATE melee_native_core)

add_executable(melee_native_timing_tests tests/timing_tests.cpp)
target_link_libraries(melee_native_timing_tests PRIVATE melee_native_core)
add_executable(melee_native_filesystem_tests tests/filesystem_tests.cpp)
target_link_libraries(melee_native_filesystem_tests PRIVATE melee_native_core)

enable_testing()
add_test(NAME native_memory_and_game COMMAND melee_native_tests)
add_test(NAME native_archive COMMAND melee_native_archive_tests)
add_test(NAME native_dat_archive COMMAND melee_native_dat_archive_tests)

add_executable(melee_native_dat_scene_tests tests/dat_scene_tests.cpp)
target_link_libraries(melee_native_dat_scene_tests PRIVATE melee_native_core)
add_test(NAME native_dat_scene COMMAND melee_native_dat_scene_tests)
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

add_executable(melee_native_swapchain_tests tests/swapchain_tests.cpp)
target_link_libraries(melee_native_swapchain_tests PRIVATE melee_native_core)
if (WIN32)
    target_link_libraries(melee_native_swapchain_tests PRIVATE d3d12 dxgi)
endif()
add_test(NAME native_swapchain COMMAND melee_native_swapchain_tests)

add_executable(melee_native_gpu_pass_tests tests/gpu_pass_tests.cpp)
target_link_libraries(melee_native_gpu_pass_tests PRIVATE melee_native_core)
if (WIN32)
    target_link_libraries(melee_native_gpu_pass_tests PRIVATE d3d12 dxgi)
endif()
add_test(NAME native_gpu_pass COMMAND melee_native_gpu_pass_tests)

add_executable(melee_native_animation_tests tests/animation_tests.cpp)
target_link_libraries(melee_native_animation_tests PRIVATE melee_native_core)
add_test(NAME native_animation COMMAND melee_native_animation_tests)

add_executable(melee_native_render_geometry_tests tests/render_geometry_tests.cpp)
target_link_libraries(melee_native_render_geometry_tests PRIVATE melee_native_core)
add_test(NAME native_render_geometry COMMAND melee_native_render_geometry_tests)

add_executable(melee_native_material_tests tests/material_tests.cpp)
target_link_libraries(melee_native_material_tests PRIVATE melee_native_core)
add_test(NAME native_material COMMAND melee_native_material_tests)

add_executable(melee_native_material_asset_tests tests/material_asset_tests.cpp)
target_link_libraries(melee_native_material_asset_tests PRIVATE melee_native_core)
add_test(NAME native_material_asset COMMAND melee_native_material_asset_tests)

add_executable(melee_native_loop_tests tests/loop_tests.cpp)
target_link_libraries(melee_native_loop_tests PRIVATE melee_native_core)
add_test(NAME native_game_loop COMMAND melee_native_loop_tests)

# Release builds define NDEBUG, which would erase the assertions in these
# executable tests. Keep assertions active so malformed native data and GPU
# capability failures are observed by CTest on the real Windows machine.
get_property(native_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(native_target IN LISTS native_targets)
    if (native_target MATCHES "_tests$")
        target_compile_options(${native_target} PRIVATE
            $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-UNDEBUG>)
    endif()
endforeach()
