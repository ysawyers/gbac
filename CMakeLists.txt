cmake_minimum_required(VERSION "0.0.0")

project("gbac")

add_executable("gbac" "main.c" "cpu.c" "memory.c" "ppu.c")
