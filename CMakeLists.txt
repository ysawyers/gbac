cmake_minimum_required(VERSION "3.29.3")
project("gbac")

set(CMAKE_CXX_COMPILER "gcc")
add_compile_options(-fsanitize=address,undefined)
add_link_options(-fsanitize=address,undefined)
find_package(SDL2 REQUIRED COMPONENTS SDL2)
add_executable(gbac "main.c" "cpu.c" "memory.c" "ppu.c" "decompressor.c")
target_link_libraries(gbac PRIVATE SDL2::SDL2)
