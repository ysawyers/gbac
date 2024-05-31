#include <stdio.h>
#include <stdlib.h>
#include "memory.h"

uint8_t bios[0x4000];
uint8_t external_wram[0x40000];
uint8_t internal_wram[0x8000];
uint8_t rom[0x2000000];

uint16_t reg_ime;
uint16_t reg_keyinput;

void load_bios(char *bios_file) {
    FILE *fp = fopen(bios_file, "rb");
    if (fp == NULL) {
        fprintf(stderr, "ERROR: file (%s) failed to open\n", bios_file);
        exit(1);
    }

    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    fread(bios, sizeof(uint8_t), size, fp);
    
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

    fread(rom, sizeof(uint8_t), size, fp);
    
    fclose(fp);
}

uint32_t read_word(uint32_t addr) {
    addr &= ~3;

    switch ((addr >> 24) & 0xFF) {
    case 0x00: return *(uint32_t *)(bios + addr);
    case 0x02: return *(uint32_t *)(external_wram + ((addr - 0x02000000) & 0x3FFFF));
    case 0x03: return *(uint32_t *)(internal_wram + ((addr - 0x03000000) & 0x7FFF));
    case 0x04:
        switch (addr) {
        case 0x04000006: return reg_vcount;
        case 0x04000130: return reg_keyinput;
        default:
            if (addr >= 0x04000000 && addr <= 0x04000054) return *(uint32_t *)(ppu_mmio + (addr - 0x04000000));
            printf("[read] unmapped hardware register: %08X\n", addr);
            exit(1);
        }
    case 0x05: return *(uint32_t *)(pallete_ram + ((addr - 0x05000000) & 0x3FF));
    case 0x06:
        addr = (addr - 0x06000000) & 0x1FFFF;
        if (addr >= 0x18000 && addr <= 0x1FFFF) {
            return *(uint32_t *)(vram + (addr - 0x8000));
        } else {
            return *(uint32_t *)(vram + addr);
        }
    case 0x07: return *(uint32_t *)(oam + ((addr - 0x07000000) & 0x3FF));
    case 0x08:
    case 0x09:
    case 0x0A:
    case 0x0B:
    case 0x0C:
    case 0x0D: return *(uint32_t *)(rom + ((addr - 0x08000000) & 0x1FFFFFF));
    case 0x0E:
        printf("cart ram\n");
        exit(1);
    }

    return 0;
}

uint16_t read_halfword(uint32_t addr) {
    addr &= ~1;

    switch ((addr >> 24) & 0xFF) {
    case 0x00: return *(uint16_t *)(bios + addr);
    case 0x02: return *(uint16_t *)(external_wram + ((addr - 0x02000000) & 0x3FFFF));
    case 0x03: return *(uint16_t *)(internal_wram + ((addr - 0x03000000) & 0x7FFF));
    case 0x04:
        switch (addr) {
        case 0x04000006: return reg_vcount;
        case 0x04000130: return reg_keyinput;
        default:
            if (addr >= 0x04000000 && addr <= 0x04000054) return *(uint16_t *)(ppu_mmio + (addr - 0x04000000));
            printf("[read] unmapped hardware register: %08X\n", addr);
            exit(1);
        }
    case 0x05: return *(uint16_t *)(pallete_ram + ((addr - 0x05000000) & 0x3FF));
    case 0x06:
        addr = (addr - 0x06000000) & 0x1FFFF;
        if (addr >= 0x18000 && addr <= 0x1FFFF) {
            return *(uint16_t *)(vram + (addr - 0x8000));
        } else {
            return *(uint16_t *)(vram + addr);
        }
    case 0x07: return *(uint16_t *)(oam + ((addr - 0x07000000) & 0x3FF));
    case 0x08:
    case 0x09:
    case 0x0A:
    case 0x0B:
    case 0x0C:
    case 0x0D: return *(uint16_t *)(rom + ((addr - 0x08000000) & 0x1FFFFFF));
    case 0x0E:
        printf("cart ram\n");
        exit(1);
    }

    return 0;
}

uint8_t read_byte(uint32_t addr) {
    switch ((addr >> 24) & 0xFF) {
    case 0x00: return bios[addr];
    case 0x02: return external_wram[(addr - 0x02000000) & 0x3FFFF];
    case 0x03: return internal_wram[(addr - 0x03000000) & 0x7FFF];
    case 0x04:
        switch (addr) {
        case 0x04000006: return reg_vcount;
        case 0x04000130: return reg_keyinput;
        default:
            if (addr >= 0x04000000 && addr <= 0x04000054) return ppu_mmio[addr];
            printf("[read] unmapped hardware register: %08X\n", addr);
            exit(1);
        }
    case 0x05: return pallete_ram[(addr - 0x05000000) & 0x3FF];
    case 0x06:
        addr = (addr - 0x06000000) & 0x1FFFF;
        if (addr >= 0x18000) addr -= 0x8000;
        return vram[addr];
    case 0x07: return oam[(addr - 0x07000000) & 0x3FF];
    case 0x08:
    case 0x09:
    case 0x0A:
    case 0x0B:
    case 0x0C:
    case 0x0D: return rom[(addr - 0x08000000) & 0x1FFFFFF];
    case 0x0E:
        printf("cart ram\n");
        exit(1); 
    }

    return 0;
}

void write_word(uint32_t addr, uint32_t word) {
    addr &= ~3;

    static void *jump_table[] = {
        &&illegal_write, &&illegal_write, &&external_wram_reg, &&internal_wram_reg,
        &&mapped_registers, &&pallete_ram_reg, &&vram_reg, &&oam_reg, &&illegal_write,
        &&illegal_write, &&illegal_write, &&illegal_write, &&illegal_write, &&illegal_write,
        &&cart_ram_reg, &&cart_ram_reg
    };

    goto *jump_table[(addr >> 24) & 0xF];

    illegal_write: return;

    external_wram_reg:
        *(uint32_t *)(external_wram + ((addr - 0x02000000) & 0x3FFFF)) = word;
        return;
    
    internal_wram_reg:
        *(uint32_t *)(internal_wram + ((addr - 0x03000000) & 0x7FFF)) = word;
        return;

    mapped_registers:
        switch (addr) {
        case 0x04000208:
            reg_ime = word;
            break;
        default:
            if (addr >= 0x04000000 && addr <= 0x04000054) {
                *(uint32_t *)(ppu_mmio + (addr - 0x04000000)) = word;
            } else {
                printf("[write] unmapped hardware register: %08X\n", addr);
                exit(1);
            }
        }
        return;

    pallete_ram_reg:
        *(uint32_t *)(pallete_ram + ((addr - 0x05000000) & 0x3FF)) = word;
        return;

    vram_reg:
        addr = (addr - 0x06000000) & 0x1FFFF;
        if (addr >= 0x18000) addr -= 0x8000;
        *(uint32_t *)(vram + addr) = word;
        return;

    oam_reg:
        *(uint32_t *)(oam + ((addr - 0x07000000) & 0x3FF)) = word;
        return;
    
    cart_ram_reg:
        printf("cart ram write unhandled\n");
        exit(1);
}

void write_halfword(uint32_t addr, uint16_t halfword) {
    addr &= ~1;

    static void *jump_table[] = {
        &&illegal_write, &&illegal_write, &&external_wram_reg, &&internal_wram_reg,
        &&mapped_registers, &&pallete_ram_reg, &&vram_reg, &&oam_reg, &&illegal_write,
        &&illegal_write, &&illegal_write, &&illegal_write, &&illegal_write, &&illegal_write,
        &&cart_ram_reg, &&cart_ram_reg
    };

    goto *jump_table[(addr >> 24) & 0xF];

    illegal_write: return;

    external_wram_reg:
        *(uint16_t *)(external_wram + ((addr - 0x02000000) & 0x3FFFF)) = halfword;
        return;
    
    internal_wram_reg:
        *(uint16_t *)(internal_wram + ((addr - 0x03000000) & 0x7FFF)) = halfword;
        return;

    mapped_registers:
        switch (addr) {
        case 0x04000208:
            reg_ime = halfword;
            break;
        default:
            if (addr >= 0x04000000 && addr <= 0x04000054) {
                *(uint16_t *)(ppu_mmio + (addr - 0x04000000)) = halfword;
            } else {
                printf("[write] unmapped hardware register: %08X\n", addr);
                exit(1);
            }
        }
        return;

    pallete_ram_reg:
        *(uint16_t *)(pallete_ram + ((addr - 0x05000000) & 0x3FF)) = halfword;
        return;

    vram_reg:
        addr = (addr - 0x06000000) & 0x1FFFF;
        if (addr >= 0x18000) addr -= 0x8000;
        *(uint16_t *)(vram + addr) = halfword;
        return;

    oam_reg:
        *(uint16_t *)(oam + ((addr - 0x07000000) & 0x3FF)) = halfword;
        return;
    
    cart_ram_reg:
        printf("cart ram write unhandled\n");
        exit(1);
}

void write_byte(uint32_t addr, uint8_t byte) {
    static void *jump_table[] = {
        &&illegal_write, &&illegal_write, &&external_wram_reg, &&internal_wram_reg,
        &&mapped_registers, &&pallete_ram_reg, &&vram_reg, &&oam_reg, &&illegal_write,
        &&illegal_write, &&illegal_write, &&illegal_write, &&illegal_write, &&illegal_write,
        &&cart_ram_reg, &&cart_ram_reg
    };

    goto *jump_table[(addr >> 24) & 0xF];

    illegal_write: return;

    external_wram_reg:
        external_wram[(addr - 0x02000000) & 0x3FFFF] = byte;
        return;
    
    internal_wram_reg:
        internal_wram[(addr - 0x03000000) & 0x7FFF] = byte;
        return;

    mapped_registers:
        switch (addr) {
        case 0x04000208:
            reg_ime = byte;
            break;
        default:
            if (addr >= 0x04000000 && addr <= 0x04000054) {
                *(ppu_mmio + (addr - 0x04000000)) = byte;
            } else {
                printf("[write] unmapped hardware register: %08X\n", addr);
                exit(1);
            }
        }
        return;

    // byte writes to pallete ram are ignored
    pallete_ram_reg: {
        uint16_t duplicated_halfword = (byte << 8) | byte;
        *(uint16_t *)(pallete_ram + (((addr - 0x05000000) & 0x3FF) & ~1)) = duplicated_halfword;
        return;
    }

    vram_reg: {
        addr = (addr - 0x06000000) & 0x1FFFF;
        if (addr >= 0x18000) addr -= 0x8000;

        // byte writes to obj vram are ignored
        if (addr >= 0x14000) return;

        uint32_t bg_vram_size = 0x10000;
        if (is_rendering_bitmap())
            bg_vram_size = 0x14000;

        // byte writes to bg vram are duplicated across the halfword
        if (addr < bg_vram_size) {
            uint16_t duplicated_halfword = (byte << 8) | byte;
            *(uint16_t *)(vram + (addr & ~1)) = duplicated_halfword;
        }
        return;
    }

    // byte writes to oam are ignored
    oam_reg: return;

    cart_ram_reg:
        printf("cart ram write unhandled\n");
        exit(1);
}
