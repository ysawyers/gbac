#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "memory.h"

typedef uint32_t Word;

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

Memory* init_mem(char *bios_file, char *rom_file) {
    Memory *mem = (Memory *)calloc(1, sizeof(Memory));
    if (mem == NULL) {
        fprintf(stderr, "unable to allocate more memory!\n");
        exit(1);
    }

    mem->reg_keyinput = 0xFFFF;

    load_bios(mem, bios_file);
    load_rom(mem, rom_file);

    return mem;
}

inline Word read_word(Memory *mem, Word addr) {
    Word word = 0;

    if (addr <= 0x00003FFF) {
        memcpy(&word, mem->bios + addr, sizeof(word));
    } else if (addr >= 0x02000000 && addr <= 0x02FFFFFF) {
        memcpy(&word, mem->external_wram + ((addr - 0x02000000) % 0x40000), sizeof(word));
    } else if (addr >= 0x03000000 && addr <= 0x03007FFF) {
        memcpy(&word, mem->internal_wram + (addr - 0x03000000), sizeof(word));
    } else if (addr >= 0x04000000 && addr <= 0x04000054) {
        word = ppu_read_register(addr);
    } else if (addr >= 0x08000000 && addr <= 0x09FFFFFF) {
        memcpy(&word, mem->rom + (addr - 0x8000000), sizeof(word));
    } else {
        fprintf(stderr, "[word] read to unmapped memory region: 0x%08X\n", addr);
        exit(1);
    }

    return word;
}

inline uint16_t read_half_word(Memory *mem, Word addr) {
    uint16_t half_word = 0;

    if (addr >= 0x03000000 && addr <= 0x03FFFFFF) {
        memcpy(&half_word, mem->internal_wram + ((addr - 0x03000000) % 0x8000), sizeof(half_word));
    } else if (addr >= 0x04000000 && addr <= 0x04000054) {
        return ppu_read_register(addr);
    } else if (addr == 0x4000130) {
        return mem->reg_keyinput;
    } else if (addr >= 0x08000000 && addr <= 0x81FFFFFF) {
        memcpy(&half_word, mem->rom + (addr - 0x08000000), sizeof(half_word));
    } else {
        fprintf(stderr, "[half_word] read to unmapped memory region: 0x%04X\n", addr);
        exit(1);
    }

    return half_word;
}

inline void write_word(Memory *mem, Word addr, Word val) {
    if (addr >= 0x02000000 && addr <= 0x02FFFFFF) {
        memcpy(mem->external_wram + ((addr - 0x02000000) % 0x40000), &val, sizeof(val));
    } else if (addr >= 0x03000000 && addr <= 0x03007FFF) {
        memcpy(mem->internal_wram + (addr - 0x03000000), &val, sizeof(val));
    } else if (addr >= 0x04000000 && addr <= 0x04000054) {
        ppu_set_register(addr, val);
    } else if (addr == 0x04000208) {
        mem->reg_ime = val;
    } else if (addr >= 0x06000000 && addr <= 0x06017FFF) {
        memcpy(vram + (addr - 0x06000000), &val, sizeof(val));
    } else {
        fprintf(stderr, "[word] write to unmapped memory region: 0x%04X\n", addr);
        exit(1);
    }
}

inline void write_half_word(Memory *mem, Word addr, uint16_t val) {
    if (addr >= 0x03000000 && addr <= 0x03FFFFFF) {
        memcpy(mem->internal_wram + ((addr - 0x03000000) % 0x8000), &val, sizeof(val));
    } else if (addr >= 0x04000000 && addr <= 0x04000054) {
        ppu_set_register(addr, val);
    } else if (addr >= 0x05000000 && addr <= 0x050003FF) {
        memcpy(pallete_ram + (addr - 0x05000000), &val, sizeof(val));
    } else if (addr >= 0x06000000 && addr <= 0x06017FFF) {
        memcpy(vram + (addr - 0x06000000), &val, sizeof(val));
    } else {
        fprintf(stderr, "[half_word] write to unmapped memory region: 0x%04X\n", addr);
        exit(1);
    }
}

inline void write_byte(Memory *mem, Word addr, uint8_t val) {
    if (addr >= 0x03000000 && addr <= 0x03FFFFFF) {
        *(mem->internal_wram + ((addr - 0x03000000) % 0x8000)) = val;
    } else {
        fprintf(stderr, "[byte] write to unmapped memory region: 0x%04X\n", addr);
        exit(1);
    }
}
