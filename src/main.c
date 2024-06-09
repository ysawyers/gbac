#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <SDL.h>
#include "cpu.h"

#define SCREEN_HEIGHT 160
#define SCREEN_WIDTH  240
#define PIXEL_SIZE 3

#define RGB_VALUE(n) (((n) << 3) | ((n) >> 2))

void sdl_render_frame(SDL_Renderer *renderer, uint16_t *frame) {
    SDL_RenderClear(renderer); // clear the previous frame

    SDL_Rect lcd[SCREEN_WIDTH * SCREEN_HEIGHT];

    for (int i = 0; i < SCREEN_HEIGHT; i++) {
        for (int j = 0; j < SCREEN_WIDTH; j++) {
            lcd[(i * 240) + j].w = PIXEL_SIZE;
            lcd[(i * 240) + j].h = PIXEL_SIZE;
            lcd[(i * 240) + j].x = j * PIXEL_SIZE;
            lcd[(i * 240) + j].y = i * PIXEL_SIZE;
        }
    }

    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        SDL_SetRenderDrawColor(renderer, RGB_VALUE(frame[i] & 0x1F), RGB_VALUE((frame[i] >> 5) & 0x1F), RGB_VALUE(((frame[i] >> 10) & 0x1F)), 255);
        SDL_RenderFillRect(renderer, &lcd[i]);
    }

    SDL_RenderPresent(renderer); // render the new frame
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

    window = SDL_CreateWindow("gbac", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH * PIXEL_SIZE, SCREEN_HEIGHT * PIXEL_SIZE, SDL_WINDOW_SHOWN);
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

    time_t start_t, end_t;
    double diff_t;

    while(running)
    {
        uint16_t key_input = 0xFFFF;

        while(SDL_PollEvent(&event))
        {
            switch (event.type) {
            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) {
                case SDLK_q: // A
                    key_input = ~1 & key_input;
                    break;
                case SDLK_w: // B
                    key_input = ~(1 << 1) & key_input;
                    break;
                case SDLK_RETURN: // START
                    key_input = ~(1 << 3) & key_input;
                    break;
                case SDLK_BACKSPACE: // SELECT
                    key_input = ~(1 << 2) & key_input;
                    break;
                case SDLK_RIGHT:
                    key_input = ~(1 << 4) & key_input;
                    break;
                case SDLK_LEFT:
                    key_input = ~(1 << 5) & key_input;
                    break;
                case SDLK_UP:
                    key_input = ~(1 << 6) & key_input;
                    break;
                case SDLK_DOWN:
                    key_input = ~(1 << 7) & key_input;
                    break;
                }
                break;
            case SDL_QUIT:
                running = false;
                break;
            }
        }
        
        sdl_render_frame(renderer, compute_frame(key_input));
    }

    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
