#include "game_core.h"
#include "game_resources.h"
#include "map_renderer.hpp"

#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <string>

int main(void)
{
    gfxInitDefault();
    hidSetRepeatParameters(15, 10);
    GameCore core;
    game_core_init(&core);

    const bool romfs_ready = R_SUCCEEDED(romfsInit());
    GameResources resources = {};
    const bool resources_ready = romfs_ready
        && game_resources_init(&resources, "romfs:/game_data.pack");
    bool c3d_ready = false;
    bool c2d_ready = false;
    bool cfgu_ready = false;
    bool renderer_ready = false;
    C3D_RenderTarget *top = NULL;
    C2D_Font debug_font = NULL;
    C2D_TextBuf debug_buffer = NULL;
    andortrail::platform3ds::MapRenderer renderer;
    std::string error;

    if (romfs_ready && C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        c3d_ready = true;
    }
    if (c3d_ready && C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
        c2d_ready = true;
        C2D_Prepare();
        top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
        renderer_ready = top != NULL && resources_ready && renderer.initialize(
            "romfs:/maps", "home", "romfs:/gfx/tilesets.list", "romfs:/gfx",
            &resources, error
        );
        if (!resources_ready) error = "original game resource pack could not be opened";
        if (renderer_ready && R_SUCCEEDED(cfguInit())) {
            cfgu_ready = true;
            debug_font = C2D_FontLoadSystem(CFG_REGION_USA);
            debug_buffer = C2D_TextBufNew(1024);
        }
    }

    if (!renderer_ready) {
        consoleInit(GFX_TOP, NULL);
        printf("Andor's Trail\nMap startup failed:\n%s\n", error.c_str());
        printf("Press START to exit.\n");
    }

    char last_input[32] = "none";
    while (aptMainLoop()) {
        hidScanInput();
        const u32 keys_down = hidKeysDownRepeat();
        if (keys_down & KEY_START) break;
        if (renderer_ready) {
            if (renderer.has_modal_interaction()) {
                if (keys_down & (KEY_A | KEY_B)) {
                    renderer.dismiss_modal_interaction();
                    snprintf(last_input, sizeof(last_input), "DISMISSED");
                }
            } else {
                int move_x = 0;
                int move_y = 0;
                const char *direction = NULL;
                if (keys_down & KEY_LEFT) { move_x = -1; direction = "LEFT"; }
                else if (keys_down & KEY_RIGHT) { move_x = 1; direction = "RIGHT"; }
                else if (keys_down & KEY_UP) { move_y = -1; direction = "UP"; }
                else if (keys_down & KEY_DOWN) { move_y = 1; direction = "DOWN"; }
                if (direction != NULL) {
                    const bool moved = renderer.move_player(move_x, move_y);
                    snprintf(last_input, sizeof(last_input), "%s %s", direction,
                        moved ? "OK" : "BLOCKED/DEFERRED");
                }
            }

            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
            C2D_TargetClear(top, C2D_Color32(0, 0, 0, 255));
            C2D_SceneBegin(top);
            C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
            renderer.draw(400, 240);
            if (debug_font != NULL && debug_buffer != NULL) {
                const andortrail::map::GridPosition player = renderer.player_position();
                const andortrail::map::ViewTransform camera = renderer.camera(400, 240);
                char debug_status[256];
                snprintf(debug_status, sizeof(debug_status),
                    "MAP %s\nPLAYER %d,%d\nCAMERA %d,%d\nWALKABLE %s\nINPUT %s",
                    renderer.map_id(), player.x, player.y, camera.camera_x, camera.camera_y,
                    renderer.player_walkable() ? "YES" : "NO", last_input);
                C2D_Text debug_text;
                C2D_TextBufClear(debug_buffer);
                C2D_TextFontParse(&debug_text, debug_font, debug_buffer, debug_status);
                C2D_TextOptimize(&debug_text);
                C2D_DrawText(&debug_text, C2D_WithColor, 7.0f, 7.0f, 0.0f,
                    0.42f, 0.42f, C2D_Color32(0, 0, 0, 255));
                C2D_DrawText(&debug_text, C2D_WithColor, 6.0f, 6.0f, 0.0f,
                    0.42f, 0.42f, C2D_Color32(255, 255, 0, 255));
                if (renderer.has_modal_interaction()) {
                    C2D_Text interaction_text;
                    std::string text = renderer.interaction_message() + "\n\nA/B: close";
                    C2D_TextFontParse(&interaction_text, debug_font, debug_buffer, text.c_str());
                    C2D_TextOptimize(&interaction_text);
                    C2D_DrawText(&interaction_text, C2D_WithColor | C2D_WordWrap,
                        8.0f, 112.0f, 0.0f, 0.42f, 0.42f,
                        C2D_Color32(255, 255, 255, 255), 380.0f);
                }
            }
            C3D_FrameEnd(0);
        } else {
            gspWaitForVBlank();
        }
        game_core_update(&core);
    }

    renderer.dispose();
    if (debug_buffer != NULL) C2D_TextBufDelete(debug_buffer);
    if (debug_font != NULL) C2D_FontFree(debug_font);
    if (top != NULL) C3D_RenderTargetDelete(top);
    if (c2d_ready) C2D_Fini();
    if (c3d_ready) C3D_Fini();
    if (cfgu_ready) cfguExit();
    if (resources_ready) game_resources_dispose(&resources);
    if (romfs_ready) romfsExit();
    game_core_shutdown(&core);
    gfxExit();
    return 0;
}