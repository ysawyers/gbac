cmake_minimum_required(VERSION "3.29.3")
project("gbac")

set(SDL2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/SDL2.framework/Resources/CMake")

add_compile_options(-fsanitize=address,undefined -std=c99)
add_link_options(-fsanitize=address,undefined -std=c99)

add_executable("gbac" "src/main.c" "src/cpu.c" "src/memory.c" "src/ppu.c" "src/decompressor.c")

find_package(SDL2 REQUIRED COMPONENTS SDL2)
target_link_libraries("gbac" PRIVATE SDL2::SDL2)
