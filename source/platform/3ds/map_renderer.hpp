#ifndef ANDORSTRAIL_3DS_MAP_RENDERER_HPP
#define ANDORSTRAIL_3DS_MAP_RENDERER_HPP

#include "game_map.hpp"
#include "game_resources.h"

#include <citro2d.h>

#include <map>
#include <string>
#include <vector>

namespace andortrail {
namespace platform3ds {

class MapRenderer {
public:
    MapRenderer();
    ~MapRenderer();

    bool initialize(const char *maps_directory, const char *initial_map_id,
        const char *tileset_list_path, const char *texture_directory,
        GameResources *resources, std::string &error);
    void dispose();
    void draw(int screen_width, int screen_height) const;
    bool move_player(int dx, int dy);
    bool has_modal_interaction() const;
    void dismiss_modal_interaction();
    const std::string &interaction_message() const;
    map::GridPosition player_position() const;
    map::ViewTransform camera(int screen_width, int screen_height) const;
    bool player_walkable() const;
    const char *map_id() const;

private:
    bool load_tileset_sheets(const char *tileset_list_path,
        const char *texture_directory, std::string &error);
    bool load_map(const std::string &map_id, const std::string &place_id,
        map::EventKind place_kind, int offset_x, int offset_y, std::string &error);
    bool prepare_tile_sources(const map::Document &document, std::string &error);
    bool validate_visible_layers(const map::Document &document, std::string &error) const;
    bool handle_map_event(const map::Event &event, map::GridPosition entered,
        std::string &error);
    bool transition(const map::Event &event, map::GridPosition entered,
        std::string &error);
    void show_sign(const map::Event &event);
    void show_container(const map::Event &event);
    void show_key_requirement(const map::Event &event);
    void draw_layer(const std::string &name, const map::ViewTransform &view,
        float depth) const;
    void draw_gid(std::uint32_t gid, int screen_x, int screen_y, float depth) const;

    map::Document document_;
    map::TranslatedDocument translated_;
    map::GridPosition player_;
    std::string maps_directory_;
    std::string interaction_message_;
    GameResources *resources_;
    std::vector<C2D_SpriteSheet> sheets_;
    std::vector<std::string> image_names_;
    std::map<std::string, std::size_t> image_indices_;
    std::map<std::string, std::vector<Tex3DS_SubTexture> > tile_subtextures_;
    C2D_Image player_image_;
    bool modal_interaction_;
    bool initialized_;
};

}
}

#endif