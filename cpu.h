// #define DEBUG_MODE

#ifdef DEBUG_MODE
#define DEBUG_PRINT(x) printf x;
#else
#define DEBUG_PRINT(x) {};
#endif

#ifndef CPU_H
#define CPU_H

void start(char *rom_file, char *bios_file);

#endif
