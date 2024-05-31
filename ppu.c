#include <stdio.h>
#include <stdlib.h>
#include "ppu.h"

#define FRAME_WIDTH  240
#define FRAME_HEIGHT 160

#define DCNT_MODE    (REG_DISPCNT & 0x7)
#define DCNT_GB      ((REG_DISPCNT >> 3) & 1)
#define DCNT_PAGE    ((REG_DISPCNT >> 4) & 1)
#define DCNT_OAM_HBL ((REG_DISPCNT >> 5) & 1)
#define DCNT_OBJ_1D  ((REG_DISPCNT >> 6) & 1)
#define DCNT_BLANK   ((REG_DISPCNT >> 7) & 1)

#define DCNT_BG0 ((REG_DISPCNT >> 0x8) & 1)
#define DCNT_BG1 ((REG_DISPCNT >> 0x9) & 1)
#define DCNT_BG2 ((REG_DISPCNT >> 0xA) & 1)
#define DCNT_BG3 ((REG_DISPCNT >> 0xB) & 1)
#define DCNT_OBJ ((REG_DISPCNT >> 0xC) & 1)

#define BGCNT_PRIO(bgcnt) (bgcnt & 0x3)

#define REG_DISPCNT *(uint16_t *)ppu_mmio
#define REG_DISPSTAT *(uint16_t *)(ppu_mmio + 0x04)

#define REG_BG0CNT *(uint16_t *)(ppu_mmio + 0x08)
#define REG_BG1CNT *(uint16_t *)(ppu_mmio + 0x0A)
#define REG_BG2CNT *(uint16_t *)(ppu_mmio + 0x0C)
#define REG_BG3CNT *(uint16_t *)(ppu_mmio + 0x0E)

#define REG_BG0HOFS *(uint16_t *)(ppu_mmio + 0x10)
#define REG_BG0VOFS *(uint16_t *)(ppu_mmio + 0x12)
#define REG_BG1HOFS *(uint16_t *)(ppu_mmio + 0x14)
#define REG_BG1VOFS *(uint16_t *)(ppu_mmio + 0x16)
#define REG_BG2HOFS *(uint16_t *)(ppu_mmio + 0x18)
#define REG_BG2VOFS *(uint16_t *)(ppu_mmio + 0x1A)
#define REG_BG3HOFS *(uint16_t *)(ppu_mmio + 0x1C)
#define REG_BG3VOFS *(uint16_t *)(ppu_mmio + 0x1E)

#define REG_BG2PA *(uint16_t *)(ppu_mmio + 0x20)
#define REG_BG3PA *(uint16_t *)(ppu_mmio + 0x30)
#define REG_BG2PB *(uint16_t *)(ppu_mmio + 0x22)
#define REG_BG3PB *(uint16_t *)(ppu_mmio + 0x32)
#define REG_BG2PC *(uint16_t *)(ppu_mmio + 0x24)
#define REG_BG3PC *(uint16_t *)(ppu_mmio + 0x34)
#define REG_BG2PD *(uint16_t *)(ppu_mmio + 0x26)
#define REG_BG3PD *(uint16_t *)(ppu_mmio + 0x36)
#define REG_BG2X *(uint32_t *)(ppu_mmio + 0x28)
#define REG_BG3X *(uint32_t *)(ppu_mmio + 0x38)
#define REG_BG2Y *(uint32_t *)(ppu_mmio + 0x2C)
#define REG_BG3Y *(uint32_t *)(ppu_mmio + 0x3C)

#define REG_WIN0H *(uint16_t *)(ppu_mmio + 0x40)
#define REG_WIN1H *(uint16_t *)(ppu_mmio + 0x42)
#define REG_WIN0V *(uint16_t *)(ppu_mmio + 0x44)
#define REG_WIN1V *(uint16_t *)(ppu_mmio + 0x46)
#define REG_WININ *(uint16_t *)(ppu_mmio + 0x48)
#define REG_WINOUT *(uint16_t *)(ppu_mmio + 0x4A)

#define REG_MOSAIC *(uint16_t *)(ppu_mmio + 0x4C)
#define REG_BLDCNT *(uint16_t *)(ppu_mmio + 0x50)
#define REG_BLDALPHA *(uint16_t *)(ppu_mmio + 0x52)
#define REG_BLDY *(uint16_t *)(ppu_mmio + 0x54)

#define CYCLES_PER_SCANLINE 1232
#define CYCLES_PER_HDRAW    1006

typedef uint16_t Pixel;

Pixel frame[FRAME_HEIGHT][FRAME_WIDTH];

uint8_t vram[0x18000];
uint8_t oam[0x400];
uint8_t pallete_ram[0x400];
uint8_t ppu_mmio[0x56];

uint8_t reg_vcount = 0;
bool is_rendering_bitmap = false;

int cycles = 0;

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
    if ((REG_DISPCNT >> 8) & 0xFF) {
        switch (DCNT_MODE) {
        // tilemap modes
        case 0x0: {
            int prio = 0;

            if (DCNT_BG0) {
                prio = BGCNT_PRIO(REG_BG0CNT);
                render_text_bg(REG_BG0CNT, REG_BG0HOFS, REG_BG0VOFS);
            }

            int bg1_prio = BGCNT_PRIO(REG_BG1CNT);
            if (DCNT_BG1 && (bg1_prio > prio)) {
                prio = bg1_prio;
                render_text_bg(REG_BG1CNT, REG_BG1HOFS, REG_BG1VOFS);
            }

            int bg2_prio = BGCNT_PRIO(REG_BG2CNT);
            if (DCNT_BG2 && (bg2_prio > prio)) {
                prio = bg2_prio;
                render_text_bg(REG_BG2CNT, REG_BG2HOFS, REG_BG2VOFS);
            }

            int bg3_prio = BGCNT_PRIO(REG_BG3CNT);
            if (DCNT_BG3 && (bg3_prio > prio)) {
                prio = bg3_prio;
                render_text_bg(REG_BG3CNT, REG_BG3HOFS, REG_BG3VOFS);
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
        REG_DISPSTAT |= 3;

        if ((cycles % CYCLES_PER_SCANLINE) == 0) {
            if (++reg_vcount == 228) {
                REG_DISPSTAT &= ~3;
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
        REG_DISPSTAT |= 2;

    if (cycles == CYCLES_PER_SCANLINE) {
        REG_DISPSTAT &= ~3;   // hdraw and vdraw will start next cycle
        cycles = 0;
        reg_vcount += 1;
    }
};
