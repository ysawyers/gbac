#ifndef MEMORY_H
#define MEMORY_H

#include "ppu.h"

typedef struct {
    uint8_t bios[0x4000];
    uint8_t external_wram[0x40000];
    uint8_t internal_wram[0x8000];
    uint8_t rom[0x20000000];

    uint16_t reg_ime;
    uint16_t reg_keyinput;
} Memory;

Memory* init_mem(char *bios_file, char *rom_file);

uint32_t read_word(Memory *mem, uint32_t addr);
uint16_t read_half_word(Memory *mem, uint32_t addr);

void write_word(Memory *mem, uint32_t addr, uint32_t val);
void write_half_word(Memory *mem, uint32_t addr, uint16_t val);
void write_byte(Memory *mem, uint32_t addr, uint8_t val);

#endif
