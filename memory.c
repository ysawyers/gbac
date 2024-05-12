#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "memory.h"

void load_bios(Memory *mem, char *bios_file) {
    FILE *fp = fopen(bios_file, "rb");
    if (fp == NULL) {
        fprintf(stderr, "ERROR: file (%s) failed to open\n", bios_file);
        exit(1);
    }

    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    fread(mem->bios, sizeof(mem->bios[0]), size, fp);
    
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

    fread(mem->rom, sizeof(mem->rom[0]), size, fp);
    
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

// ARM CPU does not support misaligned addresses
// https://problemkaputt.de/gbatek.htm#armcpumemoryalignments
static inline uint32_t align_address(uint32_t addr, size_t size) {
    switch (size) {
    case 4: return addr & ~0x3; // word transfer
    case 2: return addr & ~0x1; // half-word transfer
    }

    // byte transfers don't need to be aligned
    return addr;
}

uint32_t read_mem(Memory *mem, uint32_t addr, size_t size) {
    uint32_t word = 0;

    addr = align_address(addr, size);

    switch (addr) {
    case 0x04000130: return mem->reg_keyinput;
    }

    if (addr <= 0x00003FFF) {
        memcpy(&word, mem->bios + addr, size);
    } else if (addr >= 0x02000000 && addr <= 0x02FFFFFF) {
        memcpy(&word, mem->external_wram + ((addr - 0x02000000) % 0x40000), size);
    } else if (addr >= 0x03000000 && addr <= 0x03FFFFFF) {
        memcpy(&word, mem->internal_wram + ((addr - 0x03000000) % 0x8000), size);
    } else if (addr >= 0x04000000 && addr <= 0x04000054) {
        return ppu_read_register(addr);
    } else if (addr >= 0x08000000 && addr <= 0x81FFFFFF) {
        memcpy(&word, mem->rom + (addr - 0x08000000), size);
    } else {
        fprintf(stderr, "read to unmapped memory region: 0x%08X\n", addr);
        exit(1);
    }

    return word;
}

void write_mem(Memory *mem, uint32_t addr, uint32_t val, size_t size) {
    addr = align_address(addr, size);

    switch (addr) {
    case 0x04000208:
        memcpy(&mem->reg_ime, &val, size);
        return;
    }

    if (addr >= 0x02000000 && addr <= 0x02FFFFFF) {
        memcpy(mem->external_wram + ((addr - 0x02000000) % 0x40000), &val, size);
    } else if (addr >= 0x03000000 && addr <= 0x03FFFFFF) {
        memcpy(mem->internal_wram + ((addr - 0x03000000) % 0x8000), &val, size);
    } else if (addr >= 0x04000000 && addr <= 0x04000054) {
        ppu_set_register(addr, val);
    } else if (addr >= 0x05000000 && addr <= 0x5FFFFFF) {
        memcpy(pallete_ram + ((addr - 0x05000000) % 0x400), &val, size);
    } else if (addr >= 0x06000000 && addr <= 0x06FFFFFF) {
        memcpy(vram + ((addr - 0x06000000) % 0x20000), &val, size);
    } else {
        fprintf(stderr, "write to unmapped memory region: 0x%08X\n", addr);
        exit(1);
    }
}
