// #define DEBUG_MODE

#ifdef DEBUG_MODE
#define DEBUG_PRINT(x) printf x;
#else
#define DEBUG_PRINT(x) {};
#endif

#ifndef CPU_H
#define CPU_H

// initializes GBA components
void init_GBA(const char *rom_file, const char *bios_file);

uint16_t* compute_frame(uint16_t key_input);

#endif
