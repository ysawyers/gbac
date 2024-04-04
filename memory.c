#include <stdio.h>
#include <stdlib.h>
#include "memory.h"

Memory mem = {0};

void load_bios(char *bios_file) {
    FILE *fp = fopen(bios_file, "rb");
    if (fp == NULL) {
        fprintf(stderr, "ERROR: file (%s) failed to open\n", bios_file);
        exit(1);
    }

    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    fread(mem.bios, sizeof(uint8_t), size, fp);

    fclose(fp);
}

void load_rom(char *rom_file) {
    FILE *fp = fopen(rom_file, "rb");
    if (fp == NULL) {
        fprintf(stderr, "ERROR: file (%s) failed to open\n", rom_file);
        exit(1);
    }

    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    fread(mem.rom, sizeof(uint8_t), size, fp);

    fclose(fp);
}

uint32_t read_word(uint32_t addr) {
    if (addr < 0x4000) {
        return *((uint32_t *)(mem.bios + addr));
    }

    if (addr >= 0x08000000 && addr < 0xA000000) {
        return *((uint32_t *)(mem.rom + (addr - 0x08000000)));
    }

    fprintf(stderr, "[word] read to unmapped memory region: 0x%04X", addr);
    exit(1);
}

uint32_t read_half_word(uint32_t addr) {
    if (addr < 0x4000) {
        return *((uint16_t *)(mem.bios + addr));
    }

    if (addr >= 0x08000000 && addr < 0xA000000) {
        return *((uint16_t *)(mem.rom + (addr - 0x08000000)));
    }

    fprintf(stderr, "[half_word] read to unmapped memory region: 0x%04X", addr);
    exit(1);
}
