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

enable_testing()
add_test(NAME native_memory_and_game COMMAND melee_native_tests)
add_test(NAME native_archive COMMAND melee_native_archive_tests)
