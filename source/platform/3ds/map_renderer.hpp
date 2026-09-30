#ifndef ANDORSTRAIL_3DS_MAP_RENDERER_HPP
#define ANDORSTRAIL_3DS_MAP_RENDERER_HPP

#include "game_map.hpp"

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

    bool initialize(const char *map_path, const char *tileset_list_path,
        const char *texture_directory, std::string &error);
    void dispose();
    void draw(int screen_width, int screen_height) const;
    bool move_player(int dx, int dy);
    map::GridPosition player_position() const;

private:
    bool load_tileset_sheets(const char *tileset_list_path,
        const char *texture_directory, std::string &error);
    bool prepare_tile_sources(std::string &error);
    bool validate_visible_layers(std::string &error) const;
    void draw_layer(const std::string &name, const map::ViewTransform &view,
        float depth) const;
    void draw_gid(std::uint32_t gid, int screen_x, int screen_y, float depth) const;

    map::Document document_;
    map::GridPosition player_;
    std::vector<C2D_SpriteSheet> sheets_;
    std::vector<std::string> image_names_;
    std::map<std::string, std::size_t> image_indices_;
    std::map<std::string, std::vector<Tex3DS_SubTexture> > tile_subtextures_;
    C2D_Image player_image_;
    bool initialized_;
};

}
}

#endif