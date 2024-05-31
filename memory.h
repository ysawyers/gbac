#ifndef MEMORY_H
#define MEMORY_H

#include "ppu.h"

extern uint8_t bios[0x4000];
extern uint8_t external_wram[0x40000];
extern uint8_t internal_wram[0x8000];
extern uint8_t rom[0x2000000];

extern uint16_t reg_ime;
extern uint16_t reg_keyinput;

void load_bios(char *bios_file);
void load_rom(char *rom_file);

uint32_t read_word(uint32_t addr);
uint16_t read_halfword(uint32_t addr);
uint8_t read_byte(uint32_t addr);

void write_word(uint32_t addr, uint32_t word);
void write_halfword(uint32_t addr, uint16_t halfword);
void write_byte(uint32_t addr, uint8_t byte);

#endif