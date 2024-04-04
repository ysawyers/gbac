#include <stdio.h>
#include <stdlib.h>
#include "cpu.h"

int main(int argc, char **argv) {
    if (argc <= 1) {
        fprintf(stderr, "ERROR: must provide a .gba file\n");
        exit(1);
    }

    start(argv[1], "bios.bin");

    return 0;
}
