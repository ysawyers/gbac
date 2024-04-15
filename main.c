#include <stdio.h>
#include <stdlib.h>
#include "cpu.h"

int main(int argc, char **argv) {
    if (argc <= 1) {
        fprintf(stderr, "ERROR: must provide a .gba file\n");
        exit(1);
    }

    init_GBA(argv[1], "bios.bin");
    uint16_t *frame = render_frame();
    frame = render_frame();

    // for (int i = 0; i < 160; i++) {
    //     for (int j = 0; j < 240; j++) {
    //         printf("%04X ", *(frame + (i * 160) + j));
    //     }
    //     printf("\n");
    // }

    return 0;
}
