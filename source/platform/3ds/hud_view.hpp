#ifndef ANDORSTRAIL_3DS_HUD_VIEW_HPP
#define ANDORSTRAIL_3DS_HUD_VIEW_HPP

#include "game_core.h"
#include "map_renderer.hpp"

#include <3ds.h>
#include <citro2d.h>

#include <map>
#include <string>
#include <utility>

namespace andortrail {
namespace platform3ds {

enum HudAction {
    HUD_ACTION_NONE,
    HUD_ACTION_SAVE,
    HUD_ACTION_STATE_CHANGED
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
    void set_combat_message(const std::string &message);
    void set_notice(const std::string &message);
    void draw() const;
    void draw_interaction(const char *text) const;

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
    void draw_hero_info() const;
    void draw_level_up() const;
    void draw_toolbox() const;
    void draw_panel(float x, float y, float width, float height) const;
    void draw_nine_patch(Icon icon, float x, float y, float width,
        float height) const;
    void draw_item_icon(const std::string &item_id, float x, float y) const;
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
    mutable Tex3DS_SubTexture quickslot_closed_;
    mutable Tex3DS_SubTexture quickslot_open_;
    mutable Tex3DS_SubTexture selection_yellow_;
    mutable Tex3DS_SubTexture bar_background_;
    mutable Tex3DS_SubTexture bar_red_;
    mutable Tex3DS_SubTexture bar_yellow_;
    Screen screen_;
    int hero_tab_;
    int selected_level_up_;
    int inventory_scroll_;
    bool toolbox_open_;
    std::string combat_message_;
    std::string notice_;
    mutable std::map<std::string, std::string> item_name_cache_;
    mutable std::map<std::string, std::pair<int, unsigned int> > item_icon_cache_;
};

}
}

#endif
