#include "game_map.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include <tinyxml2.h>
#include <zlib.h>

namespace andortrail {
namespace map {
namespace {

const int android_tile_size = 32;

std::string lower_ascii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool equals_ignore_case(const std::string &left, const std::string &right)
{
    return lower_ascii(left) == lower_ascii(right);
}

std::string attribute_string(const tinyxml2::XMLElement *element, const char *name)
{
    const char *value = element->Attribute(name);
    return value != NULL ? value : "";
}

int attribute_integer(const tinyxml2::XMLElement *element, const char *name, int fallback)
{
    int value = fallback;
    element->QueryIntAttribute(name, &value);
    return value;
}

Property read_property(const tinyxml2::XMLElement *element)
{
    Property property;
    property.name = attribute_string(element, "name");
    const char *value = element->Attribute("value");
    property.has_value = value != NULL;
    property.value = value != NULL ? value : "";
    return property;
}

std::vector<Property> read_properties(const tinyxml2::XMLElement *parent)
{
    std::vector<Property> result;
    const tinyxml2::XMLElement *properties = parent->FirstChildElement("properties");
    if (properties == NULL) return result;
    for (const tinyxml2::XMLElement *property = properties->FirstChildElement("property");
            property != NULL; property = property->NextSiblingElement("property")) {
        result.push_back(read_property(property));
    }
    return result;
}

int java_round_tile(int pixels)
{
    return static_cast<int>(std::floor(static_cast<double>(pixels) / android_tile_size + 0.5));
}

int base64_value(unsigned char character)
{
    if (character >= 'A' && character <= 'Z') return character - 'A';
    if (character >= 'a' && character <= 'z') return character - 'a' + 26;
    if (character >= '0' && character <= '9') return character - '0' + 52;
    if (character == '+') return 62;
    if (character == '/') return 63;
    return -1;
}

bool decode_base64(const std::string &text, std::vector<unsigned char> &output)
{
    unsigned int accumulator = 0;
    int bits = 0;
    bool padding_started = false;
    for (unsigned char character : text) {
        if (std::isspace(character)) continue;
        if (character == '=') {
            padding_started = true;
            continue;
        }
        if (padding_started) return false;
        const int value = base64_value(character);
        if (value < 0) return false;
        accumulator = (accumulator << 6) | static_cast<unsigned int>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<unsigned char>((accumulator >> bits) & 0xff));
        }
    }
    return bits == 0 || bits == 2 || bits == 4;
}

bool decompress_layer(
    const std::vector<unsigned char> &compressed,
    const std::string &compression,
    std::size_t expected_size,
    std::vector<unsigned char> &output,
    std::string *error
)
{
    int window_bits = MAX_WBITS;
    if (equals_ignore_case(compression, "gzip")) window_bits += 16;
    else if (!equals_ignore_case(compression, "zlib")) {
        if (error != NULL) *error = "unsupported TMX compression: " + compression;
        return false;
    }
    if (compressed.size() > std::numeric_limits<uInt>::max()
            || expected_size > std::numeric_limits<uInt>::max()) {
        if (error != NULL) *error = "TMX layer exceeds zlib input limits";
        return false;
    }

    output.assign(expected_size, 0);
    z_stream stream = {};
    stream.next_in = const_cast<Bytef *>(compressed.data());
    stream.avail_in = static_cast<uInt>(compressed.size());
    stream.next_out = output.data();
    stream.avail_out = static_cast<uInt>(output.size());
    if (inflateInit2(&stream, window_bits) != Z_OK) {
        if (error != NULL) *error = "zlib initialization failed";
        return false;
    }
    const int status = inflate(&stream, Z_FINISH);
    const bool valid = status == Z_STREAM_END && stream.total_out == expected_size;
    inflateEnd(&stream);
    if (!valid) {
        output.clear();
        if (error != NULL) *error = "TMX layer decompressed to an unexpected size";
        return false;
    }
    return true;
}

bool read_layer(
    const tinyxml2::XMLElement *element,
    int map_width,
    int map_height,
    Layer &layer,
    std::string *error
)
{
    layer.name = attribute_string(element, "name");
    const tinyxml2::XMLElement *data = element->FirstChildElement("data");
    if (data == NULL || data->GetText() == NULL) {
        if (error != NULL) *error = "TMX layer has no data text: " + layer.name;
        return false;
    }

    std::vector<unsigned char> compressed;
    if (!decode_base64(data->GetText(), compressed)) {
        if (error != NULL) *error = "invalid base64 in TMX layer: " + layer.name;
        return false;
    }
    const std::size_t width = static_cast<std::size_t>(map_width);
    const std::size_t height = static_cast<std::size_t>(map_height);
    if (width > std::numeric_limits<std::size_t>::max() / height / 4) {
        if (error != NULL) *error = "TMX layer dimensions overflow";
        return false;
    }
    std::vector<unsigned char> raw;
    if (!decompress_layer(
            compressed, attribute_string(data, "compression"), width * height * 4,
            raw, error)) return false;

    layer.gids.resize(width * height);
    for (int y = 0; y < map_height; ++y) {
        for (int x = 0; x < map_width; ++x) {
            const std::size_t source = (static_cast<std::size_t>(y) * width + x) * 4;
            const std::uint32_t gid = static_cast<std::uint32_t>(raw[source])
                | (static_cast<std::uint32_t>(raw[source + 1]) << 8)
                | (static_cast<std::uint32_t>(raw[source + 2]) << 16)
                | (static_cast<std::uint32_t>(raw[source + 3]) << 24);
            layer.gids[static_cast<std::size_t>(x) * height + y] = gid;
        }
    }
    return true;
}

Object read_object(const tinyxml2::XMLElement *element)
{
    Object object;
    const char *name = element->Attribute("name");
    const char *type = element->Attribute("type");
    object.has_name = name != NULL;
    object.has_type = type != NULL;
    object.name = name != NULL ? name : "";
    object.type = type != NULL ? type : "";
    object.pixel_x = attribute_integer(element, "x", -1);
    object.pixel_y = attribute_integer(element, "y", -1);
    object.pixel_width = attribute_integer(element, "width", -1);
    object.pixel_height = attribute_integer(element, "height", -1);
    object.tile_rect = {
        java_round_tile(object.pixel_x),
        java_round_tile(object.pixel_y),
        java_round_tile(object.pixel_width),
        java_round_tile(object.pixel_height),
    };
    object.properties = read_properties(element);
    return object;
}

ObjectGroup read_object_group(const tinyxml2::XMLElement *element)
{
    ObjectGroup group;
    group.name = attribute_string(element, "name");
    group.properties = read_properties(element);
    const Property *active = find_property(group.properties, "active");
    group.active = active == NULL || (active->has_value && equals_ignore_case(active->value, "true"));
    for (const tinyxml2::XMLElement *object = element->FirstChildElement("object");
            object != NULL; object = object->NextSiblingElement("object")) {
        group.objects.push_back(read_object(object));
    }
    return group;
}

bool parse_integer(const std::string &text, int fallback, int &value)
{
    if (text.empty()) {
        value = fallback;
        return true;
    }
    errno = 0;
    char *end = NULL;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0'
            || parsed < INT_MIN || parsed > INT_MAX) return false;
    value = static_cast<int>(parsed);
    return true;
}

bool parse_chance(const std::string &text, int &current, int &maximum)
{
    if (text == "100") { current = 1; maximum = 1; return true; }
    if (text == "70") { current = 70; maximum = 100; return true; }
    if (text == "30") { current = 30; maximum = 100; return true; }
    if (text == "25") { current = 25; maximum = 100; return true; }
    if (text == "20") { current = 20; maximum = 100; return true; }
    if (text == "10") { current = 10; maximum = 100; return true; }
    if (text == "5") { current = 5; maximum = 100; return true; }
    if (text == "1") { current = 1; maximum = 100; return true; }
    if (text == "1/1000") { current = 1; maximum = 1000; return true; }
    if (text == "1/10000") { current = 1; maximum = 10000; return true; }

    const std::size_t separator = text.find('/');
    if (separator == std::string::npos) {
        maximum = 100;
        return parse_integer(text, 10, current);
    }
    if (text.find('/', separator + 1) != std::string::npos) return false;
    if (!parse_integer(text.substr(0, separator), 1, current)
            || !parse_integer(text.substr(separator + 1), 100, maximum)) return false;
    return true;
}

bool requirement_type_exists(const std::string &type)
{
    static const char *types[] = {
        "questProgress", "questLatestProgress", "inventoryRemove", "inventoryKeep",
        "wear", "skillLevel", "killedMonster", "timerElapsed", "usedItem", "spentGold",
        "consumedBonemeals", "hasActorCondition", "factionScore", "random",
        "factionScoreEquals", "wearRemove", "date", "dateEquals", "time", "timeEquals",
        "skillIncrease",
    };
    for (const char *candidate : types) {
        if (type == candidate) return true;
    }
    return false;
}

bool requirement_is_valid(const Requirement &requirement)
{
    if (!requirement.recognized_type) return false;
    const std::string &type = requirement.type;
    if (type == "consumedBonemeals") return requirement.value >= 0;
    if (type == "hasActorCondition" || type == "factionScore" || type == "factionScoreEquals") {
        return requirement.has_id;
    }
    if (type == "inventoryKeep" || type == "inventoryRemove" || type == "wear"
            || type == "wearRemove" || type == "usedItem" || type == "killedMonster"
            || type == "questLatestProgress" || type == "questProgress" || type == "skillLevel"
            || type == "skillIncrease") {
        return requirement.has_id && requirement.value >= 0;
    }
    if (type == "spentGold" || type == "date" || type == "dateEquals"
            || type == "time" || type == "timeEquals") return requirement.value >= 0;
    if (type == "random") return requirement.has_chance;
    if (type == "timerElapsed") return requirement.has_id && requirement.value >= 0;
    return false;
}

bool parse_requirement(
    const Object &object,
    Requirement &requirement,
    std::string *error
)
{
    requirement.type = "questProgress";
    requirement.id.clear();
    requirement.has_id = false;
    requirement.value = 0;
    requirement.negate = false;
    requirement.recognized_type = true;
    requirement.valid = false;
    requirement.has_chance = false;
    requirement.chance_current = 0;
    requirement.chance_max = 0;

    if (const Property *property = find_property(object.properties, "requireType")) {
        requirement.type = property->value;
        requirement.recognized_type = requirement_type_exists(requirement.type);
    }
    if (const Property *property = find_property(object.properties, "requireId")) {
        requirement.has_id = true;
        requirement.id = property->value;
    }
    if (const Property *property = find_property(object.properties, "requireValue")) {
        if (!parse_integer(property->value, 0, requirement.value)) {
            if (error != NULL) *error = "invalid requireValue on map object " + object.name;
            return false;
        }
    }
    if (const Property *property = find_property(object.properties, "requireNegation")) {
        requirement.negate = equals_ignore_case(property->value, "true");
    }

    if (requirement.type == "random" && requirement.has_id) {
        if (!parse_chance(requirement.id, requirement.chance_current, requirement.chance_max)) {
            if (error != NULL) *error = "invalid random requirement chance on map object " + object.name;
            return false;
        }
        requirement.has_chance = true;
        requirement.id.clear();
        requirement.has_id = false;
    }
    requirement.valid = requirement_is_valid(requirement);
    return true;
}

void parse_legacy_quest_progress(const std::string &value, Requirement &requirement)
{
    if (value.empty()) return;
    const std::size_t separator = value.find(':');
    requirement.type = "questProgress";
    requirement.id = value.substr(0, separator);
    requirement.has_id = true;
    requirement.value = 0;
    requirement.negate = false;
    requirement.recognized_type = true;
    requirement.has_chance = false;
    if (separator != std::string::npos) {
        int progress = 0;
        if (parse_integer(value.substr(separator + 1), 0, progress)) requirement.value = progress;
    }
    requirement.valid = requirement_is_valid(requirement);
}

bool parse_required_integer(
    const std::vector<Property> &properties,
    const std::string &name,
    int fallback,
    int &result,
    const std::string &object_name,
    std::string *error
)
{
    const Property *property = find_property(properties, name);
    if (property == NULL) {
        result = fallback;
        return true;
    }
    if (!parse_integer(property->value, fallback, result)) {
        if (error != NULL) *error = "invalid " + name + " on map object " + object_name;
        return false;
    }
    return true;
}

bool property_boolean(const std::vector<Property> &properties, const std::string &name, bool fallback)
{
    const Property *property = find_property(properties, name);
    return property == NULL ? fallback : equals_ignore_case(property->value, "true");
}

Event make_event(EventKind kind, const Object &object, const ObjectGroup &group)
{
    Event event;
    event.kind = kind;
    event.source_type = object.type;
    event.name = object.name;
    event.group = group.name;
    event.tile_rect = object.tile_rect;
    event.active = group.active;
    event.phrase_id.clear();
    event.target_map.clear();
    event.target_place.clear();
    event.drop_list_id.clear();
    event.script_when = ScriptWhen::Enter;
    event.has_requirement = false;
    event.properties = object.properties;
    event.requirement = {};
    return event;
}

bool has_type(const Object &object, const char *type, bool ignore_case)
{
    if (!object.has_type) return false;
    return ignore_case ? equals_ignore_case(object.type, type) : object.type == type;
}

}

std::uint32_t Layer::gid_at(int x, int y, int map_height) const
{
    if (x < 0 || y < 0 || map_height <= 0
            || static_cast<std::size_t>(x) * map_height + y >= gids.size()) return 0;
    return gids[static_cast<std::size_t>(x) * map_height + y];
}

const Property *find_property(const std::vector<Property> &properties, const std::string &name)
{
    for (std::vector<Property>::const_reverse_iterator property = properties.rbegin();
            property != properties.rend(); ++property) {
        if (equals_ignore_case(property->name, name)) return &*property;
    }
    return NULL;
}

const Layer *find_layer(const Document &document, const std::string &name)
{
    for (std::vector<Layer>::const_reverse_iterator layer = document.layers.rbegin();
            layer != document.layers.rend(); ++layer) {
        if (equals_ignore_case(layer->name, name)) return &*layer;
    }
    return NULL;
}

bool resolve_tile_source(const Document &document, std::uint32_t gid, TileSource &source)
{
    if (gid == 0) return false;
    for (std::size_t index = document.tilesets.size(); index > 0; --index) {
        const Tileset &tileset = document.tilesets[index - 1];
        if (gid < tileset.first_gid) continue;
        const std::uint32_t local_id = gid - tileset.first_gid;
        if (tileset.columns <= 0 || tileset.tile_count == 0 || local_id >= tileset.tile_count
                || tileset.declared_tile_width <= 0 || tileset.declared_tile_height <= 0) {
            return false;
        }
        source.tileset_index = index - 1;
        source.local_id = local_id;
        source.pixel_x = static_cast<int>(local_id % static_cast<std::uint32_t>(tileset.columns))
            * tileset.declared_tile_width;
        source.pixel_y = static_cast<int>(local_id / static_cast<std::uint32_t>(tileset.columns))
            * tileset.declared_tile_height;
        return true;
    }
    return false;
}

bool is_walkable(const Document &document, int x, int y)
{
    if (x < 0 || y < 0 || x >= document.width || y >= document.height) return false;
    const Layer *walkable = find_layer(document, "walkable");
    if (walkable == NULL) return false;
    return walkable->gid_at(x, y, document.height) == 0;
}

bool move_player(const Document &document, GridPosition &position, int dx, int dy)
{
    if (std::abs(dx) + std::abs(dy) != 1) return false;
    const GridPosition next = {position.x + dx, position.y + dy};
    if (!is_walkable(document, next.x, next.y)) return false;
    position = next;
    return true;
}

bool resolve_map_transition(
    const TranslatedDocument &source,
    const TranslatedDocument &destination,
    const std::string &event_name,
    GridPosition entered_position,
    MapTransition &transition
)
{
    for (const Event &event : source.events) {
        if (event.kind != EventKind::MapChange || !event.active || event.name != event_name) continue;
        if (entered_position.x < event.tile_rect.x || entered_position.y < event.tile_rect.y
                || entered_position.x >= event.tile_rect.x + event.tile_rect.width
                || entered_position.y >= event.tile_rect.y + event.tile_rect.height
                || event.target_map != destination.name || event.target_place.empty()) continue;

        for (const Event &place : destination.events) {
            if (place.kind != EventKind::MapChange || !place.active
                    || place.name != event.target_place || place.tile_rect.width <= 0
                    || place.tile_rect.height <= 0) continue;
            const int offset_x = entered_position.x - event.tile_rect.x;
            const int offset_y = entered_position.y - event.tile_rect.y;
            transition.target_map = event.target_map;
            transition.target_place = event.target_place;
            transition.destination = {
                place.tile_rect.x + std::min(offset_x, place.tile_rect.width - 1),
                place.tile_rect.y + std::min(offset_y, place.tile_rect.height - 1),
            };
            return true;
        }
    }
    return false;
}

ViewTransform camera_for_player(
    const Document &document,
    GridPosition player,
    int screen_width,
    int screen_height
)
{
    const int tile_size = document.tile_width > 0 ? document.tile_width : android_tile_size;
    const int screen_columns = screen_width > 0 ? screen_width / tile_size : 0;
    const int screen_rows = screen_height > 0 ? screen_height / tile_size : 0;
    ViewTransform view = {};
    view.tile_size = tile_size;
    view.visible_columns = std::min(document.width, screen_columns);
    view.visible_rows = std::min(document.height, screen_rows);
    if (view.visible_columns <= 0 || view.visible_rows <= 0) return view;

    view.camera_x = std::max(0, player.x - view.visible_columns / 2);
    view.camera_y = std::max(0, player.y - view.visible_rows / 2);
    view.camera_x = std::min(view.camera_x, document.width - view.visible_columns);
    view.camera_y = std::min(view.camera_y, document.height - view.visible_rows);
    view.screen_offset_x = (screen_width - view.visible_columns * tile_size) / 2;
    view.screen_offset_y = (screen_height - view.visible_rows * tile_size) / 2;
    return view;
}

bool map_to_screen(
    const ViewTransform &view,
    GridPosition map_position,
    GridPosition &screen_position
)
{
    if (map_position.x < view.camera_x || map_position.y < view.camera_y
            || map_position.x >= view.camera_x + view.visible_columns
            || map_position.y >= view.camera_y + view.visible_rows) return false;
    screen_position.x = view.screen_offset_x + (map_position.x - view.camera_x) * view.tile_size;
    screen_position.y = view.screen_offset_y + (map_position.y - view.camera_y) * view.tile_size;
    return true;
}

bool load_tmx(
    const std::string &path,
    const std::string &resource_name,
    bool include_layers,
    Document &document,
    std::string *error
)
{
    tinyxml2::XMLDocument xml;
    const tinyxml2::XMLError result = xml.LoadFile(path.c_str());
    if (result != tinyxml2::XML_SUCCESS) {
        if (error != NULL) *error = xml.ErrorStr() != NULL ? xml.ErrorStr() : "TMX XML parse failed";
        return false;
    }
    const tinyxml2::XMLElement *root = xml.FirstChildElement("map");
    if (root == NULL) {
        if (error != NULL) *error = "TMX document has no map root";
        return false;
    }

    Document parsed;
    parsed.name = resource_name;
    parsed.width = attribute_integer(root, "width", -1);
    parsed.height = attribute_integer(root, "height", -1);
    parsed.tile_width = android_tile_size;
    parsed.tile_height = android_tile_size;
    parsed.declared_tile_width = attribute_integer(root, "tilewidth", android_tile_size);
    parsed.declared_tile_height = attribute_integer(root, "tileheight", android_tile_size);
    if (parsed.width <= 0 || parsed.height <= 0) {
        if (error != NULL) *error = "TMX map dimensions must be positive";
        return false;
    }

    for (const tinyxml2::XMLElement *element = root->FirstChildElement();
            element != NULL; element = element->NextSiblingElement()) {
        const char *element_name = element->Name();
        if (std::strcmp(element_name, "property") == 0) {
            parsed.properties.push_back(read_property(element));
        } else if (std::strcmp(element_name, "objectgroup") == 0) {
            parsed.object_groups.push_back(read_object_group(element));
        } else if (include_layers && std::strcmp(element_name, "tileset") == 0) {
            Tileset tileset;
            tileset.name = attribute_string(element, "name");
            tileset.image_source.clear();
            tileset.first_gid = static_cast<std::uint32_t>(attribute_integer(element, "firstgid", 1));
            tileset.tile_count = static_cast<std::uint32_t>(attribute_integer(element, "tilecount", 0));
            tileset.columns = attribute_integer(element, "columns", 0);
            tileset.declared_tile_width = attribute_integer(element, "tilewidth", android_tile_size);
            tileset.declared_tile_height = attribute_integer(element, "tileheight", android_tile_size);
            tileset.image_width = 0;
            tileset.image_height = 0;
            if (const tinyxml2::XMLElement *image = element->FirstChildElement("image")) {
                tileset.image_source = attribute_string(image, "source");
                tileset.image_width = attribute_integer(image, "width", 0);
                tileset.image_height = attribute_integer(image, "height", 0);
            }
            if (tileset.columns <= 0 && tileset.image_width > 0
                    && tileset.declared_tile_width > 0
                    && tileset.image_width % tileset.declared_tile_width == 0) {
                tileset.columns = tileset.image_width / tileset.declared_tile_width;
            }
            if (tileset.tile_count == 0 && tileset.columns > 0 && tileset.image_height > 0
                    && tileset.declared_tile_height > 0
                    && tileset.image_height % tileset.declared_tile_height == 0) {
                tileset.tile_count = static_cast<std::uint32_t>(tileset.columns
                    * (tileset.image_height / tileset.declared_tile_height));
            }
            parsed.tilesets.push_back(tileset);
        } else if (include_layers && std::strcmp(element_name, "layer") == 0) {
            Layer layer;
            if (!read_layer(element, parsed.width, parsed.height, layer, error)) return false;
            parsed.layers.push_back(layer);
        }
    }
    document = std::move(parsed);
    if (error != NULL) error->clear();
    return true;
}

bool translate_document(
    const Document &document,
    TranslatedDocument &translated,
    std::string *error
)
{
    TranslatedDocument result;
    result.name = document.name;
    result.width = document.width;
    result.height = document.height;
    result.outdoors = false;
    result.color_filter.clear();

    if (const Property *property = find_property(document.properties, "outdoors")) {
        int outdoors = 0;
        if (!parse_integer(property->value, 0, outdoors)) {
            if (error != NULL) *error = "invalid outdoors map property in " + document.name;
            return false;
        }
        result.outdoors = outdoors != 0;
    }
    if (const Property *property = find_property(document.properties, "colorfilter")) {
        result.color_filter = property->value;
    }

    for (const ObjectGroup &group : document.object_groups) {
        if (group.active) result.initially_active_groups.push_back(group.name);
        for (const Object &object : group.objects) {
            if (!object.has_type) continue;

            if (has_type(object, "spawn", true)) {
                SpawnArea area;
                area.name = object.name;
                area.group = group.name;
                area.tile_rect = object.tile_rect;
                area.spawn_group = object.name;
                area.quantity = 1;
                area.respawn_speed = 10;
                area.active_for_new_game = true;
                area.ignore_areas = false;
                area.properties = object.properties;
                if (const Property *property = find_property(object.properties, "spawngroup")) {
                    area.spawn_group = property->value;
                }
                if (!parse_required_integer(object.properties, "quantity", 1, area.quantity,
                        object.name, error)
                        || !parse_required_integer(object.properties, "respawnspeed", 10,
                            area.respawn_speed, object.name, error)) return false;
                area.active_for_new_game = property_boolean(object.properties, "active", true);
                area.ignore_areas = property_boolean(object.properties, "ignoreAreas", false);
                result.spawn_areas.push_back(area);
                continue;
            }

            if (has_type(object, "replace", false)) {
                Replacement replacement;
                replacement.name = object.name;
                replacement.group = group.name;
                replacement.tile_rect = object.tile_rect;
                replacement.properties = object.properties;
                replacement.layers = {};
                const char *layer_properties[] = {
                    "base", "ground", "objects", "above", "top", "walkable",
                };
                std::string *layer_values[] = {
                    &replacement.layers.base, &replacement.layers.ground,
                    &replacement.layers.objects, &replacement.layers.above,
                    &replacement.layers.top, &replacement.layers.walkable,
                };
                for (std::size_t index = 0; index < 6; ++index) {
                    if (const Property *property = find_property(object.properties, layer_properties[index])) {
                        *layer_values[index] = property->value;
                    }
                }
                if (!parse_requirement(object, replacement.requirement, error)) return false;
                if (!replacement.requirement.valid) {
                    parse_legacy_quest_progress(object.name, replacement.requirement);
                }
                if (replacement.requirement.valid) result.replacements.push_back(replacement);
                continue;
            }

            Event event = {};
            bool recognized = true;
            if (has_type(object, "sign", true)) {
                event = make_event(EventKind::Sign, object, group);
                event.phrase_id = object.name;
            } else if (has_type(object, "mapchange", true)) {
                event = make_event(EventKind::MapChange, object, group);
                event.phrase_id = object.name;
                if (const Property *property = find_property(object.properties, "map")) {
                    event.target_map = property->value;
                }
                if (const Property *property = find_property(object.properties, "place")) {
                    event.target_place = property->value;
                }
            } else if (has_type(object, "key", true)) {
                event = make_event(EventKind::Key, object, group);
                if (const Property *property = find_property(object.properties, "phrase")) {
                    event.phrase_id = property->value;
                }
                if (!parse_requirement(object, event.requirement, error)) return false;
                event.has_requirement = event.requirement.recognized_type;
            } else if (has_type(object, "rest", false)) {
                event = make_event(EventKind::Rest, object, group);
            } else if (has_type(object, "container", false)) {
                event = make_event(EventKind::Container, object, group);
                event.drop_list_id = object.name;
            } else if (has_type(object, "script", true)) {
                event = make_event(EventKind::Script, object, group);
                event.phrase_id = object.name;
                if (const Property *property = find_property(object.properties, "when")) {
                    if (equals_ignore_case(property->value, "step")) event.script_when = ScriptWhen::Step;
                    else if (equals_ignore_case(property->value, "round")) event.script_when = ScriptWhen::Round;
                    else if (equals_ignore_case(property->value, "always")) event.script_when = ScriptWhen::Always;
                }
            } else {
                recognized = false;
            }
            if (recognized) result.events.push_back(event);
        }
    }

    translated = result;
    if (error != NULL) error->clear();
    return true;
}

bool load_world_map(
    const std::string &path,
    const std::vector<std::string> &loaded_map_names,
    const TranslationFunction &translate,
    WorldMap &world_map,
    std::string *error
)
{
    tinyxml2::XMLDocument xml;
    const tinyxml2::XMLError result = xml.LoadFile(path.c_str());
    if (result != tinyxml2::XML_SUCCESS) {
        if (error != NULL) *error = xml.ErrorStr() != NULL ? xml.ErrorStr() : "world-map XML parse failed";
        return false;
    }
    const tinyxml2::XMLElement *root = xml.FirstChildElement("worldmap");
    if (root == NULL) {
        if (error != NULL) *error = "world-map document has no worldmap root";
        return false;
    }

    const std::set<std::string> loaded(loaded_map_names.begin(), loaded_map_names.end());
    WorldMap parsed;
    for (const tinyxml2::XMLElement *segment_element = root->FirstChildElement("segment");
            segment_element != NULL; segment_element = segment_element->NextSiblingElement("segment")) {
        WorldMapSegment segment;
        segment.id = attribute_string(segment_element, "id");
        segment.source_x = attribute_integer(segment_element, "x", -1);
        segment.source_y = attribute_integer(segment_element, "y", -1);

        for (const tinyxml2::XMLElement *element = segment_element->FirstChildElement();
                element != NULL; element = element->NextSiblingElement()) {
            if (std::strcmp(element->Name(), "map") == 0) {
                WorldMapReference reference;
                reference.map_name = attribute_string(element, "id");
                if (loaded.find(reference.map_name) == loaded.end()) continue;
                reference.x = attribute_integer(element, "x", -1);
                reference.y = attribute_integer(element, "y", -1);
                const char *area = element->Attribute("area");
                reference.has_area = area != NULL;
                reference.area_id = area != NULL ? area : "";
                segment.maps.push_back(reference);
            } else if (std::strcmp(element->Name(), "namedarea") == 0) {
                NamedArea area;
                area.id = attribute_string(element, "id");
                area.source_name = attribute_string(element, "name");
                area.type = attribute_string(element, "type");
                area.translated_name = translate ? translate(area.source_name) : area.source_name;
                if (area.translated_name.compare(0, 8, "[REVIEW]") == 0) {
                    area.translated_name.erase(0, 8);
                } else if (area.translated_name.compare(0, 10, "[OUTDATED]") == 0) {
                    area.translated_name = area.source_name;
                }

                std::vector<NamedArea>::iterator existing = std::find_if(
                    segment.named_areas.begin(), segment.named_areas.end(),
                    [&area](const NamedArea &candidate) { return candidate.id == area.id; }
                );
                if (existing == segment.named_areas.end()) segment.named_areas.push_back(area);
                else *existing = area;
            }
        }

        for (const WorldMapReference &reference : segment.maps) {
            if (!reference.has_area) continue;
            for (NamedArea &area : segment.named_areas) {
                if (area.id != reference.area_id) continue;
                if (std::find(area.map_names.begin(), area.map_names.end(), reference.map_name)
                        == area.map_names.end()) {
                    area.map_names.push_back(reference.map_name);
                }
                break;
            }
        }

        std::vector<WorldMapSegment>::iterator existing = std::find_if(
            parsed.segments.begin(), parsed.segments.end(),
            [&segment](const WorldMapSegment &candidate) { return candidate.id == segment.id; }
        );
        if (existing == parsed.segments.end()) parsed.segments.push_back(segment);
        else *existing = segment;
    }
    world_map = parsed;
    if (error != NULL) error->clear();
    return true;
}

bool load_catalog(
    const std::string &directory,
    const TranslationFunction &translate,
    Catalog &catalog,
    std::string *error
)
{
    std::ifstream map_list((directory + "/maps.list").c_str());
    if (!map_list) {
        if (error != NULL) *error = "cannot open map list: " + directory + "/maps.list";
        return false;
    }

    Catalog parsed;
    std::string name;
    std::set<std::string> names;
    while (std::getline(map_list, name)) {
        if (name.empty()) continue;
        if (!names.insert(name).second) {
            if (error != NULL) *error = "duplicate map in map list: " + name;
            return false;
        }
        CatalogMap map;
        const std::string path = directory + "/" + name + ".tmx";
        if (!load_tmx(path, name, false, map.source, error)
                || !translate_document(map.source, map.translated, error)) {
            if (error != NULL) *error = path + ": " + *error;
            return false;
        }
        parsed.maps.push_back(map);
    }
    if (map_list.bad() || parsed.maps.empty()) {
        if (error != NULL) *error = "failed while reading map list: " + directory + "/maps.list";
        return false;
    }

    std::vector<std::string> map_names(names.begin(), names.end());
    if (!load_world_map(directory + "/worldmap.xml", map_names, translate,
            parsed.world_map, error)) return false;
    catalog = parsed;
    if (error != NULL) error->clear();
    return true;
}

}
}