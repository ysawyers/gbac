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

Memory* init_mem(const char *bios_file, const char *rom_file);

// read size of (1, 2, or 4) bytes from memory at given address
uint32_t read_mem(Memory *mem, uint32_t addr, size_t size);

// write size of (1, 2, or 4) bytes to memory at given address
void write_mem(Memory *mem, uint32_t addr, uint32_t val, size_t size);

#endif
