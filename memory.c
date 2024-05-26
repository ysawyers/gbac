#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "memory.h"

// https://gbadev.net/gbadoc/memory.html
// for more detail into the memory regions / mappings

// ARM does not support misaligned addresses
// https://problemkaputt.de/gbatek.htm#armcpumemoryalignments
#define FORCE_MEMORY_ALIGN(addr, access_size) switch (access_size) { \
                                              case 4: addr &= ~0x3; break; \
                                              case 2: addr &= ~0x1; break; \
                                              }

void load_bios(Memory *mem, char *bios_file) {
    FILE *fp = fopen(bios_file, "rb");
    if (fp == NULL) {
        fprintf(stderr, "ERROR: file (%s) failed to open\n", bios_file);
        exit(1);
    }

    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    fread(mem->bios, sizeof(uint8_t), size, fp);
    
    fclose(fp);
}

void load_rom(Memory *mem, char *rom_file) {
    FILE *fp = fopen(rom_file, "rb");
    if (fp == NULL) {
        fprintf(stderr, "ERROR: file (%s) failed to open\n", rom_file);
        exit(1);
    }

    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    fread(mem->rom, sizeof(uint8_t), size, fp);
    
    fclose(fp);
}

Memory* init_mem(const char *bios_file, const char *rom_file) {
    Memory *mem = (Memory *)calloc(1, sizeof(Memory));
    if (mem == NULL) {
        fprintf(stderr, "unable to allocate more memory!\n");
        exit(1);
    }

    load_bios(mem, bios_file);
    load_rom(mem, rom_file);

    return mem;
}

uint32_t read_mem(Memory *mem, uint32_t addr, size_t access_size) {
    FORCE_MEMORY_ALIGN(addr, access_size)

    uint32_t word = 0;

    switch ((addr >> 24) & 0xFF) {
    case 0x00:
        memcpy(&word, mem->bios + addr, access_size);
        break;
    case 0x02:
        memcpy(&word, mem->external_wram + ((addr - 0x02000000) & 0x3FFFF), access_size);
        break;
    case 0x03:
        memcpy(&word, mem->internal_wram + ((addr - 0x03000000) & 0x7FFF), access_size);
        break;
    case 0x04:
        switch (addr) {
        case 0x04000130: return mem->reg_keyinput;
        default:
            if (addr >= 0x04000000 && addr <= 0x04000054)
                return ppu_read_register(addr);
            printf("[read] unmapped hardware register: %08X\n", addr);
            exit(1);
        }
    case 0x05:
        memcpy(&word, pallete_ram + ((addr - 0x05000000) & 0x3FF), access_size);
        break;
    case 0x06:
        addr = (addr - 0x06000000) & 0x1FFFF;
        if (addr >= 0x18000 && addr <= 0x1FFFF) {
            memcpy(&word, vram + (addr - 0x8000), access_size);
        } else {
            memcpy(&word, vram + addr, access_size);
        }
        break;
    case 0x07:
        memcpy(&word, oam + ((addr - 0x07000000) & 0x3FF), access_size);
        break;
    case 0x08:
    case 0x09:
    case 0x0A:
    case 0x0B:
    case 0x0C:
    case 0x0D:
        memcpy(&word, mem->rom + ((addr - 0x08000000) & 0x1FFFFFF), access_size);
        break;
    case 0x0E:
        printf("cart ram\n");
        exit(1); 
    }

    return word;
}

// TODO: depending on profiling in the future an easy optimization
// could be to create a seperate write_byte function to handle the edge cases
// and reduce the amount of branching for each instruction
void write_mem(Memory *mem, uint32_t addr, uint32_t val, size_t access_size) {
    FORCE_MEMORY_ALIGN(addr, access_size);

    static void *jump_table[] = {
        &&illegal_write, &&illegal_write, &&external_wram_reg, &&internal_wram_reg,
        &&mapped_registers, &&pallete_ram_reg, &&vram_reg, &&oam_reg, &&illegal_write,
        &&illegal_write, &&illegal_write, &&illegal_write, &&illegal_write, &&illegal_write,
        &&cart_ram_reg, &&cart_ram_reg
    };

    goto *jump_table[(addr >> 24) & 0xF];

    illegal_write: return;

    external_wram_reg:
        memcpy(mem->external_wram + ((addr - 0x02000000) & 0x3FFFF), &val, access_size);
        return;
    
    internal_wram_reg:
        memcpy(mem->internal_wram + ((addr - 0x03000000) & 0x7FFF), &val, access_size);
        return;

    mapped_registers:
        switch (addr) {
        case 0x04000208:
            memcpy(&mem->reg_ime, &val, access_size);
            break;
        default:
            if (addr >= 0x04000000 && addr <= 0x04000054) {
                ppu_set_register(addr, val);
            } else {
                printf("[write] unmapped hardware register: %08X\n", addr);
                exit(1);
            }
        }
        return;

    pallete_ram_reg:
        if (access_size == 1) {
            val = (val << 8) | val;
            access_size = 2;
        }
        memcpy(pallete_ram + ((addr - 0x05000000) & 0x3FF), &val, access_size);
        return;

    vram_reg:
        addr = (addr - 0x06000000) & 0x1FFFF;
        if (addr >= 0x18000 && addr <= 0x1FFFF) addr -= 0x8000;

        if (access_size == 1) {
            // byte writes to obj vram are ignored
            if (addr >= 0x14000) return;

            uint32_t bg_vram_size = 0x10000;
            if (is_rendering_bitmap())
                bg_vram_size = 0x14000;

            // byte writes to bg vram are duplicated across the halfword
            if (addr < bg_vram_size) {
                val = (val << 8) | val;
                access_size = 2;
            } else {
                return;
            }
        }

        memcpy(vram + addr, &val, access_size);
        return;

    oam_reg:
        if (access_size != 1) // OAM byte stores should be ignored
            memcpy(oam + ((addr - 0x07000000) & 0x3FF), &val, access_size);
        return;
    
    cart_ram_reg:
        printf("cart ram write unhandled\n");
        exit(1);
}
