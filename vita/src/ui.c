// UI Entry Point - Bridges vitaki-fork to VitaRPS5 UI
// This file provides the draw_ui() function expected by main.c

#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <vita2d.h>
#include <stdio.h>

#include "ui/ui_core.h"

// Main UI entry point called by main.c
void draw_ui(void) {
    printf("Initializing VitaRPS5 UI system...\n");

    if (ui_core_init() != 0) {
        printf("Failed to initialize VitaRPS5 UI\n");
        return;
    }

    SceCtrlData pad;

    printf("VitaRPS5 UI initialized, starting main loop...\n");

    // Main UI loop
    while (true) {
        // Read controller input
        if (!sceCtrlReadBufferPositive(0, &pad, 1)) {
            printf("Failed to read controller\n");
            continue;
        }

        // Update UI logic
        ui_core_update(&pad);

        // Render UI
        ui_core_render();

        // Frame delay (~60 FPS)
        sceKernelDelayThread(16667);
    }

    // Cleanup (unreachable)
    ui_core_cleanup();
}