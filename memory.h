#ifndef MEMORY_H
#define MEMORY_H

#include "ppu.h"

void load_bios(char *bios_file);
void load_rom(char *rom_file);

uint32_t read_word(uint32_t addr);
uint32_t read_half_word(uint32_t addr);

void write_half_word(uint32_t addr, uint16_t val);
void write_word(uint32_t addr, uint32_t val);

#endif
