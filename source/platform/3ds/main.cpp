#include "game_core.h"
#include "game_items.hpp"
#include "game_resources.h"
#include "hud_view.hpp"
#include "map_renderer.hpp"

#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <string>
#include <vector>

int main(void)
{
    gfxInitDefault();
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
    const bool seed_starting_inventory = save_load_result == GAME_CORE_LOAD_NOT_FOUND
        || (save_load_result == GAME_CORE_LOAD_OK && core.inventory_count == 0
            && core.gold == 0 && core.quest_count == 0 && core.level == 1
            && core.total_experience == 1);
    bool start_items_ready = true;
    std::string error;
    if (resources_ready && seed_starting_inventory
            && !andortrail::items::apply_drop_list(&resources, &core, "startitems", error)) {
        start_items_ready = false;
        error = "Starting inventory could not be initialized: " + error;
    }
    bool c3d_ready = false;
    bool c2d_ready = false;
    bool cfgu_ready = false;
    bool renderer_ready = false;
    bool hud_ready = false;
    C3D_RenderTarget *top = NULL;
    C3D_RenderTarget *bottom = NULL;
    C2D_Font system_font = NULL;
    C2D_TextBuf text_buffer = NULL;
    andortrail::platform3ds::MapRenderer renderer;
    andortrail::platform3ds::HudView hud;

    if (romfs_ready && C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        c3d_ready = true;
    }
    if (c3d_ready && C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
        c2d_ready = true;
        C2D_Prepare();
        top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
        bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
        renderer_ready = top != NULL && bottom != NULL && resources_ready && start_items_ready
            && renderer.initialize(
            "romfs:/maps", "home", "romfs:/gfx/tilesets.list", "romfs:/gfx",
            &resources, &core, error
        );
        if (renderer_ready) {
            if (R_SUCCEEDED(cfguInit())) {
                cfgu_ready = true;
                system_font = C2D_FontLoadSystem(CFG_REGION_USA);
            }
            text_buffer = C2D_TextBufNew(8192);
            if (text_buffer == NULL) error = "HUD text buffer could not be allocated";
            else hud_ready = hud.initialize(system_font, text_buffer, &resources,
                &core, &renderer, error);
            renderer_ready = renderer_ready && hud_ready;
        }
    }
    if (!renderer_ready && error.empty()) {
        if (!c3d_ready) error = "Citro3D could not be initialized";
        else if (!c2d_ready) error = "Citro2D could not be initialized";
        else if (top == NULL) error = "top screen render target could not be created";
        else if (bottom == NULL) error = "bottom screen render target could not be created";
        else if (!resources_ready) error = "original game resource pack could not be opened";
        else error = "map or HUD initialization failed";
    }
    renderer_ready = renderer_ready && hud_ready;

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
            hud.set_combat_message(renderer.interaction_message());
            andortrail::platform3ds::HudAction hud_action =
                andortrail::platform3ds::HUD_ACTION_NONE;
            if (hidKeysDown() & KEY_TOUCH) {
                touchPosition touch;
                hidTouchRead(&touch);
                hud_action = hud.handle_touch(touch.px, touch.py);
            }
            const bool hud_consumed = hud.handle_keys(keys_down, hud_action);
            if (hud_action == andortrail::platform3ds::HUD_ACTION_SAVE
                    || hud_action == andortrail::platform3ds::HUD_ACTION_STATE_CHANGED) {
                if (!save_permitted || !game_core_save(&core, save_path)) {
                    hud.set_notice("Save failed.");
                } else {
                    hud.set_notice(hud_action == andortrail::platform3ds::HUD_ACTION_SAVE
                        ? "Game saved." : "Level-up saved.");
                }
            }

            if (!hud_consumed && renderer.has_modal_interaction()) {
                if (renderer.has_conversation()) {
                    if (keys_down & KEY_UP) renderer.move_conversation_selection(-1);
                    if (keys_down & KEY_DOWN) renderer.move_conversation_selection(1);
                    if (keys_down & KEY_A) {
                        std::string dialogue_error;
                        if (!renderer.choose_conversation_reply(dialogue_error)) {
                            hud.set_notice(dialogue_error.c_str());
                        } else if (save_permitted && !game_core_save(&core, save_path)) {
                            hud.set_notice("Save failed.");
                        }
                    }
                    if (keys_down & KEY_B) {
                        renderer.dismiss_modal_interaction();
                    }
                } else if (renderer.has_shop()) {
                    if (keys_down & KEY_A) {
                        std::string shop_error;
                        if (!renderer.buy_shop_bread(shop_error)) {
                            hud.set_notice(shop_error.c_str());
                        } else if (save_permitted && !game_core_save(&core, save_path)) {
                            hud.set_notice("Save failed.");
                        }
                    }
                    if (keys_down & KEY_B) {
                        renderer.dismiss_modal_interaction();
                    }
                } else if (keys_down & (KEY_A | KEY_B)) {
                    renderer.dismiss_modal_interaction();
                }
            } else {
                int move_x = 0;
                int move_y = 0;
                if (keys_down & KEY_A) {
                    renderer.interact();
                    if (save_permitted && !game_core_save(&core, save_path)) {
                        hud.set_notice("Save failed.");
                    }
                } else if (keys_down & KEY_X) {
                    if (renderer.attack_nearby_rat()) {
                        if (save_permitted && !game_core_save(&core, save_path)) {
                            hud.set_notice("Save failed.");
                        }
                    }
                } else if (keys_down & KEY_LEFT) move_x = -1;
                else if (keys_down & KEY_RIGHT) move_x = 1;
                else if (keys_down & KEY_UP) move_y = -1;
                else if (keys_down & KEY_DOWN) move_y = 1;
                if (move_x != 0 || move_y != 0) {
                    renderer.move_player(move_x, move_y);
                    if (save_permitted && !game_core_save(&core, save_path)) {
                        hud.set_notice("Save failed.");
                    }
                }
            }

            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
            C2D_TargetClear(top, C2D_Color32(0, 0, 0, 255));
            C2D_TargetClear(bottom, C2D_Color32(19, 20, 23, 255));
            C2D_TextBufClear(text_buffer);
            C2D_SceneBegin(top);
            C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
            renderer.draw(400, 240);
            if (renderer.has_modal_interaction()) {
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
                hud.draw_interaction(text.c_str());
            }
            C2D_SceneBegin(bottom);
            hud.draw();
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
    hud.dispose();
    if (text_buffer != NULL) C2D_TextBufDelete(text_buffer);
    if (system_font != NULL) C2D_FontFree(system_font);
    if (top != NULL) C3D_RenderTargetDelete(top);
    if (bottom != NULL) C3D_RenderTargetDelete(bottom);
    if (c2d_ready) C2D_Fini();
    if (c3d_ready) C3D_Fini();
    if (cfgu_ready) cfguExit();
    if (resources_ready) game_resources_dispose(&resources);
    if (romfs_ready) romfsExit();
    game_core_shutdown(&core);
    gfxExit();
    return 0;
}