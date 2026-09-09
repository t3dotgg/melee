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
    src/native_snapshot.cpp
    src/native_fighter.cpp
)
target_include_directories(melee_native_core PUBLIC include)
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

add_executable(melee_native_snapshot_tests tests/snapshot_tests.cpp)
target_link_libraries(melee_native_snapshot_tests PRIVATE melee_native_core)
add_test(NAME native_snapshot COMMAND melee_native_snapshot_tests)

add_executable(melee_native_fighter_tests tests/fighter_tests.cpp)
target_link_libraries(melee_native_fighter_tests PRIVATE melee_native_core)
add_test(NAME native_fighter COMMAND melee_native_fighter_tests)

