#ifndef ANDORSTRAIL_3DS_HUD_VIEW_HPP
#define ANDORSTRAIL_3DS_HUD_VIEW_HPP

#include "game_core.h"
#include "game_items.hpp"
#include "game_quest_log.hpp"
#include "game_skills.hpp"
#include "map_renderer.hpp"

#include <3ds.h>
#include <citro2d.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace andortrail {
namespace platform3ds {

enum HudAction {
    HUD_ACTION_NONE,
    HUD_ACTION_SAVE,
    HUD_ACTION_STATE_CHANGED,
    HUD_ACTION_SWAP_SCREENS
};

enum StartMenuPage {
    START_MENU_MAIN,
    START_MENU_LOAD,
    START_MENU_SAVE,
    START_MENU_CONFIRM_NEW_GAME,
    START_MENU_CONFIRM_OVERWRITE,
    START_MENU_PREFERENCES,
    START_MENU_ABOUT
};

class HudView {
public:
    HudView();
    ~HudView();

    bool initialize(C2D_Font font, C2D_TextBuf text_buffer, GameResources *resources,
        GameCore *core, MapRenderer *renderer, std::string &error);
    void dispose();
    HudAction handle_touch(int x, int y);
    bool handle_keys(u32 keys_down, HudAction &action);
    void handle_touch_move(int x, int y);
    void end_touch();
    void set_combat_message(const std::string &message);
    void set_notice(const std::string &message);
    void draw(int screen_offset_x = 0) const;
    void draw_combat_overlay() const;
    void draw_interaction(const char *text) const;
    void draw_encounter_options(int selected) const;
    void draw_start_title() const;
    void draw_start_screen(StartMenuPage page, int selected,
        const std::vector<std::string> &entries, const std::string &message) const;

private:
    enum Screen {
        SCREEN_STATUS,
        SCREEN_HERO_INFO,
        SCREEN_LEVEL_UP,
        SCREEN_COMBAT_LOG,
        SCREEN_WORLD_MAP,
        SCREEN_QUICK_ITEMS
    };

    enum Icon {
        ICON_BAR_BACKGROUND,
        ICON_COMBAT_RED_FOREGROUND,
        ICON_COMBAT_BLUE_FOREGROUND,
        ICON_COMBAT,
        ICON_EQUIPMENT,
        ICON_MAP,
        ICON_QUEST,
        ICON_SAVE,
        ICON_SKILL,
        ICON_QUICKSLOTS,
        ICON_RED_FOREGROUND,
        ICON_SELECTIONS,
        ICON_YELLOW_FOREGROUND,
        ICON_COINS,
        ICON_EQUIP_WEAPON,
        ICON_EQUIP_SHIELD,
        ICON_EQUIP_BODY,
        ICON_EQUIP_FEET,
        ICON_EQUIP_HAND,
        ICON_EQUIP_HEAD,
        ICON_EQUIP_NECK,
        ICON_EQUIP_RING,
        ICON_ITEMS_WEAPONS,
        ICON_ITEMS_ARMOURS,
        ICON_ITEMS_JEWELRY,
        ICON_ITEMS_CONSUMABLES,
        ICON_ITEMS_MISC,
        ICON_STD_FRAME,
        ICON_TAB_FRAME,
        ICON_TAB_SELECTED,
        ICON_TAB_UNSELECTED,
        ICON_TAB_TOP_OVERLAY,
        ICON_HERO_LARGE,
        ICON_EQUIP_SQUARE,
        ICON_COUNT
    };

    bool load_images(std::string &error);
    void draw_status() const;
    void draw_hero_info(int screen_offset_x) const;
    void draw_level_up() const;
    void draw_quick_items() const;
    void draw_toolbox() const;
    void refresh_quest_log() const;
    void select_skill(int selection, std::size_t skill_count);
    HudAction learn_selected_skill();
    std::vector<int> filtered_inventory_indices() const;
    bool inventory_properties(const std::string &item_id,
        items::InventoryProperties &properties) const;
    void draw_panel(float x, float y, float width, float height) const;
    void draw_nine_patch(Icon icon, float x, float y, float width,
        float height) const;
    void draw_item_icon(const std::string &item_id, float x, float y,
        float scale = 0.53f) const;
    void draw_bar(float x, float y, float width, int current, int maximum,
        Icon foreground) const;
    void draw_text(const char *text, float x, float y, float scale,
        u32 color, bool wrap = false, float wrap_width = 296.0f) const;

    GameCore *core_;
    MapRenderer *renderer_;
    C2D_Font font_;
    C2D_TextBuf text_buffer_;
    GameResources *resources_;
    C2D_SpriteSheet sheets_[ICON_COUNT];
    C2D_Image images_[ICON_COUNT];
    C2D_SpriteSheet title_sheets_[4];
    C2D_Image title_images_[4];
    mutable Tex3DS_SubTexture quickslot_closed_;
    mutable Tex3DS_SubTexture quickslot_open_;
    mutable Tex3DS_SubTexture selection_yellow_;
    mutable Tex3DS_SubTexture bar_background_;
    mutable Tex3DS_SubTexture bar_red_;
    mutable Tex3DS_SubTexture combat_bar_red_;
    mutable Tex3DS_SubTexture combat_bar_blue_;
    mutable Tex3DS_SubTexture bar_yellow_;
    Screen screen_;
    int hero_tab_;
    int selected_level_up_;
    mutable int overview_scroll_;
    mutable int overview_scroll_offset_;
    mutable int overview_max_scroll_;
    int inventory_scroll_;
    int selected_inventory_index_;
    int inventory_category_;
    int inventory_sort_;
    int selected_quick_slot_;
    int selected_equipment_slot_;
    mutable int quest_scroll_;
    mutable int skill_scroll_;
    mutable int skill_scroll_offset_;
    int skill_selection_;
    bool skill_touch_active_;
    bool skill_touch_dragging_;
    int skill_touch_start_y_;
    int skill_touch_last_y_;
    int skill_touch_selection_;
    bool overview_touch_active_;
    bool overview_touch_dragging_;
    int overview_touch_start_y_;
    int overview_touch_last_y_;
    bool toolbox_open_;
    std::string combat_message_;
    std::string notice_;
    mutable std::map<std::string, std::string> item_name_cache_;
    mutable std::map<std::string, items::InventoryProperties> item_properties_cache_;
    mutable std::map<std::string, std::pair<int, unsigned int> > item_icon_cache_;
    mutable std::string inventory_error_;
    mutable bool quest_log_dirty_;
    mutable std::vector<GameCoreValue> quest_snapshot_;
    mutable std::vector<quest_log::Entry> quest_entries_;
    mutable std::string quest_log_error_;
};

}
}

#endif
