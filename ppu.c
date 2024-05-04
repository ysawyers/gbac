#include <stdio.h>
#include <stdlib.h>
#include "ppu.h"

#define DCNT_MODE    (reg_dispcnt & 0x7)
#define DCNT_GB      ((reg_dispcnt >> 3) & 1)
#define DCNT_PAGE    ((reg_dispcnt >> 4) & 1)
#define DCNT_OAM_HBL ((reg_dispcnt >> 5) & 1)
#define DCNT_OBJ_1D  ((reg_dispcnt >> 6) & 1)
#define DCNT_BLANK   ((reg_dispcnt >> 7) & 1)

#define DCNT_BG0 ((reg_dispcnt >> 0x8) & 1)
#define DCNT_BG1 ((reg_dispcnt >> 0x9) & 1)
#define DCNT_BG2 ((reg_dispcnt >> 0xA) & 1)
#define DCNT_BG3 ((reg_dispcnt >> 0xB) & 1)
#define DCNT_OBJ ((reg_dispcnt >> 0xC) & 1)

uint16_t frame[160][240];

uint8_t vram[0x18000];
uint8_t oam[0x400];
uint8_t pallete_ram[0x400];

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

uint16_t reg_win0h;
uint16_t reg_win0v;

uint16_t reg_win1h;
uint16_t reg_win1v;

uint16_t reg_winin;
uint16_t reg_winout;

uint16_t reg_mosaic;
uint16_t reg_bldcnt;
uint16_t reg_bldalpha;
uint16_t reg_bldy;

uint8_t reg_vcount = 0; // LCY

// reset at the beginning of each scanline
size_t cycles = 0;

void render_scanline(void) {
    // forced vblank displays all white
    if (DCNT_BLANK) {
        for (int i = 0; i < 240; i++) frame[reg_vcount][i] = 0xFFFF;
        return;
    }

    // check if any rendering enable bits are set
    if ((reg_dispcnt >> 8) & 0x1F) {
        switch (DCNT_MODE) {
        // tilemap
        case 0:
        case 1:
        case 2:
            fprintf(stderr, "video mode: %d not implemented yet\n", DCNT_MODE);
            exit(1);

        // bitmap
        case 3: // 240x160 2bpp directly in vram
            if (DCNT_BG2) {
                for (int i = 0; i < 240; i++) {
                    frame[reg_vcount][i] = *(uint16_t *)(vram + (reg_vcount * (240 * 2)) + (i * 2));
                }
            }
            break;
        case 4: // 240x160 1bpp from vram as pallete index
            if (DCNT_BG2) {
                for (int i = 0; i < 240; i++) {
                    uint8_t pallete_idx = *(uint8_t *)(vram + (reg_vcount * 240) + i);
                    frame[reg_vcount][i] = *(uint16_t *)(pallete_ram + pallete_idx);
                }
            }
            break;
        case 5:
            fprintf(stderr, "bitmap mode 5 not implemented yet\n");
            exit(1);
        default:
            fprintf(stderr, "video mode [%d]: unexpected!\n", DCNT_MODE);
            exit(1);
        }
    } else { // all render bits disabled, draw backdrop (first entry in pallete RAM)
        for (int i = 0; i < 240; i++) {
            frame[reg_vcount][i] = *(uint16_t *)pallete_ram;
        }
    }
}

void tick_ppu(void) {
    cycles += 1;

    // vblank
    if (reg_vcount >= 160) {
        reg_dispstat |= 0x0001;

        if (cycles % 1232 == 0) {
            reg_vcount += 1;
            if (reg_vcount == 228) {
                // NOTE: may be bad?
                reg_dispstat &= 0xFFFE;
                reg_vcount = 0;
                cycles = 0;
            };
        }
        return;
    }

    // vdraw
    if (cycles <= 1006) reg_dispstat = reg_dispstat & 0xFFFE;

    // from "research" seems like rendering 32 cycles into H-draw creates best results for scanline PPU
    if (cycles == 32) render_scanline();

    // hblank
    if (cycles > 1006) {};

    // end of scanline
    if (cycles == 1232) {
        cycles = 0;
        reg_vcount += 1;
    }
};

uint32_t ppu_read_register(uint32_t addr) {
    switch (addr) {
    case 0x04000000: return reg_dispcnt;
    case 0x04000004: return reg_dispstat;
    case 0x04000006: return reg_vcount;
    case 0x04000008: return reg_bg0cnt;
    case 0x0400000A: return reg_bg1cnt;
    case 0x0400000C: return reg_bg2cnt;
    case 0x0400000E: return reg_bg3cnt;
    case 0x04000010: return reg_bg0hofs;
    case 0x04000012: return reg_bg0vofs;
    case 0x04000014: return reg_bg1hofs;
    case 0x04000016: return reg_bg1vofs;
    case 0x04000018: return reg_bg2hofs;
    case 0x0400001A: return reg_bg2vofs;
    case 0x0400001C: return reg_bg3hofs;
    case 0x0400001E: return reg_bg3vofs;
    case 0x04000020: return reg_bg2pa;
    case 0x04000030: return reg_bg3pa;
    case 0x04000022: return reg_bg2pb;
    case 0x04000032: return reg_bg3pb;
    case 0x04000024: return reg_bg2pc;
    case 0x04000034: return reg_bg3pc;
    case 0x04000026: return reg_bg2pd;
    case 0x04000036: return reg_bg3pd;
    case 0x04000028: return reg_bg2x;
    case 0x04000038: return reg_bg3x;
    case 0x0400002C: return reg_bg2y;
    case 0x0400003C: return reg_bg3y;
    case 0x04000040: return reg_win0h;
    case 0x04000042: return reg_win1h;
    case 0x04000044: return reg_win0v;
    case 0x04000046: return reg_win1v;
    case 0x04000048: return reg_winin;
    case 0x0400004A: return reg_winout;
    case 0x0400004C: return reg_mosaic;
    case 0x04000050: return reg_bldcnt;
    case 0x04000052: return reg_bldalpha;
    case 0x04000054: return reg_bldy;
    default:
        fprintf(stderr, "[read] unmapped ppu register: 0x%08X\n", addr);
        exit(1);
    }
}

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
    case 0x04000040:
        reg_win0h = (uint16_t)val;
        break;
    case 0x04000042:
        reg_win1h = (uint16_t)val;
        break;
    case 0x04000044:
        reg_win0v = (uint16_t)val;
        break;
    case 0x04000046:
        reg_win1v = (uint16_t)val;
        break;
    case 0x04000048:
        reg_winin = (uint16_t)val;
        break;
    case 0x0400004A:
        reg_winout = (uint16_t)val;
        break;
    case 0x0400004C:
        reg_mosaic = (uint16_t)val;
        break;
    case 0x04000050:
        reg_bldcnt = (uint16_t)val;
        break;
    case 0x04000052:
        reg_bldalpha = (uint16_t)val;
        break;
    case 0x04000054:
        reg_bldy = (uint16_t)val;
        break;
    default:
        fprintf(stderr, "[write] unmapped ppu register: 0x%08X\n", addr);
        exit(1);
    }
}
