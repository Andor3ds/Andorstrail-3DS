#include "game_core.h"
#include "game_resources.h"
#include "map_renderer.hpp"

#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <string>
#include <vector>

int main(void)
{
    gfxInitDefault();
    consoleInit(GFX_BOTTOM, NULL);
    hidSetRepeatParameters(15, 10);
    GameCore core;
    game_core_init(&core);
    const char *save_path = "sdmc:/3ds/andorstrail-save.dat";
    const GameCoreLoadResult save_load_result = game_core_load(&core, save_path);
    const bool save_permitted = save_load_result != GAME_CORE_LOAD_ERROR;

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
            &resources, &core, error
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
                if (renderer.has_conversation()) {
                    if (keys_down & KEY_UP) renderer.move_conversation_selection(-1);
                    if (keys_down & KEY_DOWN) renderer.move_conversation_selection(1);
                    if (keys_down & KEY_A) {
                        std::string dialogue_error;
                        if (!renderer.choose_conversation_reply(dialogue_error)) {
                            snprintf(last_input, sizeof(last_input), "DIALOGUE ERROR");
                        } else if (save_permitted && !game_core_save(&core, save_path)) {
                            snprintf(last_input, sizeof(last_input), "SAVE FAILED");
                        } else {
                            snprintf(last_input, sizeof(last_input), "REPLY SELECTED");
                        }
                    }
                    if (keys_down & KEY_B) {
                        renderer.dismiss_modal_interaction();
                        snprintf(last_input, sizeof(last_input), "DIALOGUE CLOSED");
                    }
                } else if (renderer.has_shop()) {
                    if (keys_down & KEY_A) {
                        std::string shop_error;
                        if (!renderer.buy_shop_bread(shop_error)) {
                            snprintf(last_input, sizeof(last_input), "PURCHASE FAILED");
                        } else if (save_permitted && !game_core_save(&core, save_path)) {
                            snprintf(last_input, sizeof(last_input), "SAVE FAILED");
                        } else {
                            snprintf(last_input, sizeof(last_input), "BOUGHT BREAD");
                        }
                    }
                    if (keys_down & KEY_B) {
                        renderer.dismiss_modal_interaction();
                        snprintf(last_input, sizeof(last_input), "SHOP CLOSED");
                    }
                } else if (keys_down & (KEY_A | KEY_B)) {
                    renderer.dismiss_modal_interaction();
                    snprintf(last_input, sizeof(last_input), "DISMISSED");
                }
            } else {
                int move_x = 0;
                int move_y = 0;
                const char *direction = NULL;
                if (keys_down & KEY_A) {
                    renderer.interact();
                    snprintf(last_input, sizeof(last_input), "INTERACT");
                    if (save_permitted && !game_core_save(&core, save_path)) {
                        snprintf(last_input, sizeof(last_input), "SAVE FAILED");
                    }
                } else if (keys_down & KEY_X) {
                    if (renderer.attack_nearby_rat()) {
                        snprintf(last_input, sizeof(last_input), "ATTACKED RAT");
                        if (save_permitted && !game_core_save(&core, save_path)) {
                            snprintf(last_input, sizeof(last_input), "SAVE FAILED");
                        }
                    } else {
                        snprintf(last_input, sizeof(last_input), "NO RAT IN RANGE");
                    }
                } else if (keys_down & KEY_LEFT) { move_x = -1; direction = "LEFT"; }
                else if (keys_down & KEY_RIGHT) { move_x = 1; direction = "RIGHT"; }
                else if (keys_down & KEY_UP) { move_y = -1; direction = "UP"; }
                else if (keys_down & KEY_DOWN) { move_y = 1; direction = "DOWN"; }
                if (direction != NULL) {
                    const bool moved = renderer.move_player(move_x, move_y);
                    snprintf(last_input, sizeof(last_input), "%s %s", direction,
                        moved ? "OK" : "BLOCKED/DEFERRED");
                    if (save_permitted && !game_core_save(&core, save_path)) {
                        snprintf(last_input, sizeof(last_input), "SAVE FAILED");
                    }
                }
            }

            consoleClear();
            if (renderer.has_modal_interaction()) {
                printf("%s\n\n", renderer.interaction_message().c_str());
                if (renderer.has_conversation()) {
                    const std::vector<andortrail::dialogue::Reply> &replies =
                        renderer.conversation_replies();
                    for (std::size_t index = 0; index < replies.size(); ++index) {
                        printf("%s%s\n",
                            index == renderer.selected_conversation_reply() ? "> " : "  ",
                            replies[index].text.c_str());
                    }
                    printf("\nUp/Down: select   A: choose   B: close");
                } else if (renderer.has_shop()) {
                    printf("A: buy bread   B: close");
                } else {
                    printf("A/B: close");
                }
            } else {
                printf("D-pad: move   A: talk/interact\n");
                printf("X: attack training rats   START: exit\n\n");
                const char *hint = renderer.nearby_interaction_hint();
                if (hint[0] != '\0') printf("%s\n\n", hint);
                printf("Mikhail quests: bread %d   rats %d\n",
                    game_core_quest_progress(&core, "mikhail_bread"),
                    game_core_quest_progress(&core, "mikhail_rats"));
                printf("Health: %d/%d   Gold: %d",
                    core.health, GAME_CORE_MAX_HEALTH, core.gold);
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
                    "MAP %s\nPLAYER %d,%d\nHP %d/%d\nGOLD %d\nQUEST B/R %d/%d"
                    "\nCAMERA %d,%d\nWALKABLE %s\nINPUT %s%s%s%s",
                    renderer.map_id(), player.x, player.y, core.health,
                    GAME_CORE_MAX_HEALTH, core.gold,
                    game_core_quest_progress(&core, "mikhail_bread"),
                    game_core_quest_progress(&core, "mikhail_rats"),
                    camera.camera_x, camera.camera_y,
                    renderer.player_walkable() ? "YES" : "NO", last_input,
                    renderer.nearby_interaction_hint()[0] != '\0' ? "\n" : "",
                    renderer.nearby_interaction_hint(),
                    save_load_result == GAME_CORE_LOAD_ERROR ? "\nSAVE LOAD FAILED" : "");
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
                    std::string text = renderer.interaction_message();
                    if (renderer.has_conversation()) {
                        const std::vector<andortrail::dialogue::Reply> &replies =
                            renderer.conversation_replies();
                        for (std::size_t index = 0; index < replies.size(); ++index) {
                            text += "\n";
                            text += index == renderer.selected_conversation_reply() ? "> " : "  ";
                            text += replies[index].text;
                        }
                        text += "\n\nUp/Down: select  A: choose  B: close";
                    } else if (renderer.has_shop()) {
                        text += "\n\nA: buy bread  B: close";
                    } else {
                        text += "\n\nA/B: close";
                    }
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

    if (save_permitted && !game_core_save(&core, save_path)) {
        consoleInit(GFX_TOP, NULL);
        printf("Andor's Trail\nSave failed:\n%s\n", save_path);
        printf("Press START to exit.\n");
        while (aptMainLoop()) {
            hidScanInput();
            if (hidKeysDown() & KEY_START) break;
            gspWaitForVBlank();
        }
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