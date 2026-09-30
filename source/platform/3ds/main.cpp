#include "game_core.h"
#include "map_renderer.hpp"

#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <string>

int main(void)
{
    gfxInitDefault();
    GameCore core;
    game_core_init(&core);

    const bool romfs_ready = R_SUCCEEDED(romfsInit());
    bool c3d_ready = false;
    bool c2d_ready = false;
    bool renderer_ready = false;
    C3D_RenderTarget *top = NULL;
    andortrail::platform3ds::MapRenderer renderer;
    std::string error;

    if (romfs_ready && C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        c3d_ready = true;
    }
    if (c3d_ready && C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
        c2d_ready = true;
        C2D_Prepare();
        top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
        renderer_ready = top != NULL && renderer.initialize(
            "romfs:/maps/home.tmx", "romfs:/gfx/tilesets.list", "romfs:/gfx", error
        );
    }

    if (!renderer_ready) {
        consoleInit(GFX_TOP, NULL);
        printf("Andor's Trail\nMap startup failed:\n%s\n", error.c_str());
        printf("Press START to exit.\n");
    }

    while (aptMainLoop()) {
        hidScanInput();
        const u32 keys_down = hidKeysDownRepeat();
        if (keys_down & KEY_START) break;
        if (renderer_ready) {
            if (keys_down & KEY_LEFT) renderer.move_player(-1, 0);
            else if (keys_down & KEY_RIGHT) renderer.move_player(1, 0);
            else if (keys_down & KEY_UP) renderer.move_player(0, -1);
            else if (keys_down & KEY_DOWN) renderer.move_player(0, 1);

            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
            C2D_TargetClear(top, C2D_Color32(0, 0, 0, 255));
            C2D_SceneBegin(top);
            renderer.draw(400, 240);
            C3D_FrameEnd(0);
        } else {
            gspWaitForVBlank();
        }
        game_core_update(&core);
    }

    renderer.dispose();
    if (top != NULL) C3D_RenderTargetDelete(top);
    if (c2d_ready) C2D_Fini();
    if (c3d_ready) C3D_Fini();
    if (romfs_ready) romfsExit();
    game_core_shutdown(&core);
    gfxExit();
    return 0;
}