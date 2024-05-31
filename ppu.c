#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ppu.h"

#define FRAME_WIDTH  240
#define FRAME_HEIGHT 160

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

#define BGCNT_PRIO(bgcnt) (bgcnt & 0x3)

#define CYCLES_PER_SCANLINE 1232
#define CYCLES_PER_HDRAW    1006

typedef uint16_t Pixel;

Pixel frame[FRAME_HEIGHT][FRAME_WIDTH];

uint8_t vram[0x18000];
uint8_t oam[0x400];
uint8_t pallete_ram[0x400];
uint8_t ppu_mmio[0x100];

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

int reg_vcount = 0; // LCY
int cycles = 0;

bool is_rendering_bitmap(void) {
    uint8_t mode = (reg_dispcnt >> 8) & 0x1F;
    return (mode == 0x3) || (mode == 0x4) || (mode == 0x5);
}

// referenced from https://www.coranac.com/tonc/text/regbg.htm
// terms here are multiplied by 2 since each screen entry is 2 bytes (uint16_t)
static int compute_se_idx(int tile_x, int tile_y, bool bg_reg_64x64) {
    int se_idx = (tile_y * (32 * 2)) + (tile_x * 2);

    if (tile_x >= 32)
        se_idx += (0x03E0 * 2);
    if (tile_y >= 32) {
        se_idx += (0x0400 * 2);
    }

    return se_idx;
}

static void render_text_bg(uint16_t reg_bgcnt, uint16_t reg_bghofs, uint16_t reg_bgvofs) {
    int num_tiles_x = 32 * (1 + ((reg_bgcnt >> 0xE) & 1));
    int num_tiles_y = 32 * (1 + ((reg_bgcnt >> 0xF) & 1));

    uint8_t *tile_map = vram + (((reg_bgcnt >> 0x8) & 0x1F) * 0x800);
    uint8_t *tile_set = vram + (((reg_bgcnt >> 0x2) & 0x3) * 0x4000);
    bool color_pallete = (reg_bgcnt >> 0x7) & 1;
    bool mosaic_enable = (reg_bgcnt >> 0x6) & 1;
    int bpp = 4 << color_pallete;

    if (mosaic_enable) {
        printf("unimplemented: mosaic bit set\n");
        exit(1);
    }

    uint16_t scroll_x = reg_bghofs & 0x3FF;
    uint16_t scroll_y = reg_bgvofs & 0x3FF;

    int tile_y_offset = ((reg_vcount & ~7) / 8);
    int tile_y = (((scroll_y & ~7) / 8) + tile_y_offset) & (num_tiles_y - 1);
    int tile_x = ((scroll_x & ~7) / 8) & (num_tiles_x - 1);

    int scanline_px_rendered = 0;

    while (true) {
        uint16_t screen_entry = *(uint16_t *)(tile_map + compute_se_idx(tile_x, tile_y, num_tiles_y == 64));

        int tile_id = screen_entry & 0x3FF;
        uint8_t *tile = tile_set + (tile_id * (0x20 << color_pallete));
        uint8_t pallete_bank = (((screen_entry >> 0xC) & 0xF) << 4); // only used in 4bpp

        bool horizontal_flip = (screen_entry >> 0xA) & 1;  
        bool vertical_flip = (screen_entry >> 0xB) & 1;

        int tile_row_to_render = (reg_vcount - (reg_vcount & ~7));
        tile += bpp * (vertical_flip ? 7 - tile_row_to_render : tile_row_to_render);

        int start, end, step;

        if (horizontal_flip) {
            start = 3;
            end = -1;
            step = -1;
        } else {
            start = 0;
            end = 4;
            step = 1;
        }

        for (int i = start; i != end; i += step) {
            for (int nibble = horizontal_flip; nibble != (!horizontal_flip + step); nibble += step) {
                if (scanline_px_rendered >= FRAME_WIDTH) return;

                int px = i * 2 + nibble;
                if (!scanline_px_rendered && (px < (scroll_x - (scroll_x & ~7)))) continue;

                uint8_t pallete_id = color_pallete ? tile[px] : pallete_bank | ((tile[i] >> (nibble * 4)) & 0x0F);
                frame[reg_vcount][scanline_px_rendered++] = *(uint16_t *)(pallete_ram + (pallete_id * sizeof(Pixel)));
            }
        }

        tile_x = (tile_x + 1) & (num_tiles_x - 1);
    }
}

static void render_scanline(void) {
    // used to manage rendering priorities
    if (DCNT_BLANK) {
        for (int row = 0; row < FRAME_WIDTH; row++) 
            frame[reg_vcount][row] = 0xFFFF;
        return;
    }

    // check if any rendering enable bits are set
    if ((reg_dispcnt >> 8) & 0xFF) {
        switch (DCNT_MODE) {
        // tilemap modes
        case 0x0: {
            int prio = 0;

            if (DCNT_BG0) {
                prio = BGCNT_PRIO(reg_bg0cnt);
                render_text_bg(reg_bg0cnt, reg_bg0hofs, reg_bg0vofs);
            }

            int bg1_prio = BGCNT_PRIO(reg_bg1cnt);
            if (DCNT_BG1 && (bg1_prio > prio)) {
                prio = bg1_prio;
                render_text_bg(reg_bg1cnt, reg_bg1hofs, reg_bg1vofs);
            }

            int bg2_prio = BGCNT_PRIO(reg_bg2cnt);
            if (DCNT_BG2 && (bg2_prio > prio)) {
                prio = bg2_prio;
                render_text_bg(reg_bg2cnt, reg_bg2hofs, reg_bg2vofs);
            }

            int bg3_prio = BGCNT_PRIO(reg_bg3cnt);
            if (DCNT_BG3 && (bg3_prio > prio)) {
                prio = bg3_prio;
                render_text_bg(reg_bg3cnt, reg_bg3hofs, reg_bg3vofs);
            } 
            break;
        }
        case 0x1:
        case 0x2:
            fprintf(stderr, "video mode: %d not implemented yet\n", DCNT_MODE);
            exit(1);

        // bitmap modes
        case 0x3:
            if (DCNT_BG2) {
                uint8_t *vram_base_ptr = vram;
                if (DCNT_PAGE)
                    vram_base_ptr += 0xA000;

                for (int col = 0; col < FRAME_WIDTH; col++) // pixels for the frame are stored directly in vram
                    frame[reg_vcount][col] = *(uint16_t *)(vram + (reg_vcount * (FRAME_WIDTH * sizeof(Pixel))) + (col * sizeof(Pixel)));
            }
            break;
        case 0x4:
            if (DCNT_BG2) {
                uint8_t *vram_base_ptr = vram;
                if (DCNT_PAGE) 
                    vram_base_ptr += 0xA000;

                for (int col = 0; col < FRAME_WIDTH; col++) {
                    // each byte in vram is interpreted as a pallete index holding a pixels color
                    uint8_t pallete_idx = *(vram_base_ptr + (reg_vcount * FRAME_WIDTH) + col);
                    // copy the 15bpp color to the associated pixel on the frame
                    frame[reg_vcount][col] = *(uint16_t *)(pallete_ram + pallete_idx * sizeof(Pixel));
                }
            }
            break;
        case 0x5:
            fprintf(stderr, "bitmap mode 5 not implemented yet\n");
            exit(1);

        default:
            fprintf(stderr, "PPU Error: invalid video mode\n", DCNT_MODE);
            exit(1);
        }
    } else {
        for (int i = 0; i < 240; i++) // no rendering bits enabled: render backdrop (first entry in pallete RAM)
            frame[reg_vcount][i] = *(uint16_t *)pallete_ram;
    }
}

void tick_ppu(void) {
    cycles += 1;

    // vblank
    if (reg_vcount >= FRAME_HEIGHT) {
        reg_dispstat |= 3;

        if ((cycles % CYCLES_PER_SCANLINE) == 0) {
            if (++reg_vcount == 228) {
                reg_dispstat &= ~3;
                cycles = 0;
                reg_vcount = 0;
            };
        }
        return;
    }

    // from "research" seems like rendering 32 cycles into hdraw 
    // creates best results for scanline PPU
    if (cycles == 32) 
        render_scanline();

    if (cycles == 1006) // start of hblank
        reg_dispstat |= 2;

    if (cycles == CYCLES_PER_SCANLINE) {
        reg_dispstat &= ~3;   // hdraw and vdraw will start next cycle
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
