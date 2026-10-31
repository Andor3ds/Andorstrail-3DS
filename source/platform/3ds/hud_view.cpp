#include "hud_view.hpp"
#include "game_items.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace andortrail {
namespace platform3ds {
namespace {

const char *const UI_IMAGE_NAMES[] = {
    "ui_bar_background.9.png",
    "ui_icon_combat.png",
    "ui_icon_equipment.png",
    "ui_icon_map.png",
    "ui_icon_quest.png",
    "ui_icon_save.png",
    "ui_icon_skill.png",
    "ui_quickslots.png",
    "ui_red_foreground.9.png",
    "ui_selections.png",
    "ui_yellow_foreground.9.png",
    "ui_icon_coins.png",
    "equip_weapon.png",
    "equip_shield.png",
    "equip_body.png",
    "equip_feet.png",
    "equip_hand.png",
    "equip_head.png",
    "equip_neck.png",
    "equip_ring.png",
    "items_weapons.png",
    "items_armours.png",
    "items_jewelry.png",
    "items_consumables.png",
    "items_misc.png",
    "ui_blue_stdframe.9.png",
    "ui_blue_tabframe.9.png",
    "ui_blue_tabwidget_enabled_selected.9.png",
    "ui_blue_tabwidget_enabled_unselected.9.png",
    "ui_blue_tabframe_topoverlay.9.png",
    "char_hero_large.png",
    "equip_square.png"
};

const u32 PANEL_BORDER = C2D_Color32(77, 102, 132, 255);
const u32 PANEL_BACKGROUND = C2D_Color32(15, 32, 51, 255);
const u32 PANEL_TEXT = C2D_Color32(224, 231, 238, 255);

Tex3DS_SubTexture crop_tile(const C2D_Image &image, unsigned int tile_index,
    unsigned int tile_count)
{
    Tex3DS_SubTexture result = *image.subtex;
    const unsigned int tile_width = result.width / tile_count;
    const float u_range = result.right - result.left;
    const unsigned int tile_x = tile_index % tile_count;
    result.left += (tile_x * tile_width / static_cast<float>(result.width)) * u_range;
    result.right = result.left + (tile_width / static_cast<float>(result.width)) * u_range;
    result.width = tile_width;
    return result;
}

Tex3DS_SubTexture crop_item_icon(const C2D_Image &image, unsigned int tile_index)
{
    const unsigned int columns = image.subtex->width / 32;
    Tex3DS_SubTexture result = *image.subtex;
    if (columns == 0) return result;
    const unsigned int tile_x = tile_index % columns;
    const unsigned int tile_y = tile_index / columns;
    const float u_range = result.right - result.left;
    const float v_range = result.bottom - result.top;
    result.left += (tile_x * 32.0f / result.width) * u_range;
    result.right = result.left + (32.0f / result.width) * u_range;
    result.top += (tile_y * 32.0f / result.height) * v_range;
    result.bottom = result.top + (32.0f / result.height) * v_range;
    result.width = 32;
    result.height = 32;
    return result;
}

Tex3DS_SubTexture crop_nine_patch(const C2D_Image &image)
{
    Tex3DS_SubTexture result = *image.subtex;
    const float u_step = (result.right - result.left) / result.width;
    const float v_step = (result.bottom - result.top) / result.height;
    result.left += u_step;
    result.right -= u_step;
    result.top += v_step;
    result.bottom -= v_step;
    result.width -= 2;
    result.height -= 2;
    return result;
}

Tex3DS_SubTexture crop_subtexture(const Tex3DS_SubTexture &source,
    unsigned int x, unsigned int y, unsigned int width, unsigned int height)
{
    Tex3DS_SubTexture result = source;
    const float u_range = source.right - source.left;
    const float v_range = source.bottom - source.top;
    result.left += (x / static_cast<float>(source.width)) * u_range;
    result.right = result.left + (width / static_cast<float>(source.width)) * u_range;
    result.top += (y / static_cast<float>(source.height)) * v_range;
    result.bottom = result.top + (height / static_cast<float>(source.height)) * v_range;
    result.width = width;
    result.height = height;
    return result;
}

}

HudView::HudView()
    : core_(NULL), renderer_(NULL), font_(NULL), text_buffer_(NULL),
      resources_(NULL), screen_(SCREEN_STATUS), hero_tab_(0), selected_level_up_(0),
      inventory_scroll_(0),
      toolbox_open_(false)
{
    std::memset(sheets_, 0, sizeof(sheets_));
    std::memset(images_, 0, sizeof(images_));
    std::memset(&quickslot_closed_, 0, sizeof(quickslot_closed_));
    std::memset(&quickslot_open_, 0, sizeof(quickslot_open_));
    std::memset(&selection_yellow_, 0, sizeof(selection_yellow_));
    std::memset(&bar_background_, 0, sizeof(bar_background_));
    std::memset(&bar_red_, 0, sizeof(bar_red_));
    std::memset(&bar_yellow_, 0, sizeof(bar_yellow_));
}

HudView::~HudView()
{
    dispose();
}

bool HudView::initialize(C2D_Font font, C2D_TextBuf text_buffer, GameResources *resources,
    GameCore *core, MapRenderer *renderer, std::string &error)
{
    dispose();
    if (text_buffer == NULL || resources == NULL || core == NULL || renderer == NULL) {
        error = "invalid HUD configuration";
        return false;
    }
    font_ = font;
    text_buffer_ = text_buffer;
    resources_ = resources;
    core_ = core;
    renderer_ = renderer;
    if (!load_images(error)) {
        dispose();
        return false;
    }

    quickslot_closed_ = crop_tile(images_[ICON_QUICKSLOTS], 0, 2);
    quickslot_open_ = crop_tile(images_[ICON_QUICKSLOTS], 1, 2);
    selection_yellow_ = crop_tile(images_[ICON_SELECTIONS], 1, 5);
    bar_background_ = crop_nine_patch(images_[ICON_BAR_BACKGROUND]);
    bar_red_ = crop_nine_patch(images_[ICON_RED_FOREGROUND]);
    bar_yellow_ = crop_nine_patch(images_[ICON_YELLOW_FOREGROUND]);
    error.clear();
    return true;
}

void HudView::dispose()
{
    for (C2D_SpriteSheet &sheet : sheets_) {
        if (sheet != NULL) C2D_SpriteSheetFree(sheet);
        sheet = NULL;
    }
    std::memset(images_, 0, sizeof(images_));
    core_ = NULL;
    renderer_ = NULL;
    font_ = NULL;
    text_buffer_ = NULL;
    resources_ = NULL;
    screen_ = SCREEN_STATUS;
    hero_tab_ = 0;
    selected_level_up_ = 0;
    inventory_scroll_ = 0;
    toolbox_open_ = false;
    combat_message_.clear();
    item_name_cache_.clear();
    item_icon_cache_.clear();
}

bool HudView::load_images(std::string &error)
{
    static_assert(sizeof(UI_IMAGE_NAMES) / sizeof(UI_IMAGE_NAMES[0]) == ICON_COUNT,
        "UI image names must match the icon enum");
    for (unsigned int index = 0; index < ICON_COUNT; ++index) {
        char path[128];
        std::string texture_name(UI_IMAGE_NAMES[index]);
        texture_name.resize(texture_name.size() - 4);
        const int length = std::snprintf(path, sizeof(path),
            "romfs:/gfx/ui/%s.t3x", texture_name.c_str());
        if (length < 0 || static_cast<std::size_t>(length) >= sizeof(path)) {
            error = "UI texture path is too long";
            return false;
        }
        sheets_[index] = C2D_SpriteSheetLoad(path);
        if (sheets_[index] == NULL || C2D_SpriteSheetCount(sheets_[index]) != 1) {
            error = "original Android UI texture could not be loaded: ";
            error += path;
            return false;
        }
        images_[index] = C2D_SpriteSheetGetImage(sheets_[index], 0);
        if (images_[index].tex == NULL || images_[index].subtex == NULL) {
            error = "original Android UI texture is empty: ";
            error += path;
            return false;
        }
    }
    if (images_[ICON_QUICKSLOTS].subtex->width != 64
            || images_[ICON_QUICKSLOTS].subtex->height != 32
            || images_[ICON_SELECTIONS].subtex->width != 160
            || images_[ICON_SELECTIONS].subtex->height != 32) {
        error = "original Android UI icon atlas dimensions are invalid";
        return false;
    }
    return true;
}

HudAction HudView::handle_touch(int x, int y)
{
    if (screen_ == SCREEN_STATUS) {
        if (x < 43 && y < 44) {
            screen_ = SCREEN_HERO_INFO;
            toolbox_open_ = false;
            return HUD_ACTION_NONE;
        }
        if (x >= 274 && y < 42) {
            toolbox_open_ = !toolbox_open_;
            return HUD_ACTION_NONE;
        }
        if (toolbox_open_ && x >= 267 && y >= 43) {
            const int item = (y - 43) / 42;
            if (item == 0) {
                toolbox_open_ = false;
                return HUD_ACTION_SAVE;
            }
            if (item == 1) screen_ = SCREEN_COMBAT_LOG;
            else if (item == 2) screen_ = SCREEN_WORLD_MAP;
            else if (item == 3) screen_ = SCREEN_QUICK_ITEMS;
            toolbox_open_ = false;
        }
        return HUD_ACTION_NONE;
    }
    if (screen_ == SCREEN_HERO_INFO) {
        if (y < 48 && x >= 0 && x < 320) {
            hero_tab_ = std::min(3, x / 80);
            return HUD_ACTION_NONE;
        }
        if (hero_tab_ == 0 && game_core_can_level_up(core_)
                && y >= 184 && x >= 78 && x <= 242) {
            screen_ = SCREEN_LEVEL_UP;
        }
        return HUD_ACTION_NONE;
    }
    if (screen_ == SCREEN_LEVEL_UP) {
        if (y >= 50 && y < 190) {
            selected_level_up_ = (y - 50) / 35;
            selected_level_up_ = std::min(3, selected_level_up_);
        }
        if (y >= 190 && x >= 88 && x <= 232) {
            const bool leveled = game_core_level_up(
                core_, static_cast<GameCoreLevelUpChoice>(selected_level_up_)
            );
            screen_ = SCREEN_HERO_INFO;
            return leveled ? HUD_ACTION_STATE_CHANGED : HUD_ACTION_NONE;
        }
        return HUD_ACTION_NONE;
    }
    if (y < 30 && x < 48) screen_ = SCREEN_STATUS;
    return HUD_ACTION_NONE;
}

bool HudView::handle_keys(u32 keys_down, HudAction &action)
{
    if (screen_ == SCREEN_STATUS) return false;

    if (keys_down & KEY_B) {
        screen_ = screen_ == SCREEN_LEVEL_UP ? SCREEN_HERO_INFO : SCREEN_STATUS;
        return true;
    }
    if (screen_ == SCREEN_HERO_INFO) {
        if (keys_down & KEY_LEFT) hero_tab_ = (hero_tab_ + 3) % 4;
        if (keys_down & KEY_RIGHT) hero_tab_ = (hero_tab_ + 1) % 4;
        if (hero_tab_ == 3) {
            const int maximum_scroll = static_cast<int>(
                core_->inventory_count > 4 ? core_->inventory_count - 4 : 0
            );
            if (keys_down & KEY_UP) inventory_scroll_ = std::max(0, inventory_scroll_ - 1);
            if (keys_down & KEY_DOWN) {
                inventory_scroll_ = std::min(maximum_scroll, inventory_scroll_ + 1);
            }
        }
        if (hero_tab_ == 0 && (keys_down & KEY_A) && game_core_can_level_up(core_)) {
            screen_ = SCREEN_LEVEL_UP;
        }
        return true;
    }
    if (screen_ == SCREEN_LEVEL_UP) {
        if (keys_down & KEY_UP) selected_level_up_ = (selected_level_up_ + 3) % 4;
        if (keys_down & KEY_DOWN) selected_level_up_ = (selected_level_up_ + 1) % 4;
        if (keys_down & KEY_A) {
            if (game_core_level_up(core_,
                    static_cast<GameCoreLevelUpChoice>(selected_level_up_))) {
                action = HUD_ACTION_STATE_CHANGED;
                screen_ = SCREEN_HERO_INFO;
            }
        }
        return true;
    }
    if (keys_down & KEY_A) screen_ = SCREEN_STATUS;
    return true;
}

void HudView::set_combat_message(const std::string &message)
{
    combat_message_ = message;
}

void HudView::set_notice(const std::string &message)
{
    notice_ = message;
}

void HudView::draw() const
{
    if (core_ == NULL || renderer_ == NULL) return;
    if (screen_ == SCREEN_STATUS) draw_status();
    else if (screen_ == SCREEN_HERO_INFO) draw_hero_info();
    else if (screen_ == SCREEN_LEVEL_UP) draw_level_up();
    else if (screen_ == SCREEN_COMBAT_LOG) {
        draw_panel(4.0f, 4.0f, 312.0f, 232.0f);
        draw_text("Combat log", 12.0f, 12.0f, 0.42f, PANEL_TEXT);
        draw_text(combat_message_.empty() ? "No combat messages." : combat_message_.c_str(),
            12.0f, 42.0f, 0.34f, PANEL_TEXT, true);
        draw_text("B: back", 12.0f, 220.0f, 0.3f, PANEL_TEXT);
    } else if (screen_ == SCREEN_WORLD_MAP) {
        draw_panel(4.0f, 4.0f, 312.0f, 232.0f);
        draw_text("World map", 12.0f, 12.0f, 0.42f, PANEL_TEXT);
        draw_text("Original visited-map tiles are not bundled yet.",
            12.0f, 42.0f, 0.34f, PANEL_TEXT, true);
        draw_text("B: back", 12.0f, 220.0f, 0.3f, PANEL_TEXT);
    } else {
        draw_panel(4.0f, 4.0f, 312.0f, 232.0f);
        draw_text("Quick items", 12.0f, 12.0f, 0.42f, PANEL_TEXT);
        draw_text("Quick-slot actions are not ported yet.",
            12.0f, 42.0f, 0.34f, PANEL_TEXT, true);
        draw_text("B: back", 12.0f, 220.0f, 0.3f, PANEL_TEXT);
    }
}

void HudView::draw_interaction(const char *text) const
{
    if (text == NULL || text[0] == '\0') return;
    draw_nine_patch(ICON_STD_FRAME, 8.0f, 92.0f, 384.0f, 140.0f);
    draw_text(text, 18.0f, 103.0f, 0.42f, PANEL_TEXT, true, 364.0f);
}

void HudView::draw_status() const
{
    draw_panel(0.0f, 0.0f, 320.0f, 48.0f);
    renderer_->draw_player_portrait(5.0f, 8.0f, 0.6f);
    if (game_core_can_level_up(core_)) {
        C2D_Image selection = images_[ICON_SELECTIONS];
        selection.subtex = &selection_yellow_;
        C2D_DrawImageAt(selection, 5.0f, 8.0f, 0.5f, NULL);
    }

    draw_text("HP", 45.0f, 8.0f, 0.32f, PANEL_TEXT);
    draw_bar(78.0f, 6.0f, 186.0f, core_->health, core_->max_health,
        ICON_RED_FOREGROUND);
    char value[32];
    std::snprintf(value, sizeof(value), "%d/%d", core_->health, core_->max_health);
    draw_text(value, 135.0f, 7.0f, 0.32f, PANEL_TEXT);

    const int next_level = game_core_experience_for_next_level(core_->level);
    const int current_level_experience = game_core_experience_in_current_level(core_);
    draw_text("EXP", 45.0f, 27.0f, 0.32f, PANEL_TEXT);
    draw_bar(78.0f, 25.0f, 186.0f, current_level_experience, next_level,
        ICON_YELLOW_FOREGROUND);
    std::snprintf(value, sizeof(value), "%d/%d", current_level_experience, next_level);
    draw_text(value, 135.0f, 26.0f, 0.32f, PANEL_TEXT);

    C2D_Image toolbox = images_[ICON_QUICKSLOTS];
    toolbox.subtex = toolbox_open_
        ? &quickslot_open_
        : &quickslot_closed_;
    C2D_DrawImageAt(toolbox, 280.0f, 8.0f, 0.5f, NULL);
    if (toolbox_open_) draw_toolbox();
    else if (!notice_.empty()) draw_text(notice_.c_str(), 45.0f, 50.0f, 0.32f,
        C2D_Color32(255, 210, 120, 255));
}

void HudView::draw_hero_info() const
{
    draw_nine_patch(ICON_TAB_FRAME, 0.0f, 0.0f, 320.0f, 240.0f);
    draw_nine_patch(ICON_TAB_TOP_OVERLAY, 0.0f, 0.0f, 320.0f, 24.0f);
    const char *const tabs[] = {"Overview", "Quests", "Skills", "Items"};
    const Icon tab_icons[] = {ICON_QUEST, ICON_QUEST, ICON_SKILL, ICON_EQUIPMENT};
    for (int index = 0; index < 4; ++index) {
        const float x = 2.0f + index * 79.0f;
        draw_nine_patch(index == hero_tab_ ? ICON_TAB_SELECTED : ICON_TAB_UNSELECTED,
            x, 2.0f, 77.0f, 42.0f);
        if (index == 0) C2D_DrawImageAt(images_[ICON_HERO_LARGE], x + 24.0f,
            1.0f, 0.3f, NULL, 0.5f, 0.5f);
        else C2D_DrawImageAt(images_[tab_icons[index]], x + 26.0f, 3.0f, 0.3f,
            NULL, 0.8f, 0.8f);
        draw_text(tabs[index], x + 8.0f, 27.0f, 0.27f, PANEL_TEXT);
    }

    if (hero_tab_ == 0) {
        draw_panel(3.0f, 47.0f, 314.0f, 190.0f);
        C2D_DrawImageAt(images_[ICON_HERO_LARGE], 9.0f, 52.0f, 0.3f,
            NULL, 1.0f, 1.0f);
        char line[96];
        std::snprintf(line, sizeof(line), "Mode: Standard    Level: %d", core_->level);
        draw_text(line, 48.0f, 52.0f, 0.29f, PANEL_TEXT);
        std::snprintf(line, sizeof(line), "Total experience: %d", core_->total_experience);
        draw_text(line, 48.0f, 67.0f, 0.29f, PANEL_TEXT);
        std::snprintf(line, sizeof(line), "Health points (HP): %d/%d",
            core_->health, core_->max_health);
        draw_text(line, 9.0f, 91.0f, 0.29f, PANEL_TEXT);
        draw_bar(134.0f, 91.0f, 172.0f, core_->health, core_->max_health,
            ICON_RED_FOREGROUND);
        const int next_level = game_core_experience_for_next_level(core_->level);
        const int current_xp = game_core_experience_in_current_level(core_);
        std::snprintf(line, sizeof(line), "Experience points (XP): %d/%d",
            current_xp, next_level);
        draw_text(line, 9.0f, 108.0f, 0.29f, PANEL_TEXT);
        draw_bar(134.0f, 108.0f, 172.0f, current_xp, next_level,
            ICON_YELLOW_FOREGROUND);
        C2D_DrawRectSolid(5.0f, 127.0f, 0.4f, 310.0f, 1.0f, PANEL_BORDER);
        draw_text("Combat statistics (current)", 9.0f, 132.0f, 0.3f, PANEL_TEXT);
        std::snprintf(line, sizeof(line), "Attack chance: %d%%", core_->attack_chance);
        draw_text(line, 9.0f, 151.0f, 0.29f, PANEL_TEXT);
        std::snprintf(line, sizeof(line), "Attack damage: %d HP", core_->attack_damage);
        draw_text(line, 164.0f, 151.0f, 0.29f, PANEL_TEXT);
        std::snprintf(line, sizeof(line), "Block chance: %d%%", core_->block_chance);
        draw_text(line, 9.0f, 169.0f, 0.29f, PANEL_TEXT);
        std::snprintf(line, sizeof(line), "Skill points: %d", core_->skill_points);
        draw_text(line, 164.0f, 169.0f, 0.29f, PANEL_TEXT);
        if (game_core_can_level_up(core_)) {
            draw_panel(79.0f, 192.0f, 162.0f, 30.0f);
            draw_text("Level up", 132.0f, 201.0f, 0.29f, PANEL_TEXT);
        }
    } else if (hero_tab_ == 1) {
        draw_panel(3.0f, 47.0f, 314.0f, 190.0f);
        draw_panel(5.0f, 50.0f, 310.0f, 23.0f);
        draw_text("Active quests", 10.0f, 55.0f, 0.29f, PANEL_TEXT);
        const GameResourceRecordEntry *record = game_resources_find(
            GAME_RESOURCE_QUEST, "andor", NULL
        );
        if (record != NULL) {
            GameJsonDocument document;
            GameJsonRef quest;
            if (game_resources_open_record(resources_, record, &document, &quest)) {
                char quest_name[96];
                if (game_json_string_copy(game_json_object_get(quest, "name"),
                        quest_name, sizeof(quest_name))) {
                    draw_panel(5.0f, 76.0f, 310.0f, 24.0f);
                    C2D_DrawCircleSolid(14.0f, 88.0f, 0.2f, 5.0f, PANEL_BORDER);
                    draw_text(quest_name, 25.0f, 82.0f, 0.3f, PANEL_TEXT);
                }
                const int progress = game_core_quest_progress(core_, "andor");
                GameJsonRef stages = game_json_object_get(quest, "stages");
                int selected_progress = -1;
                char log_text[256] = "";
                for (std::size_t stage_index = 0;
                        stage_index < game_json_array_size(stages); ++stage_index) {
                    GameJsonRef stage = game_json_array_get(stages, stage_index);
                    int64_t stage_progress = -1;
                    char stage_text[sizeof(log_text)];
                    if (!game_json_integer(game_json_object_get(stage, "progress"),
                                &stage_progress)
                            || stage_progress > progress || stage_progress < selected_progress
                            || !game_json_string_copy(game_json_object_get(stage, "logText"),
                                stage_text, sizeof(stage_text))) continue;
                    selected_progress = static_cast<int>(stage_progress);
                    std::snprintf(log_text, sizeof(log_text), "%s", stage_text);
                }
                if (log_text[0] != '\0') {
                    draw_panel(5.0f, 102.0f, 310.0f, 74.0f);
                    draw_text(log_text, 10.0f, 108.0f, 0.28f, PANEL_TEXT, true);
                }
                game_json_dispose(&document);
            }
        }
    } else if (hero_tab_ == 2) {
        draw_panel(3.0f, 47.0f, 314.0f, 190.0f);
        draw_text("Skills", 9.0f, 54.0f, 0.3f, PANEL_TEXT);
        draw_text("No skill data is available in this build.", 9.0f, 77.0f,
            0.28f, PANEL_TEXT);
    } else {
        draw_panel(3.0f, 47.0f, 314.0f, 84.0f);
        draw_text("Worn equipment", 8.0f, 49.0f, 0.24f, PANEL_TEXT);
        const Icon equipment_slots[] = {
            ICON_EQUIP_HEAD, ICON_EQUIP_BODY, ICON_EQUIP_NECK,
            ICON_EQUIP_WEAPON, ICON_EQUIP_RING,
            ICON_EQUIP_SHIELD, ICON_EQUIP_RING,
            ICON_EQUIP_HAND, ICON_EQUIP_FEET
        };
        const float slot_x[] = {78.0f, 101.0f, 124.0f, 39.0f, 62.0f,
            148.0f, 125.0f, 78.0f, 101.0f};
        const float slot_y[] = {53.0f, 53.0f, 53.0f, 74.0f, 96.0f,
            74.0f, 96.0f, 111.0f, 111.0f};
        for (int slot = 0; slot < 9; ++slot) {
            C2D_DrawImageAt(images_[ICON_EQUIP_SQUARE], slot_x[slot],
                slot_y[slot], 0.3f, NULL, 1.15f, 1.15f);
            C2D_DrawImageAt(images_[equipment_slots[slot]], slot_x[slot] + 1.0f,
                slot_y[slot] + 1.0f, 0.2f, NULL, 0.55f, 0.55f);
        }
        C2D_DrawImageAt(images_[ICON_HERO_LARGE], 87.0f, 77.0f, 0.3f,
            NULL, 1.0f, 1.0f);
        draw_panel(174.0f, 55.0f, 139.0f, 20.0f);
        C2D_DrawImageAt(images_[ICON_COINS], 176.0f, 57.0f, 0.3f,
            NULL, 0.55f, 0.55f);
        char line[80];
        std::snprintf(line, sizeof(line), "Gold: %d", core_->gold);
        draw_text(line, 196.0f, 60.0f, 0.25f, PANEL_TEXT);
        draw_panel(174.0f, 78.0f, 139.0f, 20.0f);
        C2D_DrawImageAt(images_[ICON_EQUIP_WEAPON], 176.0f, 80.0f,
            0.3f, NULL, 0.55f, 0.55f);
        std::snprintf(line, sizeof(line), "Attack: %d%%  %d",
            core_->attack_chance, core_->attack_damage);
        draw_text(line, 196.0f, 83.0f, 0.25f, PANEL_TEXT);
        draw_panel(174.0f, 101.0f, 139.0f, 20.0f);
        C2D_DrawImageAt(images_[ICON_EQUIP_SHIELD], 176.0f, 103.0f,
            0.3f, NULL, 0.55f, 0.55f);
        std::snprintf(line, sizeof(line), "Defense: %d", core_->block_chance);
        draw_text(line, 196.0f, 106.0f, 0.25f, PANEL_TEXT);

        draw_panel(3.0f, 134.0f, 314.0f, 103.0f);
        draw_text("Inventory", 8.0f, 137.0f, 0.26f, PANEL_TEXT);
        const int maximum_scroll = static_cast<int>(
            core_->inventory_count > 4 ? core_->inventory_count - 4 : 0);
        const std::size_t start = static_cast<std::size_t>(
            std::min(inventory_scroll_, maximum_scroll)
        );
        const std::size_t end = std::min(core_->inventory_count, start + 4);
        int y = 151;
        for (std::size_t index = start; index < end; ++index, y += 20) {
            draw_panel(5.0f, static_cast<float>(y), 310.0f, 19.0f);
            const std::string &item_id = core_->inventory[index].id;
            std::map<std::string, std::string>::iterator cached =
                item_name_cache_.find(item_id);
            if (cached == item_name_cache_.end()) {
                std::string name;
                std::string error;
                if (!items::item_name(resources_, item_id, name, error)) {
                    name = item_id + " (name unavailable)";
                }
                cached = item_name_cache_.insert(std::make_pair(item_id, name)).first;
            }
            draw_item_icon(item_id, 7.0f, static_cast<float>(y - 1));
            std::snprintf(line, sizeof(line), "%s x%d",
                cached->second.c_str(), core_->inventory[index].value);
            draw_text(line, 31.0f, static_cast<float>(y + 4), 0.25f, PANEL_TEXT);
        }
        if (core_->inventory_count == 0) draw_text("Inventory is empty.", 9.0f, 157.0f,
            0.27f, PANEL_TEXT);
    }
}

void HudView::draw_level_up() const
{
    C2D_DrawRectSolid(0.0f, 0.0f, 0.9f, 320.0f, 240.0f,
        C2D_Color32(19, 20, 23, 255));
    char title[48];
    std::snprintf(title, sizeof(title), "Level up: level %d", core_->level + 1);
    draw_text(title, 12.0f, 10.0f, 0.55f, C2D_Color32(255, 255, 255, 255));
    const char *const choices[] = {
        "+5 maximum health",
        "+5 attack chance",
        "+1 attack damage",
        "+3 block chance"
    };
    for (int index = 0; index < 4; ++index) {
        const float y = 50.0f + index * 35.0f;
        if (index == selected_level_up_) {
            C2D_DrawRectSolid(8.0f, y - 4.0f, 0.7f, 304.0f, 30.0f,
                C2D_Color32(80, 74, 56, 255));
        }
        draw_text(choices[index], 20.0f, y, 0.4f,
            C2D_Color32(255, 255, 255, 255));
    }
    if ((core_->level + 1) >= 4 && ((core_->level + 1) - 4) % 4 == 0) {
        draw_text("This level also awards a skill point.", 12.0f, 194.0f, 0.34f,
            C2D_Color32(255, 222, 125, 255));
    }
    draw_text("Up/Down: select   A: choose   B: back", 12.0f, 218.0f, 0.32f,
        C2D_Color32(190, 190, 190, 255));
}

void HudView::draw_toolbox() const
{
    draw_panel(267.0f, 43.0f, 53.0f, 168.0f);
    const Icon icons[] = {ICON_SAVE, ICON_COMBAT, ICON_MAP, ICON_EQUIPMENT};
    for (int index = 0; index < 4; ++index) {
        C2D_DrawImageAt(images_[icons[index]], 277.0f,
            47.0f + index * 40.0f, 0.5f, NULL);
    }
}

void HudView::draw_panel(float x, float y, float width, float height) const
{
    draw_nine_patch(ICON_STD_FRAME, x, y, width, height);
}

void HudView::draw_nine_patch(Icon icon, float x, float y, float width,
    float height) const
{
    const Tex3DS_SubTexture source = crop_nine_patch(images_[icon]);
    const unsigned int source_width = static_cast<unsigned int>(source.width);
    const unsigned int source_height = static_cast<unsigned int>(source.height);
    if (source_width == 0 || source_height == 0) return;
    unsigned int stretch_x = 0;
    unsigned int stretch_width = 1;
    unsigned int stretch_y = 0;
    unsigned int stretch_height = 1;
    if (icon == ICON_STD_FRAME) {
        stretch_x = 9;
        stretch_y = 9;
    } else if (icon == ICON_TAB_FRAME) {
        stretch_x = 8;
        stretch_y = 2;
    } else if (icon == ICON_TAB_SELECTED) {
        stretch_x = 8;
        stretch_y = 9;
    } else if (icon == ICON_TAB_UNSELECTED) {
        stretch_x = 8;
        stretch_y = 9;
        stretch_height = 2;
    } else if (icon == ICON_TAB_TOP_OVERLAY) {
        stretch_x = 5;
        stretch_width = 3;
    } else {
        stretch_x = source_width / 3;
        stretch_y = source_height / 3;
    }
    if (stretch_x >= source_width || stretch_y >= source_height
            || stretch_width > source_width - stretch_x
            || stretch_height > source_height - stretch_y) return;
    const unsigned int source_left = stretch_x;
    const unsigned int source_right = source_width - stretch_x - stretch_width;
    const unsigned int source_top = stretch_y;
    const unsigned int source_bottom = source_height - stretch_y - stretch_height;
    const float destination_left = std::min(static_cast<float>(source_left), width / 2.0f);
    const float destination_right = std::min(static_cast<float>(source_right),
        width - destination_left);
    const float destination_top = std::min(static_cast<float>(source_top), height / 2.0f);
    const float destination_bottom = std::min(static_cast<float>(source_bottom),
        height - destination_top);
    const unsigned int source_widths[] = {
        source_left, stretch_width, source_right
    };
    const unsigned int source_heights[] = {
        source_top, stretch_height, source_bottom
    };
    const float destination_widths[] = {
        destination_left, width - destination_left - destination_right,
        destination_right
    };
    const float destination_heights[] = {
        destination_top, height - destination_top - destination_bottom,
        destination_bottom
    };
    const unsigned int source_x[] = {
        0, source_left, source_width - source_right
    };
    const unsigned int source_y[] = {
        0, source_top, source_height - source_bottom
    };
    float destination_y = y;
    for (unsigned int row = 0; row < 3; ++row) {
        float destination_x = x;
        for (unsigned int column = 0; column < 3; ++column) {
            if (source_widths[column] != 0 && source_heights[row] != 0
                    && destination_widths[column] > 0.0f
                    && destination_heights[row] > 0.0f) {
                Tex3DS_SubTexture part = crop_subtexture(source, source_x[column],
                    source_y[row], source_widths[column], source_heights[row]);
                C2D_Image image = images_[icon];
                image.subtex = &part;
                C2D_DrawImageAt(image, destination_x, destination_y, 0.8f, NULL,
                    destination_widths[column] / source_widths[column],
                    destination_heights[row] / source_heights[row]);
            }
            destination_x += destination_widths[column];
        }
        destination_y += destination_heights[row];
    }
}

void HudView::draw_item_icon(const std::string &item_id, float x, float y) const
{
    std::map<std::string, std::pair<int, unsigned int> >::iterator cached =
        item_icon_cache_.find(item_id);
    if (cached == item_icon_cache_.end()) {
        std::string sheet;
        unsigned int index = 0;
        std::string error;
        int icon = -1;
        if (items::item_icon(resources_, item_id, sheet, index, error)) {
            if (sheet == "items_weapons") icon = ICON_ITEMS_WEAPONS;
            else if (sheet == "items_armours") icon = ICON_ITEMS_ARMOURS;
            else if (sheet == "items_jewelry") icon = ICON_ITEMS_JEWELRY;
            else if (sheet == "items_consumables") icon = ICON_ITEMS_CONSUMABLES;
            else if (sheet == "items_misc") icon = ICON_ITEMS_MISC;
        }
        cached = item_icon_cache_.insert(
            std::make_pair(item_id, std::make_pair(icon, index))
        ).first;
    }
    if (cached->second.first < 0 || cached->second.first >= ICON_COUNT) return;
    C2D_Image item = images_[cached->second.first];
    const unsigned int columns = item.subtex->width / 32;
    const unsigned int rows = item.subtex->height / 32;
    if (columns == 0 || cached->second.second >= columns * rows) return;
    Tex3DS_SubTexture subtexture = crop_item_icon(item, cached->second.second);
    item.subtex = &subtexture;
    C2D_DrawImageAt(item, x, y, 0.5f, NULL, 0.53f, 0.53f);
}

void HudView::draw_bar(float x, float y, float width, int current, int maximum,
    Icon foreground) const
{
    C2D_Image background = images_[ICON_BAR_BACKGROUND];
    background.subtex = &bar_background_;
    C2D_DrawImageAt(background, x, y, 0.6f, NULL,
        width / bar_background_.width, 12.0f / bar_background_.height);
    if (current <= 0 || maximum <= 0) return;

    const float fraction = std::min(1.0f, static_cast<float>(current) / maximum);
    Tex3DS_SubTexture clipped = foreground == ICON_RED_FOREGROUND
        ? bar_red_ : bar_yellow_;
    clipped.right = clipped.left + (clipped.right - clipped.left) * fraction;
    clipped.width = std::max<unsigned int>(1,
        static_cast<unsigned int>(clipped.width * fraction));
    C2D_Image fill = images_[foreground];
    fill.subtex = &clipped;
    C2D_DrawImageAt(fill, x, y, 0.5f, NULL,
        width / (foreground == ICON_RED_FOREGROUND ? bar_red_.width : bar_yellow_.width),
        12.0f / clipped.height);
}

void HudView::draw_text(const char *text, float x, float y, float scale,
    u32 color, bool wrap, float wrap_width) const
{
    if (text == NULL) return;
    if (text_buffer_ == NULL) return;
    C2D_Text parsed;
    C2D_TextFontParse(&parsed, font_, text_buffer_, text);
    C2D_TextOptimize(&parsed);
    C2D_DrawText(&parsed, C2D_WithColor | (wrap ? C2D_WordWrap : 0),
        x, y, 0.4f, scale, scale, color, wrap ? wrap_width : 0.0f);
}

}
}
