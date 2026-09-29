#include "game_core.h"
#include "game_resources.h"
#include "game_resource_index.h"

#include <3ds.h>
#include <stdio.h>

int main(void)
{
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);

    GameCore core;
    game_core_init(&core);

    GameResources resources;
    const bool romfs_ready = R_SUCCEEDED(romfsInit());
    const bool resources_ready = romfs_ready
        && game_resources_init(&resources, "romfs:/game_data.pack");

    printf("Andor's Trail\n");
    if (resources_ready) {
        printf("Resource data ready\n");
        printf("%u documents, %u records\n",
            (unsigned int)game_resource_document_count,
            (unsigned int)game_resource_record_count);
    } else {
        printf("Resource data unavailable\n");
    }
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

    if (resources_ready) game_resources_dispose(&resources);
    if (romfs_ready) romfsExit();
    game_core_shutdown(&core);
    gfxExit();
    return 0;
}