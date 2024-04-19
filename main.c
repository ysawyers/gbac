#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <SDL.h>
#include "cpu.h"

#define SCREEN_HEIGHT 160
#define SCREEN_WIDTH  240

#define RGB_VALUE(n) (((n) << 3) | ((n) >> 2))

void sdl_render_frame(SDL_Renderer *renderer, uint16_t *frame) {
    SDL_Rect scanline_pixels[SCREEN_WIDTH * SCREEN_HEIGHT];

    SDL_RenderClear(renderer);

    for (int i = 0; i < SCREEN_HEIGHT; i++) {
        for (int j = 0; j < SCREEN_WIDTH; j++) {
            scanline_pixels[(i * 240) + j].w = 2;
            scanline_pixels[(i * 240) + j].h = 2;
            scanline_pixels[(i * 240) + j].x = j * 2;
            scanline_pixels[(i * 240) + j].y = i * 2;
        }
    }

    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        SDL_SetRenderDrawColor(renderer, RGB_VALUE(frame[i] & 0x1F), RGB_VALUE((frame[i] >> 5) & 0x1F), RGB_VALUE(((frame[i] >> 10) & 0x1F)), 255);
        SDL_RenderFillRect(renderer, &scanline_pixels[i]);
    }

    SDL_RenderPresent(renderer);
}

int main(int argc, char **argv) {
    if (argc <= 1) {
        fprintf(stderr, "ERROR: must provide a .gba file\n");
        exit(1);
    }

    init_GBA(argv[1], "bios.bin");

    SDL_Window* window = NULL;
    SDL_Renderer *renderer;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
        exit(1);
    }

    window = SDL_CreateWindow("gbac", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH * 2, SCREEN_HEIGHT * 2, SDL_WINDOW_SHOWN);
    if(window == NULL) {
        printf( "SDL_CreateWindow Error: %s\n", SDL_GetError());
        exit(1);
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == NULL) {
        fprintf(stderr, "SDL_CreateRenderer Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_Event event; 
    bool running = true;

    while(running)
    {
        while(SDL_PollEvent(&event))
        {
            switch (event.type) {
            case SDL_QUIT:
                running = false;
                break;
            }
        }

        // TODO: lock at ~60 FPS
        sdl_render_frame(renderer, compute_frame());
    }

    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
