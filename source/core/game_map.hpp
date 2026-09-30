#ifndef ANDORSTRAIL_GAME_MAP_HPP
#define ANDORSTRAIL_GAME_MAP_HPP

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace andortrail {
namespace map {

struct Property {
    std::string name;
    std::string value;
    bool has_value;
};

struct Rect {
    int x;
    int y;
    int width;
    int height;
};

struct Tileset {
    std::string name;
    std::string image_source;
    std::uint32_t first_gid;
    std::uint32_t tile_count;
    int columns;
    int declared_tile_width;
    int declared_tile_height;
    int image_width;
    int image_height;
};

struct TileSource {
    std::size_t tileset_index;
    std::uint32_t local_id;
    int pixel_x;
    int pixel_y;
};

struct GridPosition {
    int x;
    int y;
};

struct ViewTransform {
    int camera_x;
    int camera_y;
    int visible_columns;
    int visible_rows;
    int screen_offset_x;
    int screen_offset_y;
    int tile_size;
};

struct Layer {
    std::string name;
    std::vector<std::uint32_t> gids;

    std::uint32_t gid_at(int x, int y, int map_height) const;
};

struct Object {
    std::string name;
    std::string type;
    bool has_name;
    bool has_type;
    int pixel_x;
    int pixel_y;
    int pixel_width;
    int pixel_height;
    Rect tile_rect;
    std::vector<Property> properties;
};

struct ObjectGroup {
    std::string name;
    std::vector<Property> properties;
    std::vector<Object> objects;
    bool active;
};

struct Document {
    std::string name;
    int width;
    int height;
    int tile_width;
    int tile_height;
    int declared_tile_width;
    int declared_tile_height;
    std::vector<Property> properties;
    std::vector<Tileset> tilesets;
    std::vector<Layer> layers;
    std::vector<ObjectGroup> object_groups;
};

struct Requirement {
    std::string type;
    std::string id;
    bool has_id;
    int value;
    bool negate;
    bool recognized_type;
    bool valid;
    bool has_chance;
    int chance_current;
    int chance_max;
};

enum class EventKind {
    Sign,
    MapChange,
    Key,
    Rest,
    Container,
    Script
};

enum class ScriptWhen {
    Enter,
    Step,
    Round,
    Always
};

struct Event {
    EventKind kind;
    std::string source_type;
    std::string name;
    std::string group;
    Rect tile_rect;
    bool active;
    std::string phrase_id;
    std::string target_map;
    std::string target_place;
    std::string drop_list_id;
    ScriptWhen script_when;
    Requirement requirement;
    bool has_requirement;
    std::vector<Property> properties;
};

struct SpawnArea {
    std::string name;
    std::string group;
    Rect tile_rect;
    std::string spawn_group;
    int quantity;
    int respawn_speed;
    bool active_for_new_game;
    bool ignore_areas;
    std::vector<Property> properties;
};

struct ReplacementLayers {
    std::string base;
    std::string ground;
    std::string objects;
    std::string above;
    std::string top;
    std::string walkable;
};

struct Replacement {
    std::string name;
    std::string group;
    Rect tile_rect;
    ReplacementLayers layers;
    Requirement requirement;
    std::vector<Property> properties;
};

struct TranslatedDocument {
    std::string name;
    int width;
    int height;
    bool outdoors;
    std::string color_filter;
    std::vector<std::string> initially_active_groups;
    std::vector<Event> events;
    std::vector<SpawnArea> spawn_areas;
    std::vector<Replacement> replacements;
};

struct WorldMapReference {
    std::string map_name;
    int x;
    int y;
    std::string area_id;
    bool has_area;
};

struct NamedArea {
    std::string id;
    std::string source_name;
    std::string translated_name;
    std::string type;
    std::vector<std::string> map_names;
};

struct WorldMapSegment {
    std::string id;
    int source_x;
    int source_y;
    std::vector<WorldMapReference> maps;
    std::vector<NamedArea> named_areas;
};

struct WorldMap {
    std::vector<WorldMapSegment> segments;
};

typedef std::function<std::string(const std::string &)> TranslationFunction;

struct CatalogMap {
    Document source;
    TranslatedDocument translated;
};

struct Catalog {
    std::vector<CatalogMap> maps;
    WorldMap world_map;
};

bool load_tmx(
    const std::string &path,
    const std::string &resource_name,
    bool include_layers,
    Document &document,
    std::string *error
);
bool translate_document(
    const Document &document,
    TranslatedDocument &translated,
    std::string *error
);
bool load_world_map(
    const std::string &path,
    const std::vector<std::string> &loaded_map_names,
    const TranslationFunction &translate,
    WorldMap &world_map,
    std::string *error
);
bool load_catalog(
    const std::string &directory,
    const TranslationFunction &translate,
    Catalog &catalog,
    std::string *error
);

const Property *find_property(const std::vector<Property> &properties, const std::string &name);
const Layer *find_layer(const Document &document, const std::string &name);
bool resolve_tile_source(const Document &document, std::uint32_t gid, TileSource &source);
bool is_walkable(const Document &document, int x, int y);
bool move_player(const Document &document, GridPosition &position, int dx, int dy);
ViewTransform camera_for_player(
    const Document &document,
    GridPosition player,
    int screen_width,
    int screen_height
);
bool map_to_screen(const ViewTransform &view, GridPosition map_position, GridPosition &screen_position);

}
}

#endif