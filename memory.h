#ifndef MEMORY_H
#define MEMORY_H

typedef struct {
    uint8_t bios[0x4000];
    uint8_t external_wram[0x4000];
    uint8_t internal_wram[0x8000];
    
    uint8_t pallete_ram[0x400];
    uint8_t vram[0x18000];
    uint8_t oam[0x400];
    uint8_t io_ram[0x400];

    uint8_t rom[0x2000000];
} Memory;

void load_bios(char *bios_file);
void load_rom(char *rom_file);

uint32_t read_word(uint32_t addr);
uint32_t read_half_word(uint32_t addr);

void write_half_word(uint32_t addr, int16_t val);

#endif
