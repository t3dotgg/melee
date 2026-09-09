cmake_minimum_required(VERSION 3.20)
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "Native game architecture")
project(melee-source LANGUAGES C)

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

add_executable(test_effects tests/test_effects.c)
target_sources(test_effects PRIVATE "${MELEE_ROOT}/src/melee/ef/eflib.c")
target_link_libraries(test_effects PRIVATE melee_host_options)
target_link_options(test_effects PRIVATE -Wl,-dead_strip)
add_test(NAME effects COMMAND test_effects)

add_executable(test_archive tests/test_archive.c assets/archive.c assets/descriptors.c platform/report.c)
target_link_libraries(test_archive PRIVATE melee_host_options)
add_test(NAME archive COMMAND test_archive)
