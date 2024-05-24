#ifndef PPU_H
#define PPU_H

#include <stdint.h>
#include <stdbool.h>

extern uint16_t frame[160][240];

extern uint8_t vram[0x18000];
extern uint8_t oam[0x400];
extern uint8_t pallete_ram[0x400];

extern uint16_t reg_dispcnt;

void tick_ppu(void);

uint32_t ppu_read_register(uint32_t addr);
void ppu_set_register(uint32_t addr, uint32_t val);


#endif
