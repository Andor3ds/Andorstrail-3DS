#include "map_renderer.hpp"

#include <cstdio>
#include <cstring>

namespace andortrail {
namespace platform3ds {
namespace {

std::string basename_of(const std::string &path)
{
    const std::size_t separator = path.find_last_of("/\\");
    return separator == std::string::npos ? path : path.substr(separator + 1);
}

}

MapRenderer::MapRenderer()
    : player_({0, 0}), player_image_({NULL, NULL}), initialized_(false)
{
}

MapRenderer::~MapRenderer()
{
    dispose();
}

void MapRenderer::dispose()
{
    for (C2D_SpriteSheet sheet : sheets_) {
        if (sheet != NULL) C2D_SpriteSheetFree(sheet);
    }
    sheets_.clear();
    image_names_.clear();
    image_indices_.clear();
    tile_subtextures_.clear();
    initialized_ = false;
}

bool MapRenderer::initialize(
    const char *map_path,
    const char *tileset_list_path,
    const char *texture_directory,
    std::string &error
)
{
    dispose();
    if (map_path == NULL || tileset_list_path == NULL || texture_directory == NULL) {
        error = "invalid map renderer paths";
        return false;
    }
    if (!map::load_tmx(map_path, "home", true, document_, &error)) return false;

    map::TranslatedDocument translated;
    if (!map::translate_document(document_, translated, &error)) return false;
    bool found_start = false;
    for (const map::Event &event : translated.events) {
        if (event.kind == map::EventKind::Rest && event.name == "rest") {
            player_ = {event.tile_rect.x, event.tile_rect.y};
            found_start = true;
            break;
        }
    }
    if (!found_start) {
        error = "Home map has no Android rest start point";
        return false;
    }
    if (!map::is_walkable(document_, player_.x, player_.y)) {
        error = "Android Home rest point is not walkable";
        return false;
    }

    if (!load_tileset_sheets(tileset_list_path, texture_directory, error)
            || !prepare_tile_sources(error)
            || !validate_visible_layers(error)) {
        dispose();
        return false;
    }

    const std::map<std::string, std::size_t>::const_iterator hero_index =
        image_indices_.find("char_hero.png");
    if (hero_index == image_indices_.end()) {
        error = "original player image is missing from the tileset list";
        dispose();
        return false;
    }
    player_image_ = C2D_SpriteSheetGetImage(sheets_[hero_index->second], 0);
    if (player_image_.subtex == NULL || player_image_.subtex->width != 32
            || player_image_.subtex->height != 32) {
        error = "original Android player image is not one 32-pixel tile";
        dispose();
        return false;
    }

    initialized_ = true;
    error.clear();
    return true;
}

bool MapRenderer::load_tileset_sheets(
    const char *tileset_list_path,
    const char *texture_directory,
    std::string &error
)
{
    FILE *list = std::fopen(tileset_list_path, "rb");
    if (list == NULL) {
        error = std::string("cannot open original tileset list: ") + tileset_list_path;
        return false;
    }

    char line[256];
    while (std::fgets(line, sizeof(line), list) != NULL) {
        std::size_t length = std::strlen(line);
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
            line[--length] = '\0';
        }
        if (length == 0) continue;
        if (std::strchr(line, '/') != NULL || std::strchr(line, '\\') != NULL) {
            std::fclose(list);
            error = "invalid filename in original tileset list";
            return false;
        }

        const std::string image_name(line);
        const std::size_t dot = image_name.find_last_of('.');
        const std::string texture_name = dot == std::string::npos
            ? image_name + ".t3x" : image_name.substr(0, dot) + ".t3x";
        const std::string texture_path = std::string(texture_directory) + "/" + texture_name;
        C2D_SpriteSheet sheet = C2D_SpriteSheetLoad(texture_path.c_str());
        if (sheet == NULL || C2D_SpriteSheetCount(sheet) != 1) {
            if (sheet != NULL) C2D_SpriteSheetFree(sheet);
            std::fclose(list);
            error = "cannot load original map texture sheet: " + texture_path;
            return false;
        }

        const std::size_t index = sheets_.size();
        sheets_.push_back(sheet);
        image_names_.push_back(image_name);
        image_indices_[image_name] = index;
    }

    const bool failed = std::ferror(list) != 0;
    std::fclose(list);
    if (failed || sheets_.empty()) {
        error = "failed while reading original tileset list";
        return false;
    }
    return true;
}

bool MapRenderer::prepare_tile_sources(std::string &error)
{
    for (const map::Tileset &tileset : document_.tilesets) {
        const std::string image_name = basename_of(tileset.image_source);
        const std::map<std::string, std::size_t>::const_iterator image_index =
            image_indices_.find(image_name);
        if (image_index == image_indices_.end()) continue;
        if (tileset.columns <= 0 || tileset.tile_count == 0 || tileset.image_width <= 0
                || tileset.image_height <= 0 || tileset.image_width % tileset.columns != 0
                || tileset.image_height % static_cast<int>(
                    (tileset.tile_count + tileset.columns - 1) / tileset.columns) != 0) {
            error = "invalid original tileset grid: " + tileset.name;
            return false;
        }

        const C2D_Image whole = C2D_SpriteSheetGetImage(sheets_[image_index->second], 0);
        if (whole.subtex == NULL || whole.subtex->width < tileset.image_width
                || whole.subtex->height < tileset.image_height) {
            error = "original tileset image is smaller than TMX metadata: " + tileset.name;
            return false;
        }

        std::vector<Tex3DS_SubTexture> &subtextures = tile_subtextures_[image_name];
        subtextures.resize(tileset.tile_count);
        const float u_range = whole.subtex->right - whole.subtex->left;
        const float v_range = whole.subtex->bottom - whole.subtex->top;
        for (std::uint32_t local_id = 0; local_id < tileset.tile_count; ++local_id) {
            const int tile_x = static_cast<int>(local_id % static_cast<std::uint32_t>(tileset.columns));
            const int tile_y = static_cast<int>(local_id / static_cast<std::uint32_t>(tileset.columns));
            Tex3DS_SubTexture &subtexture = subtextures[local_id];
            subtexture.width = static_cast<u16>(tileset.declared_tile_width);
            subtexture.height = static_cast<u16>(tileset.declared_tile_height);
            subtexture.left = whole.subtex->left
                + (tile_x * tileset.declared_tile_width / static_cast<float>(whole.subtex->width)) * u_range;
            subtexture.right = whole.subtex->left
                + ((tile_x + 1) * tileset.declared_tile_width / static_cast<float>(whole.subtex->width)) * u_range;
            subtexture.top = whole.subtex->top
                + (tile_y * tileset.declared_tile_height / static_cast<float>(whole.subtex->height)) * v_range;
            subtexture.bottom = whole.subtex->top
                + ((tile_y + 1) * tileset.declared_tile_height / static_cast<float>(whole.subtex->height)) * v_range;
        }
    }
    return true;
}

bool MapRenderer::validate_visible_layers(std::string &error) const
{
    static const char *layer_names[] = {"base", "ground", "objects", "above", "top"};
    for (const char *name : layer_names) {
        const map::Layer *layer = map::find_layer(document_, name);
        if (layer == NULL) continue;
        for (std::uint32_t gid : layer->gids) {
            if (gid == 0) continue;
            map::TileSource source;
            if (!map::resolve_tile_source(document_, gid, source)) {
                error = std::string("cannot resolve original GID in ") + name;
                return false;
            }
            const std::string image_name = basename_of(document_.tilesets[source.tileset_index].image_source);
            const std::map<std::string, std::vector<Tex3DS_SubTexture> >::const_iterator tiles =
                tile_subtextures_.find(image_name);
            if (tiles == tile_subtextures_.end() || source.local_id >= tiles->second.size()) {
                error = "original tile image is not bundled: " + image_name;
                return false;
            }
        }
    }
    return true;
}

void MapRenderer::draw_layer(
    const std::string &name,
    const map::ViewTransform &view,
    float depth
) const
{
    const map::Layer *layer = map::find_layer(document_, name);
    if (layer == NULL) return;
    for (int y = view.camera_y; y < view.camera_y + view.visible_rows; ++y) {
        for (int x = view.camera_x; x < view.camera_x + view.visible_columns; ++x) {
            const std::uint32_t gid = layer->gid_at(x, y, document_.height);
            if (gid == 0) continue;
            map::GridPosition screen;
            if (!map::map_to_screen(view, {x, y}, screen)) continue;
            draw_gid(gid, screen.x, screen.y, depth);
        }
    }
}

void MapRenderer::draw_gid(std::uint32_t gid, int screen_x, int screen_y, float depth) const
{
    map::TileSource source;
    if (!map::resolve_tile_source(document_, gid, source)) return;
    const map::Tileset &tileset = document_.tilesets[source.tileset_index];
    const std::string image_name = basename_of(tileset.image_source);
    const std::map<std::string, std::size_t>::const_iterator sheet = image_indices_.find(image_name);
    const std::map<std::string, std::vector<Tex3DS_SubTexture> >::const_iterator tiles =
        tile_subtextures_.find(image_name);
    if (sheet == image_indices_.end() || tiles == tile_subtextures_.end()
            || source.local_id >= tiles->second.size()) return;
    C2D_Image image = C2D_SpriteSheetGetImage(sheets_[sheet->second], 0);
    image.subtex = &tiles->second[source.local_id];
    C2D_DrawImageAt(image, static_cast<float>(screen_x), static_cast<float>(screen_y), depth, NULL);
}

void MapRenderer::draw(int screen_width, int screen_height) const
{
    if (!initialized_) return;
    const map::ViewTransform view = map::camera_for_player(
        document_, player_, screen_width, screen_height
    );
    draw_layer("base", view, 0.50f);
    draw_layer("ground", view, 0.45f);
    draw_layer("objects", view, 0.40f);

    map::GridPosition player_screen;
    if (map::map_to_screen(view, player_, player_screen)) {
        C2D_DrawImageAt(player_image_, static_cast<float>(player_screen.x),
            static_cast<float>(player_screen.y), 0.35f, NULL);
    }

    draw_layer("above", view, 0.30f);
    draw_layer("top", view, 0.25f);
}

bool MapRenderer::move_player(int dx, int dy)
{
    return initialized_ && map::move_player(document_, player_, dx, dy);
}

map::GridPosition MapRenderer::player_position() const
{
    return player_;
}

}
}