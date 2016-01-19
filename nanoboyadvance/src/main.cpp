/*
* Copyright (C) 2015 Frederic Meyer
*
* This file is part of nanoboyadvance.
*
* nanoboyadvance is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 2 of the License, or
* (at your option) any later version.
*
* nanoboyadvance is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with nanoboyadvance. If not, see <http://www.gnu.org/licenses/>.
*/

#include "core/arm7.h"
#include "core/gba_memory.h"
#include "core/gba_video.h"
#include "common/log.h"
#include "cmdline.h"
#include "debugger.h"
#include <SDL/SDL.h>
#include <png.h>
#include <iostream>
#undef main

using namespace std;
using namespace NanoboyAdvance;

// Emulation related globals
GBAMemory* memory;
ARM7* arm;
CmdLine* cmdline;

// SDL related globals
SDL_Surface* window;
uint32_t* buffer;
int window_width;
int window_height;
int frameskip_counter;

#define plotpixel(x,y,c) buffer[(y) * window_width + (x)] = c;
void setpixel(int x, int y, int color)
{
    int scale_x = window_width / 240;
    int scale_y = window_height / 160;

    // Plot the neccessary amount of pixels
    for (int i = 0; i < scale_x; i++)
    {
        for (int j = 0; j < scale_y; j++)
        {
            plotpixel(x * scale_x + i, y * scale_y + j, color);
        }
    }
}

// Read key input from SDL and feed it into the GBA
void schedule_keyinput()
{
    u8* kb_state = SDL_GetKeyState(NULL);
    ushort joypad = 0;

    // Check which keys are pressed and set corresponding bits (0 = pressed, 1 = vice versa)
    joypad |= kb_state[SDLK_y] ? 0 : 1;
    joypad |= kb_state[SDLK_x] ? 0 : (1 << 1);
    joypad |= kb_state[SDLK_BACKSPACE] ? 0 : (1 << 2);
    joypad |= kb_state[SDLK_RETURN] ? 0 : (1 << 3);
    joypad |= kb_state[SDLK_RIGHT] ? 0 : (1 << 4);
    joypad |= kb_state[SDLK_LEFT] ? 0 : (1 << 5);
    joypad |= kb_state[SDLK_UP] ? 0 : (1 << 6);
    joypad |= kb_state[SDLK_DOWN] ? 0 : (1 << 7);
    joypad |= kb_state[SDLK_s] ? 0 : (1 << 8);
    joypad |= kb_state[SDLK_a] ? 0 : (1 << 9);

    // Write the generated value to IO ram
    memory->gba_io->keyinput = joypad;
}

// Schedules the GBA and generates exactly one frame
void schedule_frame()
{
    for (int i = 0; i < 280896; i++)
    {
        // Run the hardware's components
        arm->Step();
        memory->timer->Step();
        memory->video->Step();
        memory->dma->Step();

        // Raise an IRQ if neccessary
        if (memory->gba_io->ime != 0 && memory->gba_io->if_ != 0)
        {
            arm->FireIRQ();
        }

        // Copy the rendered line (if any) to SDLs pixel buffer
        if (memory->video->render_scanline)
        {
            int y = memory->gba_io->vcount;
            for (int x = 0; x < 240; x++)
            {
                setpixel(x, y, memory->video->buffer[y * 240 + x]);
            }
        }
    }
}

// Creates a new SDL window with given size
void create_window(int width, int height)
{
    // Initialize SDL
    if (SDL_Init(SDL_INIT_EVERYTHING) != 0)
    {
        printf("SDL_Init Error: %s\n", SDL_GetError());
        SDL_Quit();
    }

    // Create surface / window
    window = SDL_SetVideoMode(width, height, 32, SDL_SWSURFACE);

    // Check for errors during window creation
    if (window == NULL)
    {
        printf("SDL_SetVideoMode Error: %s\n", SDL_GetError());
        SDL_Quit();
    }

    // Get pixelbuffer
    buffer = (uint32_t*)window->pixels;

    // Set window caption
    SDL_WM_SetCaption("NanoboyAdvance", "NanoboyAdvance");

    // Set width and height globals
    window_width = width;
    window_height = height;
}

void create_screenshot() 
{
    FILE* fp = fopen("screenshot.png", "wb");
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;
    png_byte** rows = NULL;
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    info_ptr = png_create_info_struct(png_ptr);
    png_set_IHDR(png_ptr,
                 info_ptr,
                 window_width,
                 window_height,
                 8,
                 PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    rows = (png_byte**)png_malloc(png_ptr, window_height * sizeof(png_byte*));
    for (int y = 0; y < window_height; y++)
    {
        png_byte* row = (png_byte*)png_malloc(png_ptr, window_width * sizeof(u32));
        rows[y] = row;
        for (int x = 0; x < window_width; x++) 
        {
            u32 argb = buffer[y * window_width + x];
            *row++ = (argb >> 16) & 0xFF;
            *row++ = (argb >> 8) & 0xFF;
            *row++ = argb & 0xFF;
            *row++ = (argb >> 24) & 0xFF;
        }
    }
    png_init_io(png_ptr, fp);
    png_set_rows(png_ptr, info_ptr, rows);
    png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);
    for (int y = 0; y < window_height; y++) 
    {
        png_free(png_ptr, rows[y]);
    }    
    png_free(png_ptr, rows);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);
}

// This is our main function / entry point. It gets called when the emulator is started.
int main(int argc, char** argv)
{
    SDL_Event event;
    bool running = true;
    frameskip_counter = 0;

    if (argc > 1)
    {
        cmdline = parse_parameters(argc, argv);

        // Check for parsing errors
        if (cmdline == NULL)
        {
            usage();
            return 0;
        }

        // Initialize memory and ARM interpreter core
        memory = new GBAMemory(cmdline->bios_file, cmdline->rom_file);
        arm = new ARM7(memory, !cmdline->use_bios);
        
        // Append debugger if desired
        if (cmdline->debug)
        {
            debugger_attach(arm, memory);
        }
    }
    else
    {
        usage();
        return 0;
    }

    // Initialize SDL and create window
    create_window(240 * cmdline->scale, 160 * cmdline->scale);

    // Run debugger immediatly if specified so
    if (cmdline->debug && cmdline->debug_immediatly)
    {
        debugger_shell();
    }
    
    // Main loop
    while (running)
    {
        u8* kb_state = SDL_GetKeyState(NULL);
        
        // Check if cli debugger is requested and run if needed
        if (cmdline->debug && kb_state[SDLK_F11])
        {
            debugger_shell();
        }

        // Dump screenshot on F10
        if (kb_state[SDLK_F10]) create_screenshot();
        
        // Feed keyboard input and generate exactly one frame
        schedule_keyinput();
        schedule_frame();

        // We must process SDL events
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
            {
                running = false;
            }
        }

        // Switch buffers
        SDL_Flip(window);
    }

    SDL_FreeSurface(window);
    return 0;
}
