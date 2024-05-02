cmake_minimum_required(VERSION "0.0.0")

project("gbac")

add_compile_options(-std=c99)
add_compile_options(-fsanitize=address)
add_link_options(-fsanitize=address)
find_package(SDL2 REQUIRED COMPONENTS SDL2)
add_executable(gbac "main.c" "cpu.c" "memory.c" "ppu.c")
target_link_libraries(gbac PRIVATE SDL2::SDL2)
