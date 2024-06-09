#ifndef PPU_H
#define PPU_H

#include <stdbool.h>

extern uint16_t frame[160][240];

extern uint8_t vram[0x18000];
extern uint8_t oam[0x400];
extern uint8_t pallete_ram[0x400];
extern uint8_t ppu_mmio[0x56];
extern uint8_t reg_vcount;

extern bool is_rendering_bitmap;

void tick_ppu(void);

#endif
