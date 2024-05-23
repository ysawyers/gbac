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

    switch (addr) {
    case 0x04000130: return mem->reg_keyinput;
    default:
        if (addr >= 0x040000060 && addr <= 0x04000300) {
            printf("read to unmapped reg: %08X\n", addr);
            exit(1);
        }
    }

    if (addr <= 0x00003FFF) {
        memcpy(&word, mem->bios + addr, access_size);
    } else if (addr >= 0x02000000 && addr <= 0x02FFFFFF) {
        memcpy(&word, mem->external_wram + ((addr - 0x02000000) % 0x40000), access_size);
    } else if (addr >= 0x03000000 && addr <= 0x03FFFFFF) {
        memcpy(&word, mem->internal_wram + ((addr - 0x03000000) % 0x8000), access_size);
    } else if (addr >= 0x04000000 && addr <= 0x04000054) {
        return ppu_read_register(addr);
    } else if (addr >= 0x07000000 && addr <= 0x07FFFFFF) {
        memcpy(&word, oam + ((addr - 0x07000000) & 0x400), access_size);
    } else if (addr >= 0x08000000 && addr <= 0x81FFFFFF) {
        memcpy(&word, mem->rom + (addr - 0x08000000), access_size);
    } else {
        fprintf(stderr, "read to unmapped memory region: 0x%08X\n", addr);
        exit(1);
    }

    return word;
}

void write_mem(Memory *mem, uint32_t addr, uint32_t val, size_t access_size) {
    FORCE_MEMORY_ALIGN(addr, access_size);
    
    switch (addr) {
    case 0x04000208:
        memcpy(&mem->reg_ime, &val, access_size);
        return;
    default:
        if (addr >= 0x040000060 && addr <= 0x04000300) {
            printf("write to umapped reg: %08X\n", addr);
            exit(1);
        }
    }

    if (addr >= 0x02000000 && addr <= 0x02FFFFFF) {
        memcpy(mem->external_wram + ((addr - 0x02000000) % 0x40000), &val, access_size);
    } else if (addr >= 0x03000000 && addr <= 0x03FFFFFF) {
        memcpy(mem->internal_wram + ((addr - 0x03000000) % 0x8000), &val, access_size);
    } else if (addr >= 0x04000000 && addr <= 0x04000054) {
        ppu_set_register(addr, val);
    } else if (addr >= 0x05000000 && addr <= 0x5FFFFFF) {
        memcpy(pallete_ram + ((addr - 0x05000000) % 0x400), &val, access_size);
    } else if (addr >= 0x06000000 && addr <= 0x06FFFFFF) {
        addr = (addr - 0x06000000) % 0x20000;
        if (addr >= 0x18000 && addr <= 0x1FFFF) {
            memcpy(vram + (addr - 0x8000), &val, access_size);
        } else {
            memcpy(vram + addr, &val, access_size);
        }
    }
}
