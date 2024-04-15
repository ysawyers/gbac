// #define DEBUG_MODE

#ifdef DEBUG_MODE
#define DEBUG_PRINT(x) printf x;
#else
#define DEBUG_PRINT(x) {};
#endif

#ifndef CPU_H
#define CPU_H

void init_GBA(char *rom_file, char *bios_file);
uint16_t* render_frame(void);

#endif
