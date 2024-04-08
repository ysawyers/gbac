#include <stdio.h>
#include <stdlib.h>
#include "ppu.h"

uint8_t vram[0x18000] = {0};

uint16_t reg_dispcnt;
uint16_t reg_dispstat;

uint16_t reg_bg0cnt;
uint16_t reg_bg0hofs;
uint16_t reg_bg0vofs;

uint16_t reg_bg1cnt;
uint16_t reg_bg1hofs;
uint16_t reg_bg1vofs;

uint16_t reg_bg2cnt;
uint16_t reg_bg2hofs;
uint16_t reg_bg2vofs;
uint16_t reg_bg2pa;
uint16_t reg_bg2pb;
uint16_t reg_bg2pc;
uint16_t reg_bg2pd;
uint32_t reg_bg2x;
uint32_t reg_bg2y;

uint16_t reg_bg3cnt;
uint16_t reg_bg3hofs;
uint16_t reg_bg3vofs;
uint16_t reg_bg3pa;
uint16_t reg_bg3pb;
uint16_t reg_bg3pc;
uint16_t reg_bg3pd;
uint32_t reg_bg3x;
uint32_t reg_bg3y;

uint8_t reg_vcount;

// 4 cycles per pixel
// 1232 cycles per scanline

void tick_ppu(void) {
    
};

void ppu_set_register(uint32_t addr, uint32_t val) {
    switch (addr) {
    case 0x04000000:
        reg_dispcnt = (uint16_t)val;
        break;
    case 0x04000004:
        reg_dispstat = (uint16_t)val;
        break;
    case 0x04000008:
        reg_bg0cnt = (uint16_t)val;
        break;
    case 0x0400000A:
        reg_bg1cnt = (uint16_t)val;
        break;
    case 0x0400000C:
        reg_bg2cnt = (uint16_t)val;
        break;
    case 0x0400000E:
        reg_bg3cnt = (uint16_t)val;
        break;
    case 0x04000010:
        reg_bg0hofs = (uint16_t)val;
        break;
    case 0x04000012:
        reg_bg0vofs = (uint16_t)val;
        break;
    case 0x04000014:
        reg_bg1hofs = (uint16_t)val;
        break;
    case 0x04000016:
        reg_bg1vofs = (uint16_t)val;
        break;
    case 0x04000018:
        reg_bg2hofs = (uint16_t)val;
        break;
    case 0x0400001A:
        reg_bg2vofs = (uint16_t)val;
        break;
    case 0x0400001C:
        reg_bg3hofs = (uint16_t)val;
        break;
    case 0x0400001E:
        reg_bg3vofs = (uint16_t)val;
        break;
    case 0x04000020:
        reg_bg2pa = (uint16_t)val;
        break;
    case 0x04000030:
        reg_bg3pa = (uint16_t)val;
        break;
    case 0x04000022:
        reg_bg2pb = (uint16_t)val;
        break;
    case 0x04000032:
        reg_bg3pb = (uint16_t)val;
        break;
    case 0x04000024:
        reg_bg2pc = (uint16_t)val;
        break;
    case 0x04000034:
        reg_bg3pc = (uint16_t)val;
        break;
    case 0x04000026:
        reg_bg2pd = (uint16_t)val;
        break;
    case 0x04000036:
        reg_bg3pd = (uint16_t)val;
        break;
    case 0x04000028:
        reg_bg2x = val;
        break;
    case 0x04000038:
        reg_bg3x = val;
        break;
    case 0x0400002C:
        reg_bg2y = val;
        break;
    case 0x0400003C:
        reg_bg3y = val;
        break;
    default:
        fprintf(stderr, "unmapped ppu register: 0x%08X\n", addr);
        exit(1);
    }
}
