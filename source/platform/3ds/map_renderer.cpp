#include "map_renderer.hpp"
#include "game_combat.hpp"
#include "game_items.hpp"

#include <cctype>
#include <climits>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <sstream>

namespace andortrail {
namespace platform3ds {
namespace {

std::string basename_of(const std::string &path)
{
    const std::size_t separator = path.find_last_of("/\\");
    return separator == std::string::npos ? path : path.substr(separator + 1);
}

bool valid_map_id(const std::string &map_id)
{
    if (map_id.empty()) return false;
    for (unsigned char character : map_id) {
        if (!std::isalnum(character) && character != '_' && character != '-') return false;
    }
    return true;
}

bool contains(const map::Rect &rect, map::GridPosition point)
{
    return point.x >= rect.x && point.y >= rect.y
        && point.x < rect.x + rect.width && point.y < rect.y + rect.height;
}

int distance_to_tile(map::GridPosition from, map::GridPosition to)
{
    return std::abs(from.x - to.x) + std::abs(from.y - to.y);
}

std::string rat_state_id(int rat_index, const char *suffix)
{
    std::ostringstream id;
    id << "mikhail_rat" << rat_index << "_" << suffix;
    return id.str();
}

const map::SpawnArea *find_spawn_area(
    const map::TranslatedDocument &translated,
    const std::string &spawn_group
)
{
    for (const map::SpawnArea &area : translated.spawn_areas) {
        if (area.active_for_new_game && area.spawn_group == spawn_group) return &area;
    }
    return NULL;
}

bool spawn_position(
    const map::Document &document,
    const map::TranslatedDocument &translated,
    const std::string &spawn_group,
    int spawn_index,
    map::GridPosition &position
)
{
    const map::SpawnArea *area = find_spawn_area(translated, spawn_group);
    if (area == NULL || spawn_index < 0 || spawn_index >= area->quantity) return false;

    int walkable_count = 0;
    for (int y = area->tile_rect.y; y < area->tile_rect.y + area->tile_rect.height; ++y) {
        for (int x = area->tile_rect.x; x < area->tile_rect.x + area->tile_rect.width; ++x) {
            if (!map::is_walkable(document, x, y)) continue;
            if (walkable_count++ == spawn_index) {
                position = {x, y};
                return true;
            }
        }
    }
    return false;
}

bool phrase_message(GameResources *resources, const std::string &phrase_id, std::string &message)
{
    if (resources == NULL || phrase_id.empty()) return false;
    const GameResourceRecordEntry *record = game_resources_find(
        GAME_RESOURCE_CONVERSATION, phrase_id.c_str(), NULL
    );
    if (record == NULL) return false;
    GameJsonDocument document;
    GameJsonRef phrase;
    if (!game_resources_open_record(resources, record, &document, &phrase)) return false;
    char source[2048];
    const bool copied = game_json_string_copy(
        game_json_object_get(phrase, "message"), source, sizeof(source)
    );
    if (copied) message.assign(source);
    game_json_dispose(&document);
    return copied;
}

}

MapRenderer::MapRenderer()
    : player_({0, 0}), resources_(NULL), core_(NULL), conversation_(NULL, NULL),
      selected_conversation_reply_(0), player_image_({NULL, NULL}),
      modal_interaction_(false), shop_interaction_(false), initialized_(false)
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
    document_ = map::Document();
    translated_ = map::TranslatedDocument();
    maps_directory_.clear();
    interaction_message_.clear();
    conversation_.set_context(NULL, NULL);
    resources_ = NULL;
    core_ = NULL;
    selected_conversation_reply_ = 0;
    player_image_ = {NULL, NULL};
    modal_interaction_ = false;
    shop_interaction_ = false;
    initialized_ = false;
}

bool MapRenderer::initialize(
    const char *maps_directory,
    const char *initial_map_id,
    const char *tileset_list_path,
    const char *texture_directory,
    GameResources *resources,
    GameCore *core,
    std::string &error
)
{
    dispose();
    if (maps_directory == NULL || initial_map_id == NULL || tileset_list_path == NULL
            || texture_directory == NULL || resources == NULL || core == NULL) {
        error = "invalid map renderer configuration";
        return false;
    }
    maps_directory_ = maps_directory;
    resources_ = resources;
    core_ = core;
    conversation_.set_context(resources_, core_);
    if (!load_tileset_sheets(tileset_list_path, texture_directory, error)) {
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

    if (!load_map(initial_map_id, "rest", map::EventKind::Rest, 0, 0, error)) {
        dispose();
        return false;
    }
    initialized_ = true;
    error.clear();
    return true;
}

bool MapRenderer::load_map(
    const std::string &map_id,
    const std::string &place_id,
    map::EventKind place_kind,
    int offset_x,
    int offset_y,
    std::string &error
)
{
    if (!valid_map_id(map_id)) {
        error = "invalid source map ID";
        return false;
    }
    map::Document next_document;
    const std::string map_path = maps_directory_ + "/" + map_id + ".tmx";
    if (!map::load_tmx(map_path, map_id, true, next_document, &error)) return false;
    map::TranslatedDocument next_translated;
    if (!map::translate_document(next_document, next_translated, &error)
            || !prepare_tile_sources(next_document, error)
            || !validate_visible_layers(next_document, error)) return false;

    const map::Event *entry = NULL;
    for (const map::Event &event : next_translated.events) {
        if (event.kind == place_kind && event.active && event.name == place_id) {
            entry = &event;
            break;
        }
    }
    if (entry == NULL || entry->tile_rect.width <= 0 || entry->tile_rect.height <= 0) {
        error = "source map place not found: " + map_id + ":" + place_id;
        return false;
    }

    map::GridPosition entry_position = {
        entry->tile_rect.x + std::min(offset_x, entry->tile_rect.width - 1),
        entry->tile_rect.y + std::min(offset_y, entry->tile_rect.height - 1),
    };
    if (!map::is_walkable(next_document, entry_position.x, entry_position.y)) {
        bool found_fallback = false;
        for (const map::Event &event : next_translated.events) {
            if (event.kind != map::EventKind::MapChange || !event.active) continue;
            if (map::is_walkable(next_document, event.tile_rect.x, event.tile_rect.y)) {
                entry_position = {event.tile_rect.x, event.tile_rect.y};
                found_fallback = true;
                break;
            }
        }
        if (!found_fallback) {
            error = "source entry place is not walkable and map has no walkable mapchange fallback";
            return false;
        }
    }

    document_ = std::move(next_document);
    translated_ = std::move(next_translated);
    player_ = entry_position;
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

        const C2D_Image whole = C2D_SpriteSheetGetImage(sheet, 0);
        if (whole.tex == NULL || whole.subtex == NULL) {
            C2D_SpriteSheetFree(sheet);
            std::fclose(list);
            error = "original texture sheet contains no image: " + texture_path;
            return false;
        }
        C3D_TexSetFilter(whole.tex, GPU_NEAREST, GPU_NEAREST);

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

bool MapRenderer::prepare_tile_sources(const map::Document &document, std::string &error)
{
    for (const map::Tileset &tileset : document.tilesets) {
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

bool MapRenderer::validate_visible_layers(const map::Document &document, std::string &error) const
{
    static const char *layer_names[] = {"base", "ground", "objects", "above", "top"};
    for (const char *name : layer_names) {
        const map::Layer *layer = map::find_layer(document, name);
        if (layer == NULL) continue;
        for (std::uint32_t gid : layer->gids) {
            if (gid == 0) continue;
            map::TileSource source;
            if (!map::resolve_tile_source(document, gid, source)) {
                error = std::string("cannot resolve original GID in ") + name;
                return false;
            }
            const std::string image_name = basename_of(document.tilesets[source.tileset_index].image_source);
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

bool MapRenderer::transition(const map::Event &event, map::GridPosition entered,
    std::string &error)
{
    if (!valid_map_id(event.target_map)) {
        error = "invalid destination map ID in source map data";
        return false;
    }
    const std::string path = maps_directory_ + "/" + event.target_map + ".tmx";
    map::Document destination_document;
    if (!map::load_tmx(path, event.target_map, true, destination_document, &error)) return false;
    map::TranslatedDocument destination_translated;
    if (!map::translate_document(destination_document, destination_translated, &error)
            || !prepare_tile_sources(destination_document, error)
            || !validate_visible_layers(destination_document, error)) return false;

    map::MapTransition resolved;
    if (!map::resolve_map_transition(translated_, destination_translated,
            event.name, entered, resolved)) {
        error = "source destination place not found: " + event.target_map + ":" + event.target_place;
        return false;
    }
    if (!map::is_walkable(destination_document, resolved.destination.x, resolved.destination.y)) {
        bool found_fallback = false;
        for (const map::Event &destination_event : destination_translated.events) {
            if (destination_event.kind != map::EventKind::MapChange || !destination_event.active) continue;
            if (map::is_walkable(destination_document, destination_event.tile_rect.x,
                    destination_event.tile_rect.y)) {
                resolved.destination = {destination_event.tile_rect.x, destination_event.tile_rect.y};
                found_fallback = true;
                break;
            }
        }
        if (!found_fallback) {
            error = "Android destination and mapchange fallback are not walkable";
            return false;
        }
    }
    document_ = std::move(destination_document);
    translated_ = std::move(destination_translated);
    player_ = resolved.destination;
    interaction_message_.clear();
    modal_interaction_ = false;
    error.clear();
    return true;
}

bool MapRenderer::handle_map_event(const map::Event &event, map::GridPosition entered,
    std::string &error)
{
    switch (event.kind) {
    case map::EventKind::MapChange:
        return transition(event, entered, error);
    case map::EventKind::Sign:
        show_sign(event);
        return true;
    case map::EventKind::Rest:
        game_core_restore_player(core_);
        interaction_message_ = "Rested at " + document_.name + ":" + event.name
            + ". Health restored.";
        modal_interaction_ = true;
        return true;
    case map::EventKind::Container:
        show_container(event);
        return true;
    case map::EventKind::Key:
    case map::EventKind::Script:
        return false;
    }
    return false;
}

void MapRenderer::show_sign(const map::Event &event)
{
    std::string message;
    if (!phrase_message(resources_, event.phrase_id, message)) {
        interaction_message_ = "Sign phrase data unavailable: " + event.phrase_id;
    } else {
        interaction_message_ = message;
    }
    modal_interaction_ = true;
}

void MapRenderer::show_container(const map::Event &event)
{
    if (resources_ == NULL || event.drop_list_id.empty()) {
        interaction_message_ = "Container " + event.name
            + ": original drop-list reference is unavailable.";
        modal_interaction_ = true;
        return;
    }
    const std::string opened_flag = "container_" + event.name;
    if (game_core_quest_progress(core_, opened_flag.c_str()) > 0) {
        interaction_message_ = "Container " + event.name + " has already been opened.";
        modal_interaction_ = true;
        return;
    }
    const std::unique_ptr<GameCore> previous_state(new (std::nothrow) GameCore);
    if (!previous_state) {
        interaction_message_ = "Container state could not be allocated.";
        modal_interaction_ = true;
        return;
    }
    *previous_state = *core_;
    if (!items::apply_drop_list(resources_, core_, event.drop_list_id, interaction_message_)
            || !game_core_set_quest_progress(core_, opened_flag.c_str(), 1)) {
        *core_ = *previous_state;
        if (interaction_message_.empty()) {
            interaction_message_ = "Container could not be opened: " + event.name;
        }
        modal_interaction_ = true;
        return;
    }
    std::ostringstream message;
    message << "Opened " << event.name << ". Supplies added to your inventory.";
    interaction_message_ = message.str();
    modal_interaction_ = true;
}

void MapRenderer::show_key_requirement(const map::Event &event)
{
    interaction_message_.clear();
    if (!event.phrase_id.empty()) phrase_message(resources_, event.phrase_id, interaction_message_);
    if (interaction_message_.empty()) {
        interaction_message_ = "You cannot go that way yet.";
    }
    modal_interaction_ = true;
}

static bool key_requirement_met(const map::Requirement &requirement, const GameCore *core)
{
    if (!requirement.valid || !requirement.recognized_type || core == NULL) return false;
    bool met = false;
    if (requirement.type == "questProgress" || requirement.type == "questLatestProgress") {
        met = game_core_quest_progress(core, requirement.id.c_str()) == requirement.value;
    } else {
        return false;
    }
    return requirement.negate ? !met : met;
}

bool MapRenderer::move_player(int dx, int dy)
{
    if (!initialized_ || modal_interaction_) return false;
    const map::GridPosition previous = player_;
    map::GridPosition next = player_;
    if (!map::move_player(document_, next, dx, dy)) return false;

    map::GridPosition actor_position;
    if (document_.name == "home"
            && spawn_position(document_, translated_, "mikhail", 0, actor_position)
            && next.x == actor_position.x && next.y == actor_position.y) {
        return false;
    }

    for (const map::Event &event : translated_.events) {
        if (event.kind != map::EventKind::Key || !event.active || !contains(event.tile_rect, next)
                || !event.has_requirement) continue;
        if (!key_requirement_met(event.requirement, core_)) {
            show_key_requirement(event);
            return false;
        }
    }

    player_ = next;
    std::string error;
    for (const map::Event &event : translated_.events) {
        if (!event.active || !contains(event.tile_rect, player_)
                || contains(event.tile_rect, previous)) continue;
        if (handle_map_event(event, player_, error)) return true;
        if (!error.empty()) {
            interaction_message_ = "Map event deferred: " + error;
            modal_interaction_ = true;
            return false;
        }
    }
    interaction_message_.clear();
    return true;
}

bool MapRenderer::interact()
{
    if (!initialized_ || modal_interaction_) return false;
    for (const map::Event &event : translated_.events) {
        if (event.kind != map::EventKind::Container || !event.active) continue;
        const map::GridPosition target = {event.tile_rect.x, event.tile_rect.y};
        if (!contains(event.tile_rect, player_)
                && distance_to_tile(player_, target) > 1) continue;
        shop_interaction_ = false;
        show_container(event);
        return true;
    }
    if (can_shop_with_mara()) {
        int bread_price = 0;
        std::string error;
        if (!items::item_price(resources_, "bread", bread_price, error)) {
            interaction_message_ = "Mara's shop: " + error;
            shop_interaction_ = true;
            modal_interaction_ = true;
            return false;
        }
        std::ostringstream message;
        message << "Mara's shop\nBread: " << bread_price
            << " gold\nYour gold: " << game_core_inventory_count(core_, "gold");
        interaction_message_ = message.str();
        shop_interaction_ = true;
        modal_interaction_ = true;
        return true;
    }
    if (can_talk_to_mikhail()) {
        std::string error;
        if (!conversation_.start("mikhail_start_select", error)) {
            interaction_message_ = "Mikhail: " + error;
            modal_interaction_ = true;
            return false;
        }
        selected_conversation_reply_ = 0;
        interaction_message_ = "Mikhail\n" + conversation_.message();
        shop_interaction_ = false;
        modal_interaction_ = true;
        return true;
    }
    interaction_message_.clear();
    shop_interaction_ = false;
    modal_interaction_ = false;
    return false;
}

bool MapRenderer::can_talk_to_mikhail() const
{
    if (!initialized_ || modal_interaction_ || document_.name != "home") return false;
    map::GridPosition mikhail;
    return spawn_position(document_, translated_, "mikhail", 0, mikhail)
        && distance_to_tile(player_, mikhail) <= 2;
}

bool MapRenderer::can_shop_with_mara() const
{
    if (!initialized_ || modal_interaction_ || document_.name != "crossglen_hall") return false;
    map::GridPosition mara;
    return spawn_position(document_, translated_, "Mara", 0, mara)
        && distance_to_tile(player_, mara) <= 1;
}

const char *MapRenderer::nearby_interaction_hint() const
{
    if (!initialized_ || modal_interaction_) return "";
    if (can_talk_to_mikhail()) return "MIKHAIL NEARBY - A: TALK";
    if (can_shop_with_mara()) return "MARA'S SHOP - A: BUY";
    if (document_.name == "crossglen"
            && game_core_quest_progress(core_, "mikhail_rats") == 10) {
        for (int index = 0; index < 2; ++index) {
            const std::string defeated_flag = rat_state_id(index + 1, "defeated");
            map::GridPosition rat;
            if (game_core_quest_progress(core_, defeated_flag.c_str()) == 0
                    && spawn_position(document_, translated_, "trainingrat", index, rat)
                    && distance_to_tile(player_, rat) <= 1) {
                return "TRAINING RAT NEARBY - X: ATTACK";
            }
        }
    }
    return "";
}

bool MapRenderer::attack_nearby_rat()
{
    if (!initialized_ || modal_interaction_ || document_.name != "crossglen"
            || game_core_quest_progress(core_, "mikhail_rats") != 10) return false;

    int target_index = 0;
    for (; target_index < 2; ++target_index) {
        const std::string defeated_flag = rat_state_id(target_index + 1, "defeated");
        if (game_core_quest_progress(core_, defeated_flag.c_str()) > 0) continue;
        map::GridPosition target;
        if (spawn_position(document_, translated_, "trainingrat", target_index, target)
                && distance_to_tile(player_, target) <= 1) break;
    }
    if (target_index == 2) {
        interaction_message_ = "No training rat is close enough to attack.";
        modal_interaction_ = true;
        return false;
    }

    std::string error;
    const int rat_index = target_index + 1;
    std::ostringstream instance_id;
    instance_id << "mikhail_rat" << rat_index;
    combat::AttackResult result;
    if (!combat::attack_monster(resources_, core_, "tobby_trainingrat", "trainingrat",
            instance_id.str(), core_->attack_damage, result, error)) {
        interaction_message_ = "Combat failed: " + error;
        modal_interaction_ = true;
        return false;
    }

    std::ostringstream message;
    if (result.enemy_defeated) {
        message << "Training rat defeated. A training-rat tail was added to your bag.";
    } else if (result.player_hit) {
        message << "You hit the training rat for " << result.damage_dealt << " damage ("
            << result.enemy_health << " HP left).";
    } else {
        message << "Your attack missed the training rat.";
    }
    if (result.player_knocked_out) {
        game_core_restore_player(core_);
        std::string respawn_error;
        if (load_map("home", "rest", map::EventKind::Rest, 0, 0, respawn_error)) {
            message << "\nYou were knocked out and woke at home.";
        } else {
            message << "\nYou were knocked out; returning home failed: " << respawn_error;
        }
    } else if (result.damage_taken > 0) {
        message << "\nThe rat hit you for " << result.damage_taken << ".";
    }
    interaction_message_ = message.str();
    shop_interaction_ = false;
    modal_interaction_ = true;
    return true;
}

bool MapRenderer::has_modal_interaction() const
{
    return modal_interaction_;
}

void MapRenderer::dismiss_modal_interaction()
{
    conversation_.close();
    modal_interaction_ = false;
    interaction_message_.clear();
    selected_conversation_reply_ = 0;
    shop_interaction_ = false;
}

bool MapRenderer::has_conversation() const
{
    return conversation_.active();
}

bool MapRenderer::has_shop() const
{
    return shop_interaction_;
}

bool MapRenderer::buy_shop_bread(std::string &error)
{
    if (!shop_interaction_) {
        error = "shop is not open";
        interaction_message_ = "Mara's shop: " + error;
        return false;
    }
    int bread_price = 0;
    if (!items::item_price(resources_, "bread", bread_price, error)) {
        interaction_message_ = "Mara's shop: " + error;
        return false;
    }
    if (!items::buy_item(resources_, core_, "bread", 1, error)) {
        interaction_message_ = "Mara's shop: " + error;
        return false;
    }
    std::ostringstream message;
    message << "Mara's shop\nBread bought for " << bread_price
        << " gold.\nYour gold: " << game_core_inventory_count(core_, "gold");
    interaction_message_ = message.str();
    error.clear();
    return true;
}

const std::vector<dialogue::Reply> &MapRenderer::conversation_replies() const
{
    return conversation_.replies();
}

std::size_t MapRenderer::selected_conversation_reply() const
{
    return selected_conversation_reply_;
}

void MapRenderer::move_conversation_selection(int direction)
{
    const std::size_t count = conversation_.replies().size();
    if (!conversation_.active() || count == 0 || direction == 0) return;
    if (direction < 0) {
        selected_conversation_reply_ = selected_conversation_reply_ == 0
            ? count - 1 : selected_conversation_reply_ - 1;
    } else {
        selected_conversation_reply_ = (selected_conversation_reply_ + 1) % count;
    }
}

bool MapRenderer::choose_conversation_reply(std::string &error)
{
    if (!conversation_.active()) {
        error = "no conversation is active";
        interaction_message_ = "Mikhail: " + error;
        return false;
    }
    if (!conversation_.choose(selected_conversation_reply_, error)) {
        interaction_message_ = "Mikhail: " + error;
        return false;
    }
    selected_conversation_reply_ = 0;
    interaction_message_ = "Mikhail\n" + conversation_.message();
    error.clear();
    return true;
}

const std::string &MapRenderer::interaction_message() const
{
    return interaction_message_;
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

void MapRenderer::draw_actor_icon(
    const char *sheet_name,
    std::uint32_t icon_index,
    map::GridPosition position,
    const map::ViewTransform &view,
    float depth
) const
{
    const std::map<std::string, std::size_t>::const_iterator sheet =
        image_indices_.find(sheet_name);
    if (sheet == image_indices_.end()) return;
    const C2D_Image whole = C2D_SpriteSheetGetImage(sheets_[sheet->second], 0);
    if (whole.subtex == NULL || whole.subtex->width % 32 != 0
            || whole.subtex->height % 32 != 0) return;
    const std::uint32_t columns = whole.subtex->width / 32;
    const std::uint32_t rows = whole.subtex->height / 32;
    if (columns == 0 || icon_index >= columns * rows) return;

    const std::uint32_t tile_x = icon_index % columns;
    const std::uint32_t tile_y = icon_index / columns;
    const float u_range = whole.subtex->right - whole.subtex->left;
    const float v_range = whole.subtex->bottom - whole.subtex->top;
    Tex3DS_SubTexture subtexture;
    subtexture.width = 32;
    subtexture.height = 32;
    subtexture.left = whole.subtex->left + (tile_x * 32.0f / whole.subtex->width) * u_range;
    subtexture.right = whole.subtex->left + ((tile_x + 1) * 32.0f / whole.subtex->width) * u_range;
    subtexture.top = whole.subtex->top + (tile_y * 32.0f / whole.subtex->height) * v_range;
    subtexture.bottom = whole.subtex->top + ((tile_y + 1) * 32.0f / whole.subtex->height) * v_range;

    map::GridPosition screen;
    if (!map::map_to_screen(view, position, screen)) return;
    C2D_Image image = whole;
    image.subtex = &subtexture;
    C2D_DrawImageAt(image, static_cast<float>(screen.x), static_cast<float>(screen.y), depth, NULL);
}

void MapRenderer::draw(int screen_width, int screen_height) const
{
    if (!initialized_) return;
    const map::ViewTransform view = map::camera_for_player(
        document_, player_, screen_width, screen_height
    );
    draw_layer("base", view, 0.5f);
    draw_layer("ground", view, 0.5f);
    draw_layer("objects", view, 0.5f);
    if (document_.name == "home") {
        map::GridPosition mikhail;
        if (spawn_position(document_, translated_, "mikhail", 0, mikhail)) {
            draw_actor_icon("monsters_mage2.png", 0, mikhail, view, 0.45f);
        }
    } else if (document_.name == "crossglen_hall") {
        map::GridPosition mara;
        if (spawn_position(document_, translated_, "Mara", 0, mara)) {
            draw_actor_icon("monsters_men.png", 7, mara, view, 0.45f);
        }
    } else if (document_.name == "crossglen") {
        if (core_ != NULL && game_core_quest_progress(core_, "mikhail_rats") == 10) {
            for (int index = 0; index < 2; ++index) {
                const std::string defeated_flag = rat_state_id(index + 1, "defeated");
                map::GridPosition rat;
                if (game_core_quest_progress(core_, defeated_flag.c_str()) == 0
                        && spawn_position(document_, translated_, "trainingrat", index, rat)) {
                    draw_actor_icon("monsters_rats.png", 0, rat, view, 0.45f);
                }
            }
        }
    }
    map::GridPosition player_screen;
    if (map::map_to_screen(view, player_, player_screen)) {
        C2D_DrawImageAt(player_image_, static_cast<float>(player_screen.x),
            static_cast<float>(player_screen.y), 0.5f, NULL);
    }

    draw_layer("above", view, 0.5f);
    draw_layer("top", view, 0.5f);
}

void MapRenderer::draw_player_portrait(float x, float y, float depth) const
{
    if (player_image_.tex != NULL && player_image_.subtex != NULL) {
        C2D_DrawImageAt(player_image_, x, y, depth, NULL);
    }
}

map::GridPosition MapRenderer::player_position() const
{
    return player_;
}

map::ViewTransform MapRenderer::camera(int screen_width, int screen_height) const
{
    return map::camera_for_player(document_, player_, screen_width, screen_height);
}

bool MapRenderer::player_walkable() const
{
    return map::is_walkable(document_, player_.x, player_.y);
}

const char *MapRenderer::map_id() const
{
    return document_.name.c_str();
}

}
}