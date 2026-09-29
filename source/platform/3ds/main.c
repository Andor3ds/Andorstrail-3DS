#include "game_core.h"

#include <3ds.h>
#include <stdio.h>

int main(void)
{
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);

    GameCore core;
    game_core_init(&core);

    printf("Andor's Trail\n");
    printf("Native Nintendo 3DS foundation\n");
    printf("\nPress START to exit.\n");

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) {
            break;
        }
        game_core_update(&core);
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    game_core_shutdown(&core);
    gfxExit();
    return 0;
}