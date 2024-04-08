#ifndef PPU_H
#define PPU_H

#include <stdint.h>

extern uint8_t vram[0x18000];

void tick_ppu(void);

void ppu_set_register(uint32_t addr, uint32_t val);

#endif
